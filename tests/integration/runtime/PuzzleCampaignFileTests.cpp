#include "astro/runtime/PuzzleCampaign.hpp"
#include "astro/runtime/PuzzleCampaignSession.hpp"
#include "astro/runtime/PuzzleLevel.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

astro::runtime::PuzzleCampaignDefinition fixture()
{
    using namespace astro::runtime;
    return {"story", "Основная история Ω", "intro", {"intro", "bonus", "finale"}, {
        {"intro", PuzzleCampaignRuleKind::ScoreAtLeast, 900, "bonus"},
        {"intro", PuzzleCampaignRuleKind::ScoreAtLeast, 500, "finale"},
        {"intro", PuzzleCampaignRuleKind::Default, 0, "bonus"},
        {"bonus", PuzzleCampaignRuleKind::Default, 0, "finale"},
        {"finale", PuzzleCampaignRuleKind::Default, 0, std::nullopt}}};
}

std::string replaceOnce(std::string value, const std::string& from, const std::string& to)
{
    const auto position = value.find(from);
    if (position != std::string::npos) value.replace(position, from.size(), to);
    return value;
}
}

int main()
{
    using namespace astro::runtime;
    auto campaign = fixture();
    auto encoded = serializePuzzleCampaign(campaign);
    check(static_cast<bool>(encoded), "serialize valid campaign");
    if (encoded)
    {
        const auto parsed = parsePuzzleCampaign(encoded.value());
        check(parsed && parsed.value() == campaign, "UTF-8 campaign round-trip preserves ordered levels and ordered rules");
        check(encoded.value().find("DEFAULT END") != std::string::npos, "terminal default uses the reserved unquoted END token");
        auto escapedText = campaign;
        escapedText.name = "История \\\"Ω\\\" \\\\ финал";
        const auto escapedEncoded = serializePuzzleCampaign(escapedText);
        const auto escapedParsed = escapedEncoded ? parsePuzzleCampaign(escapedEncoded.value())
            : astro::core::Result<PuzzleCampaignDefinition>::failure({astro::core::ErrorCode::InternalError, "serialize failed"});
        check(escapedParsed && escapedParsed.value() == escapedText, "quoted names round-trip supported quote and backslash escapes");

        const auto highThreshold = resolvePuzzleCampaignTransition(campaign, "intro", 900);
        const auto lowerThreshold = resolvePuzzleCampaignTransition(campaign, "intro", 500);
        const auto defaultTarget = resolvePuzzleCampaignTransition(campaign, "intro", 499);
        const auto terminal = resolvePuzzleCampaignTransition(campaign, "finale", 0);
        check(highThreshold && highThreshold.value() == std::optional<std::string>("bonus"),
            "select the matching high score threshold");
        check(lowerThreshold && lowerThreshold.value() == std::optional<std::string>("finale"),
            "select the lower threshold when the higher threshold does not match");
        check(defaultTarget && defaultTarget.value() == std::optional<std::string>("bonus"),
            "fall back to the source level DEFAULT rule when no score threshold matches");
        check(terminal && !terminal.value(), "return an empty target for terminal DEFAULT END");
        const auto unknownSource = resolvePuzzleCampaignTransition(campaign, "absent", 1000);
        check(!unknownSource && unknownSource.error().code == astro::core::ErrorCode::NotFound,
            "reject completion requests for levels outside the campaign");
        auto invalidForResolve = campaign;
        invalidForResolve.startLevelId = "absent";
        check(!resolvePuzzleCampaignTransition(invalidForResolve, "intro", 1000),
            "reject malformed campaign data before resolving a transition");
        const auto oneEdge = highThreshold && highThreshold.value().has_value()
            ? resolvePuzzleCampaignTransition(campaign, *highThreshold.value(), 900)
            : astro::core::Result<std::optional<std::string>>::failure({astro::core::ErrorCode::InternalError, "no first target"});
        check(oneEdge && oneEdge.value() == std::optional<std::string>("finale"),
            "resolve only the completed level's edge and do not recursively advance the destination");

        auto expectRejected = [&](const std::string& text, const char* message) {
            check(!parsePuzzleCampaign(text), message);
        };
        expectRejected(replaceOnce(encoded.value(), "CAMPAIGN 1", "CAMPAIGN 2"), "reject unsupported format version");
        expectRejected(replaceOnce(encoded.value(), "SCORE_AT_LEAST", "FLAG_IS"), "reject unknown condition token");
        expectRejected(replaceOnce(encoded.value(), "NAME \"Основная история Ω\"", "NAME \"bad\\qescape\""),
            "reject unsupported quoted-string escape");
        expectRejected(replaceOnce(encoded.value(), "LEVEL 1", "LEVEL 2"), "reject non-contiguous ordered level indices");
        expectRejected(replaceOnce(encoded.value(), "TRANSITIONS 5", "TRANSITIONS 6"), "reject truncated transition list");
        expectRejected(encoded.value() + "\n", "reject trailing blank records after terminator");
        auto duplicateDefault = replaceOnce(encoded.value(), "TRANSITIONS 5", "TRANSITIONS 6");
        duplicateDefault = replaceOnce(std::move(duplicateDefault), "TRANSITION \"intro\" DEFAULT",
            "TRANSITION \"intro\" DEFAULT\nTRANSITION \"intro\" DEFAULT");
        expectRejected(duplicateDefault, "reject duplicate default rules");
        expectRejected(replaceOnce(encoded.value(), "LEVEL 1 \"bonus\"", "LEVEL 1 \"intro\""), "reject duplicate level IDs");
        expectRejected(replaceOnce(encoded.value(), "LEVEL 1 \"bonus\"", "LEVEL 1 \"bad/id\""), "reject unsafe referenced IDs");
        expectRejected(replaceOnce(encoded.value(), "START \"intro\"", "START \"absent\""), "reject start ID not in ordered levels");
        expectRejected(replaceOnce(encoded.value(), "SCORE_AT_LEAST 900", "SCORE_AT_LEAST 0"), "reject zero score threshold");
        expectRejected(replaceOnce(encoded.value(), "SCORE_AT_LEAST 900", "SCORE_AT_LEAST 18446744073709551616"), "reject score threshold overflow");
        expectRejected(replaceOnce(encoded.value(), "SCORE_AT_LEAST 900 \"bonus\"", "SCORE_AT_LEAST 900 \"absent\""),
            "reject transition target absent from campaign");
        expectRejected(replaceOnce(encoded.value(), "TRANSITION \"bonus\" DEFAULT", "TRANSITION \"finale\" DEFAULT"),
            "reject unreachable listed level");
        expectRejected(std::string("ASTRO_PUZZLE_CAMPAIGN 1\nID \"x\"\nNAME \"bad\xFF\"\n"), "reject malformed UTF-8");
        expectRejected(replaceOnce(encoded.value(), "NAME \"Основная история Ω\"", "NAME \"line\nbreak\""),
            "reject raw line break inside quoted name");
        expectRejected(std::string(maxPuzzleCampaignFileBytes + 1, 'x'), "reject documents above the configured size limit");

        auto descending = campaign;
        descending.transitions.insert(descending.transitions.begin() + 1,
            {"intro", PuzzleCampaignRuleKind::ScoreAtLeast, 200, "finale"});
        check(!validatePuzzleCampaign(descending), "require score thresholds to be strictly descending");
        auto defaultNotLast = campaign;
        std::swap(defaultNotLast.transitions[0], defaultNotLast.transitions[1]);
        check(!validatePuzzleCampaign(defaultNotLast), "require default rule to follow all score rules");
        auto missingDefault = campaign;
        missingDefault.transitions.erase(missingDefault.transitions.begin() + 2);
        check(!validatePuzzleCampaign(missingDefault), "require exactly one default per level");
        auto cycle = campaign;
        cycle.transitions[2].targetLevelId = "intro";
        check(static_cast<bool>(validatePuzzleCampaign(cycle)), "allow reachable cycles in campaign transitions");
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto project = std::filesystem::temp_directory_path() / ("astro-puzzle-campaign-test-" + std::to_string(unique));
    std::error_code ec;
    std::filesystem::create_directories(project, ec);
    check(!ec, "create isolated temporary project");
    if (!ec)
    {
        for (const auto* id : {"intro", "bonus", "finale"})
        {
            PuzzleLevelDefinition level{id, std::string("Уровень ") + id, 3, 3, 3, 42, 100,
                {0,1,2, 1,2,0, 2,0,1}};
            check(static_cast<bool>(PuzzleLevelFile::write(project, level)), "create referenced real Puzzle level file");
        }
        const auto saved = PuzzleCampaignFile::write(project, fixture());
        const auto loaded = PuzzleCampaignFile::read(project, "story");
        check(saved && loaded && loaded.value() == fixture(), "project write/read round-trip validates referenced level files");
        check(saved && saved.value() == PuzzleCampaignFile::campaignsDirectory(project) / "story.astro-puzzle-campaign",
            "campaign writer stores only under project Puzzle/Campaigns with stable ID filename");
        const auto catalogBeforeIssues = PuzzleCampaignFile::list(project);
        const astro::runtime::PuzzleCampaignCatalogEntry* storyEntry = nullptr;
        if (catalogBeforeIssues)
            for (const auto& entry : catalogBeforeIssues.value())
                if (entry.id == "story") storyEntry = &entry;
        check(storyEntry && storyEntry->readable() && storyEntry->referencesValid()
            && storyEntry->levelReferences.size() == fixture().orderedLevelIds.size(),
            "campaign catalog lists a valid campaign and confirms each referenced Puzzle level");

        astro::runtime::PuzzleCampaignDefinition missingReference{
            "missing-link", "Missing reference", "missing", {"missing"},
            {{"missing", PuzzleCampaignRuleKind::Default, 0, std::nullopt}}};
        const auto missingDocument = serializePuzzleCampaign(missingReference);
        std::filesystem::create_directories(PuzzleCampaignFile::campaignsDirectory(project), ec);
        const auto missingPath = PuzzleCampaignFile::campaignsDirectory(project) / "missing-link.astro-puzzle-campaign";
        const auto malformedPath = PuzzleCampaignFile::campaignsDirectory(project) / "bad-parse.astro-puzzle-campaign";
        if (missingDocument)
        {
            std::ofstream output(missingPath, std::ios::binary);
            output << missingDocument.value();
        }
        { std::ofstream output(malformedPath, std::ios::binary); output << "not a campaign\n"; }
        const auto catalogWithIssues = PuzzleCampaignFile::list(project);
        const astro::runtime::PuzzleCampaignCatalogEntry* missingEntry = nullptr;
        const astro::runtime::PuzzleCampaignCatalogEntry* malformedEntry = nullptr;
        if (catalogWithIssues)
            for (const auto& entry : catalogWithIssues.value())
            {
                if (entry.id == "missing-link") missingEntry = &entry;
                if (entry.id == "bad-parse") malformedEntry = &entry;
            }
        check(missingDocument && !ec && missingEntry && missingEntry->readable()
            && !missingEntry->referencesValid() && missingEntry->levelReferences.size() == 1
            && missingEntry->levelReferences.front().levelId == "missing"
            && !missingEntry->levelReferences.front().available
            && !missingEntry->levelReferences.front().diagnostic.empty(),
            "campaign catalog preserves parseable campaign data and diagnoses an unavailable level reference");
        check(malformedEntry && !malformedEntry->readable() && !malformedEntry->diagnostic.empty(),
            "campaign catalog keeps malformed campaign files visible with a parse diagnostic");
        std::filesystem::remove(missingPath, ec);
        std::filesystem::remove(malformedPath, ec);

        auto missing = fixture();
        missing.orderedLevelIds.push_back("missing");
        missing.transitions.push_back({"missing", PuzzleCampaignRuleKind::Default, 0, std::nullopt});
        check(!PuzzleCampaignFile::write(project, missing), "refuse writing a campaign with a missing referenced level asset");
        check(!PuzzleCampaignFile::read(project, "../outside"), "reject traversal in requested campaign ID");

        if (saved)
        {
            { std::ofstream output(saved.value(), std::ios::binary | std::ios::trunc);
                output << replaceOnce(encoded.value(), "ID \"story\"", "ID \"other-story\""); }
            check(!PuzzleCampaignFile::read(project, "story"), "reject campaign internal ID that does not match its filename");
            const auto restored = PuzzleCampaignFile::write(project, fixture());
            const auto reread = PuzzleCampaignFile::read(project, "story");
            check(restored && reread && reread.value() == fixture(), "atomically replace malformed campaign with a valid document");
        }

        const std::vector<std::uint8_t> playableBoard{0,1,0, 2,0,1, 2,1,2};
        std::vector<PuzzleLevelDefinition> runtimeLevels;
        for (const auto* id : {"play-intro", "play-bonus", "play-finale"})
        {
            PuzzleLevelDefinition level{id, std::string("Runtime ") + id, 3, 3, 3, 42, 10, playableBoard};
            runtimeLevels.push_back(level);
            check(static_cast<bool>(PuzzleLevelFile::write(project, level)), "write playable level for campaign runtime test");
        }
        PuzzleCampaignDefinition runtimeCampaign{"runtime-story", "Runtime story", "play-intro",
            {"play-intro", "play-bonus", "play-finale"}, {
                {"play-intro", PuzzleCampaignRuleKind::ScoreAtLeast, 30, "play-bonus"},
                {"play-intro", PuzzleCampaignRuleKind::ScoreAtLeast, 10, "play-finale"},
                {"play-intro", PuzzleCampaignRuleKind::Default, 0, "play-bonus"},
                {"play-bonus", PuzzleCampaignRuleKind::Default, 0, "play-finale"},
                {"play-finale", PuzzleCampaignRuleKind::Default, 0, std::nullopt}}};
        check(static_cast<bool>(PuzzleCampaignFile::write(project, runtimeCampaign)), "write runtime campaign fixture");

        auto started = PuzzleCampaignSession::start(project, "runtime-story");
        check(started && started.value().state() == PuzzleCampaignSessionState::Running
            && started.value().currentLevel().id == "play-intro" && started.value().game().score() == 0,
            "starting a campaign loads START into an independent PuzzleGame");
        if (started)
        {
            auto session = std::move(started).value();
            const auto earlyCompletion = session.completeCurrentLevel();
            check(!earlyCompletion && earlyCompletion.error().code == astro::core::ErrorCode::InvalidState,
                "reject campaign completion before the authored Puzzle target is reached");
            const auto firstMove = session.swap({1, 0}, {1, 1});
            check(firstMove && firstMove.value().accepted && firstMove.value().levelCompleted,
                "session forwards playable Puzzle moves and observes authored level completion");
            const auto introScore = session.game().score();

            const auto bonusPath = PuzzleLevelFile::levelsDirectory(project) / "play-bonus.astro-puzzle-level";
            const bool removedBonus = std::filesystem::remove(bonusPath, ec);
            check(removedBonus && !ec, "temporarily remove only the test-owned next-level fixture");
            const auto failedLoad = session.completeCurrentLevel();
            check(!failedLoad && session.state() == PuzzleCampaignSessionState::Running
                && session.currentLevel().id == "play-intro" && session.game().score() == introScore,
                "failed next-level load preserves the completed current game for retry");
            check(static_cast<bool>(PuzzleLevelFile::write(project, runtimeLevels[1])),
                "restore test-owned next-level fixture after the load-failure check");

            const auto loadedBonus = session.completeCurrentLevel();
            check(loadedBonus && loadedBonus.value() == std::optional<std::string>("play-bonus")
                && session.currentLevel().id == "play-bonus" && session.game().score() == 0,
                "completion resolves the real score threshold and loads the selected next level");
            const auto secondMove = session.swap({1, 0}, {1, 1});
            const auto defaultToFinale = session.completeCurrentLevel();
            check(secondMove && secondMove.value().levelCompleted && defaultToFinale
                && defaultToFinale.value() == std::optional<std::string>("play-finale")
                && session.currentLevel().id == "play-finale",
                "runtime session follows DEFAULT and initializes the next authored board");
            const auto finalMove = session.swap({1, 0}, {1, 1});
            const auto ended = session.completeCurrentLevel();
            check(finalMove && finalMove.value().levelCompleted && ended && !ended.value()
                && session.state() == PuzzleCampaignSessionState::Ended
                && session.currentLevel().id == "play-finale" && session.game().score() >= 10,
                "END completes the campaign and retains the final level and score for results");
            check(!session.swap({1, 0}, {1, 1}) && !session.completeCurrentLevel(),
                "ended campaign rejects further gameplay and completion requests");
            const auto restarted = PuzzleCampaignSession::start(project, "runtime-story");
            check(restarted && restarted.value().state() == PuzzleCampaignSessionState::Running
                && restarted.value().currentLevel().id == "play-intro",
                "an explicit new session restarts the campaign from START after END");
        }
    }
    std::filesystem::remove_all(project, ec);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
