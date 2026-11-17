#include "astro/runtime/PuzzleSaveAdapter.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace astro::runtime
{
namespace
{
constexpr std::size_t maxPuzzleSnapshotBytes = 65536;
constexpr char hexDigits[] = "0123456789abcdef";

core::Error adapterError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool hasStringField(const SaveProfile& profile, const std::string_view key)
{
    const auto selected = profile.customFields().find(std::string(key));
    return selected != profile.customFields().end()
        && std::holds_alternative<std::string>(selected->second);
}

bool validLevelId(const std::string_view id)
{
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](const unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_';
    });
}

core::Result<std::string> encodeSnapshot(const PuzzleSnapshot& snapshot)
{
    auto valid = PuzzleGame::restore(snapshot);
    if (!valid) return core::Result<std::string>::failure(valid.error());

    std::string tiles;
    tiles.reserve(snapshot.tiles.size() * 2);
    for (const auto tile : snapshot.tiles)
    {
        tiles.push_back(hexDigits[(tile >> 4U) & 0x0fU]);
        tiles.push_back(hexDigits[tile & 0x0fU]);
    }

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "ASTRO_PUZZLE_SNAPSHOT 1\n"
           << "RULES " << snapshot.width << ' ' << snapshot.height << ' ' << snapshot.tileKinds << ' '
           << snapshot.levelTarget << '\n'
           << "PROGRESS " << snapshot.score << ' ' << snapshot.level << '\n'
           << "TILES " << tiles << '\n'
           << "RNG " << std::quoted(snapshot.randomState) << '\n'
           << "END\n";
    if (!output) return core::Result<std::string>::failure(adapterError(core::ErrorCode::InternalError,
        "Could not encode Puzzle snapshot"));
    auto encoded = output.str();
    if (encoded.size() > maxPuzzleSnapshotBytes)
        return core::Result<std::string>::failure(adapterError(core::ErrorCode::ValidationError,
            "Encoded Puzzle snapshot exceeds the SaveStore custom string limit"));
    return core::Result<std::string>::success(std::move(encoded));
}

int hexValue(const char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

core::Result<PuzzleSnapshot> decodeSnapshot(const std::string_view encoded)
{
    if (encoded.empty() || encoded.size() > maxPuzzleSnapshotBytes)
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ValidationError,
            "Puzzle snapshot payload is empty or too large"));
    std::istringstream input{std::string(encoded)};
    input.imbue(std::locale::classic());
    std::string token;
    std::uint32_t version = 0;
    PuzzleSnapshot snapshot;
    if (!(input >> token >> version) || token != "ASTRO_PUZZLE_SNAPSHOT")
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
            "Puzzle snapshot header is missing or malformed"));
    if (version != 1)
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::VersionMismatch,
            "Unsupported Puzzle snapshot payload version"));
    if (!(input >> token >> snapshot.width >> snapshot.height >> snapshot.tileKinds >> snapshot.levelTarget)
        || token != "RULES")
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
            "Puzzle snapshot rules are malformed"));
    if (snapshot.width < 3 || snapshot.width > 64 || snapshot.height < 3 || snapshot.height > 64
        || snapshot.width > 4096 / snapshot.height || snapshot.tileKinds < 3 || snapshot.tileKinds > 32
        || snapshot.levelTarget == 0)
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ValidationError,
            "Puzzle snapshot rules are outside supported bounds"));
    if (!(input >> token >> snapshot.score >> snapshot.level) || token != "PROGRESS" || snapshot.level == 0)
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
            "Puzzle snapshot progress is malformed"));
    std::string tileHex;
    if (!(input >> token >> tileHex) || token != "TILES"
        || tileHex.size() != snapshot.width * snapshot.height * 2)
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
            "Puzzle snapshot tile data has an invalid length"));
    snapshot.tiles.reserve(snapshot.width * snapshot.height);
    for (std::size_t index = 0; index < tileHex.size(); index += 2)
    {
        const int high = hexValue(tileHex[index]);
        const int low = hexValue(tileHex[index + 1]);
        if (high < 0 || low < 0)
            return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
                "Puzzle snapshot tile data contains a non-hex character"));
        const auto tile = static_cast<std::uint8_t>((high << 4) | low);
        if (tile >= snapshot.tileKinds)
            return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ValidationError,
                "Puzzle snapshot contains a tile outside its palette"));
        snapshot.tiles.push_back(tile);
    }
    if (!(input >> token >> std::quoted(snapshot.randomState)) || token != "RNG")
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
            "Puzzle snapshot RNG state is malformed"));
    if (!(input >> token) || token != "END")
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
            "Puzzle snapshot terminator is missing"));
    input >> std::ws;
    if (!input.eof())
        return core::Result<PuzzleSnapshot>::failure(adapterError(core::ErrorCode::ParseError,
            "Unexpected data follows Puzzle snapshot terminator"));
    auto valid = PuzzleGame::restore(snapshot);
    if (!valid) return core::Result<PuzzleSnapshot>::failure(valid.error());
    return core::Result<PuzzleSnapshot>::success(std::move(snapshot));
}
}

core::Result<void> selectPuzzleSnapshotField(SaveProfile& profile, std::string key)
{
    return profile.addCustomField(std::move(key), std::string{});
}

core::Result<void> selectPuzzleLevelIdentityField(SaveProfile& profile, std::string key)
{
    return profile.addCustomField(std::move(key), std::string{});
}

core::Result<SaveWriteResult> savePuzzleSnapshot(const SaveStore& store, const std::size_t slot,
    const scene::Scene& scene, const SaveProfile& profile, const std::string_view gameVersion,
    const PuzzleGame& puzzle, const SaveCustomState& additionalCustomState, const std::string_view snapshotKey)
{
    if (!hasStringField(profile, snapshotKey))
        return core::Result<SaveWriteResult>::failure(adapterError(core::ErrorCode::TypeMismatch,
            "SaveProfile must explicitly select the Puzzle snapshot key as a string"));
    auto values = additionalCustomState;
    if (values.contains(std::string(snapshotKey)))
        return core::Result<SaveWriteResult>::failure(adapterError(core::ErrorCode::AlreadyExists,
            "Additional custom state already contains the Puzzle snapshot key"));
    auto encoded = encodeSnapshot(puzzle.snapshot());
    if (!encoded) return core::Result<SaveWriteResult>::failure(encoded.error());
    values.emplace(std::string(snapshotKey), std::move(encoded).value());
    return store.save(slot, scene, profile, gameVersion, values);
}

core::Result<PuzzleSnapshotLoadResult> loadPuzzleSnapshot(const SaveStore& store, const std::size_t slot,
    scene::Scene& scene, const SaveProfile& profile, const std::string_view gameVersion,
    const std::string_view snapshotKey)
{
    if (!hasStringField(profile, snapshotKey))
        return core::Result<PuzzleSnapshotLoadResult>::failure(adapterError(core::ErrorCode::TypeMismatch,
            "SaveProfile must explicitly select the Puzzle snapshot key as a string"));

    auto sceneCandidate = scene;
    SaveCustomState customState;
    auto loaded = store.load(slot, sceneCandidate, profile, gameVersion, customState);
    if (!loaded) return core::Result<PuzzleSnapshotLoadResult>::failure(loaded.error());
    const auto savedSnapshot = customState.find(std::string(snapshotKey));
    if (savedSnapshot == customState.end())
        return core::Result<PuzzleSnapshotLoadResult>::failure(adapterError(core::ErrorCode::NotFound,
            "Save slot does not contain the selected Puzzle snapshot"));
    const auto* text = std::get_if<std::string>(&savedSnapshot->second);
    if (!text)
        return core::Result<PuzzleSnapshotLoadResult>::failure(adapterError(core::ErrorCode::TypeMismatch,
            "Saved Puzzle snapshot field is not a string"));
    auto decoded = decodeSnapshot(*text);
    if (!decoded) return core::Result<PuzzleSnapshotLoadResult>::failure(decoded.error());
    auto puzzle = PuzzleGame::restore(decoded.value());
    if (!puzzle) return core::Result<PuzzleSnapshotLoadResult>::failure(puzzle.error());

    customState.erase(savedSnapshot);
    scene = std::move(sceneCandidate);
    return core::Result<PuzzleSnapshotLoadResult>::success({std::move(puzzle).value(),
        std::move(loaded).value(), std::move(customState)});
}

core::Result<SaveWriteResult> savePuzzleLevelSnapshot(const SaveStore& store, const std::size_t slot,
    const scene::Scene& scene, const SaveProfile& profile, const std::string_view gameVersion,
    const PuzzleGame& puzzle, const std::string_view levelId, const SaveCustomState& additionalCustomState)
{
    if (!validLevelId(levelId))
        return core::Result<SaveWriteResult>::failure(adapterError(core::ErrorCode::ValidationError,
            "Puzzle level save requires a valid 1-64 character level ID"));
    if (!hasStringField(profile, PuzzleSaveField::levelId))
        return core::Result<SaveWriteResult>::failure(adapterError(core::ErrorCode::TypeMismatch,
            "SaveProfile must explicitly select the Puzzle level identity key as a string"));
    if (additionalCustomState.contains(std::string(PuzzleSaveField::levelId)))
        return core::Result<SaveWriteResult>::failure(adapterError(core::ErrorCode::AlreadyExists,
            "Additional custom state already contains the Puzzle level identity key"));
    auto customState = additionalCustomState;
    customState.emplace(std::string(PuzzleSaveField::levelId), std::string(levelId));
    return savePuzzleSnapshot(store, slot, scene, profile, gameVersion, puzzle, customState);
}

core::Result<PuzzleSnapshotLoadResult> loadPuzzleLevelSnapshot(const SaveStore& store, const std::size_t slot,
    scene::Scene& scene, const SaveProfile& profile, const std::string_view gameVersion,
    const std::string_view expectedLevelId)
{
    if (!validLevelId(expectedLevelId))
        return core::Result<PuzzleSnapshotLoadResult>::failure(adapterError(core::ErrorCode::ValidationError,
            "Puzzle level load requires a valid expected level ID"));
    if (!hasStringField(profile, PuzzleSaveField::levelId))
        return core::Result<PuzzleSnapshotLoadResult>::failure(adapterError(core::ErrorCode::TypeMismatch,
            "SaveProfile must explicitly select the Puzzle level identity key as a string"));

    auto sceneCandidate = scene;
    auto loaded = loadPuzzleSnapshot(store, slot, sceneCandidate, profile, gameVersion);
    if (!loaded) return loaded;
    auto state = std::move(loaded).value();
    const auto identity = state.additionalCustomState.find(std::string(PuzzleSaveField::levelId));
    const auto* savedLevelId = identity == state.additionalCustomState.end()
        ? nullptr : std::get_if<std::string>(&identity->second);
    if (!savedLevelId || *savedLevelId != expectedLevelId)
        return core::Result<PuzzleSnapshotLoadResult>::failure(adapterError(core::ErrorCode::VersionMismatch,
            "Puzzle save slot belongs to a different authored level"));
    state.additionalCustomState.erase(std::string(PuzzleSaveField::levelId));
    scene = std::move(sceneCandidate);
    return core::Result<PuzzleSnapshotLoadResult>::success(std::move(state));
}
}
