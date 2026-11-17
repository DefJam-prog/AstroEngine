#include "astro/runtime/PuzzleSaveAdapter.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace
{
int failures = 0;
template <typename T>
void check(const T& condition, const char* message)
{
    if (!static_cast<bool>(condition)) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool sameSnapshot(const astro::runtime::PuzzleSnapshot& lhs, const astro::runtime::PuzzleSnapshot& rhs)
{
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.tileKinds == rhs.tileKinds
        && lhs.levelTarget == rhs.levelTarget && lhs.score == rhs.score && lhs.level == rhs.level
        && lhs.tiles == rhs.tiles && lhs.randomState == rhs.randomState;
}
}

int main()
{
    using namespace astro;
    using namespace astro::runtime;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("AstroEngine Puzzle Save " + std::to_string(unique));
    auto opened = SaveStore::open(directory, 2);
    check(opened, "open isolated Puzzle SaveStore");
    if (!opened) return EXIT_FAILURE;
    auto store = std::move(opened).value();

    scene::Scene scene{942, "Puzzle Save Ω"};
    auto playerResult = scene.createEntity("Сохранённый игрок");
    check(playerResult, "create scene object for combined Scene and Puzzle save");
    if (!playerResult) return EXIT_FAILURE;
    const auto player = playerResult.value();

    auto profileResult = SaveProfile::create("puzzle-progress", 1);
    check(profileResult, "create Puzzle save profile");
    if (!profileResult) return EXIT_FAILURE;
    auto profile = std::move(profileResult).value();
    check(profile.addField(player, std::string(SaveProperty::entityName)), "select explicit scene name field");
    check(selectPuzzleSnapshotField(profile), "select versioned Puzzle snapshot field");
    check(profile.addCustomField("campaign.label", std::string{}), "select an additional custom value");
    const SaveCustomState additional{{"campaign.label", std::string("demo Ω")}};

    auto puzzleResult = PuzzleGame::create(8, 8, 9123, 5, 10);
    check(puzzleResult, "create deterministic Puzzle game");
    if (!puzzleResult) return EXIT_FAILURE;
    auto puzzle = std::move(puzzleResult).value();
    bool madeProgress = false;
    for (std::size_t row = 0; row < puzzle.height() && !madeProgress; ++row)
        for (std::size_t column = 0; column < puzzle.width() && !madeProgress; ++column)
            for (const GridPosition offset : {GridPosition{1, 0}, GridPosition{0, 1}})
            {
                const GridPosition next{column + offset.column, row + offset.row};
                if (next.column >= puzzle.width() || next.row >= puzzle.height()) continue;
                auto candidate = puzzle;
                auto move = candidate.swap({column, row}, next);
                if (move && move.value().accepted)
                {
                    puzzle = std::move(candidate);
                    madeProgress = true;
                    break;
                }
            }
    check(madeProgress && puzzle.score() > 0 && puzzle.level() > 1,
        "prepare snapshot with changed board, score and level");
    const auto expectedSnapshot = puzzle.snapshot();

    check(!savePuzzleSnapshot(store, 0, scene, profile, "1.0.0", puzzle, additional, "not.selected"),
        "reject a snapshot key that the profile did not select as a string");
    auto saved = savePuzzleSnapshot(store, 0, scene, profile, "1.0.0", puzzle, additional);
    check(saved && saved.value().slotInfo.propertyCount == 3,
        "save explicitly selected Scene field, Puzzle snapshot and extra custom value");
    auto loadedInfo = store.inspect(0);
    check(loadedInfo && loadedInfo.value().propertyCount == 3,
        "slot metadata counts selected Puzzle and additional fields");

    check(scene.setProperty(player, "name", scene::PropertyValue{std::string("Unsaved name")}),
        "change Scene after saving");
    auto loaded = loadPuzzleSnapshot(store, 0, scene, profile, "1.0.0");
    check(loaded, "load and validate combined Scene/Puzzle snapshot");
    if (loaded)
    {
        check(sameSnapshot(loaded.value().puzzle.snapshot(), expectedSnapshot),
            "restore board rules, score, level and deterministic RNG state");
        check(scene.find(player)->name == "Сохранённый игрок" && loaded.value().save.appliedProperties == 1,
            "publish selected Scene fields only after Puzzle snapshot restore succeeds");
        check(loaded.value().additionalCustomState == additional,
            "return other custom profile values separately from Puzzle snapshot payload");
        auto expectedNext = puzzle;
        auto resumedNext = loaded.value().puzzle;
        bool comparedMove = false;
        for (std::size_t row = 0; row < puzzle.height() && !comparedMove; ++row)
            for (std::size_t column = 0; column < puzzle.width() && !comparedMove; ++column)
                for (const GridPosition offset : {GridPosition{1, 0}, GridPosition{0, 1}})
                {
                    const GridPosition next{column + offset.column, row + offset.row};
                    if (next.column >= puzzle.width() || next.row >= puzzle.height()) continue;
                    auto left = expectedNext;
                    auto right = resumedNext;
                    const auto a = left.swap({column, row}, next);
                    const auto b = right.swap({column, row}, next);
                    if (a && b)
                    {
                        check(a.value().accepted == b.value().accepted && a.value().pointsAwarded == b.value().pointsAwarded
                            && sameSnapshot(left.snapshot(), right.snapshot()),
                            "saved Puzzle snapshot preserves deterministic next move and refill");
                        comparedMove = true;
                    }
                }
        check(comparedMove, "compare a future Puzzle move after load");
    }

    auto levelStoreResult = SaveStore::open(directory / "authored-levels" / "chapter-one", 2);
    auto levelProfileResult = SaveProfile::create("puzzle-level-progress", 1);
    check(levelStoreResult && levelProfileResult, "create isolated authored-level progress store/profile");
    if (levelStoreResult && levelProfileResult)
    {
        auto levelStore = std::move(levelStoreResult).value();
        auto levelProfile = std::move(levelProfileResult).value();
        check(levelProfile.addField(player, std::string(SaveProperty::entityName)),
            "select Scene field for authored-level progress");
        check(selectPuzzleSnapshotField(levelProfile), "select Puzzle snapshot for authored-level progress");
        check(selectPuzzleLevelIdentityField(levelProfile), "select stable Puzzle level identity");
        const auto savedLevel = savePuzzleLevelSnapshot(levelStore, 0, scene, levelProfile,
            "1.0.0", puzzle, "chapter-one");
        check(savedLevel && savedLevel.value().slotInfo.propertyCount == 3,
            "save snapshot with explicit authored-level identity");

        check(scene.setProperty(player, "name", scene::PropertyValue{std::string("Must survive wrong-level load")}),
            "mutate Scene before checking authored-level mismatch");
        const auto mismatchedName = scene.find(player)->name;
        const auto wrongLevel = loadPuzzleLevelSnapshot(levelStore, 0, scene, levelProfile, "1.0.0", "chapter-two");
        check(!wrongLevel && wrongLevel.error().code == core::ErrorCode::VersionMismatch,
            "reject a slot whose level ID differs from the active level");
        check(scene.find(player)->name == mismatchedName,
            "wrong-level rejection leaves Scene state unchanged");

        const auto restoredLevel = loadPuzzleLevelSnapshot(levelStore, 0, scene, levelProfile,
            "1.0.0", "chapter-one");
        check(restoredLevel && sameSnapshot(restoredLevel.value().puzzle.snapshot(), expectedSnapshot),
            "load exact Puzzle progress for the matching authored level");
        check(scene.find(player)->name == "Сохранённый игрок"
            && restoredLevel.value().additionalCustomState.empty(),
            "publish Scene only after level identity matches and consume the internal identity field");
        check(!savePuzzleLevelSnapshot(levelStore, 1, scene, levelProfile,
            "1.0.0", puzzle, "../outside"), "reject traversal as authored save identity");
    }

    const auto stableName = scene.find(player)->name;
    SaveCustomState invalidPayload{
        {"puzzle.snapshot", std::string("ASTRO_PUZZLE_SNAPSHOT 99\n")},
        {"campaign.label", std::string("bad payload")}
    };
    check(store.save(1, scene, profile, "1.0.0", invalidPayload),
        "store a checksummed slot containing an unsupported Puzzle payload version");
    check(scene.setProperty(player, "name", scene::PropertyValue{std::string("Must remain unchanged")}),
        "set value to verify decode failure is atomic");
    const auto beforeFailedLoad = scene.find(player)->name;
    auto unsupported = loadPuzzleSnapshot(store, 1, scene, profile, "1.0.0");
    check(!unsupported && unsupported.error().code == core::ErrorCode::VersionMismatch,
        "diagnose unsupported Puzzle snapshot payload version");
    check(scene.find(player)->name == beforeFailedLoad && beforeFailedLoad != stableName,
        "failed Puzzle payload validation leaves the caller Scene unchanged");

    invalidPayload["puzzle.snapshot"] = std::string(
        "ASTRO_PUZZLE_SNAPSHOT 1\nRULES 8 8 5 10\nPROGRESS 40 5\nTILES 000\n");
    check(store.save(1, scene, profile, "1.0.0", invalidPayload),
        "store a checksummed slot with malformed Puzzle tile data");
    auto malformed = loadPuzzleSnapshot(store, 1, scene, profile, "1.0.0");
    check(!malformed && malformed.error().code == core::ErrorCode::ParseError,
        "reject malformed versioned Puzzle payload");
    check(scene.find(player)->name == beforeFailedLoad,
        "malformed Puzzle payload also leaves the caller Scene unchanged");

    auto noSnapshotProfileResult = SaveProfile::create("scene-only", 1);
    check(noSnapshotProfileResult && noSnapshotProfileResult.value().addField(player,
        std::string(SaveProperty::entityName)), "create profile without a Puzzle snapshot selection");
    if (noSnapshotProfileResult)
        check(!savePuzzleSnapshot(store, 0, scene, noSnapshotProfileResult.value(), "1.0.0", puzzle),
            "refuse implicit snapshot persistence for profiles without explicit selection");
    auto wrongTypeProfileResult = SaveProfile::create("wrong-puzzle-field", 1);
    check(wrongTypeProfileResult && wrongTypeProfileResult.value().addCustomField("puzzle.snapshot", 0.0),
        "create profile with a non-string snapshot field");
    if (wrongTypeProfileResult)
        check(!loadPuzzleSnapshot(store, 0, scene, wrongTypeProfileResult.value(), "1.0.0"),
            "refuse to decode Puzzle snapshot from a non-string selected field");

    std::error_code fsError;
    std::filesystem::remove_all(directory, fsError);
    check(!fsError, "remove isolated Puzzle save test files");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
