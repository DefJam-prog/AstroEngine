#include "astro/runtime/PuzzleLevel.hpp"
#include "astro/runtime/PuzzleCampaign.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace astro::runtime
{
namespace
{
constexpr std::size_t maxLevelBytes = 64 * 1024;

core::Error levelError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool validId(const std::string& id)
{
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](const unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_';
    });
}

bool validUtf8(const std::string& value)
{
    const auto continuation = [&value](const std::size_t index) {
        return index < value.size() && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80;
    };
    for (std::size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i]);
        if (lead <= 0x7F) { ++i; continue; }
        if (lead >= 0xC2 && lead <= 0xDF && continuation(i + 1)) { i += 2; continue; }
        if (lead >= 0xE0 && lead <= 0xEF && continuation(i + 1) && continuation(i + 2))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xE0 || second >= 0xA0) && (lead != 0xED || second < 0xA0)) { i += 3; continue; }
        }
        if (lead >= 0xF0 && lead <= 0xF4 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xF0 || second >= 0x90) && (lead != 0xF4 || second <= 0x8F)) { i += 4; continue; }
        }
        return false;
    }
    return true;
}

bool remainsInsideProject(const std::filesystem::path& projectRoot, const std::filesystem::path& candidate)
{
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec) return false;
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return false;
    const auto relative = canonicalCandidate.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != L"..";
}

std::string serialize(const PuzzleLevelDefinition& level)
{
    std::ostringstream output;
    output << "ASTRO_PUZZLE_LEVEL 1\n"
           << "ID " << std::quoted(level.id) << '\n'
           << "NAME " << std::quoted(level.name) << '\n'
           << "BOARD " << level.width << ' ' << level.height << ' ' << level.tileKinds << ' '
           << level.seed << ' ' << level.targetScore << '\n'
           << "TILES " << level.initialTiles.size();
    for (const auto tile : level.initialTiles) output << ' ' << static_cast<unsigned int>(tile);
    output << "\nEND\n";
    return output.str();
}
}

core::Result<void> validatePuzzleLevel(const PuzzleLevelDefinition& level)
{
    if (!validId(level.id))
        return core::Result<void>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level ID must use 1-64 ASCII letters, digits, hyphens or underscores"));
    if (level.name.empty() || level.name.size() > 256 || level.name.find('\0') != std::string::npos
        || !validUtf8(level.name))
        return core::Result<void>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level name must contain 1-256 UTF-8 bytes"));
    if (level.width < 3 || level.height < 3 || level.width > 64 || level.height > 64
        || level.width > 4096 / level.height || level.tileKinds < 3 || level.tileKinds > 32
        || level.targetScore == 0 || level.initialTiles.size() != level.width * level.height)
        return core::Result<void>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level board metadata or tile count is invalid"));
    if (std::any_of(level.initialTiles.begin(), level.initialTiles.end(), [&](const std::uint8_t tile) {
            return tile >= level.tileKinds;
        }))
        return core::Result<void>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level contains a tile outside its palette"));
    return core::Result<void>::success();
}

std::filesystem::path PuzzleLevelFile::levelsDirectory(const std::filesystem::path& projectRoot)
{
    return projectRoot / L"Puzzle" / L"Levels";
}

namespace
{
core::Result<std::filesystem::path> writePuzzleLevelFile(const std::filesystem::path& projectRoot,
    const PuzzleLevelDefinition& level, const bool replaceExisting)
{
    const auto valid = validatePuzzleLevel(level);
    if (!valid) return core::Result<std::filesystem::path>::failure(valid.error());
    std::error_code ec;
    if (!std::filesystem::is_directory(projectRoot, ec) || ec)
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
            "Puzzle levels require an existing project directory"));
    const auto directory = PuzzleLevelFile::levelsDirectory(projectRoot);
    std::filesystem::create_directories(directory, ec);
    if (ec) return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
        "Cannot create Puzzle/Levels directory: " + ec.message()));
    if (!remainsInsideProject(projectRoot, directory))
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle/Levels resolves outside the project directory"));

    static std::atomic<unsigned long> sequence{0};
    const auto destination = directory / (std::filesystem::path(level.id + ".astro-puzzle-level"));
    if (!replaceExisting)
    {
        const bool destinationExists = std::filesystem::exists(destination, ec);
        if (destinationExists)
            return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::AlreadyExists,
                "A Puzzle level already uses this ID"));
        if (ec)
            return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
                "Cannot inspect Puzzle level destination: " + ec.message()));
    }
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64())
            + L"." + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        temporary = directory / (L".puzzle-level.tmp." + suffix);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
                "Cannot create temporary Puzzle level file"));
    }
    if (file == INVALID_HANDLE_VALUE)
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
            "Could not reserve a unique temporary Puzzle level file"));

    const auto contents = serialize(level);
    DWORD written = 0;
    const bool writeOk = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == contents.size() && FlushFileBuffers(file);
    const auto writeError = writeOk ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!writeOk)
    {
        DeleteFileW(temporary.c_str());
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
            "Cannot write Puzzle level file (Windows error " + std::to_string(writeError) + ")"));
    }
    const DWORD moveFlags = MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), moveFlags))
    {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str());
        if (!replaceExisting && (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS))
            return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::AlreadyExists,
                "A Puzzle level already uses this ID"));
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
            "Cannot publish Puzzle level file (Windows error " + std::to_string(error) + ")"));
    }
#else
    temporary = directory / (".puzzle-level.tmp." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const auto contents = serialize(level);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output)
        {
            output.close();
            std::filesystem::remove(temporary, ec);
            return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
                "Cannot write temporary Puzzle level file"));
        }
    }
    if (replaceExisting)
        std::filesystem::rename(temporary, destination, ec);
    else
    {
        std::filesystem::create_hard_link(temporary, destination, ec);
        if (!ec)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            if (cleanupError)
            {
                std::error_code rollbackError;
                std::filesystem::remove(destination, rollbackError);
                ec = cleanupError;
            }
        }
    }
    if (ec)
    {
        const auto publishError = ec.message();
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        std::error_code existsError;
        if (!replaceExisting && std::filesystem::exists(destination, existsError))
            return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::AlreadyExists,
                "A Puzzle level already uses this ID"));
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError,
            "Cannot atomically publish Puzzle level file: " + publishError));
    }
#endif
    return core::Result<std::filesystem::path>::success(destination);
}
}

core::Result<std::filesystem::path> PuzzleLevelFile::write(const std::filesystem::path& projectRoot,
    const PuzzleLevelDefinition& level)
{
    return writePuzzleLevelFile(projectRoot, level, true);
}

core::Result<std::filesystem::path> PuzzleLevelFile::createNew(const std::filesystem::path& projectRoot,
    const PuzzleLevelDefinition& level)
{
    return writePuzzleLevelFile(projectRoot, level, false);
}

core::Result<PuzzleLevelDefinition> PuzzleLevelFile::read(const std::filesystem::path& projectRoot,
    const std::string& levelId)
{
    if (!validId(levelId))
        return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level ID is invalid"));
    const auto file = levelsDirectory(projectRoot) / (std::filesystem::path(levelId + ".astro-puzzle-level"));
    if (!remainsInsideProject(projectRoot, file))
        return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level path resolves outside the project directory"));
    std::ifstream input(file, std::ios::binary);
    if (!input) return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::IoError,
        "Cannot open Puzzle level file"));
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > maxLevelBytes)
        return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level file exceeds the 64 KiB limit"));
    input.seekg(0, std::ios::beg);
    std::string magic, idTag, nameTag, boardTag, tilesTag, endTag, id, name, extra;
    int version = 0;
    std::size_t width = 0, height = 0, kinds = 0, tileCount = 0;
    std::uint32_t seed = 0;
    std::uint64_t target = 0;
    if (!(input >> magic >> version >> idTag >> std::quoted(id) >> nameTag >> std::quoted(name)
        >> boardTag >> width >> height >> kinds >> seed >> target >> tilesTag >> tileCount)
        || magic != "ASTRO_PUZZLE_LEVEL" || version != 1 || idTag != "ID" || nameTag != "NAME"
        || boardTag != "BOARD" || tilesTag != "TILES" || tileCount > 4096)
        return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::ParseError,
            "Puzzle level header is malformed or uses an unsupported version"));
    std::vector<std::uint8_t> tiles;
    tiles.reserve(tileCount);
    for (std::size_t i = 0; i < tileCount; ++i)
    {
        unsigned int tile = 0;
        if (!(input >> tile) || tile > std::numeric_limits<std::uint8_t>::max())
            return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::ParseError,
                "Puzzle level tile data is malformed"));
        tiles.push_back(static_cast<std::uint8_t>(tile));
    }
    if (!(input >> endTag) || endTag != "END" || (input >> extra))
        return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::ParseError,
            "Puzzle level has malformed end marker or trailing data"));
    PuzzleLevelDefinition level{std::move(id), std::move(name), width, height, kinds, seed, target, std::move(tiles)};
    const auto valid = validatePuzzleLevel(level);
    if (!valid) return core::Result<PuzzleLevelDefinition>::failure(valid.error());
    if (level.id != levelId)
        return core::Result<PuzzleLevelDefinition>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level document ID does not match its filename"));
    return core::Result<PuzzleLevelDefinition>::success(std::move(level));
}

core::Result<std::vector<PuzzleLevelCatalogEntry>> PuzzleLevelFile::list(
    const std::filesystem::path& projectRoot)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(projectRoot, ec) || ec)
        return core::Result<std::vector<PuzzleLevelCatalogEntry>>::failure(levelError(core::ErrorCode::IoError,
            "Puzzle level catalog requires an existing project directory"));
    const auto directory = levelsDirectory(projectRoot);
    if (!std::filesystem::exists(directory, ec))
    {
        if (ec) return core::Result<std::vector<PuzzleLevelCatalogEntry>>::failure(levelError(
            core::ErrorCode::IoError, "Cannot inspect Puzzle levels directory: " + ec.message()));
        return core::Result<std::vector<PuzzleLevelCatalogEntry>>::success({});
    }
    if (!std::filesystem::is_directory(directory, ec) || ec || !remainsInsideProject(projectRoot, directory))
        return core::Result<std::vector<PuzzleLevelCatalogEntry>>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle levels directory is invalid or resolves outside the project"));

    std::vector<PuzzleLevelCatalogEntry> entries;
    std::filesystem::directory_iterator current(directory, ec), end;
    if (ec) return core::Result<std::vector<PuzzleLevelCatalogEntry>>::failure(levelError(core::ErrorCode::IoError,
        "Cannot enumerate Puzzle levels directory: " + ec.message()));
    while (current != end)
    {
        const auto& path = current->path();
        if (path.extension() == ".astro-puzzle-level")
        {
            if (entries.size() >= 4096)
                return core::Result<std::vector<PuzzleLevelCatalogEntry>>::failure(levelError(
                    core::ErrorCode::ValidationError, "Puzzle level catalog exceeds the 4096-file limit"));
            const auto stem = path.stem().u8string();
            const std::string id(reinterpret_cast<const char*>(stem.data()), stem.size());
            if (!validId(id))
                entries.push_back({id, std::nullopt, "Filename does not use a safe Puzzle level ID"});
            else
            {
                const auto status = current->symlink_status(ec);
                if (ec)
                    return core::Result<std::vector<PuzzleLevelCatalogEntry>>::failure(levelError(core::ErrorCode::IoError,
                        "Cannot inspect Puzzle level catalog entry: " + ec.message()));
                if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
                    entries.push_back({id, std::nullopt, "Catalog entry is not a regular non-link file"});
                else
                {
                    auto loaded = read(projectRoot, id);
                    if (!loaded) entries.push_back({id, std::nullopt, loaded.error().message});
                    else entries.push_back({id, loaded.value().name, {}});
                }
            }
        }
        current.increment(ec);
        if (ec) return core::Result<std::vector<PuzzleLevelCatalogEntry>>::failure(levelError(core::ErrorCode::IoError,
            "Failed while enumerating Puzzle levels directory: " + ec.message()));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return core::Result<std::vector<PuzzleLevelCatalogEntry>>::success(std::move(entries));
}

core::Result<std::filesystem::path> PuzzleLevelFile::duplicate(const std::filesystem::path& projectRoot,
    const std::string& sourceId, std::string newId, std::string newName)
{
    auto source = read(projectRoot, sourceId);
    if (!source) return core::Result<std::filesystem::path>::failure(source.error());
    auto copy = std::move(source).value();
    copy.id = std::move(newId);
    copy.name = std::move(newName);
    return createNew(projectRoot, copy);
}

core::Result<std::filesystem::path> PuzzleLevelFile::rename(const std::filesystem::path& projectRoot,
    const std::string& sourceId, std::string newId)
{
    if (!validId(sourceId) || !validId(newId) || sourceId == newId)
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level rename requires distinct valid source and destination IDs"));
    const auto campaigns = PuzzleCampaignFile::list(projectRoot);
    if (!campaigns)
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::InvalidState,
            "Cannot safely rename Puzzle level because the campaign catalog could not be checked: "
                + campaigns.error().message));
    std::vector<std::string> referencingCampaigns;
    std::vector<std::string> unreadableCampaigns;
    for (const auto& entry : campaigns.value())
    {
        if (!entry.readable())
        {
            unreadableCampaigns.push_back(entry.id);
            continue;
        }
        if (std::find(entry.definition->orderedLevelIds.begin(), entry.definition->orderedLevelIds.end(), sourceId)
            != entry.definition->orderedLevelIds.end())
            referencingCampaigns.push_back(entry.id);
    }
    if (!unreadableCampaigns.empty())
    {
        std::string names;
        for (const auto& id : unreadableCampaigns) names += (names.empty() ? "" : ", ") + id;
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::InvalidState,
            "Cannot safely rename Puzzle level while campaign references are unverified; repair campaign files first: "
                + names));
    }
    if (!referencingCampaigns.empty())
    {
        std::string names;
        for (const auto& id : referencingCampaigns) names += (names.empty() ? "" : ", ") + id;
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::InvalidState,
            "Cannot rename Puzzle level '" + sourceId + "'; it is referenced by campaigns: " + names
                + ". Update those campaign references and save them before renaming."));
    }
    const auto sourcePath = levelsDirectory(projectRoot) / (sourceId + ".astro-puzzle-level");
    std::error_code statusError;
    const auto sourceStatus = std::filesystem::symlink_status(sourcePath, statusError);
    if (statusError || !std::filesystem::is_regular_file(sourceStatus) || std::filesystem::is_symlink(sourceStatus)
        || !remainsInsideProject(projectRoot, sourcePath))
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::ValidationError,
            "Puzzle level rename source is missing, linked, or outside the project"));

    auto source = read(projectRoot, sourceId);
    if (!source) return core::Result<std::filesystem::path>::failure(source.error());
    const auto original = source.value();
    auto renamed = original;
    renamed.id = std::move(newId);
    const auto published = createNew(projectRoot, renamed);
    if (!published) return published;

    const auto sourceRecheck = read(projectRoot, sourceId);
    if (!sourceRecheck || sourceRecheck.value() != original)
    {
        std::error_code rollbackError;
        std::filesystem::remove(published.value(), rollbackError);
        const auto message = rollbackError
            ? "Source level changed during rename and the new copy could not be rolled back: "
                + rollbackError.message()
            : "Source level changed during rename; the new file was rolled back";
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::InvalidState, message));
    }

    std::error_code removeError;
    if (!std::filesystem::remove(sourcePath, removeError) || removeError)
    {
        std::error_code rollbackError;
        std::filesystem::remove(published.value(), rollbackError);
        const auto message = rollbackError
            ? "Could not remove old level or roll back new level: " + removeError.message() + "; " + rollbackError.message()
            : "Could not remove old level; new level publication was rolled back: " + removeError.message();
        return core::Result<std::filesystem::path>::failure(levelError(core::ErrorCode::IoError, message));
    }
    return published;
}
}
