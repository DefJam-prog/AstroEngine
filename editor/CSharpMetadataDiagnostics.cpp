#include "CSharpMetadataDiagnostics.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <string_view>
#include <system_error>

namespace astro::editor
{
namespace
{
using astro::core::ErrorCode;
using astro::core::Result;

constexpr std::size_t maxWorkerOutputBytes = 32 * 1024;
constexpr std::size_t maxReportOutputBytes = 128 * 1024;
constexpr std::size_t maxWorkerTypeCount = 64;
constexpr std::size_t maxTypeNameCharactersPerBatch = 8192;
constexpr std::size_t maxManagedTypeCount = 256;
constexpr DWORD validatorTimeoutMs = 90'000;

Result<CSharpMetadataValidationPlan> failure(const ErrorCode code, std::string message)
{
    return Result<CSharpMetadataValidationPlan>::failure({code, std::move(message)});
}

template<typename T>
Result<T> failureAs(const ErrorCode code, std::string message)
{
    return Result<T>::failure({code, std::move(message)});
}

std::filesystem::path fromUtf8(const std::string& value)
{
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

std::wstring toWideUtf8(const std::string& value)
{
    if (value.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring decoded(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        decoded.data(), length) != length)
        return {};
    return decoded;
}

bool samePathPart(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
}

bool isWithin(const std::filesystem::path& candidate, const std::filesystem::path& root)
{
    auto candidatePart = candidate.begin();
    auto rootPart = root.begin();
    for (; rootPart != root.end(); ++rootPart, ++candidatePart)
        if (candidatePart == candidate.end() || !samePathPart(*candidatePart, *rootPart)) return false;
    return candidatePart != candidate.end();
}

bool hasPrefix(const std::filesystem::path& path, const std::filesystem::path& prefix)
{
    auto pathPart = path.begin();
    for (auto prefixPart = prefix.begin(); prefixPart != prefix.end(); ++prefixPart, ++pathPart)
        if (pathPart == path.end() || !samePathPart(*pathPart, *prefixPart)) return false;
    return true;
}

bool hasReparsePoint(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    auto current = root;
    for (const auto& part : relative)
    {
        current /= part;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return true;
    }
    return false;
}

std::wstring quoteArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\') { ++backslashes; continue; }
        if (character == L'"')
        {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(character);
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::string readOutput(HANDLE file)
{
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(maxWorkerOutputBytes))
        return "Metadata validator output was missing or exceeded 32 KiB.";
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN))
        return "Could not seek metadata validator output.";
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
        || read != bytes.size()))
        return "Could not read complete metadata validator output.";
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF
        && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
        bytes.erase(0, 3);
    return bytes;
}

std::filesystem::path makeTemporaryOutputPath()
{
    std::array<wchar_t, MAX_PATH> directory{};
    const auto length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    if (length == 0 || length >= directory.size()) return {};
    std::array<wchar_t, MAX_PATH> file{};
    if (GetTempFileNameW(directory.data(), L"ast", 0, file.data()) == 0) return {};
    return file.data();
}

bool launchDotnet(const std::filesystem::path& dotnetRoot, const std::filesystem::path& validatorAssembly,
    const CSharpMetadataValidationPlan& plan, HANDLE outputFile, DWORD& exitCode, bool& timedOut)
{
    const auto dotnet = dotnetRoot / L"dotnet.exe";
    if (!std::filesystem::is_regular_file(dotnet) || !std::filesystem::is_regular_file(validatorAssembly))
        return false;

    std::wstring command = quoteArgument(dotnet.wstring()) + L" " + quoteArgument(validatorAssembly.wstring())
        + L" inspect-many --dotnet-root " + quoteArgument(dotnetRoot.wstring())
        + L" --assembly " + quoteArgument(plan.assemblyPath.wstring());
    for (const auto& typeName : plan.managedTypes)
    {
        const auto wide = toWideUtf8(typeName);
        if (wide.empty()) return false;
        command += L" --type " + quoteArgument(wide);
    }
    if (command.size() >= 30'000) return false;

    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) return false;

    SIZE_T attributeListBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListBytes);
    auto* attributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attributeListBytes));
    if (!attributeList || attributeListBytes == 0)
    {
        CloseHandle(nullInput);
        return false;
    }
    bool initialized = false;
    HANDLE allowedHandles[] = {nullInput, outputFile};
    PROCESS_INFORMATION process{};
    bool started = false;
    if (InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListBytes))
    {
        initialized = true;
        started = UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, allowedHandles,
            sizeof(allowedHandles), nullptr, nullptr);
    }
    if (started)
    {
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullInput;
        startup.StartupInfo.hStdOutput = outputFile;
        startup.StartupInfo.hStdError = outputFile;
        startup.lpAttributeList = attributeList;
        started = CreateProcessW(dotnet.c_str(), command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, plan.projectRoot.c_str(),
            &startup.StartupInfo, &process);
    }
    if (initialized) DeleteProcThreadAttributeList(attributeList);
    if (attributeList) HeapFree(GetProcessHeap(), 0, attributeList);
    CloseHandle(nullInput);
    if (!started) return false;

    const auto wait = WaitForSingleObject(process.hProcess, validatorTimeoutMs);
    if (wait == WAIT_TIMEOUT)
    {
        timedOut = true;
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 2000);
    }
    else if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
    }
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0 || wait == WAIT_TIMEOUT;
}
}

Result<CSharpMetadataValidationPlan> prepareCSharpMetadataValidation(
    const astro::assets::AssetRegistry& registry, const astro::assets::ScriptBuildReport& buildReport)
{
    if (buildReport.freshness != astro::assets::ScriptBuildFreshness::Current)
        return failure(ErrorCode::InvalidState, "Metadata validation requires a current C# build");
    if (buildReport.assemblyRelativePath.empty())
        return failure(ErrorCode::ValidationError, "Current C# build has no managed assembly path");

    std::error_code error;
    const auto projectRoot = std::filesystem::canonical(registry.projectRoot(), error);
    if (error) return failure(ErrorCode::IoError, "Cannot resolve project root: " + error.message());
    const auto relativeAssembly = fromUtf8(buildReport.assemblyRelativePath);
    const auto requiredRelativeRoot = std::filesystem::path(L"Library") / L"Scripts";
    if (relativeAssembly.empty() || relativeAssembly.is_absolute() || relativeAssembly.has_root_name()
        || relativeAssembly.has_root_directory() || relativeAssembly != relativeAssembly.lexically_normal()
        || !hasPrefix(relativeAssembly, requiredRelativeRoot)
        || relativeAssembly.extension() != L".dll")
        return failure(ErrorCode::ValidationError, "Managed assembly path must be a normalized Library/Scripts DLL path");

    const auto assemblyRoot = std::filesystem::canonical(projectRoot / requiredRelativeRoot, error);
    if (error || !std::filesystem::is_directory(assemblyRoot, error) || error)
        return failure(ErrorCode::NotFound, "Project Library/Scripts output directory is unavailable");
    if (hasReparsePoint(projectRoot, relativeAssembly))
        return failure(ErrorCode::ValidationError, "Managed assembly path cannot traverse a Windows reparse point");
    const auto assemblyPath = std::filesystem::canonical(projectRoot / relativeAssembly, error);
    if (error || !std::filesystem::is_regular_file(assemblyPath, error) || error || !isWithin(assemblyPath, assemblyRoot))
        return failure(ErrorCode::ValidationError, "Managed assembly resolves outside the project Library/Scripts directory");

    std::vector<std::string> managedTypes;
    for (const auto& binding : buildReport.bindings)
    {
        if (!binding.registered) continue;
        if (!binding.assetId)
            return failure(ErrorCode::InvalidState, "Registered C# binding has no AssetId");
        const auto resolved = registry.resolveScript(*binding.assetId);
        if (!resolved)
            return failure(resolved.error().code, resolved.error().message);
        const auto registeredAssembly = std::filesystem::canonical(resolved.value().assemblyPath, error);
        if (error || registeredAssembly != assemblyPath || resolved.value().managedTypeName != binding.managedTypeName)
            return failure(ErrorCode::InvalidState, "Registered C# binding no longer matches the current build output");
        if (std::find(managedTypes.begin(), managedTypes.end(), binding.managedTypeName) == managedTypes.end())
            managedTypes.push_back(binding.managedTypeName);
    }
    if (managedTypes.empty())
        return failure(ErrorCode::InvalidState, "Register at least one current C# binding before metadata validation");

    return Result<CSharpMetadataValidationPlan>::success({projectRoot, assemblyPath, std::move(managedTypes)});
}

Result<std::vector<CSharpMetadataValidationPlan>> splitCSharpMetadataValidationPlan(
    const CSharpMetadataValidationPlan& plan)
{
    if (plan.managedTypes.empty() || plan.managedTypes.size() > maxManagedTypeCount)
        return failureAs<std::vector<CSharpMetadataValidationPlan>>(ErrorCode::ValidationError,
            "Metadata validation accepts from one to 256 managed types");

    std::vector<CSharpMetadataValidationPlan> batches;
    CSharpMetadataValidationPlan current{plan.projectRoot, plan.assemblyPath, {}};
    std::size_t currentNameCharacters = 0;
    for (const auto& typeName : plan.managedTypes)
    {
        if (typeName.empty() || typeName.size() > 512
            || std::any_of(typeName.begin(), typeName.end(), [](const unsigned char character) { return character < 0x20 || character == 0x7F; }))
            return failureAs<std::vector<CSharpMetadataValidationPlan>>(ErrorCode::ValidationError,
                "Managed type names must be printable UTF-8 and no longer than 512 bytes");

        if (current.managedTypes.size() == maxWorkerTypeCount
            || currentNameCharacters + typeName.size() > maxTypeNameCharactersPerBatch)
        {
            batches.push_back(std::move(current));
            current = {plan.projectRoot, plan.assemblyPath, {}};
            currentNameCharacters = 0;
        }
        currentNameCharacters += typeName.size();
        current.managedTypes.push_back(typeName);
    }
    if (!current.managedTypes.empty()) batches.push_back(std::move(current));
    return Result<std::vector<CSharpMetadataValidationPlan>>::success(std::move(batches));
}

CSharpMetadataValidationResult runCSharpMetadataValidation(const std::filesystem::path& dotnetRoot,
    const std::filesystem::path& validatorAssembly, const CSharpMetadataValidationPlan& plan)
{
    CSharpMetadataValidationResult result;
    const auto batches = splitCSharpMetadataValidationPlan(plan);
    if (!batches)
    {
        result.output = batches.error().message;
        return result;
    }
    result.managedTypeCount = plan.managedTypes.size();
    result.workerBatchCount = batches.value().size();
    const auto outputPath = makeTemporaryOutputPath();
    if (outputPath.empty())
    {
        result.output = "Could not create a temporary metadata validator output file.";
        return result;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(outputPath.c_str());
        result.output = "Could not open metadata validator output file.";
        return result;
    }

    bool allStarted = true;
    bool outputTruncated = false;
    result.passed = true;
    for (std::size_t batchIndex = 0; batchIndex < batches.value().size(); ++batchIndex)
    {
        const auto& batch = batches.value()[batchIndex];
        LARGE_INTEGER start{};
        SetFilePointerEx(output, start, nullptr, FILE_BEGIN);
        SetEndOfFile(output);
        DWORD exitCode = 1;
        const bool started = launchDotnet(dotnetRoot, validatorAssembly, batch, output, exitCode, result.timedOut);
        result.exitCode = static_cast<std::uint32_t>(exitCode);
        allStarted = allStarted && started;
        const auto batchOutput = readOutput(output);
        if (result.output.size() < maxReportOutputBytes)
        {
            const auto remaining = maxReportOutputBytes - result.output.size();
            result.output.append(batchOutput.data(), std::min(remaining, batchOutput.size()));
            outputTruncated = outputTruncated || batchOutput.size() > remaining;
        }
        else outputTruncated = true;
        result.passed = result.passed && started && !result.timedOut && result.exitCode == 0;
        if (!started || result.timedOut) break;
    }
    CloseHandle(output);
    DeleteFileW(outputPath.c_str());
    if (result.timedOut) result.output = "Metadata validation exceeded the 90-second process limit and was terminated.\n" + result.output;
    else if (!allStarted) result.output = "Could not start the isolated metadata validator process.\n" + result.output;
    if (outputTruncated)
    {
        constexpr std::string_view marker = "\n[additional metadata results truncated at 128 KiB]";
        if (result.output.size() + marker.size() > maxReportOutputBytes)
            result.output.resize(maxReportOutputBytes - marker.size());
        result.output += marker;
    }
    return result;
}
}
