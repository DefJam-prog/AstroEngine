#include "astro/runtime/PuzzleCampaignSaveAdapter.hpp"
#include "PuzzleCampaignSaveAdapterTestHooks.hpp"

#include <array>
#include <algorithm>
#include <cerrno>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <tuple>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace astro::runtime
{
namespace
{
thread_local testing::CampaignArchiveHook activeCampaignArchiveHook;

void notifyCampaignArchiveTestHook(const testing::CampaignArchiveBoundary boundary)
{
    if (activeCampaignArchiveHook) activeCampaignArchiveHook(boundary);
}

core::Error campaignSaveError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool selectedAs(const SaveProfile& profile, const std::string_view key, const std::size_t typeIndex)
{
    const auto field = profile.customFields().find(std::string(key));
    return field != profile.customFields().end() && field->second.index() == typeIndex;
}

core::Error previewError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool isSafeCampaignId(const std::string_view value)
{
    if (value.empty() || value.size() > 64) return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
    });
}

bool isReparsePoint(const std::filesystem::path& path, const std::filesystem::file_status& status)
{
    if (std::filesystem::is_symlink(status)) return true;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

core::Result<std::filesystem::path> validatePreviewDirectory(const std::filesystem::path& input,
    const bool required)
{
    if (input.empty())
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::InvalidArgument,
            "Save transfer preview path is empty"));
    for (const auto& component : input)
        if (component == "..")
            return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
                "Save transfer preview path must not contain parent traversal"));

    std::error_code error;
    const auto absolute = std::filesystem::absolute(input, error).lexically_normal();
    if (error)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not normalize save transfer preview path"));

    auto prefix = absolute.root_path();
    for (const auto& component : absolute.relative_path())
    {
        if (component == "." || component.empty()) continue;
        prefix /= component;
        const auto status = std::filesystem::symlink_status(prefix, error);
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
        {
            error.clear();
            break;
        }
        if (error)
            return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
                "Could not inspect save transfer preview path"));
        if (status.type() != std::filesystem::file_type::not_found && isReparsePoint(prefix, status))
            return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
                "Save transfer preview path contains a symlink or reparse point"));
    }

    const bool exists = std::filesystem::exists(absolute, error);
    if (error)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect save transfer preview directory"));
    if (required && (!exists || !std::filesystem::is_directory(absolute, error) || error))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::NotFound,
            "Save transfer preview source directory does not exist"));
    if (exists && !std::filesystem::is_directory(absolute, error))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Save transfer preview path is not a directory"));
    if (error)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect save transfer preview directory"));

    auto canonical = exists ? std::filesystem::canonical(absolute, error)
                            : std::filesystem::weakly_canonical(absolute, error);
    if (error)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not canonicalize save transfer preview directory"));
    return core::Result<std::filesystem::path>::success(std::move(canonical));
}

core::Result<bool> pathIsReparsePoint(const std::filesystem::path& path)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return core::Result<bool>::success(false);
    if (error)
        return core::Result<bool>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect a save slot path"));
    return core::Result<bool>::success(status.type() != std::filesystem::file_type::not_found
        && isReparsePoint(path, status));
}

struct PreviewFileInfo final
{
    bool exists = false;
    std::uintmax_t bytes = 0;
    bool fingerprintComplete = true;
    std::uint64_t fingerprint = 0;
};

core::Result<PreviewFileInfo> inspectPreviewFile(const std::filesystem::path& path,
    const bool captureFingerprint)
{
    auto reparse = pathIsReparsePoint(path);
    if (!reparse) return core::Result<PreviewFileInfo>::failure(reparse.error());
    if (reparse.value())
        return core::Result<PreviewFileInfo>::failure(previewError(core::ErrorCode::ValidationError,
            "Save slot files must not be symlinks or reparse points"));

    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
        return core::Result<PreviewFileInfo>::success({});
    }
    if (error)
        return core::Result<PreviewFileInfo>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect a save slot file"));
    if (status.type() == std::filesystem::file_type::not_found)
        return core::Result<PreviewFileInfo>::success({});
    if (!std::filesystem::is_regular_file(status))
    {
        if (!captureFingerprint)
        {
            PreviewFileInfo conflict;
            conflict.exists = true;
            return core::Result<PreviewFileInfo>::success(conflict);
        }
        return core::Result<PreviewFileInfo>::failure(previewError(core::ErrorCode::ValidationError,
            "Source save slot artifacts must be regular files"));
    }

    PreviewFileInfo result;
    result.exists = true;
    result.bytes = std::filesystem::file_size(path, error);
    if (error) return core::Result<PreviewFileInfo>::failure(previewError(core::ErrorCode::IoError,
        "Could not read save slot file size"));

    constexpr std::uintmax_t maximumFingerprintBytes = 4u * 1024u * 1024u;
    if (!captureFingerprint) return core::Result<PreviewFileInfo>::success(result);
    if (result.bytes > maximumFingerprintBytes)
    {
        result.fingerprintComplete = false;
        return core::Result<PreviewFileInfo>::success(result);
    }

    // FNV-1a is only a bounded stale-preview change detector, not an integrity or security check.
    std::uint64_t fingerprint = 14695981039346656037ull;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return core::Result<PreviewFileInfo>::failure(previewError(core::ErrorCode::IoError,
            "Could not read a save slot file for preview fingerprinting"));
    std::array<char, 8192> buffer{};
    std::uintmax_t bytesRead = 0;
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index)
        {
            fingerprint ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            fingerprint *= 1099511628211ull;
        }
        bytesRead += static_cast<std::uintmax_t>(count);
    }
    if (!input.eof() || bytesRead != result.bytes)
        return core::Result<PreviewFileInfo>::failure(previewError(core::ErrorCode::IoError,
            "Save slot changed or could not be read while fingerprinting"));
    result.fingerprint = fingerprint;
    return core::Result<PreviewFileInfo>::success(result);
}

std::filesystem::path slotFilePath(const std::filesystem::path& directory, const std::size_t slot)
{
    return directory / ("slot-" + std::to_string(slot) + ".astro-save");
}

std::filesystem::path backupFilePath(const std::filesystem::path& primary)
{
    auto backup = primary;
    backup += ".bak";
    return backup;
}

core::Result<void> validateCampaignProfile(const SaveProfile& profile)
{
    if (!selectedAs(profile, PuzzleSaveField::snapshot, SaveCustomValue{std::string{}}.index())
        || !selectedAs(profile, PuzzleSaveField::levelId, SaveCustomValue{std::string{}}.index())
        || !selectedAs(profile, PuzzleCampaignSaveField::campaignId, SaveCustomValue{std::string{}}.index())
        || !selectedAs(profile, PuzzleCampaignSaveField::ended, SaveCustomValue{false}.index()))
        return core::Result<void>::failure(campaignSaveError(core::ErrorCode::TypeMismatch,
            "SaveProfile must explicitly select campaign ID, level ID, completion state and Puzzle snapshot fields"));
    return core::Result<void>::success();
}

std::uint64_t contentFingerprint(const std::vector<std::uint8_t>& bytes)
{
    std::uint64_t fingerprint = 14695981039346656037ull;
    for (const auto byte : bytes)
    {
        fingerprint ^= byte;
        fingerprint *= 1099511628211ull;
    }
    return fingerprint;
}

core::Result<std::vector<std::uint8_t>> readTransferSourceFile(const std::filesystem::path& path,
    const std::uintmax_t expectedBytes, const std::uint64_t expectedFingerprint)
{
    constexpr std::uintmax_t maximumFileBytes = 4u * 1024u * 1024u;
    if (expectedBytes > maximumFileBytes || expectedBytes > std::numeric_limits<std::size_t>::max())
        return core::Result<std::vector<std::uint8_t>>::failure(previewError(core::ErrorCode::ValidationError,
            "Source save artifact exceeds the transfer size limit"));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(expectedBytes));
#ifdef _WIN32
    const HANDLE input = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (input == INVALID_HANDLE_VALUE)
        return core::Result<std::vector<std::uint8_t>>::failure(previewError(core::ErrorCode::IoError,
            "Could not open source save artifact for transfer"));
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER fileSize{};
    bool validFile = GetFileInformationByHandle(input, &info) != FALSE
        && GetFileSizeEx(input, &fileSize) != FALSE
        && (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0
        && fileSize.QuadPart >= 0 && static_cast<std::uintmax_t>(fileSize.QuadPart) == expectedBytes;
    std::size_t offset = 0;
    while (validFile && offset < bytes.size())
    {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 64u * 1024u));
        DWORD count = 0;
        validFile = ReadFile(input, bytes.data() + offset, chunk, &count, nullptr) != FALSE && count == chunk;
        offset += count;
    }
    unsigned char extra = 0;
    DWORD extraCount = 0;
    if (validFile) validFile = ReadFile(input, &extra, 1, &extraCount, nullptr) != FALSE && extraCount == 0;
    CloseHandle(input);
    if (!validFile)
        return core::Result<std::vector<std::uint8_t>>::failure(previewError(core::ErrorCode::IoError,
            "Source save artifact changed or is not a regular file"));
#else
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int input = ::open(path.c_str(), flags);
    if (input < 0)
        return core::Result<std::vector<std::uint8_t>>::failure(previewError(core::ErrorCode::IoError,
            "Could not open source save artifact for transfer"));
    struct stat info{};
    bool validFile = ::fstat(input, &info) == 0 && S_ISREG(info.st_mode)
        && info.st_size >= 0 && static_cast<std::uintmax_t>(info.st_size) == expectedBytes;
    std::size_t offset = 0;
    while (validFile && offset < bytes.size())
    {
        const auto count = ::read(input, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { validFile = false; break; }
        offset += static_cast<std::size_t>(count);
    }
    unsigned char extra = 0;
    if (validFile)
    {
        ssize_t count = 0;
        do { count = ::read(input, &extra, 1); } while (count < 0 && errno == EINTR);
        validFile = count == 0;
    }
    ::close(input);
    if (!validFile)
        return core::Result<std::vector<std::uint8_t>>::failure(previewError(core::ErrorCode::IoError,
            "Source save artifact changed or is not a regular file"));
#endif
    if (contentFingerprint(bytes) != expectedFingerprint)
        return core::Result<std::vector<std::uint8_t>>::failure(previewError(core::ErrorCode::ValidationError,
            "Source save artifact no longer matches the approved preview"));
    return core::Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

struct ExclusiveWriteOutcome final
{
    bool created = false;
    bool complete = false;
    std::string diagnostic;
};

class CampaignTransferJournalLease final
{
public:
    CampaignTransferJournalLease() = default;
    CampaignTransferJournalLease(const CampaignTransferJournalLease&) = delete;
    CampaignTransferJournalLease& operator=(const CampaignTransferJournalLease&) = delete;
    ~CampaignTransferJournalLease() { reset(); }

#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    OVERLAPPED lockState{};
    bool locked = false;
    bool valid() const noexcept { return file != INVALID_HANDLE_VALUE; }
    void reset() noexcept
    {
        if (file != INVALID_HANDLE_VALUE)
        {
            if (locked) UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &lockState);
            CloseHandle(file);
        }
        file = INVALID_HANDLE_VALUE;
        locked = false;
        lockState = {};
    }
#else
    int directory = -1;
    int file = -1;
    std::string leaf;
    bool valid() const noexcept { return file >= 0 && directory >= 0; }
    void reset() noexcept
    {
        if (file >= 0) ::close(file);
        if (directory >= 0) ::close(directory);
        file = -1;
        directory = -1;
        leaf.clear();
    }
#endif
};

bool readTransferJournalLease(const CampaignTransferJournalLease& lease,
    std::string& text, std::string& diagnostic);

ExclusiveWriteOutcome writeTransferTargetExclusive(const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes)
{
#ifdef _WIN32
    const HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        const auto code = GetLastError();
        return {false, false, code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS
            ? "Destination slot artifact appeared during transfer"
            : "Could not create destination slot artifact exclusively"};
    }
    std::size_t offset = 0;
    bool complete = true;
    while (offset < bytes.size())
    {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 64u * 1024u));
        DWORD count = 0;
        if (WriteFile(output, bytes.data() + offset, chunk, &count, nullptr) == FALSE || count != chunk)
        { complete = false; break; }
        offset += count;
    }
    if (complete && FlushFileBuffers(output) == FALSE) complete = false;
    if (CloseHandle(output) == FALSE) complete = false;
    return {true, complete, complete ? std::string{} : "Could not completely write destination slot artifact"};
#else
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int output = ::open(path.c_str(), flags, 0600);
    if (output < 0)
        return {false, false, errno == EEXIST
            ? "Destination slot artifact appeared during transfer"
            : "Could not create destination slot artifact exclusively"};
    std::size_t offset = 0;
    bool complete = true;
    while (offset < bytes.size())
    {
        const auto count = ::write(output, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { complete = false; break; }
        offset += static_cast<std::size_t>(count);
    }
    if (complete && ::fsync(output) != 0) complete = false;
    if (::close(output) != 0) complete = false;
    return {true, complete, complete ? std::string{} : "Could not completely write destination slot artifact"};
#endif
}

core::Result<void> ensureTransferTargetDirectory(const std::filesystem::path& target,
    std::vector<std::filesystem::path>& created)
{
    auto current = target.root_path();
    std::error_code error;
    for (const auto& component : target.relative_path())
    {
        if (component.empty() || component == ".") continue;
        current /= component;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            if (std::filesystem::create_directory(current, error))
            {
                created.push_back(current);
                continue;
            }
            if (error)
                return core::Result<void>::failure(previewError(
                    core::ErrorCode::IoError, "Could not create the transfer target directory"));
            const auto racedStatus = std::filesystem::symlink_status(current, error);
            if (error || !std::filesystem::is_directory(racedStatus) || isReparsePoint(current, racedStatus))
                return core::Result<void>::failure(previewError(
                    core::ErrorCode::ValidationError, "Transfer target directory changed during creation"));
            continue;
        }
        if (error)
            return core::Result<void>::failure(previewError(
                core::ErrorCode::IoError, "Could not inspect a transfer target directory"));
        if (!std::filesystem::is_directory(status) || isReparsePoint(current, status))
            return core::Result<void>::failure(previewError(
                core::ErrorCode::ValidationError, "Transfer target path contains a non-directory or reparse point"));
    }
    return core::Result<void>::success();
}

bool removeCreatedArtifactIfOwned(const std::filesystem::path& path,
    const std::vector<std::uint8_t>& expectedBytes, std::string& diagnostic)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) return true;
    if (error) { diagnostic = "could not inspect rollback path"; return false; }
    if (!std::filesystem::is_regular_file(status) || isReparsePoint(path, status))
    { diagnostic = "rollback path is not the regular file created by the transfer"; return false; }
    const auto currentSize = std::filesystem::file_size(path, error);
    if (error) { diagnostic = "could not read rollback file size"; return false; }
    if (currentSize > expectedBytes.size()) { diagnostic = "rollback file is larger than the transfer output"; return false; }
    std::ifstream input(path, std::ios::binary);
    if (!input) { diagnostic = "could not open rollback file"; return false; }
    std::vector<std::uint8_t> actual(static_cast<std::size_t>(currentSize));
    if (!actual.empty() && !input.read(reinterpret_cast<char*>(actual.data()),
        static_cast<std::streamsize>(actual.size())))
    { diagnostic = "could not read rollback file"; return false; }
    if (!std::equal(actual.begin(), actual.end(), expectedBytes.begin()))
    { diagnostic = "rollback file no longer matches the transfer output"; return false; }
    input.close();
    const bool removed = std::filesystem::remove(path, error);
    if (removed && !error) return true;
    error.clear();
    if (!std::filesystem::exists(path, error) && !error) return true;
    diagnostic = "could not remove rollback file";
    return false;
}

constexpr std::string_view transferJournalFileName = ".astro-campaign-transfer.pending";
constexpr std::uintmax_t maximumTransferJournalBytes = 4096;

std::filesystem::path transferJournalPath(const std::filesystem::path& targetDirectory)
{
    return targetDirectory / transferJournalFileName;
}

std::filesystem::path campaignTransferArchivePath(const std::filesystem::path& targetDirectory,
    const std::uint64_t journalFingerprint)
{
    std::ostringstream archiveName;
    archiveName << ".astro-campaign-transfer.invalid-" << std::hex << journalFingerprint << ".archive";
    return targetDirectory / archiveName.str();
}

PuzzleCampaignSaveArchiveInspection inspectCampaignArchivePath(
    const std::filesystem::path& archivePath, const std::string_view expectedJournal)
{
    PuzzleCampaignSaveArchiveInspection result;
    result.archivePath = archivePath;
    result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::NotApplicable;
#ifdef _WIN32
    const HANDLE file = CreateFileW(archivePath.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        {
            result.pathKind = PuzzleCampaignSaveArchivePathKind::Missing;
            return result;
        }
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
        result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Unavailable;
        result.diagnostic = "Could not open archive path without following reparse points";
        return result;
    }
    BY_HANDLE_FILE_INFORMATION before{};
    if (GetFileInformationByHandle(file, &before) == FALSE)
    {
        CloseHandle(file);
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
        result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Unavailable;
        result.diagnostic = "Could not inspect archive handle";
        return result;
    }
    if ((before.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        result.pathKind = PuzzleCampaignSaveArchivePathKind::SymlinkOrReparsePoint;
    else if ((before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Directory;
    else if (GetFileType(file) != FILE_TYPE_DISK)
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Other;
    else
    {
        result.pathKind = PuzzleCampaignSaveArchivePathKind::RegularFile;
        const ULARGE_INTEGER size{.LowPart = before.nFileSizeLow, .HighPart = before.nFileSizeHigh};
        result.byteSize = size.QuadPart;
        if (*result.byteSize > maximumTransferJournalBytes)
        {
            result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Oversized;
            CloseHandle(file);
            return result;
        }
        std::string bytes(static_cast<std::size_t>(*result.byteSize), '\0');
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            DWORD count = 0;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 4096));
            if (ReadFile(file, bytes.data() + offset, chunk, &count, nullptr) == FALSE || count == 0) break;
            offset += count;
        }
        BY_HANDLE_FILE_INFORMATION after{};
        const bool stable = offset == bytes.size() && GetFileInformationByHandle(file, &after) != FALSE
            && before.dwVolumeSerialNumber == after.dwVolumeSerialNumber
            && before.nFileIndexHigh == after.nFileIndexHigh && before.nFileIndexLow == after.nFileIndexLow
            && before.nFileSizeHigh == after.nFileSizeHigh && before.nFileSizeLow == after.nFileSizeLow
            && before.ftLastWriteTime.dwHighDateTime == after.ftLastWriteTime.dwHighDateTime
            && before.ftLastWriteTime.dwLowDateTime == after.ftLastWriteTime.dwLowDateTime;
        if (!stable)
        {
            result.pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
            result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Unavailable;
            result.diagnostic = "Archive changed or could not be read during inspection";
        }
        else if (bytes == expectedJournal)
            result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::ExactMatch;
        else if (bytes.size() < expectedJournal.size()
            && std::equal(bytes.begin(), bytes.end(), expectedJournal.begin()))
            result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::PrefixMatch;
        else
            result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Different;
    }
    CloseHandle(file);
#else
    const auto parent = archivePath.parent_path();
    const auto leaf = archivePath.filename().native();
    int directoryFlags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
    directoryFlags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    directoryFlags |= O_NOFOLLOW;
#endif
    const int directory = ::open(parent.c_str(), directoryFlags);
    if (directory < 0)
    {
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
        result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Unavailable;
        result.diagnostic = "Could not open archive directory without following links";
        return result;
    }
    struct stat before{};
    if (::fstatat(directory, leaf.c_str(), &before, AT_SYMLINK_NOFOLLOW) != 0)
    {
        const int code = errno;
        ::close(directory);
        if (code == ENOENT)
        {
            result.pathKind = PuzzleCampaignSaveArchivePathKind::Missing;
            return result;
        }
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
        result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Unavailable;
        result.diagnostic = "Could not inspect archive path without following links";
        return result;
    }
    if (S_ISLNK(before.st_mode))
        result.pathKind = PuzzleCampaignSaveArchivePathKind::SymlinkOrReparsePoint;
    else if (S_ISDIR(before.st_mode))
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Directory;
    else if (!S_ISREG(before.st_mode))
        result.pathKind = PuzzleCampaignSaveArchivePathKind::Other;
    else
    {
        result.pathKind = PuzzleCampaignSaveArchivePathKind::RegularFile;
        result.byteSize = static_cast<std::uintmax_t>(before.st_size);
        if (before.st_size < 0 || *result.byteSize > maximumTransferJournalBytes)
            result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Oversized;
        else
        {
            int fileFlags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
            fileFlags |= O_NOFOLLOW;
#endif
            const int file = ::openat(directory, leaf.c_str(), fileFlags);
            struct stat opened{};
            if (file < 0 || ::fstat(file, &opened) != 0 || !S_ISREG(opened.st_mode)
                || opened.st_dev != before.st_dev || opened.st_ino != before.st_ino)
            {
                if (file >= 0) ::close(file);
                result.pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
                result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Unavailable;
                result.diagnostic = "Archive changed or could not be opened safely";
            }
            else
            {
                std::string bytes(static_cast<std::size_t>(*result.byteSize), '\0');
                std::size_t offset = 0;
                while (offset < bytes.size())
                {
                    const auto count = ::pread(file, bytes.data() + offset, bytes.size() - offset,
                        static_cast<off_t>(offset));
                    if (count <= 0) break;
                    offset += static_cast<std::size_t>(count);
                }
                struct stat after{};
                const bool stable = offset == bytes.size() && ::fstat(file, &after) == 0
                    && before.st_dev == after.st_dev && before.st_ino == after.st_ino
                    && before.st_size == after.st_size && before.st_mtime == after.st_mtime
                    && before.st_ctime == after.st_ctime;
                ::close(file);
                if (!stable)
                {
                    result.pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
                    result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Unavailable;
                    result.diagnostic = "Archive changed or could not be read during inspection";
                }
                else if (bytes == expectedJournal)
                    result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::ExactMatch;
                else if (bytes.size() < expectedJournal.size()
                    && std::equal(bytes.begin(), bytes.end(), expectedJournal.begin()))
                    result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::PrefixMatch;
                else
                    result.contentMatch = PuzzleCampaignSaveArchiveContentMatch::Different;
            }
        }
    }
    ::close(directory);
#endif
    return result;
}

struct CampaignArchiveConflictSourceLease final
{
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION openedInfo{};
    bool valid() const noexcept { return file != INVALID_HANDLE_VALUE; }
    void reset() noexcept
    {
        if (valid()) CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        openedInfo = {};
    }
#else
    int directory = -1;
    int file = -1;
    std::string leaf;
    struct stat openedInfo{};
    bool valid() const noexcept { return directory >= 0 && file >= 0; }
    void reset() noexcept
    {
        if (file >= 0) ::close(file);
        if (directory >= 0) ::close(directory);
        file = -1;
        directory = -1;
        leaf.clear();
        openedInfo = {};
    }
#endif
    ~CampaignArchiveConflictSourceLease() { reset(); }
};

bool acquireCampaignArchiveConflictSource(const std::filesystem::path& path,
    CampaignArchiveConflictSourceLease& lease, std::string& diagnostic)
{
#ifdef _WIN32
    lease.file = CreateFileW(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (lease.file == INVALID_HANDLE_VALUE
        || GetFileInformationByHandle(lease.file, &lease.openedInfo) == FALSE)
    {
        lease.reset();
        diagnostic = "could not open the archive conflict as a stable no-follow read handle";
        return false;
    }
    const auto& info = lease.openedInfo;
    const ULARGE_INTEGER size{.LowPart = info.nFileSizeLow, .HighPart = info.nFileSizeHigh};
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0
        || GetFileType(lease.file) != FILE_TYPE_DISK || size.QuadPart > maximumTransferJournalBytes)
    {
        lease.reset();
        diagnostic = "archive conflict is not a bounded regular file";
        return false;
    }
#else
    auto parent = path.parent_path();
    int directoryFlags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
    directoryFlags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    directoryFlags |= O_NOFOLLOW;
#endif
    lease.directory = ::open(parent.c_str(), directoryFlags);
    if (lease.directory < 0)
    {
        diagnostic = "could not open archive conflict directory without following links";
        return false;
    }
    lease.leaf = path.filename().string();
    struct stat named{};
    if (lease.leaf.empty() || ::fstatat(lease.directory, lease.leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(named.st_mode) || named.st_size < 0
        || static_cast<std::uintmax_t>(named.st_size) > maximumTransferJournalBytes)
    {
        lease.reset();
        diagnostic = "archive conflict is not a bounded regular file";
        return false;
    }
    int fileFlags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
    fileFlags |= O_NOFOLLOW;
#endif
    lease.file = ::openat(lease.directory, lease.leaf.c_str(), fileFlags);
    if (lease.file < 0 || ::fstat(lease.file, &lease.openedInfo) != 0
        || !S_ISREG(lease.openedInfo.st_mode) || named.st_dev != lease.openedInfo.st_dev
        || named.st_ino != lease.openedInfo.st_ino
        || ::flock(lease.file, LOCK_EX | LOCK_NB) != 0)
    {
        lease.reset();
        diagnostic = "archive conflict changed or is locked by another cooperating process";
        return false;
    }
    if (lease.openedInfo.st_size < 0
        || static_cast<std::uintmax_t>(lease.openedInfo.st_size) > maximumTransferJournalBytes)
    {
        lease.reset();
        diagnostic = "archive conflict exceeds the supported copy size";
        return false;
    }
#endif
    return true;
}

bool readCampaignArchiveConflictSource(const CampaignArchiveConflictSourceLease& lease,
    std::string& bytes, std::string& diagnostic)
{
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION before{};
    if (!lease.valid() || GetFileInformationByHandle(lease.file, &before) == FALSE
        || before.dwVolumeSerialNumber != lease.openedInfo.dwVolumeSerialNumber
        || before.nFileIndexHigh != lease.openedInfo.nFileIndexHigh
        || before.nFileIndexLow != lease.openedInfo.nFileIndexLow
        || (before.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
    {
        diagnostic = "archive conflict source identity changed";
        return false;
    }
    const ULARGE_INTEGER size{.LowPart = before.nFileSizeLow, .HighPart = before.nFileSizeHigh};
    if (size.QuadPart > maximumTransferJournalBytes)
    {
        diagnostic = "archive conflict source exceeds the supported copy size";
        return false;
    }
    LARGE_INTEGER beginning{};
    if (SetFilePointerEx(lease.file, beginning, nullptr, FILE_BEGIN) == FALSE)
    {
        diagnostic = "could not seek archive conflict source handle";
        return false;
    }
    bytes.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD count = 0;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 4096));
        if (ReadFile(lease.file, bytes.data() + offset, chunk, &count, nullptr) == FALSE || count == 0)
            break;
        offset += count;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if (offset != bytes.size() || GetFileInformationByHandle(lease.file, &after) == FALSE
        || before.dwVolumeSerialNumber != after.dwVolumeSerialNumber
        || before.nFileIndexHigh != after.nFileIndexHigh || before.nFileIndexLow != after.nFileIndexLow
        || before.nFileSizeHigh != after.nFileSizeHigh || before.nFileSizeLow != after.nFileSizeLow
        || before.ftLastWriteTime.dwHighDateTime != after.ftLastWriteTime.dwHighDateTime
        || before.ftLastWriteTime.dwLowDateTime != after.ftLastWriteTime.dwLowDateTime)
    {
        diagnostic = "archive conflict source changed or could not be read completely";
        return false;
    }
#else
    struct stat before{}, named{};
    if (!lease.valid() || ::fstat(lease.file, &before) != 0 || !S_ISREG(before.st_mode)
        || before.st_dev != lease.openedInfo.st_dev || before.st_ino != lease.openedInfo.st_ino
        || before.st_size < 0 || static_cast<std::uintmax_t>(before.st_size) > maximumTransferJournalBytes
        || ::fstatat(lease.directory, lease.leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(named.st_mode) || named.st_dev != before.st_dev || named.st_ino != before.st_ino)
    {
        diagnostic = "archive conflict source identity changed";
        return false;
    }
    bytes.assign(static_cast<std::size_t>(before.st_size), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const auto count = ::pread(lease.file, bytes.data() + offset, bytes.size() - offset,
            static_cast<off_t>(offset));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    struct stat after{}, namedAfter{};
    if (offset != bytes.size() || ::fstat(lease.file, &after) != 0
        || ::fstatat(lease.directory, lease.leaf.c_str(), &namedAfter, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(namedAfter.st_mode) || before.st_dev != after.st_dev || before.st_ino != after.st_ino
        || before.st_size != after.st_size || before.st_mtime != after.st_mtime
        || before.st_ctime != after.st_ctime || before.st_dev != namedAfter.st_dev
        || before.st_ino != namedAfter.st_ino)
    {
        diagnostic = "archive conflict source changed or could not be read completely";
        return false;
    }
#endif
    return true;
}

std::filesystem::path nextCampaignArchiveConflictCopyPath(const std::filesystem::path& archivePath)
{
    static std::atomic<std::uint64_t> sequence{0};
#ifdef _WIN32
    const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto processId = static_cast<unsigned long long>(::getpid());
#endif
    const auto serial = static_cast<unsigned long long>(sequence.fetch_add(1, std::memory_order_relaxed));
    return archivePath.parent_path() / (archivePath.filename().string() + ".preserved-"
        + std::to_string(processId) + "-" + std::to_string(serial) + ".copy");
}

bool isGeneratedCampaignArchiveConflictCopyPath(const std::filesystem::path& archivePath,
    const std::filesystem::path& destinationPath)
{
    if (destinationPath.parent_path().lexically_normal() != archivePath.parent_path().lexically_normal())
        return false;
    const auto archiveName = archivePath.filename().string();
    const auto destinationName = destinationPath.filename().string();
    const auto prefix = archiveName + ".preserved-";
    if (!destinationName.starts_with(prefix) || !destinationName.ends_with(".copy")) return false;
    const auto identity = std::string_view(destinationName).substr(prefix.size(),
        destinationName.size() - prefix.size() - std::string_view(".copy").size());
    const auto separator = identity.find('-');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= identity.size()
        || identity.find('-', separator + 1) != std::string_view::npos) return false;
    for (std::size_t index = 0; index < identity.size(); ++index)
        if (index == separator ? identity[index] != '-'
            : identity[index] < '0' || identity[index] > '9') return false;
    return true;
}

std::filesystem::path nextCampaignArchiveStagingPath(const std::filesystem::path& archivePath)
{
    static std::atomic<std::uint64_t> sequence{0};
#ifdef _WIN32
    const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto processId = static_cast<unsigned long long>(::getpid());
#endif
    const auto serial = static_cast<unsigned long long>(sequence.fetch_add(1, std::memory_order_relaxed));
    return archivePath.parent_path() / (archivePath.filename().string() + ".tmp-"
        + std::to_string(processId) + "-" + std::to_string(serial));
}

core::Result<std::string> readTransferJournalText(const std::filesystem::path& path)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) return core::Result<std::string>::failure(previewError(core::ErrorCode::IoError,
        "Could not inspect campaign transfer recovery journal"));
    if (!std::filesystem::is_regular_file(status) || isReparsePoint(path, status))
        return core::Result<std::string>::failure(previewError(core::ErrorCode::ValidationError,
            "Campaign transfer recovery journal is not a regular file"));
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximumTransferJournalBytes)
        return core::Result<std::string>::failure(previewError(core::ErrorCode::ValidationError,
            "Campaign transfer recovery journal exceeds its size limit"));
    std::ifstream input(path, std::ios::binary);
    if (!input) return core::Result<std::string>::failure(previewError(core::ErrorCode::IoError,
        "Could not read campaign transfer recovery journal"));
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!text.empty() && !input.read(text.data(), static_cast<std::streamsize>(text.size())))
        return core::Result<std::string>::failure(previewError(core::ErrorCode::IoError,
            "Could not read complete campaign transfer recovery journal"));
    return core::Result<std::string>::success(std::move(text));
}

bool parseUnsigned(std::string_view text, std::uint64_t& value, const int base = 10)
{
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>> parseTransferJournal(
    const std::string_view text, std::string& campaignId)
{
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line) || line != "ASTRO_CAMPAIGN_TRANSFER 1")
        return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
            previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal header is invalid"));
    if (!std::getline(input, line) || !line.starts_with("CAMPAIGN "))
        return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
            previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal identity is missing"));
    campaignId = line.substr(9);
    if (!isSafeCampaignId(campaignId))
        return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
            previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal identity is unsafe"));
    if (!std::getline(input, line) || !line.starts_with("COUNT "))
        return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
            previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal count is missing"));
    std::uint64_t count = 0;
    if (!parseUnsigned(std::string_view(line).substr(6), count) || count == 0 || count > 20)
        return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
            previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal count is invalid"));

    std::vector<PuzzleCampaignSaveTransferRecoveryArtifact> artifacts;
    std::array<std::array<bool, 2>, 10> seen{};
    for (std::uint64_t index = 0; index < count; ++index)
    {
        if (!std::getline(input, line))
            return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
                previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal is truncated"));
        std::istringstream row(line);
        std::string keyword, kind, sizeText, fingerprintText, extra;
        std::uint64_t slot = 0, size = 0, fingerprint = 0;
        if (!(row >> keyword >> slot >> kind >> sizeText >> fingerprintText)
            || (row >> extra) || keyword != "ITEM" || slot >= 10
            || (kind != "P" && kind != "B")
            || !parseUnsigned(sizeText, size) || size > 4u * 1024u * 1024u
            || !parseUnsigned(fingerprintText, fingerprint, 16))
            return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
                previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal artifact is invalid"));
        const bool backup = kind == "B";
        auto& wasSeen = seen[static_cast<std::size_t>(slot)][backup ? 1u : 0u];
        if (wasSeen)
            return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
                previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal has duplicate artifacts"));
        wasSeen = true;
        PuzzleCampaignSaveTransferRecoveryArtifact artifact;
        artifact.slot = static_cast<std::size_t>(slot);
        artifact.backup = backup;
        artifact.expectedBytes = size;
        artifact.targetPath = backup
            ? backupFilePath(slotFilePath({}, artifact.slot)) : slotFilePath({}, artifact.slot);
        artifact.expectedFingerprint = fingerprint;
        artifacts.push_back(std::move(artifact));
    }
    if (!std::getline(input, line) || line != "END" || std::getline(input, line))
        return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::failure(
            previewError(core::ErrorCode::ValidationError, "Campaign transfer recovery journal has trailing or missing data"));
    return core::Result<std::vector<PuzzleCampaignSaveTransferRecoveryArtifact>>::success(std::move(artifacts));
}

std::string formatTransferJournal(const std::string_view campaignId,
    const std::vector<std::tuple<std::size_t, bool, std::uintmax_t, std::uint64_t>>& artifacts)
{
    std::ostringstream output;
    output << "ASTRO_CAMPAIGN_TRANSFER 1\nCAMPAIGN " << campaignId << "\nCOUNT " << artifacts.size() << '\n';
    for (const auto& [slot, backup, size, fingerprint] : artifacts)
    {
        output << "ITEM " << slot << ' ' << (backup ? 'B' : 'P') << ' ' << size << ' '
            << std::hex << fingerprint << std::dec << '\n';
    }
    output << "END\n";
    return output.str();
}

ExclusiveWriteOutcome writeTransferJournalExclusive(const std::filesystem::path& path,
    const std::string_view text, CampaignTransferJournalLease* retainedLease = nullptr)
{
#ifdef _WIN32
    CampaignTransferJournalLease localLease;
    auto& lease = retainedLease ? *retainedLease : localLease;
    const HANDLE output = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        const auto code = GetLastError();
        return {false, false, code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS
            ? "A pending campaign transfer journal already exists"
            : "Could not create campaign transfer recovery journal"};
    }
    OVERLAPPED lockState{};
    if (LockFileEx(output, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
        0, MAXDWORD, MAXDWORD, &lockState) == FALSE)
    {
        CloseHandle(output);
        return {true, false, "Could not lock campaign transfer recovery journal"};
    }
    std::size_t offset = 0;
    bool complete = true;
    const auto partialWriteBoundary = activeCampaignArchiveHook && text.size() > 1
        ? text.size() / 2 : text.size();
    while (offset < text.size())
    {
        const auto writeLimit = offset < partialWriteBoundary ? partialWriteBoundary : text.size();
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(writeLimit - offset, 64u * 1024u));
        DWORD count = 0;
        if (WriteFile(output, text.data() + offset, chunk, &count, nullptr) == FALSE || count != chunk)
        { complete = false; break; }
        offset += count;
        if (activeCampaignArchiveHook && offset == partialWriteBoundary && partialWriteBoundary < text.size())
            notifyCampaignArchiveTestHook(testing::CampaignArchiveBoundary::PartialWrite);
    }
    if (complete && FlushFileBuffers(output) == FALSE) complete = false;
    if (complete && retainedLease)
    {
        lease.file = output;
        lease.lockState = lockState;
        lease.locked = true;
    }
    else
    {
        UnlockFileEx(output, 0, MAXDWORD, MAXDWORD, &lockState);
        if (CloseHandle(output) == FALSE) complete = false;
    }
    return {true, complete, complete ? std::string{} : "Could not flush campaign transfer recovery journal"};
#else
    auto parent = path.parent_path();
    if (parent.empty()) parent = ".";
    int directoryFlags = O_RDONLY;
#ifdef O_DIRECTORY
    directoryFlags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    directoryFlags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    directoryFlags |= O_NOFOLLOW;
#endif
    const int directory = ::open(parent.c_str(), directoryFlags);
    if (directory < 0)
        return {false, false, "Could not open campaign transfer journal directory"};
    const auto leaf = path.filename().string();
    if (leaf.empty() || leaf == "." || leaf == "..")
    {
        ::close(directory);
        return {false, false, "Campaign transfer journal filename is invalid"};
    }
    int flags = O_RDWR | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int output = ::openat(directory, leaf.c_str(), flags, 0600);
    if (output < 0)
    {
        const auto savedErrno = errno;
        ::close(directory);
        return {false, false, savedErrno == EEXIST
            ? "A pending campaign transfer journal already exists"
            : "Could not create campaign transfer recovery journal"};
    }
    if (::flock(output, LOCK_EX | LOCK_NB) != 0)
    {
        ::close(output);
        ::close(directory);
        return {true, false, "Could not lock campaign transfer recovery journal"};
    }
    std::size_t offset = 0;
    bool complete = true;
    const auto partialWriteBoundary = activeCampaignArchiveHook && text.size() > 1
        ? text.size() / 2 : text.size();
    while (offset < text.size())
    {
        const auto writeLimit = offset < partialWriteBoundary ? partialWriteBoundary : text.size();
        const auto count = ::write(output, text.data() + offset, writeLimit - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { complete = false; break; }
        offset += static_cast<std::size_t>(count);
        if (activeCampaignArchiveHook && offset >= partialWriteBoundary
            && partialWriteBoundary < text.size())
            notifyCampaignArchiveTestHook(testing::CampaignArchiveBoundary::PartialWrite);
    }
    if (complete && ::fsync(output) != 0) complete = false;
    if (complete && retainedLease)
    {
        retainedLease->directory = directory;
        retainedLease->file = output;
        retainedLease->leaf = leaf;
    }
    else
    {
        if (::close(output) != 0) complete = false;
        ::close(directory);
    }
    return {true, complete, complete ? std::string{} : "Could not flush campaign transfer recovery journal"};
#endif
}

bool publishCampaignArchiveNoReplace(CampaignTransferJournalLease& staged,
    const std::filesystem::path& archivePath, const std::string_view expectedText,
    std::string& diagnostic)
{
    std::string stagedText;
    if (!staged.valid() || !readTransferJournalLease(staged, stagedText, diagnostic)
        || stagedText != expectedText)
    {
        if (diagnostic.empty()) diagnostic = "staged campaign archive does not match the reviewed journal";
        return false;
    }
    notifyCampaignArchiveTestHook(testing::CampaignArchiveBoundary::VerifiedBeforePublish);
#ifdef _WIN32
    const auto destination = archivePath.native();
    if (destination.size() > ((std::numeric_limits<DWORD>::max)() - sizeof(FILE_RENAME_INFO)) / sizeof(wchar_t))
    {
        diagnostic = "campaign archive path is too long to publish";
        return false;
    }
    const auto nameBytes = destination.size() * sizeof(wchar_t);
    std::vector<std::max_align_t> storage((sizeof(FILE_RENAME_INFO) + nameBytes
        + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(rename->FileName, destination.data(), nameBytes);
    if (SetFileInformationByHandle(staged.file, FileRenameInfo, rename,
        static_cast<DWORD>(sizeof(FILE_RENAME_INFO) + nameBytes)) == FALSE)
    {
        const auto code = GetLastError();
        diagnostic = code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS
            ? "campaign archive path appeared before publication"
            : "could not atomically publish staged campaign archive without replacement";
        return false;
    }
    notifyCampaignArchiveTestHook(testing::CampaignArchiveBoundary::PublishedBeforeStagingCleanup);
    staged.reset();
    return true;
#else
    struct stat opened{}, named{};
    if (::fstat(staged.file, &opened) != 0 || !S_ISREG(opened.st_mode)
        || ::fstatat(staged.directory, staged.leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(named.st_mode) || opened.st_dev != named.st_dev || opened.st_ino != named.st_ino)
    {
        diagnostic = "staged campaign archive directory entry changed before publication";
        return false;
    }
    const auto finalLeaf = archivePath.filename().string();
    if (finalLeaf.empty() || finalLeaf == "." || finalLeaf == ".."
        || ::linkat(staged.directory, staged.leaf.c_str(), staged.directory, finalLeaf.c_str(), 0) != 0)
    {
        diagnostic = errno == EEXIST
            ? "campaign archive path appeared before publication"
            : "filesystem could not publish staged campaign archive with an atomic no-replace link";
        return false;
    }
    struct stat published{};
    if (::fstatat(staged.directory, finalLeaf.c_str(), &published, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(published.st_mode) || opened.st_dev != published.st_dev || opened.st_ino != published.st_ino)
    {
        diagnostic = "published campaign archive could not be verified against the staged file";
        return false;
    }
    if (::fsync(staged.file) != 0 || ::fsync(staged.directory) != 0)
    {
        diagnostic = "campaign archive was published but filesystem durability could not be confirmed";
        return false;
    }
    notifyCampaignArchiveTestHook(testing::CampaignArchiveBoundary::PublishedBeforeStagingCleanup);
    if (::unlinkat(staged.directory, staged.leaf.c_str(), 0) != 0)
    {
        diagnostic = "campaign archive was published but its staging name could not be removed";
        return false;
    }
    if (::fsync(staged.directory) != 0)
    {
        diagnostic = "campaign archive was published but staging cleanup durability could not be confirmed";
        staged.reset();
        return false;
    }
    staged.reset();
    return true;
#endif
}

bool acquireTransferJournalLease(const std::filesystem::path& path,
    CampaignTransferJournalLease& lease, std::string& diagnostic)
{
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        diagnostic = "could not obtain an exclusive handle for the campaign transfer journal";
        return false;
    }
    lease.file = file;
    if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
        0, MAXDWORD, MAXDWORD, &lease.lockState) == FALSE)
    {
        lease.reset();
        diagnostic = "campaign transfer journal is locked by another cooperating process";
        return false;
    }
    lease.locked = true;
#else
    auto parent = path.parent_path();
    if (parent.empty()) parent = ".";
    int directoryFlags = O_RDONLY;
#ifdef O_DIRECTORY
    directoryFlags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    directoryFlags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    directoryFlags |= O_NOFOLLOW;
#endif
    const int directory = ::open(parent.c_str(), directoryFlags);
    if (directory < 0)
    {
        diagnostic = "could not open the campaign transfer journal directory";
        return false;
    }
    const auto leaf = path.filename().string();
    if (leaf.empty() || leaf == "." || leaf == "..")
    {
        ::close(directory);
        diagnostic = "campaign transfer journal filename is invalid";
        return false;
    }
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int file = ::openat(directory, leaf.c_str(), flags);
    if (file < 0)
    {
        ::close(directory);
        diagnostic = "could not open the campaign transfer journal without following links";
        return false;
    }
    struct stat info{};
    if (::fstat(file, &info) != 0 || !S_ISREG(info.st_mode))
    {
        ::close(file);
        ::close(directory);
        diagnostic = "campaign transfer journal is not a regular file";
        return false;
    }
    if (::flock(file, LOCK_EX | LOCK_NB) != 0)
    {
        ::close(file);
        ::close(directory);
        diagnostic = "campaign transfer journal is locked by another cooperating process";
        return false;
    }
    lease.directory = directory;
    lease.file = file;
    lease.leaf = leaf;
#endif
    return true;
}

bool readTransferJournalLease(const CampaignTransferJournalLease& lease,
    std::string& text, std::string& diagnostic)
{
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION information{};
    LARGE_INTEGER size{};
    if (lease.file == INVALID_HANDLE_VALUE || GetFileType(lease.file) != FILE_TYPE_DISK
        || GetFileInformationByHandle(lease.file, &information) == FALSE
        || (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0
        || GetFileSizeEx(lease.file, &size) == FALSE || size.QuadPart < 0
        || static_cast<unsigned long long>(size.QuadPart) > maximumTransferJournalBytes)
    {
        diagnostic = "campaign transfer journal handle is not a bounded regular file";
        return false;
    }
    LARGE_INTEGER beginning{};
    if (SetFilePointerEx(lease.file, beginning, nullptr, FILE_BEGIN) == FALSE)
    {
        diagnostic = "could not seek campaign transfer journal handle";
        return false;
    }
    text.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, 64u * 1024u));
        DWORD count = 0;
        if (ReadFile(lease.file, text.data() + offset, chunk, &count, nullptr) == FALSE || count != chunk)
        {
            diagnostic = "could not read complete campaign transfer journal through its locked handle";
            return false;
        }
        offset += count;
    }
#else
    struct stat info{};
    if (lease.file < 0 || ::fstat(lease.file, &info) != 0 || !S_ISREG(info.st_mode)
        || info.st_size < 0 || static_cast<std::uintmax_t>(info.st_size) > maximumTransferJournalBytes)
    {
        diagnostic = "campaign transfer journal handle is not a bounded regular file";
        return false;
    }
    text.assign(static_cast<std::size_t>(info.st_size), '\0');
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const auto count = ::pread(lease.file, text.data() + offset, text.size() - offset,
            static_cast<off_t>(offset));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            diagnostic = "could not read complete campaign transfer journal through its locked handle";
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
#endif
    return true;
}

bool removeTransferJournalLeaseIfOwned(CampaignTransferJournalLease& lease,
    const std::string_view expectedText, std::string& diagnostic)
{
    std::string actual;
    if (!readTransferJournalLease(lease, actual, diagnostic)) return false;
    if (actual != expectedText)
    {
        diagnostic = "recovery journal changed during transfer";
        return false;
    }
#ifdef _WIN32
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (SetFileInformationByHandle(lease.file, FileDispositionInfo, &disposition, sizeof(disposition)) == FALSE)
    {
        diagnostic = "filesystem refused handle-based removal of the campaign transfer journal";
        return false;
    }
#else
    struct stat opened{}, named{};
    if (::fstat(lease.file, &opened) != 0
        || ::fstatat(lease.directory, lease.leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(named.st_mode) || opened.st_dev != named.st_dev || opened.st_ino != named.st_ino)
    {
        diagnostic = "campaign transfer journal directory entry changed while its handle was locked";
        return false;
    }
    if (::unlinkat(lease.directory, lease.leaf.c_str(), 0) != 0)
    {
        diagnostic = "could not remove the acknowledged campaign transfer journal by directory handle";
        return false;
    }
#endif
    // Windows completes disposition-based deletion when the last handle closes;
    // POSIX can release the directory entry immediately after unlinkat.
    lease.reset();
    return true;
}

bool removeTransferJournalIfOwned(const std::filesystem::path& path,
    const std::string_view expectedText, std::string& diagnostic,
    CampaignTransferJournalLease* heldLease = nullptr)
{
    if (heldLease)
        return removeTransferJournalLeaseIfOwned(*heldLease, expectedText, diagnostic);
    CampaignTransferJournalLease lease;
    if (!acquireTransferJournalLease(path, lease, diagnostic))
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error) && !error) return true;
        return false;
    }
    return removeTransferJournalLeaseIfOwned(lease, expectedText, diagnostic);
}

bool removeAcknowledgedJournalByHandle(CampaignTransferJournalLease& lease,
    const std::string_view expectedText, std::string& diagnostic)
{
    // The caller holds the same exclusive lease used to read/validate the journal.
    // Windows LockFileEx and POSIX flock coordinate cooperating transfer processes.
    return removeTransferJournalLeaseIfOwned(lease, expectedText, diagnostic);
}
}

core::Result<void> selectPuzzleCampaignProgressFields(SaveProfile& profile)
{
    auto candidate = profile;
    auto selected = selectPuzzleSnapshotField(candidate);
    if (!selected) return selected;
    selected = selectPuzzleLevelIdentityField(candidate);
    if (!selected) return selected;
    selected = candidate.addCustomField(std::string(PuzzleCampaignSaveField::campaignId), std::string{});
    if (!selected) return selected;
    selected = candidate.addCustomField(std::string(PuzzleCampaignSaveField::ended), false);
    if (!selected) return selected;
    profile = std::move(candidate);
    return core::Result<void>::success();
}

core::Result<SaveWriteResult> savePuzzleCampaignProgress(const SaveStore& store, const std::size_t slot,
    const scene::Scene& scene, const SaveProfile& profile, const std::string_view gameVersion,
    const PuzzleCampaignSession& campaign, const SaveCustomState& additionalCustomState)
{
    const auto validProfile = validateCampaignProfile(profile);
    if (!validProfile) return core::Result<SaveWriteResult>::failure(validProfile.error());
    auto customState = additionalCustomState;
    if (customState.contains(std::string(PuzzleCampaignSaveField::campaignId))
        || customState.contains(std::string(PuzzleCampaignSaveField::ended)))
        return core::Result<SaveWriteResult>::failure(campaignSaveError(core::ErrorCode::AlreadyExists,
            "Additional custom state already contains a reserved Puzzle campaign field"));
    customState.emplace(std::string(PuzzleCampaignSaveField::campaignId), campaign.campaign().id);
    customState.emplace(std::string(PuzzleCampaignSaveField::ended),
        campaign.state() == PuzzleCampaignSessionState::Ended);
    return savePuzzleLevelSnapshot(store, slot, scene, profile, gameVersion, campaign.game(),
        campaign.currentLevel().id, customState);
}

core::Result<PuzzleCampaignProgressLoadResult> loadPuzzleCampaignProgress(const SaveStore& store,
    const std::size_t slot, scene::Scene& scene, const SaveProfile& profile, const std::string_view gameVersion,
    const std::filesystem::path& projectRoot, const std::string_view expectedCampaignId)
{
    const auto validProfile = validateCampaignProfile(profile);
    if (!validProfile)
        return core::Result<PuzzleCampaignProgressLoadResult>::failure(validProfile.error());

    auto sceneCandidate = scene;
    auto snapshot = loadPuzzleSnapshot(store, slot, sceneCandidate, profile, gameVersion);
    if (!snapshot) return core::Result<PuzzleCampaignProgressLoadResult>::failure(snapshot.error());
    auto& customState = snapshot.value().additionalCustomState;
    const auto campaignField = customState.find(std::string(PuzzleCampaignSaveField::campaignId));
    const auto levelField = customState.find(std::string(PuzzleSaveField::levelId));
    const auto endedField = customState.find(std::string(PuzzleCampaignSaveField::ended));
    if (campaignField == customState.end() || levelField == customState.end() || endedField == customState.end())
        return core::Result<PuzzleCampaignProgressLoadResult>::failure(campaignSaveError(core::ErrorCode::NotFound,
            "Save slot is missing required Puzzle campaign progress fields"));
    const auto* savedCampaignId = std::get_if<std::string>(&campaignField->second);
    const auto* savedLevelId = std::get_if<std::string>(&levelField->second);
    const auto* campaignEnded = std::get_if<bool>(&endedField->second);
    if (!savedCampaignId || !savedLevelId || !campaignEnded)
        return core::Result<PuzzleCampaignProgressLoadResult>::failure(campaignSaveError(core::ErrorCode::TypeMismatch,
            "Saved Puzzle campaign progress fields have unexpected types"));
    if (*savedCampaignId != expectedCampaignId)
        return core::Result<PuzzleCampaignProgressLoadResult>::failure(campaignSaveError(core::ErrorCode::VersionMismatch,
            "Save slot belongs to a different Puzzle campaign"));

    auto resumed = PuzzleCampaignSession::resume(projectRoot, std::string(expectedCampaignId), *savedLevelId,
        snapshot.value().puzzle.snapshot(), *campaignEnded);
    if (!resumed) return core::Result<PuzzleCampaignProgressLoadResult>::failure(resumed.error());

    customState.erase(campaignField);
    customState.erase(levelField);
    customState.erase(endedField);
    scene = std::move(sceneCandidate);
    return core::Result<PuzzleCampaignProgressLoadResult>::success({std::move(resumed).value(),
        std::move(snapshot.value().save), std::move(customState)});
}

core::Result<PuzzleCampaignSaveTransferPreview> previewPuzzleCampaignSaveTransfer(
    const std::filesystem::path& sourceDirectory, const std::filesystem::path& targetDirectory,
    const std::string_view expectedCampaignId)
{
    if (!isSafeCampaignId(expectedCampaignId))
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(previewError(
            core::ErrorCode::ValidationError, "Campaign ID is not safe for save transfer preview"));

    auto sourcePath = validatePreviewDirectory(sourceDirectory, true);
    if (!sourcePath)
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(sourcePath.error());
    auto targetPath = validatePreviewDirectory(targetDirectory, false);
    if (!targetPath)
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(targetPath.error());
    if (sourcePath.value() == targetPath.value())
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(previewError(
            core::ErrorCode::InvalidArgument, "Save transfer source and target directories are identical"));

    PuzzleCampaignSaveTransferPreview preview;
    preview.sourceDirectory = sourcePath.value();
    preview.targetDirectory = targetPath.value();
    preview.campaignId = std::string(expectedCampaignId);
    std::error_code error;
    preview.targetDirectoryExists = std::filesystem::exists(targetPath.value(), error);
    if (error == std::errc::no_such_file_or_directory)
        error.clear();
    if (error)
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect save transfer target directory"));
    if (preview.targetDirectoryExists
        && !std::filesystem::is_directory(targetPath.value(), error))
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(previewError(core::ErrorCode::ValidationError,
            "Save transfer target path is not a directory"));
    if (error)
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect save transfer target directory"));

    for (std::size_t slot = 0; slot < 10; ++slot)
    {
        PuzzleCampaignSaveSlotPreview entry;
        entry.slot = slot;
        const auto primary = slotFilePath(sourcePath.value(), slot);
        const auto backup = backupFilePath(primary);
        const auto targetPrimary = slotFilePath(targetPath.value(), slot);
        const auto targetBackup = backupFilePath(targetPrimary);

        auto primaryFile = inspectPreviewFile(primary, true);
        auto backupFile = inspectPreviewFile(backup, true);
        auto targetPrimaryFile = inspectPreviewFile(targetPrimary, false);
        auto targetBackupFile = inspectPreviewFile(targetBackup, false);
        if (!primaryFile || !backupFile || !targetPrimaryFile || !targetBackupFile)
            return core::Result<PuzzleCampaignSaveTransferPreview>::failure(
                !primaryFile ? primaryFile.error() : !backupFile ? backupFile.error()
                : !targetPrimaryFile ? targetPrimaryFile.error() : targetBackupFile.error());
        entry.sourceBytes = primaryFile.value().bytes + backupFile.value().bytes;
        entry.sourcePrimaryBytes = primaryFile.value().bytes;
        entry.sourceBackupBytes = backupFile.value().bytes;
        entry.sourcePrimaryExists = primaryFile.value().exists;
        entry.sourceBackupExists = backupFile.value().exists;
        entry.sourceFingerprintComplete = primaryFile.value().fingerprintComplete
            && backupFile.value().fingerprintComplete;
        entry.sourcePrimaryFingerprint = primaryFile.value().fingerprint;
        entry.sourceBackupFingerprint = backupFile.value().fingerprint;
        entry.primaryConflict = targetPrimaryFile.value().exists;
        entry.backupConflict = targetBackupFile.value().exists;
        preview.sourceBytes += entry.sourceBytes;
        if (entry.primaryConflict || entry.backupConflict) ++preview.conflictingSlots;

        const auto inspection = SaveStore::inspectExistingSlot(sourcePath.value(), slot);
        if (!inspection)
        {
            if (inspection.error().code == core::ErrorCode::NotFound && entry.sourceBytes == 0)
            {
                entry.state = PuzzleCampaignSaveSlotPreviewState::Empty;
                entry.diagnostic = "Slot is empty";
            }
            else
            {
                entry.state = PuzzleCampaignSaveSlotPreviewState::Invalid;
                entry.diagnostic = inspection.error().message;
                ++preview.invalidSlots;
            }
            preview.slots.push_back(std::move(entry));
            continue;
        }

        const auto& fields = inspection.value().customFields;
        const auto campaignField = fields.find(std::string(PuzzleCampaignSaveField::campaignId));
        const auto levelField = fields.find(std::string(PuzzleSaveField::levelId));
        const auto endedField = fields.find(std::string(PuzzleCampaignSaveField::ended));
        const auto snapshotField = fields.find(std::string(PuzzleSaveField::snapshot));
        const auto* savedCampaignId = campaignField == fields.end() ? nullptr
            : std::get_if<std::string>(&campaignField->second);
        const auto* savedLevelId = levelField == fields.end() ? nullptr
            : std::get_if<std::string>(&levelField->second);
        const auto* ended = endedField == fields.end() ? nullptr
            : std::get_if<bool>(&endedField->second);
        const auto* snapshot = snapshotField == fields.end() ? nullptr
            : std::get_if<std::string>(&snapshotField->second);

        if (!savedCampaignId)
        {
            entry.state = PuzzleCampaignSaveSlotPreviewState::Invalid;
            entry.diagnostic = "Slot does not contain a valid campaign identity";
            ++preview.invalidSlots;
        }
        else if (*savedCampaignId != expectedCampaignId)
        {
            entry.state = PuzzleCampaignSaveSlotPreviewState::CampaignMismatch;
            entry.diagnostic = "Slot belongs to campaign '" + *savedCampaignId + "'";
            ++preview.campaignMismatchSlots;
        }
        else if (!savedLevelId || !ended || !snapshot)
        {
            entry.state = PuzzleCampaignSaveSlotPreviewState::Invalid;
            entry.diagnostic = "Slot is missing required campaign progress fields";
            ++preview.invalidSlots;
        }
        else
        {
            entry.savedLevelId = *savedLevelId;
            entry.ended = *ended;
            if (inspection.value().info.fromBackup)
            {
                entry.state = PuzzleCampaignSaveSlotPreviewState::RecoveredFromBackup;
                entry.diagnostic = "Primary slot is unavailable; a valid backup was found";
                ++preview.recoveredSlots;
            }
            else
            {
                entry.state = PuzzleCampaignSaveSlotPreviewState::Valid;
                entry.diagnostic = "Campaign identity and required fields are valid";
                ++preview.validSlots;
            }
        }
        preview.slots.push_back(std::move(entry));
    }

    for (std::filesystem::directory_iterator iterator(sourcePath.value(), error), end;
        iterator != end && !error; iterator.increment(error))
    {
        const auto name = iterator->path().filename().string();
        bool expected = false;
        for (std::size_t slot = 0; slot < 10; ++slot)
        {
            const auto primaryName = slotFilePath({}, slot).filename().string();
            if (name == primaryName || name == primaryName + ".bak")
            {
                expected = true;
                break;
            }
        }
        if (!expected) preview.unexpectedSourceArtifacts.push_back(name);
    }
    if (error)
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(previewError(core::ErrorCode::IoError,
            "Could not enumerate save transfer source artifacts"));
    std::sort(preview.unexpectedSourceArtifacts.begin(), preview.unexpectedSourceArtifacts.end());
    auto recovery = inspectPuzzleCampaignSaveTransferRecovery(preview.targetDirectory);
    if (!recovery)
        return core::Result<PuzzleCampaignSaveTransferPreview>::failure(recovery.error());
    if (recovery.value().present)
    {
        if (recovery.value().journalValid && recovery.value().campaignId != preview.campaignId)
            recovery.value().diagnostic = "Pending transfer journal belongs to a different campaign ID";
        preview.pendingRecovery = std::move(recovery).value();
    }
    return core::Result<PuzzleCampaignSaveTransferPreview>::success(std::move(preview));
}

core::Result<PuzzleCampaignSaveTransferRecovery> inspectPuzzleCampaignSaveTransferRecovery(
    const std::filesystem::path& targetDirectory)
{
    auto targetPath = validatePreviewDirectory(targetDirectory, false);
    if (!targetPath) return core::Result<PuzzleCampaignSaveTransferRecovery>::failure(targetPath.error());

    PuzzleCampaignSaveTransferRecovery result;
    result.journalPath = transferJournalPath(targetPath.value());
    std::error_code error;
    const auto journalStatus = std::filesystem::symlink_status(result.journalPath, error);
    if (error == std::errc::no_such_file_or_directory || journalStatus.type() == std::filesystem::file_type::not_found)
    {
        error.clear();
        result.journalValid = true;
        return core::Result<PuzzleCampaignSaveTransferRecovery>::success(std::move(result));
    }
    if (error)
        return core::Result<PuzzleCampaignSaveTransferRecovery>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect campaign transfer recovery journal"));
    result.present = true;
    auto text = readTransferJournalText(result.journalPath);
    if (!text)
    {
        result.journalState = text.error().code == core::ErrorCode::ValidationError
            ? (text.error().message.find("exceeds its size limit") != std::string::npos
                ? PuzzleCampaignSaveTransferRecoveryJournalState::Oversized
                : PuzzleCampaignSaveTransferRecoveryJournalState::Unsafe)
            : PuzzleCampaignSaveTransferRecoveryJournalState::Unreadable;
        result.diagnostic = text.error().message;
        return core::Result<PuzzleCampaignSaveTransferRecovery>::success(std::move(result));
    }
    result.journalBytes = text.value().size();
    result.journalFingerprint = contentFingerprint(std::vector<std::uint8_t>(text.value().begin(), text.value().end()));
    auto parsed = parseTransferJournal(text.value(), result.campaignId);
    if (!parsed)
    {
        result.journalState = PuzzleCampaignSaveTransferRecoveryJournalState::Malformed;
        result.diagnostic = parsed.error().message;
        return core::Result<PuzzleCampaignSaveTransferRecovery>::success(std::move(result));
    }
    result.journalValid = true;
    result.journalState = PuzzleCampaignSaveTransferRecoveryJournalState::Valid;
    for (auto& artifact : parsed.value())
    {
        const auto relativePath = artifact.targetPath;
        artifact.targetPath = targetPath.value() / relativePath.filename();
        const auto inspected = inspectPreviewFile(artifact.targetPath, true);
        if (!inspected)
        {
            artifact.state = PuzzleCampaignSaveTransferRecoveryArtifactState::Unsafe;
            if (result.diagnostic.empty()) result.diagnostic = inspected.error().message;
        }
        else if (!inspected.value().exists)
            artifact.state = PuzzleCampaignSaveTransferRecoveryArtifactState::Missing;
        else
        {
            artifact.observedBytes = inspected.value().bytes;
            if (inspected.value().bytes == artifact.expectedBytes
                && inspected.value().fingerprintComplete
                && inspected.value().fingerprint == artifact.expectedFingerprint)
                artifact.state = PuzzleCampaignSaveTransferRecoveryArtifactState::Complete;
            else if (inspected.value().bytes < artifact.expectedBytes)
                artifact.state = PuzzleCampaignSaveTransferRecoveryArtifactState::Partial;
            else
                artifact.state = PuzzleCampaignSaveTransferRecoveryArtifactState::Changed;
        }
        result.artifacts.push_back(std::move(artifact));
    }
    return core::Result<PuzzleCampaignSaveTransferRecovery>::success(std::move(result));
}

core::Result<void> acknowledgePuzzleCampaignSaveTransferRecovery(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery, const bool explicitlyConfirmed)
{
    if (!explicitlyConfirmed)
        return core::Result<void>::failure(previewError(core::ErrorCode::InvalidState,
            "Explicit confirmation is required to acknowledge a campaign transfer recovery record"));
    if (!displayedRecovery.present || !displayedRecovery.journalValid)
        return core::Result<void>::failure(previewError(core::ErrorCode::ValidationError,
            "Only a valid campaign transfer recovery record can be acknowledged"));
    auto reviewedJournalResult = readTransferJournalText(displayedRecovery.journalPath);
    if (!reviewedJournalResult
        || reviewedJournalResult.value().size() != displayedRecovery.journalBytes
        || contentFingerprint(std::vector<std::uint8_t>(reviewedJournalResult.value().begin(),
            reviewedJournalResult.value().end())) != displayedRecovery.journalFingerprint)
        return core::Result<void>::failure(previewError(core::ErrorCode::ValidationError,
            "Campaign transfer recovery record changed; inspect it again before acknowledging"));
    const std::string reviewedJournalText = std::move(reviewedJournalResult).value();
    auto current = inspectPuzzleCampaignSaveTransferRecovery(displayedRecovery.journalPath.parent_path());
    if (!current) return core::Result<void>::failure(current.error());
    if (!current.value().present || !current.value().journalValid
        || current.value().journalFingerprint != displayedRecovery.journalFingerprint
        || current.value().journalBytes != displayedRecovery.journalBytes)
        return core::Result<void>::failure(previewError(core::ErrorCode::ValidationError,
            "Campaign transfer recovery record changed; inspect it again before acknowledging"));
    if (current.value().artifacts.size() != displayedRecovery.artifacts.size())
        return core::Result<void>::failure(previewError(core::ErrorCode::ValidationError,
            "Campaign transfer artifacts changed; inspect the recovery record again"));
    for (std::size_t index = 0; index < current.value().artifacts.size(); ++index)
    {
        const auto& now = current.value().artifacts[index];
        const auto& shown = displayedRecovery.artifacts[index];
        if (now.slot != shown.slot || now.backup != shown.backup || now.targetPath != shown.targetPath
            || now.expectedBytes != shown.expectedBytes || now.observedBytes != shown.observedBytes
            || now.state != shown.state)
            return core::Result<void>::failure(previewError(core::ErrorCode::ValidationError,
                "Campaign transfer artifacts changed; inspect the recovery record again"));
    }
    CampaignTransferJournalLease journalLease;
    std::string leaseDiagnostic;
    if (!acquireTransferJournalLease(current.value().journalPath, journalLease, leaseDiagnostic))
        return core::Result<void>::failure(previewError(core::ErrorCode::IoError,
            "Could not safely lock the campaign transfer recovery record: " + leaseDiagnostic));
    std::string currentJournalText;
    if (!readTransferJournalLease(journalLease, currentJournalText, leaseDiagnostic)
        || currentJournalText != reviewedJournalText)
        return core::Result<void>::failure(previewError(core::ErrorCode::ValidationError,
            "Campaign transfer recovery record changed; inspect it again before acknowledging"));
    std::string diagnostic;
    if (!removeAcknowledgedJournalByHandle(journalLease, currentJournalText, diagnostic))
        return core::Result<void>::failure(previewError(core::ErrorCode::IoError,
            "Could not safely remove the acknowledged campaign transfer recovery record: " + diagnostic));
    return core::Result<void>::success();
}

core::Result<std::filesystem::path> archiveMalformedPuzzleCampaignSaveTransferJournal(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery, const bool explicitlyConfirmed)
{
    if (!explicitlyConfirmed)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::InvalidState,
            "Explicit confirmation is required to create an archive copy of a malformed campaign transfer journal"));
    if (!displayedRecovery.present || displayedRecovery.journalValid
        || displayedRecovery.journalState != PuzzleCampaignSaveTransferRecoveryJournalState::Malformed
        || displayedRecovery.journalBytes > maximumTransferJournalBytes)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Only a readable, bounded malformed campaign transfer journal can be archived"));

    auto targetDirectory = validatePreviewDirectory(displayedRecovery.journalPath.parent_path(), false);
    if (!targetDirectory)
        return core::Result<std::filesystem::path>::failure(targetDirectory.error());
    const auto journalPath = transferJournalPath(targetDirectory.value());
    if (journalPath != displayedRecovery.journalPath)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Displayed recovery journal path is not the expected journal in its target directory"));

    auto current = inspectPuzzleCampaignSaveTransferRecovery(targetDirectory.value());
    if (!current)
        return core::Result<std::filesystem::path>::failure(current.error());
    if (!current.value().present || current.value().journalValid
        || current.value().journalState != PuzzleCampaignSaveTransferRecoveryJournalState::Malformed
        || current.value().journalFingerprint != displayedRecovery.journalFingerprint
        || current.value().journalBytes != displayedRecovery.journalBytes)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Malformed journal changed since review; inspect it again before archiving"));

    auto journalTextResult = readTransferJournalText(journalPath);
    if (!journalTextResult)
        return core::Result<std::filesystem::path>::failure(journalTextResult.error());
    const auto& journalText = journalTextResult.value();
    const std::vector<std::uint8_t> journalBytes(journalText.begin(), journalText.end());
    if (journalBytes.size() != displayedRecovery.journalBytes
        || contentFingerprint(journalBytes) != displayedRecovery.journalFingerprint)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Malformed journal bytes changed since review; inspect it again before archiving"));

    // Inspect/classify by path before taking the Windows byte-range lock: that lock
    // intentionally prevents a second handle from reading the locked byte range.
    // Once acquired, all decisions about source bytes use this same held object.
    CampaignTransferJournalLease sourceLease;
    std::string leaseDiagnostic;
    if (!acquireTransferJournalLease(journalPath, sourceLease, leaseDiagnostic))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Malformed journal is active in another transfer or archive operation: " + leaseDiagnostic));
    std::string lockedJournalText;
    if (!readTransferJournalLease(sourceLease, lockedJournalText, leaseDiagnostic)
        || lockedJournalText != journalText)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Malformed journal changed while acquiring its archive lease; inspect it again"));

    const auto archivePath = campaignTransferArchivePath(targetDirectory.value(),
        displayedRecovery.journalFingerprint);
    std::error_code error;
    const auto archiveStatus = std::filesystem::symlink_status(archivePath, error);
    bool archiveAlreadyPresent = false;
    if (error == std::errc::no_such_file_or_directory
        || archiveStatus.type() == std::filesystem::file_type::not_found)
        error.clear();
    else if (error)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not inspect the campaign transfer journal archive path"));
    else
    {
        archiveAlreadyPresent = true;
        auto existingArchive = readTransferJournalText(archivePath);
        if (!existingArchive || existingArchive.value() != journalText)
            return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
                "The journal archive path already exists with different or unsafe contents; it was not replaced"));
    }

    if (!archiveAlreadyPresent)
    {
        CampaignTransferJournalLease stagedArchive;
        std::filesystem::path stagingPath;
        ExclusiveWriteOutcome write;
        bool stagingCreated = false;
        for (unsigned attempt = 0; attempt < 32; ++attempt)
        {
            stagingPath = nextCampaignArchiveStagingPath(archivePath);
            write = writeTransferJournalExclusive(stagingPath, journalText, &stagedArchive);
            if (write.created)
            {
                stagingCreated = true;
                break;
            }
            std::error_code stagingError;
            const bool collision = std::filesystem::exists(stagingPath, stagingError) && !stagingError;
            if (!collision) break;
        }
        if (!stagingCreated || !write.complete)
        {
            auto diagnostic = write.diagnostic;
            if (stagingCreated)
                diagnostic += "; an incomplete staging file may remain for manual review";
            return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
                "Could not completely write the staged journal archive: " + diagnostic));
        }

        std::string publishDiagnostic;
        if (!publishCampaignArchiveNoReplace(stagedArchive, archivePath, journalText, publishDiagnostic))
        {
            std::string cleanupDiagnostic;
            if (stagedArchive.valid()
                && !removeTransferJournalLeaseIfOwned(stagedArchive, journalText, cleanupDiagnostic))
                publishDiagnostic += "; verified staging file was preserved for manual review ("
                    + cleanupDiagnostic + ")";
            return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
                "Could not publish the journal archive: " + publishDiagnostic));
        }
    }

    auto archivedText = readTransferJournalText(archivePath);
    if (!archivedText || archivedText.value() != journalText)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "The archive could not be verified against the reviewed journal; the original remains in place"));

    std::string unchangedJournal;
    if (!readTransferJournalLease(sourceLease, unchangedJournal, leaseDiagnostic)
        || unchangedJournal != journalText)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "The original journal changed during archiving; both files were preserved for manual review"));

    return core::Result<std::filesystem::path>::success(archivePath);
}

core::Result<PuzzleCampaignSaveArchiveInspection> inspectMalformedPuzzleCampaignSaveArchive(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery)
{
    if (!displayedRecovery.present || displayedRecovery.journalValid
        || displayedRecovery.journalState != PuzzleCampaignSaveTransferRecoveryJournalState::Malformed
        || displayedRecovery.journalBytes > maximumTransferJournalBytes)
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(previewError(
            core::ErrorCode::ValidationError,
            "Only a readable, bounded malformed campaign transfer journal can be inspected"));

    auto targetDirectory = validatePreviewDirectory(displayedRecovery.journalPath.parent_path(), false);
    if (!targetDirectory)
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(targetDirectory.error());
    const auto journalPath = transferJournalPath(targetDirectory.value());
    if (journalPath != displayedRecovery.journalPath)
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(previewError(
            core::ErrorCode::ValidationError,
            "Displayed recovery journal path is not the expected journal in its target directory"));

    auto current = inspectPuzzleCampaignSaveTransferRecovery(targetDirectory.value());
    if (!current)
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(current.error());
    if (!current.value().present || current.value().journalValid
        || current.value().journalState != PuzzleCampaignSaveTransferRecoveryJournalState::Malformed
        || current.value().journalFingerprint != displayedRecovery.journalFingerprint
        || current.value().journalBytes != displayedRecovery.journalBytes)
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(previewError(
            core::ErrorCode::ValidationError,
            "Malformed journal changed since review; inspect it again before checking its archive"));

    auto journalText = readTransferJournalText(journalPath);
    if (!journalText)
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(journalText.error());
    const std::vector<std::uint8_t> journalBytes(journalText.value().begin(), journalText.value().end());
    if (journalBytes.size() != displayedRecovery.journalBytes
        || contentFingerprint(journalBytes) != displayedRecovery.journalFingerprint)
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(previewError(
            core::ErrorCode::ValidationError,
            "Malformed journal bytes changed since review; inspect it again before checking its archive"));

    CampaignTransferJournalLease sourceLease;
    std::string leaseDiagnostic;
    if (!acquireTransferJournalLease(journalPath, sourceLease, leaseDiagnostic))
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(previewError(
            core::ErrorCode::IoError,
            "Could not safely lock the campaign transfer recovery record: " + leaseDiagnostic));
    std::string lockedJournalText;
    if (!readTransferJournalLease(sourceLease, lockedJournalText, leaseDiagnostic)
        || lockedJournalText != journalText.value())
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(previewError(
            core::ErrorCode::ValidationError,
            "Malformed journal changed while acquiring its inspection lease; inspect it again"));

    auto inspection = inspectCampaignArchivePath(
        campaignTransferArchivePath(targetDirectory.value(), displayedRecovery.journalFingerprint),
        journalText.value());
    std::string unchangedJournal;
    if (!readTransferJournalLease(sourceLease, unchangedJournal, leaseDiagnostic)
        || unchangedJournal != journalText.value())
        return core::Result<PuzzleCampaignSaveArchiveInspection>::failure(previewError(
            core::ErrorCode::ValidationError,
            "Malformed journal changed during archive inspection; inspect it again"));
    return core::Result<PuzzleCampaignSaveArchiveInspection>::success(std::move(inspection));
}

core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview> previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery,
    const PuzzleCampaignSaveArchiveInspection& displayedInspection)
{
    const auto currentInspection = inspectMalformedPuzzleCampaignSaveArchive(displayedRecovery);
    if (!currentInspection)
        return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(currentInspection.error());
    if (displayedInspection.pathKind != PuzzleCampaignSaveArchivePathKind::RegularFile
        || !displayedInspection.byteSize || *displayedInspection.byteSize > maximumTransferJournalBytes
        || (displayedInspection.contentMatch != PuzzleCampaignSaveArchiveContentMatch::Different
            && displayedInspection.contentMatch != PuzzleCampaignSaveArchiveContentMatch::PrefixMatch)
        || currentInspection.value().archivePath != displayedInspection.archivePath
        || currentInspection.value().pathKind != displayedInspection.pathKind
        || currentInspection.value().byteSize != displayedInspection.byteSize
        || currentInspection.value().contentMatch != displayedInspection.contentMatch)
        return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(previewError(
            core::ErrorCode::ValidationError,
            "Only an unchanged, readable regular archive conflict can be copied aside"));

    CampaignArchiveConflictSourceLease sourceLease;
    std::string diagnostic;
    if (!acquireCampaignArchiveConflictSource(currentInspection.value().archivePath, sourceLease, diagnostic))
        return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(previewError(
            core::ErrorCode::IoError, "Could not prepare a safe archive conflict copy: " + diagnostic));
    std::string sourceBytes;
    if (!readCampaignArchiveConflictSource(sourceLease, sourceBytes, diagnostic))
        return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(previewError(
            core::ErrorCode::ValidationError, "Could not revalidate archive conflict bytes: " + diagnostic));
    const auto reviewedJournal = readTransferJournalText(displayedRecovery.journalPath);
    if (!reviewedJournal)
        return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(reviewedJournal.error());
    if (sourceBytes.size() != *displayedInspection.byteSize || sourceBytes == reviewedJournal.value())
        return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(previewError(
            core::ErrorCode::ValidationError, "Archive conflict changed or is no longer an unknown occupant"));

    PuzzleCampaignSaveArchiveConflictCopyPreview result;
    result.sourcePath = currentInspection.value().archivePath;
    result.reviewedBytes.assign(sourceBytes.begin(), sourceBytes.end());
#ifdef _WIN32
    const auto& info = sourceLease.openedInfo;
    result.sourceIdentityHigh = info.dwVolumeSerialNumber;
    result.sourceIdentityLow = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    result.sourceChangeStamp = (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
        | info.ftLastWriteTime.dwLowDateTime;
#else
    result.sourceIdentityHigh = static_cast<std::uint64_t>(sourceLease.openedInfo.st_dev);
    result.sourceIdentityLow = static_cast<std::uint64_t>(sourceLease.openedInfo.st_ino);
    result.sourceChangeStamp = static_cast<std::uint64_t>(sourceLease.openedInfo.st_ctime);
#endif
    for (unsigned attempt = 0; attempt < 64; ++attempt)
    {
        const auto candidate = nextCampaignArchiveConflictCopyPath(result.sourcePath);
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error == std::errc::no_such_file_or_directory
            || (!error && status.type() == std::filesystem::file_type::not_found))
        {
            result.destinationPath = candidate;
            return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::success(std::move(result));
        }
        if (error)
            return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(previewError(
                core::ErrorCode::IoError, "Could not select an unused archive evidence-copy path"));
    }
    return core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure(previewError(
        core::ErrorCode::AlreadyExists, "Could not find an unused adjacent archive evidence-copy name"));
}

core::Result<std::filesystem::path> preserveMalformedPuzzleCampaignSaveArchiveConflict(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery,
    const PuzzleCampaignSaveArchiveConflictCopyPreview& displayedCopyPreview,
    const bool explicitlyConfirmed)
{
    if (!explicitlyConfirmed)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::InvalidState,
            "Explicit confirmation is required to preserve an unknown archive conflict as a separate copy"));
    if (displayedCopyPreview.reviewedBytes.size() > maximumTransferJournalBytes
        || displayedCopyPreview.sourcePath.empty() || displayedCopyPreview.destinationPath.empty())
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Archive conflict copy preview is invalid or exceeds the size limit"));

    auto targetDirectory = validatePreviewDirectory(displayedRecovery.journalPath.parent_path(), false);
    if (!targetDirectory)
        return core::Result<std::filesystem::path>::failure(targetDirectory.error());
    const auto journalPath = transferJournalPath(targetDirectory.value());
    if (journalPath != displayedRecovery.journalPath)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Displayed recovery journal path is not the expected journal in its target directory"));
    const auto expectedArchivePath = campaignTransferArchivePath(targetDirectory.value(),
        displayedRecovery.journalFingerprint);
    if (displayedCopyPreview.sourcePath != expectedArchivePath
        || !isGeneratedCampaignArchiveConflictCopyPath(expectedArchivePath, displayedCopyPreview.destinationPath))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Archive conflict copy preview path is not the generated adjacent destination"));

    auto inspection = inspectMalformedPuzzleCampaignSaveArchive(displayedRecovery);
    if (!inspection)
        return core::Result<std::filesystem::path>::failure(inspection.error());
    if (inspection.value().pathKind != PuzzleCampaignSaveArchivePathKind::RegularFile
        || !inspection.value().byteSize || *inspection.value().byteSize != displayedCopyPreview.reviewedBytes.size()
        || (inspection.value().contentMatch != PuzzleCampaignSaveArchiveContentMatch::Different
            && inspection.value().contentMatch != PuzzleCampaignSaveArchiveContentMatch::PrefixMatch))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Archive conflict changed or is no longer eligible; inspect it again"));

    auto journalText = readTransferJournalText(journalPath);
    if (!journalText)
        return core::Result<std::filesystem::path>::failure(journalText.error());
    const std::vector<std::uint8_t> journalBytes(journalText.value().begin(), journalText.value().end());
    if (journalBytes.size() != displayedRecovery.journalBytes
        || contentFingerprint(journalBytes) != displayedRecovery.journalFingerprint)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Malformed journal changed since review; inspect it again before preserving the conflict"));
    CampaignTransferJournalLease journalLease;
    std::string journalDiagnostic;
    if (!acquireTransferJournalLease(journalPath, journalLease, journalDiagnostic))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not safely lock the campaign recovery journal: " + journalDiagnostic));
    std::string lockedJournal;
    if (!readTransferJournalLease(journalLease, lockedJournal, journalDiagnostic)
        || lockedJournal != journalText.value())
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Malformed journal changed while preparing the archive conflict copy"));

    CampaignArchiveConflictSourceLease sourceLease;
    std::string sourceDiagnostic;
    if (!acquireCampaignArchiveConflictSource(displayedCopyPreview.sourcePath, sourceLease, sourceDiagnostic))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not open the reviewed archive conflict safely: " + sourceDiagnostic));
    std::string sourceBytes;
    if (!readCampaignArchiveConflictSource(sourceLease, sourceBytes, sourceDiagnostic)
        || sourceBytes.size() != displayedCopyPreview.reviewedBytes.size()
        || !std::equal(sourceBytes.begin(), sourceBytes.end(), displayedCopyPreview.reviewedBytes.begin()))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Archive conflict contents changed after confirmation preview; no copy was published"));
#ifdef _WIN32
    const auto& sourceInfo = sourceLease.openedInfo;
    const auto sourceIdentityHigh = static_cast<std::uint64_t>(sourceInfo.dwVolumeSerialNumber);
    const auto sourceIdentityLow = (static_cast<std::uint64_t>(sourceInfo.nFileIndexHigh) << 32)
        | sourceInfo.nFileIndexLow;
    const auto sourceChangeStamp = (static_cast<std::uint64_t>(sourceInfo.ftLastWriteTime.dwHighDateTime) << 32)
        | sourceInfo.ftLastWriteTime.dwLowDateTime;
#else
    const auto sourceIdentityHigh = static_cast<std::uint64_t>(sourceLease.openedInfo.st_dev);
    const auto sourceIdentityLow = static_cast<std::uint64_t>(sourceLease.openedInfo.st_ino);
    const auto sourceChangeStamp = static_cast<std::uint64_t>(sourceLease.openedInfo.st_ctime);
#endif
    if (sourceIdentityHigh != displayedCopyPreview.sourceIdentityHigh
        || sourceIdentityLow != displayedCopyPreview.sourceIdentityLow
        || sourceChangeStamp != displayedCopyPreview.sourceChangeStamp)
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "Archive conflict file was replaced or changed after confirmation preview"));

    std::error_code destinationError;
    const auto destinationStatus = std::filesystem::symlink_status(displayedCopyPreview.destinationPath,
        destinationError);
    if ((destinationError != std::errc::no_such_file_or_directory
            && (destinationError || destinationStatus.type() != std::filesystem::file_type::not_found)))
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::AlreadyExists,
            "Archive evidence-copy destination already exists; it was not replaced"));

    const std::string copyBytes(sourceBytes.begin(), sourceBytes.end());
    CampaignTransferJournalLease stagedCopy;
    std::filesystem::path stagingPath;
    ExclusiveWriteOutcome write;
    bool stagingCreated = false;
    for (unsigned attempt = 0; attempt < 32; ++attempt)
    {
        stagingPath = nextCampaignArchiveStagingPath(displayedCopyPreview.destinationPath);
        write = writeTransferJournalExclusive(stagingPath, copyBytes, &stagedCopy);
        if (write.created) { stagingCreated = true; break; }
        std::error_code stagingError;
        const bool collision = std::filesystem::exists(stagingPath, stagingError) && !stagingError;
        if (!collision) break;
    }
    if (!stagingCreated || !write.complete)
    {
        auto diagnostic = write.diagnostic;
        if (stagingCreated) diagnostic += "; an incomplete owned staging copy may remain for manual review";
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not completely stage the archive evidence copy: " + diagnostic));
    }

    std::string publishDiagnostic;
    if (!publishCampaignArchiveNoReplace(stagedCopy, displayedCopyPreview.destinationPath,
        copyBytes, publishDiagnostic))
    {
        std::string cleanupDiagnostic;
        if (stagedCopy.valid() && !removeTransferJournalLeaseIfOwned(stagedCopy, copyBytes, cleanupDiagnostic))
            publishDiagnostic += "; verified staging copy was preserved for manual review (" + cleanupDiagnostic + ")";
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::IoError,
            "Could not publish archive evidence copy without replacement: " + publishDiagnostic));
    }

    auto publishedBytes = readTransferJournalText(displayedCopyPreview.destinationPath);
    std::string unchangedSource, unchangedJournal;
    if (!publishedBytes || publishedBytes.value() != copyBytes
        || !readCampaignArchiveConflictSource(sourceLease, unchangedSource, sourceDiagnostic)
        || unchangedSource != copyBytes
        || !readTransferJournalLease(journalLease, unchangedJournal, journalDiagnostic)
        || unchangedJournal != journalText.value())
        return core::Result<std::filesystem::path>::failure(previewError(core::ErrorCode::ValidationError,
            "The evidence copy was published but source/journal revalidation failed; both copies were retained at: "
                + displayedCopyPreview.destinationPath.string()));
    return core::Result<std::filesystem::path>::success(displayedCopyPreview.destinationPath);
}

core::Result<std::filesystem::path> testing::archiveMalformedCampaignJournalWithHook(
    const PuzzleCampaignSaveTransferRecovery& recovery, testing::CampaignArchiveHook hook)
{
    struct HookScope final
    {
        testing::CampaignArchiveHook previous;
        explicit HookScope(testing::CampaignArchiveHook replacement)
            : previous(std::move(activeCampaignArchiveHook))
        {
            activeCampaignArchiveHook = std::move(replacement);
        }
        ~HookScope() { activeCampaignArchiveHook = std::move(previous); }
    } scope(std::move(hook));
    return archiveMalformedPuzzleCampaignSaveTransferJournal(recovery, true);
}

core::Result<std::filesystem::path> testing::preserveMalformedCampaignArchiveConflictWithHook(
    const PuzzleCampaignSaveTransferRecovery& recovery,
    const PuzzleCampaignSaveArchiveConflictCopyPreview& copyPreview, testing::CampaignArchiveHook hook)
{
    struct HookScope final
    {
        testing::CampaignArchiveHook previous;
        explicit HookScope(testing::CampaignArchiveHook replacement)
            : previous(std::move(activeCampaignArchiveHook))
        {
            activeCampaignArchiveHook = std::move(replacement);
        }
        ~HookScope() { activeCampaignArchiveHook = std::move(previous); }
    } scope(std::move(hook));
    return preserveMalformedPuzzleCampaignSaveArchiveConflict(recovery, copyPreview, true);
}

core::Result<PuzzleCampaignSaveTransferPreflight> revalidatePuzzleCampaignSaveTransferPreview(
    const PuzzleCampaignSaveTransferPreview& displayedPreview)
{
    if (displayedPreview.slots.size() != 10)
        return core::Result<PuzzleCampaignSaveTransferPreflight>::failure(previewError(
            core::ErrorCode::InvalidArgument, "Displayed save transfer preview must contain ten slots"));
    for (std::size_t slot = 0; slot < displayedPreview.slots.size(); ++slot)
        if (displayedPreview.slots[slot].slot != slot)
            return core::Result<PuzzleCampaignSaveTransferPreflight>::failure(previewError(
                core::ErrorCode::InvalidArgument, "Displayed save transfer preview has an invalid slot order"));

    auto currentResult = previewPuzzleCampaignSaveTransfer(displayedPreview.sourceDirectory,
        displayedPreview.targetDirectory, displayedPreview.campaignId);
    if (!currentResult)
        return core::Result<PuzzleCampaignSaveTransferPreflight>::failure(currentResult.error());
    auto current = std::move(currentResult).value();

    bool sourceChanged = displayedPreview.sourceBytes != current.sourceBytes
        || displayedPreview.unexpectedSourceArtifacts != current.unexpectedSourceArtifacts;
    bool destinationChanged = displayedPreview.targetDirectoryExists != current.targetDirectoryExists;
    for (std::size_t slot = 0; slot < current.slots.size(); ++slot)
    {
        const auto& shown = displayedPreview.slots[slot];
        const auto& now = current.slots[slot];
        sourceChanged = sourceChanged || shown.state != now.state || shown.savedLevelId != now.savedLevelId
            || shown.ended != now.ended || shown.sourceBytes != now.sourceBytes
            || shown.sourcePrimaryBytes != now.sourcePrimaryBytes
            || shown.sourceBackupBytes != now.sourceBackupBytes
            || shown.sourcePrimaryExists != now.sourcePrimaryExists
            || shown.sourceBackupExists != now.sourceBackupExists
            || shown.sourceFingerprintComplete != now.sourceFingerprintComplete
            || shown.sourcePrimaryFingerprint != now.sourcePrimaryFingerprint
            || shown.sourceBackupFingerprint != now.sourceBackupFingerprint;
        destinationChanged = destinationChanged || shown.primaryConflict != now.primaryConflict
            || shown.backupConflict != now.backupConflict;
    }

    PuzzleCampaignSaveTransferPreflight result;
    result.currentPreview = std::move(current);
    if (result.currentPreview.pendingRecovery)
    {
        result.state = PuzzleCampaignSaveTransferPreflightState::RecoveryRequired;
        result.diagnostic = "A pending campaign transfer recovery record must be inspected before another copy";
    }
    else if (sourceChanged)
    {
        result.state = PuzzleCampaignSaveTransferPreflightState::StaleSource;
        result.diagnostic = "Campaign save source changed; review a new preview before any transfer";
    }
    else if (destinationChanged)
    {
        result.state = PuzzleCampaignSaveTransferPreflightState::StaleDestination;
        result.diagnostic = "Transfer destination changed; review a new preview before any transfer";
    }
    else if (result.currentPreview.conflictingSlots != 0)
    {
        result.state = PuzzleCampaignSaveTransferPreflightState::DestinationConflict;
        result.diagnostic = "Destination contains one or more primary or backup slot files";
    }
    else
    {
        const bool hasSourceSlot = std::any_of(result.currentPreview.slots.begin(),
            result.currentPreview.slots.end(), [](const auto& slot) {
                return slot.sourcePrimaryExists || slot.sourceBackupExists;
            });
        const bool hasUntransferableSlot = std::any_of(result.currentPreview.slots.begin(),
            result.currentPreview.slots.end(), [](const auto& slot) {
                return slot.state == PuzzleCampaignSaveSlotPreviewState::Invalid
                    || slot.state == PuzzleCampaignSaveSlotPreviewState::CampaignMismatch
                    || !slot.sourceFingerprintComplete;
            });
        if (!hasSourceSlot || hasUntransferableSlot || !result.currentPreview.unexpectedSourceArtifacts.empty())
        {
            result.state = PuzzleCampaignSaveTransferPreflightState::SourceNotTransferable;
            result.diagnostic = !hasSourceSlot
                ? "Source contains no campaign save slots"
                : "Source contains an invalid, mismatched, oversized or unexpected artifact";
        }
        else
        {
            result.state = PuzzleCampaignSaveTransferPreflightState::Ready;
            result.diagnostic = "Preview is current and destination slot paths are clear; explicit confirmation is still required before a future write";
        }
    }
    return core::Result<PuzzleCampaignSaveTransferPreflight>::success(std::move(result));
}

core::Result<PuzzleCampaignSaveTransferCopyResult> copyPuzzleCampaignSaveTransfer(
    const PuzzleCampaignSaveTransferPreview& displayedPreview, const bool explicitlyConfirmed,
    PuzzleCampaignSaveTransferProgress progress)
{
    PuzzleCampaignSaveTransferCopyResult result;
    if (!explicitlyConfirmed)
    {
        result.state = PuzzleCampaignSaveTransferCopyState::ConfirmationRequired;
        result.diagnostic = "Explicit confirmation is required before campaign saves are copied";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    auto preflightResult = revalidatePuzzleCampaignSaveTransferPreview(displayedPreview);
    if (!preflightResult)
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::failure(preflightResult.error());
    const auto& preflight = preflightResult.value();
    switch (preflight.state)
    {
    case PuzzleCampaignSaveTransferPreflightState::Ready: break;
    case PuzzleCampaignSaveTransferPreflightState::StaleSource:
        result.state = PuzzleCampaignSaveTransferCopyState::StaleSource;
        result.diagnostic = preflight.diagnostic;
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    case PuzzleCampaignSaveTransferPreflightState::StaleDestination:
        result.state = PuzzleCampaignSaveTransferCopyState::StaleDestination;
        result.diagnostic = preflight.diagnostic;
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    case PuzzleCampaignSaveTransferPreflightState::DestinationConflict:
        result.state = PuzzleCampaignSaveTransferCopyState::DestinationConflict;
        result.diagnostic = preflight.diagnostic;
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    case PuzzleCampaignSaveTransferPreflightState::SourceNotTransferable:
        result.state = PuzzleCampaignSaveTransferCopyState::SourceNotTransferable;
        result.diagnostic = preflight.diagnostic;
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    case PuzzleCampaignSaveTransferPreflightState::RecoveryRequired:
        result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
        result.diagnostic = preflight.diagnostic;
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    struct Artifact final
    {
        std::filesystem::path source;
        std::filesystem::path target;
        std::size_t slot = 0;
        bool backup = false;
        std::uintmax_t bytes = 0;
        std::uint64_t fingerprint = 0;
    };
    std::vector<Artifact> artifacts;
    const auto& current = preflight.currentPreview;
    for (const auto& slot : current.slots)
    {
        if (slot.state != PuzzleCampaignSaveSlotPreviewState::Valid
            && slot.state != PuzzleCampaignSaveSlotPreviewState::RecoveredFromBackup)
            continue;
        if (slot.sourcePrimaryExists)
            artifacts.push_back({slotFilePath(current.sourceDirectory, slot.slot),
                slotFilePath(current.targetDirectory, slot.slot), slot.slot, false, slot.sourcePrimaryBytes,
                slot.sourcePrimaryFingerprint});
        if (slot.sourceBackupExists)
            artifacts.push_back({backupFilePath(slotFilePath(current.sourceDirectory, slot.slot)),
                backupFilePath(slotFilePath(current.targetDirectory, slot.slot)), slot.slot, true, slot.sourceBackupBytes,
                slot.sourceBackupFingerprint});
    }
    if (artifacts.empty())
    {
        result.state = PuzzleCampaignSaveTransferCopyState::SourceNotTransferable;
        result.diagnostic = "Source contains no campaign save artifacts";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    std::vector<std::filesystem::path> createdDirectories;
    std::vector<std::pair<std::filesystem::path, std::vector<std::uint8_t>>> createdFiles;
    const auto journalPath = transferJournalPath(current.targetDirectory);
    std::string journalText;
    bool journalCreated = false;
    CampaignTransferJournalLease journalLease;
    const auto rollback = [&](const PuzzleCampaignSaveTransferCopyState failureState,
        std::string diagnostic) {
        bool complete = true;
        for (auto iterator = createdFiles.rbegin(); iterator != createdFiles.rend(); ++iterator)
        {
            std::string cleanupDiagnostic;
            if (removeCreatedArtifactIfOwned(iterator->first, iterator->second, cleanupDiagnostic))
                ++result.filesRolledBack;
            else
            {
                complete = false;
                result.remainingArtifacts.push_back(iterator->first);
                diagnostic += "; " + cleanupDiagnostic;
            }
        }
        if (journalCreated)
        {
            std::string cleanupDiagnostic;
            if (!removeTransferJournalIfOwned(journalPath, journalText, cleanupDiagnostic,
                journalLease.valid() ? &journalLease : nullptr))
            {
                complete = false;
                result.remainingArtifacts.push_back(journalPath);
                diagnostic += "; " + cleanupDiagnostic;
            }
        }
        for (auto iterator = createdDirectories.rbegin(); iterator != createdDirectories.rend(); ++iterator)
        {
            std::error_code error;
            const bool removed = std::filesystem::remove(*iterator, error);
            if (error || !removed)
            {
                error.clear();
                const bool stillExists = std::filesystem::exists(*iterator, error);
                if (error || stillExists)
                {
                    complete = false;
                    result.remainingArtifacts.push_back(*iterator);
                }
            }
        }
        result.filesCopied = 0;
        result.bytesCopied = 0;
        result.state = complete ? failureState : PuzzleCampaignSaveTransferCopyState::RollbackIncomplete;
        result.diagnostic = std::move(diagnostic);
        if (!complete) result.diagnostic += "; rollback left artifacts that need inspection";
    };

    auto directoryResult = ensureTransferTargetDirectory(current.targetDirectory, createdDirectories);
    if (!directoryResult)
    {
        rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack, directoryResult.error().message);
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    std::uintmax_t totalBytes = 0;
    for (const auto& artifact : artifacts)
    {
        if (artifact.bytes > std::numeric_limits<std::uintmax_t>::max() - totalBytes)
        {
            rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack,
                "Campaign save transfer byte count overflowed");
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
        totalBytes += artifact.bytes;
    }
    if (totalBytes != current.sourceBytes)
    {
        rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack,
            "Preview byte total does not match the selected primary and backup artifacts");
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    std::vector<std::tuple<std::size_t, bool, std::uintmax_t, std::uint64_t>> journalArtifacts;
    journalArtifacts.reserve(artifacts.size());
    for (const auto& artifact : artifacts)
        journalArtifacts.emplace_back(artifact.slot, artifact.backup, artifact.bytes, artifact.fingerprint);
    journalText = formatTransferJournal(current.campaignId, journalArtifacts);
    const auto journalWrite = writeTransferJournalExclusive(journalPath, journalText, &journalLease);
    journalCreated = journalWrite.created;
    if (!journalWrite.complete)
    {
        rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack, journalWrite.diagnostic);
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    for (const auto& artifact : artifacts)
    {
        ++result.filesAttempted;
        auto sourceBytes = readTransferSourceFile(artifact.source, artifact.bytes, artifact.fingerprint);
        if (!sourceBytes)
        {
            rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack, sourceBytes.error().message);
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
        auto write = writeTransferTargetExclusive(artifact.target, sourceBytes.value());
        if (write.created) createdFiles.emplace_back(artifact.target, sourceBytes.value());
        if (!write.complete)
        {
            rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack, std::move(write.diagnostic));
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
        ++result.filesCopied;
        result.bytesCopied += artifact.bytes;

        if (progress)
        {
            bool keepGoing = false;
            try { keepGoing = progress(result.filesCopied, artifacts.size()); }
            catch (...) { keepGoing = false; }
            if (!keepGoing)
            {
                rollback(PuzzleCampaignSaveTransferCopyState::CancelledRolledBack,
                    "Campaign save transfer was cancelled and its created files were rolled back");
                return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
            }
        }
    }

    std::string journalCleanupDiagnostic;
    if (!removeTransferJournalIfOwned(journalPath, journalText, journalCleanupDiagnostic, &journalLease))
    {
        result.state = PuzzleCampaignSaveTransferCopyState::CopiedRecoveryPending;
        result.remainingArtifacts.push_back(journalPath);
        result.diagnostic = "Campaign saves were copied, but the recovery journal could not be removed: "
            + journalCleanupDiagnostic;
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    result.state = PuzzleCampaignSaveTransferCopyState::Copied;
    result.diagnostic = "Campaign save slot artifacts copied without replacing destination files";
    return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
}

core::Result<PuzzleCampaignSaveTransferCopyResult> resumePuzzleCampaignSaveTransfer(
    const PuzzleCampaignSaveTransferPreview& displayedPreview, const bool explicitlyConfirmed,
    PuzzleCampaignSaveTransferProgress progress)
{
    PuzzleCampaignSaveTransferCopyResult result;
    if (!explicitlyConfirmed)
    {
        result.state = PuzzleCampaignSaveTransferCopyState::ConfirmationRequired;
        result.diagnostic = "Explicit confirmation is required before resuming a campaign save transfer";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    if (!displayedPreview.pendingRecovery || !displayedPreview.pendingRecovery->present
        || !displayedPreview.pendingRecovery->journalValid)
    {
        result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
        result.diagnostic = "A valid displayed recovery journal is required before resuming";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    const auto& shownRecovery = *displayedPreview.pendingRecovery;
    auto journalTextResult = readTransferJournalText(shownRecovery.journalPath);
    if (!journalTextResult)
    {
        result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
        result.diagnostic = "The displayed recovery journal cannot be read; inspect it again";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    std::string journalText = std::move(journalTextResult).value();
    const std::vector<std::uint8_t> journalBytes(journalText.begin(), journalText.end());
    if (journalBytes.size() != shownRecovery.journalBytes
        || contentFingerprint(journalBytes) != shownRecovery.journalFingerprint)
    {
        result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
        result.diagnostic = "The displayed recovery journal changed; inspect it again before resuming";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    auto currentPreviewResult = previewPuzzleCampaignSaveTransfer(displayedPreview.sourceDirectory,
        displayedPreview.targetDirectory, displayedPreview.campaignId);
    if (!currentPreviewResult)
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::failure(currentPreviewResult.error());
    auto current = std::move(currentPreviewResult).value();
    if (!current.pendingRecovery || !current.pendingRecovery->present || !current.pendingRecovery->journalValid
        || current.pendingRecovery->journalPath != shownRecovery.journalPath
        || current.pendingRecovery->campaignId != displayedPreview.campaignId
        || current.pendingRecovery->journalFingerprint != shownRecovery.journalFingerprint
        || current.pendingRecovery->journalBytes != shownRecovery.journalBytes
        || current.pendingRecovery->artifacts.size() != shownRecovery.artifacts.size())
    {
        result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
        result.diagnostic = "The recovery journal changed or no longer matches this campaign; inspect it again";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    for (std::size_t index = 0; index < current.pendingRecovery->artifacts.size(); ++index)
    {
        const auto& now = current.pendingRecovery->artifacts[index];
        const auto& shown = shownRecovery.artifacts[index];
        if (now.slot != shown.slot || now.backup != shown.backup || now.targetPath != shown.targetPath
            || now.expectedBytes != shown.expectedBytes || now.expectedFingerprint != shown.expectedFingerprint
            || now.observedBytes != shown.observedBytes || now.state != shown.state)
        {
            result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
            result.diagnostic = "A target artifact changed since recovery review; inspect it again before resuming";
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
    }

    bool sourceChanged = displayedPreview.slots.size() != current.slots.size()
        || displayedPreview.sourceBytes != current.sourceBytes
        || displayedPreview.unexpectedSourceArtifacts != current.unexpectedSourceArtifacts;
    for (std::size_t index = 0; index < current.slots.size() && index < displayedPreview.slots.size(); ++index)
    {
        const auto& shown = displayedPreview.slots[index];
        const auto& now = current.slots[index];
        sourceChanged = sourceChanged || shown.slot != now.slot || shown.state != now.state
            || shown.savedLevelId != now.savedLevelId || shown.ended != now.ended
            || shown.sourceBytes != now.sourceBytes || shown.sourcePrimaryBytes != now.sourcePrimaryBytes
            || shown.sourceBackupBytes != now.sourceBackupBytes
            || shown.sourcePrimaryExists != now.sourcePrimaryExists
            || shown.sourceBackupExists != now.sourceBackupExists
            || shown.sourceFingerprintComplete != now.sourceFingerprintComplete
            || shown.sourcePrimaryFingerprint != now.sourcePrimaryFingerprint
            || shown.sourceBackupFingerprint != now.sourceBackupFingerprint;
    }
    if (sourceChanged)
    {
        result.state = PuzzleCampaignSaveTransferCopyState::StaleSource;
        result.diagnostic = "Campaign save source changed since recovery review; create a fresh preview";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    const bool hasSource = std::any_of(current.slots.begin(), current.slots.end(), [](const auto& slot) {
        return slot.sourcePrimaryExists || slot.sourceBackupExists;
    });
    const bool untransferable = std::any_of(current.slots.begin(), current.slots.end(), [](const auto& slot) {
        return slot.state == PuzzleCampaignSaveSlotPreviewState::Invalid
            || slot.state == PuzzleCampaignSaveSlotPreviewState::CampaignMismatch
            || !slot.sourceFingerprintComplete;
    });
    if (!hasSource || untransferable || !current.unexpectedSourceArtifacts.empty())
    {
        result.state = PuzzleCampaignSaveTransferCopyState::SourceNotTransferable;
        result.diagnostic = "Source inventory is empty, invalid, mismatched or contains unexpected artifacts";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    struct Artifact final
    {
        std::filesystem::path source;
        std::filesystem::path target;
        std::size_t slot = 0;
        bool backup = false;
        std::uintmax_t bytes = 0;
        std::uint64_t fingerprint = 0;
        bool alreadyComplete = false;
    };
    std::vector<Artifact> artifacts;
    for (const auto& slot : current.slots)
    {
        if (slot.state != PuzzleCampaignSaveSlotPreviewState::Valid
            && slot.state != PuzzleCampaignSaveSlotPreviewState::RecoveredFromBackup)
            continue;
        if (slot.sourcePrimaryExists)
            artifacts.push_back({slotFilePath(current.sourceDirectory, slot.slot),
                slotFilePath(current.targetDirectory, slot.slot), slot.slot, false,
                slot.sourcePrimaryBytes, slot.sourcePrimaryFingerprint});
        if (slot.sourceBackupExists)
            artifacts.push_back({backupFilePath(slotFilePath(current.sourceDirectory, slot.slot)),
                backupFilePath(slotFilePath(current.targetDirectory, slot.slot)), slot.slot, true,
                slot.sourceBackupBytes, slot.sourceBackupFingerprint});
    }
    if (artifacts.size() != current.pendingRecovery->artifacts.size())
    {
        result.state = PuzzleCampaignSaveTransferCopyState::StaleSource;
        result.diagnostic = "Source artifact inventory no longer matches the interrupted transfer";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    for (auto& artifact : artifacts)
    {
        const auto expected = std::find_if(current.pendingRecovery->artifacts.begin(),
            current.pendingRecovery->artifacts.end(), [&](const auto& journalArtifact) {
                return journalArtifact.slot == artifact.slot && journalArtifact.backup == artifact.backup;
            });
        if (expected == current.pendingRecovery->artifacts.end()
            || expected->targetPath != artifact.target || expected->expectedBytes != artifact.bytes
            || expected->expectedFingerprint != artifact.fingerprint)
        {
            result.state = PuzzleCampaignSaveTransferCopyState::StaleSource;
            result.diagnostic = "Source artifact identity or content no longer matches the recovery journal";
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
        if (expected->state == PuzzleCampaignSaveTransferRecoveryArtifactState::Complete)
        {
            artifact.alreadyComplete = true;
            ++result.filesSkipped;
        }
        else if (expected->state != PuzzleCampaignSaveTransferRecoveryArtifactState::Missing)
        {
            result.state = PuzzleCampaignSaveTransferCopyState::DestinationConflict;
            result.diagnostic = "Partial, changed or unsafe target artifacts require manual review; resume will not overwrite them";
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
    }

    CampaignTransferJournalLease journalLease;
    std::string journalDiagnostic;
    if (!acquireTransferJournalLease(shownRecovery.journalPath, journalLease, journalDiagnostic))
    {
        result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
        result.diagnostic = "The recovery journal is active in another transfer; inspect it again before resuming";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    std::string lockedJournalText;
    if (!readTransferJournalLease(journalLease, lockedJournalText, journalDiagnostic)
        || lockedJournalText != journalText)
    {
        result.state = PuzzleCampaignSaveTransferCopyState::RecoveryRequired;
        result.diagnostic = "The recovery journal changed during review; inspect it again before resuming";
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }

    std::vector<std::pair<std::filesystem::path, std::vector<std::uint8_t>>> createdFiles;
    const auto rollback = [&](const PuzzleCampaignSaveTransferCopyState failureState, std::string diagnostic) {
        bool complete = true;
        for (auto iterator = createdFiles.rbegin(); iterator != createdFiles.rend(); ++iterator)
        {
            std::string cleanupDiagnostic;
            if (removeCreatedArtifactIfOwned(iterator->first, iterator->second, cleanupDiagnostic))
                ++result.filesRolledBack;
            else
            {
                complete = false;
                result.remainingArtifacts.push_back(iterator->first);
                diagnostic += "; " + cleanupDiagnostic;
            }
        }
        result.filesCopied = 0;
        result.bytesCopied = 0;
        result.state = complete ? failureState : PuzzleCampaignSaveTransferCopyState::RollbackIncomplete;
        result.diagnostic = std::move(diagnostic);
        if (!complete) result.diagnostic += "; rollback left artifacts that need inspection";
    };

    const auto totalToCopy = artifacts.size() - result.filesSkipped;
    for (const auto& artifact : artifacts)
    {
        if (artifact.alreadyComplete) continue;
        ++result.filesAttempted;
        auto bytes = readTransferSourceFile(artifact.source, artifact.bytes, artifact.fingerprint);
        if (!bytes)
        {
            rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack, bytes.error().message);
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
        auto write = writeTransferTargetExclusive(artifact.target, bytes.value());
        if (write.created) createdFiles.emplace_back(artifact.target, bytes.value());
        if (!write.complete)
        {
            rollback(PuzzleCampaignSaveTransferCopyState::DestinationConflict, std::move(write.diagnostic));
            return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
        }
        ++result.filesCopied;
        result.bytesCopied += artifact.bytes;
        if (progress)
        {
            bool keepGoing = false;
            try { keepGoing = progress(result.filesCopied, totalToCopy); }
            catch (...) { keepGoing = false; }
            if (!keepGoing)
            {
                rollback(PuzzleCampaignSaveTransferCopyState::CancelledRolledBack,
                    "Campaign save transfer resume was cancelled; files created during this resume were rolled back");
                return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
            }
        }
    }

    bool allArtifactsComplete = true;
    for (const auto& artifact : shownRecovery.artifacts)
    {
        const auto inspected = inspectPreviewFile(artifact.targetPath, true);
        if (!inspected || !inspected.value().exists
            || inspected.value().bytes != artifact.expectedBytes
            || !inspected.value().fingerprintComplete
            || inspected.value().fingerprint != artifact.expectedFingerprint)
        {
            allArtifactsComplete = false;
            break;
        }
    }
    if (!allArtifactsComplete)
    {
        rollback(PuzzleCampaignSaveTransferCopyState::FailedRolledBack,
            "Target artifacts changed during resume; inspect the recovery record before retrying");
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    std::string cleanupDiagnostic;
    if (!removeTransferJournalIfOwned(shownRecovery.journalPath, journalText, cleanupDiagnostic,
        &journalLease))
    {
        result.state = PuzzleCampaignSaveTransferCopyState::CopiedRecoveryPending;
        result.remainingArtifacts.push_back(shownRecovery.journalPath);
        result.diagnostic = "Campaign saves were resumed, but the recovery journal could not be removed: "
            + cleanupDiagnostic;
        return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
    }
    result.state = PuzzleCampaignSaveTransferCopyState::Resumed;
    result.diagnostic = "Interrupted campaign save transfer resumed; complete artifacts were preserved";
    return core::Result<PuzzleCampaignSaveTransferCopyResult>::success(std::move(result));
}
}
