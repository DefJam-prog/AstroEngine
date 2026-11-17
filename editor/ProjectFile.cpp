#include "ProjectFile.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#define NOMINMAX
#include <windows.h>

namespace astro::editor
{
namespace
{
using astro::core::Error;
using astro::core::ErrorCode;
using astro::core::Result;

constexpr std::size_t maxSettingsBytes = 4096;
constexpr std::size_t maxProfileIdBytes = 64;
constexpr std::size_t maxSaveSubdirectoryBytes = 512;

Result<ProjectInfo> failure(const ErrorCode code, std::string message)
{
    return Result<ProjectInfo>::failure({code, std::move(message)});
}

std::string serialize(const std::string& name)
{
    std::ostringstream output;
    output << "ASTRO_PROJECT 1\n" << std::quoted(name) << '\n';
    return output.str();
}

std::string winError(const char* action, const DWORD code)
{
    return std::string(action) + " failed (Windows error " + std::to_string(code) + ")";
}

void removeEmptyDirectory(const std::filesystem::path& directory) noexcept
{
    std::error_code ignored;
    std::filesystem::remove(directory, ignored); // Removes only if still empty.
}

std::string pathUtf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path settingsFilePath(const std::filesystem::path& root)
{
    return root / L".astro" / L"puzzle-save.settings";
}

bool validSettings(const PuzzleSaveProjectSettings& settings, std::string& error)
{
    if (settings.profileId.empty() || settings.profileId.size() > maxProfileIdBytes
        || !std::all_of(settings.profileId.begin(), settings.profileId.end(), [](const unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                || ch == '.' || ch == '_' || ch == '-';
        }))
    {
        error = "Puzzle SaveProfile ID must use 1-64 ASCII letters, digits, dot, underscore or hyphen";
        return false;
    }
    if (settings.profileVersion == 0)
    {
        error = "Puzzle SaveProfile version must be greater than zero";
        return false;
    }
    if (settings.userDataSubdirectory.empty() || settings.userDataSubdirectory.size() > maxSaveSubdirectoryBytes
        || settings.userDataSubdirectory.find('\0') != std::string::npos)
    {
        error = "Puzzle save directory must contain 1-512 UTF-8 bytes";
        return false;
    }

    try
    {
        const auto utf8 = std::u8string(reinterpret_cast<const char8_t*>(settings.userDataSubdirectory.data()),
            settings.userDataSubdirectory.size());
        const std::filesystem::path relative(utf8);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        {
            error = "Puzzle save directory must be relative to the SDL user-data folder";
            return false;
        }
        for (const auto& component : relative)
        {
            if (component == L"." || component == L".." || component.empty())
            {
                error = "Puzzle save directory cannot contain dot or parent-directory segments";
                return false;
            }
        }
    }
    catch (const std::exception& exception)
    {
        error = std::string("Puzzle save directory is not valid UTF-8: ") + exception.what();
        return false;
    }
    return true;
}

std::string serializePuzzleSettings(const PuzzleSaveProjectSettings& settings)
{
    const auto directoryUtf8 = std::u8string(reinterpret_cast<const char8_t*>(settings.userDataSubdirectory.data()),
        settings.userDataSubdirectory.size());
    const auto normalizedDirectory = pathUtf8(std::filesystem::path(directoryUtf8).lexically_normal());
    std::ostringstream output;
    output << "ASTRO_PUZZLE_SAVE_SETTINGS 1\n"
           << "PROFILE " << std::quoted(settings.profileId) << ' ' << settings.profileVersion << '\n'
           << "DIRECTORY " << std::quoted(normalizedDirectory) << '\n'
           << "END\n";
    return output.str();
}
}

Result<ProjectInfo> readProject(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary);
    if (!input) return failure(ErrorCode::IoError, "Cannot open project file");
    std::string magic, name, extra;
    int version = 0;
    if (!(input >> magic >> version >> std::quoted(name)) || magic != "ASTRO_PROJECT" || version != 1
        || name.empty() || (input >> extra))
        return failure(ErrorCode::ParseError, "Invalid project file or unsupported version");
    if (input.bad()) return failure(ErrorCode::IoError, "Error while reading project file");
    return Result<ProjectInfo>::success({std::move(name)});
}

Result<ProjectInfo> createProject(const std::filesystem::path& directory, const std::string& name)
{
    if (directory.empty() || name.empty() || name.size() > 240)
        return failure(ErrorCode::InvalidArgument, "Project directory and a name of at most 240 UTF-8 bytes are required");

    std::error_code fsError;
    if (!std::filesystem::create_directory(directory, fsError))
    {
        if (fsError) return failure(ErrorCode::IoError, "Cannot create project directory: " + fsError.message());
        return failure(ErrorCode::AlreadyExists, "Project directory already exists; it was not modified");
    }

    const auto destination = directory / L"project.astro";
    static std::atomic<unsigned long> sequence{0};
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64())
            + L"." + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        temporary = directory / (L"project.astro.tmp." + suffix);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            const auto message = winError("Creating temporary project file", GetLastError());
            removeEmptyDirectory(directory);
            return failure(ErrorCode::IoError, message);
        }
    }
    if (file == INVALID_HANDLE_VALUE)
    {
        removeEmptyDirectory(directory);
        return failure(ErrorCode::IoError, "Could not reserve a unique temporary project file");
    }

    const auto contents = serialize(name);
    DWORD written = 0;
    const bool writeOk = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == contents.size() && FlushFileBuffers(file);
    const auto writeError = writeOk ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!writeOk)
    {
        DeleteFileW(temporary.c_str());
        removeEmptyDirectory(directory);
        return failure(ErrorCode::IoError, winError("Writing project file", writeError));
    }

    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
    {
        const auto message = winError("Publishing project file without replacement", GetLastError());
        DeleteFileW(temporary.c_str());
        removeEmptyDirectory(directory);
        return failure(ErrorCode::IoError, message);
    }
    return Result<ProjectInfo>::success({name});
}

PuzzleSaveProjectSettings defaultPuzzleSaveProjectSettings(const std::filesystem::path& projectRoot)
{
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec) normalized = std::filesystem::absolute(projectRoot, ec).lexically_normal();
    std::string key = pathUtf8(normalized);
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto character : key)
    {
        auto byte = static_cast<unsigned char>(character);
        if (byte == '\\') byte = '/';
        if (byte >= 'A' && byte <= 'Z') byte = static_cast<unsigned char>(byte - 'A' + 'a');
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream suffix;
    suffix << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << hash;
    return {"puzzle-preview", 1, "Saves/Projects/" + suffix.str() + "/Puzzle"};
}

Result<std::filesystem::path> resolvePuzzleCampaignSaveDirectory(
    const std::filesystem::path& userDataRoot, const PuzzleSaveProjectSettings& settings,
    const std::string_view campaignId)
{
    if (userDataRoot.empty())
        return Result<std::filesystem::path>::failure({ErrorCode::InvalidArgument,
            "Puzzle campaign save directory requires a user-data root"});

    std::string settingsError;
    if (!validSettings(settings, settingsError))
        return Result<std::filesystem::path>::failure({ErrorCode::ValidationError, std::move(settingsError)});

    const bool validCampaignId = !campaignId.empty() && campaignId.size() <= 64
        && std::all_of(campaignId.begin(), campaignId.end(), [](const unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        });
    if (!validCampaignId)
        return Result<std::filesystem::path>::failure({ErrorCode::ValidationError,
            "Puzzle campaign ID is not safe for a user-data directory"});

    try
    {
        const auto directoryUtf8 = std::u8string(
            reinterpret_cast<const char8_t*>(settings.userDataSubdirectory.data()),
            settings.userDataSubdirectory.size());
        const auto campaign = std::filesystem::path(std::string(campaignId));
        return Result<std::filesystem::path>::success((userDataRoot / std::filesystem::path(directoryUtf8)
            / L"PuzzleCampaigns" / campaign).lexically_normal());
    }
    catch (const std::exception& exception)
    {
        return Result<std::filesystem::path>::failure({ErrorCode::ValidationError,
            std::string("Could not resolve Puzzle campaign save path: ") + exception.what()});
    }
}

Result<PuzzleSaveProjectSettings> readPuzzleSaveProjectSettings(const std::filesystem::path& projectRoot)
{
    const auto file = settingsFilePath(projectRoot);
    std::error_code fsError;
    if (!std::filesystem::exists(file, fsError))
    {
        if (fsError) return Result<PuzzleSaveProjectSettings>::failure(
            {ErrorCode::IoError, "Cannot inspect Puzzle project settings: " + fsError.message()});
        return Result<PuzzleSaveProjectSettings>::success(defaultPuzzleSaveProjectSettings(projectRoot));
    }

    std::ifstream input(file, std::ios::binary);
    if (!input) return Result<PuzzleSaveProjectSettings>::failure(
        {ErrorCode::IoError, "Cannot open Puzzle project settings"});
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > maxSettingsBytes)
        return Result<PuzzleSaveProjectSettings>::failure(
            {ErrorCode::ValidationError, "Puzzle project settings exceed the 4 KiB limit"});
    input.seekg(0, std::ios::beg);
    std::string magic, profileTag, directoryTag, endTag, profileId, subdirectory, extra;
    int formatVersion = 0;
    std::uint32_t schemaVersion = 0;
    if (!(input >> magic >> formatVersion >> profileTag >> std::quoted(profileId) >> schemaVersion
        >> directoryTag >> std::quoted(subdirectory) >> endTag)
        || magic != "ASTRO_PUZZLE_SAVE_SETTINGS" || formatVersion != 1 || profileTag != "PROFILE"
        || directoryTag != "DIRECTORY" || endTag != "END" || (input >> extra))
        return Result<PuzzleSaveProjectSettings>::failure(
            {ErrorCode::ParseError, "Puzzle project settings are malformed or use an unsupported version"});
    if (input.bad()) return Result<PuzzleSaveProjectSettings>::failure(
        {ErrorCode::IoError, "Error while reading Puzzle project settings"});

    PuzzleSaveProjectSettings settings{std::move(profileId), schemaVersion, std::move(subdirectory)};
    std::string validationError;
    if (!validSettings(settings, validationError))
        return Result<PuzzleSaveProjectSettings>::failure({ErrorCode::ValidationError, std::move(validationError)});
    try
    {
        const auto utf8 = std::u8string(reinterpret_cast<const char8_t*>(settings.userDataSubdirectory.data()),
            settings.userDataSubdirectory.size());
        settings.userDataSubdirectory = pathUtf8(std::filesystem::path(utf8).lexically_normal());
    }
    catch (const std::exception& exception)
    {
        return Result<PuzzleSaveProjectSettings>::failure({ErrorCode::ValidationError,
            std::string("Puzzle save directory is not valid UTF-8: ") + exception.what()});
    }
    return Result<PuzzleSaveProjectSettings>::success(std::move(settings));
}

Result<void> writePuzzleSaveProjectSettings(const std::filesystem::path& projectRoot,
    const PuzzleSaveProjectSettings& settings)
{
    std::string validationError;
    if (!validSettings(settings, validationError))
        return Result<void>::failure({ErrorCode::ValidationError, std::move(validationError)});
    std::error_code fsError;
    if (!std::filesystem::is_directory(projectRoot, fsError) || fsError)
        return Result<void>::failure({ErrorCode::IoError, "Puzzle settings require an existing project directory"});
    const auto directory = settingsFilePath(projectRoot).parent_path();
    std::filesystem::create_directories(directory, fsError);
    if (fsError) return Result<void>::failure({ErrorCode::IoError,
        "Cannot create project settings directory: " + fsError.message()});

    static std::atomic<unsigned long> sequence{0};
    const auto destination = settingsFilePath(projectRoot);
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64())
            + L"." + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        temporary = directory / (L"puzzle-save.settings.tmp." + suffix);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return Result<void>::failure({ErrorCode::IoError,
                winError("Creating temporary Puzzle settings file", GetLastError())});
    }
    if (file == INVALID_HANDLE_VALUE)
        return Result<void>::failure({ErrorCode::IoError, "Could not reserve a unique temporary Puzzle settings file"});

    const auto contents = serializePuzzleSettings(settings);
    DWORD written = 0;
    const bool writeOk = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == contents.size() && FlushFileBuffers(file);
    const auto writeError = writeOk ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!writeOk)
    {
        DeleteFileW(temporary.c_str());
        return Result<void>::failure({ErrorCode::IoError, winError("Writing Puzzle settings file", writeError)});
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto message = winError("Publishing Puzzle settings", GetLastError());
        DeleteFileW(temporary.c_str());
        return Result<void>::failure({ErrorCode::IoError, message});
    }
    return Result<void>::success();
}
}
