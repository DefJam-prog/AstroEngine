#include "CSharpCodeWorkspace.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <regex>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace astro::editor
{
namespace
{
constexpr std::size_t maxSourceBytes = 4 * 1024 * 1024;
core::Error workspaceError(core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != root.end() && candidateIt != candidate.end(); ++rootIt, ++candidateIt)
    {
#ifdef _WIN32
        if (_wcsicmp(rootIt->c_str(), candidateIt->c_str()) != 0) return false;
#else
        if (*rootIt != *candidateIt) return false;
#endif
    }
    return rootIt == root.end();
}

bool validUtf8(const std::string& value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t i = 0;
    while (i < value.size())
    {
        const unsigned char first = bytes[i++];
        if (first <= 0x7f) continue;
        unsigned int count = 0;
        std::uint32_t codepoint = 0;
        if ((first & 0xe0) == 0xc0) { count = 1; codepoint = first & 0x1f; if (codepoint < 2) return false; }
        else if ((first & 0xf0) == 0xe0) { count = 2; codepoint = first & 0x0f; }
        else if ((first & 0xf8) == 0xf0) { count = 3; codepoint = first & 0x07; }
        else return false;
        if (i + count > value.size()) return false;
        for (unsigned int n = 0; n < count; ++n)
        {
            const unsigned char next = bytes[i++];
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((count == 2 && codepoint < 0x800) || (count == 3 && codepoint < 0x10000)
            || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

std::string narrowPath(const std::filesystem::path& path)
{
    const auto u8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

std::filesystem::path pathFromUtf8(const std::string& value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}
}

core::Result<std::filesystem::path> CSharpCodeWorkspace::resolve(const std::filesystem::path& projectRoot,
    const std::filesystem::path& relativeScript, bool allowMissingLeaf) const
{
    if (projectRoot.empty() || relativeScript.empty() || relativeScript.is_absolute())
        return core::Result<std::filesystem::path>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "C# script path must be relative to the project Scripts directory"));
    if (relativeScript.extension() != ".cs")
        return core::Result<std::filesystem::path>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "C# code workspace accepts only .cs files"));
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(projectRoot / "Scripts", ec);
    if (ec) return core::Result<std::filesystem::path>::failure(workspaceError(core::ErrorCode::IoError,
        "Cannot resolve project Scripts directory: " + ec.message()));
    const auto canonicalProject = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec || !withinRoot(canonicalProject, root))
        return core::Result<std::filesystem::path>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "Project Scripts directory resolves outside the project root"));
    const auto requested = root / relativeScript;
    const auto resolved = allowMissingLeaf ? std::filesystem::weakly_canonical(requested, ec)
        : std::filesystem::canonical(requested, ec);
    if (ec) return core::Result<std::filesystem::path>::failure(workspaceError(core::ErrorCode::IoError,
        "Cannot resolve C# script path: " + ec.message()));
    if (!withinRoot(root, resolved))
        return core::Result<std::filesystem::path>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "C# script path resolves outside the project Scripts directory"));
    if (!allowMissingLeaf && !std::filesystem::is_regular_file(resolved, ec))
        return core::Result<std::filesystem::path>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "C# script path is not a regular file"));
    return core::Result<std::filesystem::path>::success(resolved);
}

core::Result<void> CSharpCodeWorkspace::open(const std::filesystem::path& projectRoot,
    const std::filesystem::path& relativeScript)
{
    auto path = resolve(projectRoot, relativeScript, false);
    if (!path) return core::Result<void>::failure(path.error());
    std::ifstream input(path.value(), std::ios::binary);
    if (!input) return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
        "Cannot open C# script"));
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path.value(), ec);
    if (ec) return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
        "Cannot inspect C# source size: " + ec.message()));
    if (fileSize > maxSourceBytes) return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
        "C# source exceeds the 4 MiB editor limit"));
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (contents.size() >= 3 && static_cast<unsigned char>(contents[0]) == 0xef
        && static_cast<unsigned char>(contents[1]) == 0xbb && static_cast<unsigned char>(contents[2]) == 0xbf)
        contents.erase(0, 3);
    if (!validUtf8(contents)) return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
        "C# script is not valid UTF-8"));
    projectRoot_ = std::filesystem::weakly_canonical(projectRoot);
    relativeScript_ = relativeScript;
    text_ = std::move(contents);
    savedText_ = text_;
    return core::Result<void>::success();
}

core::Result<void> CSharpCodeWorkspace::create(const std::filesystem::path& projectRoot,
    const std::filesystem::path& relativeScript)
{
    std::error_code ec;
    const auto scriptsRoot = std::filesystem::weakly_canonical(projectRoot / "Scripts", ec);
    if (ec) return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
        "Cannot resolve project Scripts directory: " + ec.message()));
    const auto canonicalProject = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec || !withinRoot(canonicalProject, scriptsRoot))
        return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "Project Scripts directory resolves outside the project root"));
    const auto destination = scriptsRoot / relativeScript;
    const auto parent = std::filesystem::weakly_canonical(destination.parent_path(), ec);
    if (ec || !withinRoot(scriptsRoot, parent) || relativeScript.is_absolute()
        || relativeScript.extension() != ".cs")
        return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "New C# script must remain inside Scripts and use the .cs extension"));
    std::filesystem::create_directories(parent, ec);
    if (ec || !withinRoot(scriptsRoot, std::filesystem::weakly_canonical(parent, ec)) || ec)
        return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "Cannot create a C# script directory outside project Scripts"));
    std::filesystem::create_directories(scriptsRoot, ec);
    if (ec) return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
        "Cannot create project Scripts directory: " + ec.message()));
    if (std::filesystem::exists(destination, ec) || ec)
        return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
            "C# script already exists or cannot be inspected"));
#ifdef _WIN32
    HANDLE reserved = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (reserved == INVALID_HANDLE_VALUE)
        return core::Result<void>::failure(workspaceError(core::ErrorCode::AlreadyExists,
            "C# script already exists or cannot be created"));
    CloseHandle(reserved);
#else
    std::ofstream reserved(destination, std::ios::binary | std::ios::out);
    if (!reserved) return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
        "C# script already exists or cannot be created"));
    reserved.close();
#endif
    projectRoot_ = std::filesystem::weakly_canonical(projectRoot);
    relativeScript_ = relativeScript;
    text_.clear();
    savedText_.clear();
    return core::Result<void>::success();
}

core::Result<void> CSharpCodeWorkspace::save()
{
    if (projectRoot_.empty() || relativeScript_.empty())
        return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
            "No C# script is open"));
    auto path = resolve(projectRoot_, relativeScript_, true);
    if (!path) return core::Result<void>::failure(path.error());
    if (!validUtf8(text_)) return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
        "C# source text is not valid UTF-8"));
    if (text_.size() > maxSourceBytes) return core::Result<void>::failure(workspaceError(core::ErrorCode::InvalidArgument,
        "C# source exceeds the 4 MiB editor limit"));
    static std::atomic_uint64_t sequence{};
    auto temporary = path.value();
    temporary += ".astro-tmp-" + std::to_string(++sequence);
#ifdef _WIN32
    temporary += "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
            "Could not reserve a unique temporary C# source"));
    DWORD written{};
    const bool writtenOk = WriteFile(file, text_.data(), static_cast<DWORD>(text_.size()), &written, nullptr)
        && written == text_.size() && FlushFileBuffers(file);
    const DWORD writeError = writtenOk ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!writtenOk)
    {
        DeleteFileW(temporary.c_str());
        return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
            "Could not write temporary C# source (Windows error " + std::to_string(writeError) + ")"));
    }
#else
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text_.data(), static_cast<std::streamsize>(text_.size()));
        output.flush();
        if (!output)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
                "Could not write temporary C# source"));
        }
    }
#endif
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.value().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto error = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
            "Could not publish C# source: " + error));
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, path.value(), ec);
    if (ec)
    {
        std::filesystem::remove(temporary, ec);
        return core::Result<void>::failure(workspaceError(core::ErrorCode::IoError,
            "Could not publish C# source: " + ec.message()));
    }
#endif
    savedText_ = text_;
    return core::Result<void>::success();
}

void CSharpCodeWorkspace::setText(std::string text) { text_ = std::move(text); }

std::vector<CSharpDiagnostic> CSharpCodeWorkspace::parseDiagnostics(const std::string& output)
{
    static const std::regex pattern(R"(^(.+\.cs)\((\d+),(\d+)\): (error|warning) ([A-Z]+\d+): (.*)$)");
    std::vector<CSharpDiagnostic> result;
    std::size_t start = 0;
    while (start < output.size())
    {
        const auto end = output.find('\n', start);
        auto line = output.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch match;
        if (std::regex_match(line, match, pattern))
        {
            auto source = pathFromUtf8(match[1].str());
            const auto marker = source.generic_string().find("/_/AstroEngine/Scripts/");
            if (marker != std::string::npos) source = std::filesystem::path("Scripts")
                / source.generic_string().substr(marker + std::string("/_/AstroEngine/Scripts/").size());
            result.push_back({narrowPath(source), std::stoi(match[2].str()), std::stoi(match[3].str()),
                match[4].str(), match[5].str(), match[6].str()});
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
}
