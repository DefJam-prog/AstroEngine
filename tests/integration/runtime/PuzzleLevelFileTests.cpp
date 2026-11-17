#include "astro/runtime/PuzzleLevel.hpp"
#include "astro/runtime/PuzzleCampaign.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::runtime;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto project = std::filesystem::temp_directory_path() / ("astro-puzzle-level-test-" + std::to_string(unique));
    std::error_code ec;
    std::filesystem::create_directories(project, ec);
    check(!ec, "create isolated temporary project");
    if (ec) return EXIT_FAILURE;

    PuzzleLevelDefinition level{"first-level", "Первый уровень Ω", 4, 3, 4, 12345, 240,
        {0,1,2,3, 1,2,3,0, 2,3,0,1}};
    const auto written = PuzzleLevelFile::write(project, level);
    check(written && written.value() == PuzzleLevelFile::levelsDirectory(project) / "first-level.astro-puzzle-level",
        "write level inside the project Puzzle/Levels directory");
    const auto loaded = PuzzleLevelFile::read(project, "first-level");
    check(loaded && loaded.value() == level, "round-trip level metadata, UTF-8 name and authored cells");
    const auto createCollision = PuzzleLevelFile::createNew(project, level);
    check(!createCollision && createCollision.error().code == astro::core::ErrorCode::AlreadyExists,
        "create-only operation refuses to replace an existing level");
    const auto unchangedAfterCollision = PuzzleLevelFile::read(project, "first-level");
    check(unchangedAfterCollision && unchangedAfterCollision.value() == level,
        "create collision preserves the existing level contents");
    level.targetScore = 360;
    const auto replaced = PuzzleLevelFile::write(project, level);
    const auto replacedLevel = PuzzleLevelFile::read(project, "first-level");
    check(replaced && replacedLevel && replacedLevel.value() == level,
        "replace an existing level atomically with updated draft data");
    check(!PuzzleLevelFile::read(project, "../outside"), "reject traversal in requested level ID");

    const auto duplicate = PuzzleLevelFile::duplicate(project, "first-level", "first-copy", "Копия Ω");
    auto duplicateRead = PuzzleLevelFile::read(project, "first-copy");
    const auto sourceAfterDuplicate = PuzzleLevelFile::read(project, "first-level");
    check(duplicate && duplicateRead && duplicateRead.value().id == "first-copy"
        && duplicateRead.value().name == "Копия Ω" && duplicateRead.value().initialTiles == level.initialTiles,
        "duplicate copies authored metadata/cells under a distinct ID and requested name");
    check(sourceAfterDuplicate && sourceAfterDuplicate.value() == level,
        "duplicate leaves the source level unchanged");
    const auto duplicateCollision = PuzzleLevelFile::duplicate(project, "first-level", "first-copy", "Overwrite?");
    check(!duplicateCollision && duplicateCollision.error().code == astro::core::ErrorCode::AlreadyExists,
        "duplicate refuses an occupied destination ID");

    const auto invalidVersion = PuzzleLevelFile::levelsDirectory(project) / "broken.astro-puzzle-level";
    std::filesystem::create_directories(invalidVersion.parent_path(), ec);
    { std::ofstream output(invalidVersion, std::ios::binary); output << "ASTRO_PUZZLE_LEVEL 99\n"; }
    // Create a syntactically valid file first, then corrupt only its version token.
    auto versionFile = PuzzleLevelFile::write(project, {"bad-version", "Bad", 3, 3, 3, 1, 10,
        {0,1,2,1,2,0,2,0,1}});
    check(static_cast<bool>(versionFile), "write valid file used for version rejection test");
    if (versionFile)
    {
        { std::ofstream output(versionFile.value(), std::ios::binary | std::ios::trunc); output << "ASTRO_PUZZLE_LEVEL 99\n"; }
        check(!PuzzleLevelFile::read(project, "bad-version"), "reject unsupported level format version");
    }
    check(!PuzzleLevelFile::read(project, "broken"), "reject malformed level file");

    auto catalog = PuzzleLevelFile::list(project);
    const auto catalogHas = [&](const std::string& id, const bool readable) {
        return catalog && std::any_of(catalog.value().begin(), catalog.value().end(), [&](const auto& entry) {
            return entry.id == id && entry.readable() == readable;
        });
    };
    check(catalog && catalog.value().size() == 4 && catalogHas("first-copy", true)
        && catalogHas("first-level", true) && catalogHas("bad-version", false) && catalogHas("broken", false),
        "catalog lists readable levels and reports malformed files without hiding valid entries");
    if (catalog)
        check(std::is_sorted(catalog.value().begin(), catalog.value().end(), [](const auto& left, const auto& right) {
            return left.id < right.id;
        }), "catalog order is deterministic by level ID");

    const auto renamed = PuzzleLevelFile::rename(project, "first-copy", "renamed-copy");
    const auto renamedLevel = PuzzleLevelFile::read(project, "renamed-copy");
    check(renamed && renamedLevel && renamedLevel.value().id == "renamed-copy"
        && renamedLevel.value().name == "Копия Ω" && renamedLevel.value().initialTiles == level.initialTiles,
        "rename preserves the level definition under the new ID");
    check(!PuzzleLevelFile::read(project, "first-copy"), "rename removes the old level path after publishing the new file");
    const auto renameCollision = PuzzleLevelFile::rename(project, "first-level", "renamed-copy");
    check(!renameCollision && renameCollision.error().code == astro::core::ErrorCode::AlreadyExists,
        "rename refuses an occupied destination ID");
    check(PuzzleLevelFile::read(project, "first-level") && PuzzleLevelFile::read(project, "renamed-copy"),
        "failed rename preserves both pre-existing level files");
    check(!PuzzleLevelFile::rename(project, "first-level", "first-level"),
        "rename refuses a no-op identity change");

    PuzzleCampaignDefinition campaign{"story", "История", "first-level", {"first-level", "renamed-copy"}, {
        {"first-level", PuzzleCampaignRuleKind::Default, 0, std::string("renamed-copy")},
        {"renamed-copy", PuzzleCampaignRuleKind::Default, 0, std::nullopt}}};
    const auto campaignWritten = PuzzleCampaignFile::write(project, campaign);
    check(static_cast<bool>(campaignWritten), "write campaign referencing the level being renamed");
    const auto referencedRename = PuzzleLevelFile::rename(project, "first-level", "campaign-renamed-level");
    check(!referencedRename && referencedRename.error().message.find("story") != std::string::npos,
        "refuse to rename a level while a readable campaign references it and identify that campaign");
    check(PuzzleLevelFile::read(project, "first-level") && !PuzzleLevelFile::read(project, "campaign-renamed-level"),
        "blocked referenced-level rename leaves source untouched and publishes no destination");

    std::filesystem::remove(PuzzleCampaignFile::campaignsDirectory(project) / "story.astro-puzzle-campaign", ec);
    check(!ec, "remove the test-owned campaign before unreferenced rename check");
    const auto unreferencedRename = PuzzleLevelFile::rename(project, "first-level", "campaign-renamed-level");
    check(unreferencedRename && PuzzleLevelFile::read(project, "campaign-renamed-level")
        && !PuzzleLevelFile::read(project, "first-level"),
        "allow the rename after campaign links have been explicitly removed");

    const auto malformedCampaignPath = PuzzleCampaignFile::campaignsDirectory(project)
        / "unreadable.astro-puzzle-campaign";
    std::filesystem::create_directories(malformedCampaignPath.parent_path(), ec);
    { std::ofstream output(malformedCampaignPath, std::ios::binary); output << "not a campaign\n"; }
    const auto unverifiableRename = PuzzleLevelFile::rename(project, "renamed-copy", "blocked-by-catalog");
    check(!unverifiableRename && unverifiableRename.error().message.find("unreadable") != std::string::npos,
        "refuse level rename when a campaign cannot be checked for references");
    check(PuzzleLevelFile::read(project, "renamed-copy") && !PuzzleLevelFile::read(project, "blocked-by-catalog"),
        "unverifiable campaign blocks without changing level files");

    auto invalid = level;
    invalid.id = "../outside";
    check(!PuzzleLevelFile::write(project, invalid), "reject traversal in written level ID");
    invalid = level;
    invalid.initialTiles.pop_back();
    check(!PuzzleLevelFile::write(project, invalid), "reject incomplete authored board");
    invalid = level;
    invalid.name = std::string("bad-utf8-\xFF", 10);
    check(!PuzzleLevelFile::write(project, invalid), "reject invalid UTF-8 level names");

    std::filesystem::remove_all(project, ec);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
