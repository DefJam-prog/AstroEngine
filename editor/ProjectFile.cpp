#include "ProjectFile.hpp"

#include <atomic>
#include <fstream>
#include <iomanip>
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
}
