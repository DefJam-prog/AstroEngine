#include "astro/runtime/PuzzleCampaignSession.hpp"

#include "astro/runtime/PuzzleLevel.hpp"

#include <algorithm>
#include <utility>

namespace astro::runtime
{
namespace
{
core::Error sessionError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}
}

PuzzleCampaignSession::PuzzleCampaignSession(std::filesystem::path projectRoot,
    PuzzleCampaignDefinition campaign, PuzzleLevelDefinition currentLevel, PuzzleGame game)
    : projectRoot_(std::move(projectRoot)), campaign_(std::move(campaign)),
      currentLevel_(std::move(currentLevel)), game_(std::move(game))
{}

core::Result<PuzzleCampaignSession> PuzzleCampaignSession::start(
    const std::filesystem::path& projectRoot, const std::string& campaignId)
{
    std::error_code ec;
    auto absoluteRoot = std::filesystem::absolute(projectRoot, ec);
    if (ec)
        return core::Result<PuzzleCampaignSession>::failure(sessionError(core::ErrorCode::IoError,
            "Cannot resolve Puzzle campaign project directory: " + ec.message()));
    absoluteRoot = absoluteRoot.lexically_normal();

    auto campaign = PuzzleCampaignFile::read(absoluteRoot, campaignId);
    if (!campaign) return core::Result<PuzzleCampaignSession>::failure(campaign.error());
    auto firstLevel = PuzzleLevelFile::read(absoluteRoot, campaign.value().startLevelId);
    if (!firstLevel) return core::Result<PuzzleCampaignSession>::failure(firstLevel.error());
    auto firstGame = PuzzleGame::create(firstLevel.value());
    if (!firstGame) return core::Result<PuzzleCampaignSession>::failure(firstGame.error());

    return core::Result<PuzzleCampaignSession>::success(PuzzleCampaignSession(absoluteRoot,
        std::move(campaign).value(), std::move(firstLevel).value(), std::move(firstGame).value()));
}

core::Result<PuzzleCampaignSession> PuzzleCampaignSession::resume(
    const std::filesystem::path& projectRoot, const std::string& campaignId,
    const std::string& activeLevelId, const PuzzleSnapshot& snapshot, const bool campaignEnded)
{
    std::error_code ec;
    auto absoluteRoot = std::filesystem::absolute(projectRoot, ec);
    if (ec)
        return core::Result<PuzzleCampaignSession>::failure(sessionError(core::ErrorCode::IoError,
            "Cannot resolve Puzzle campaign project directory: " + ec.message()));
    absoluteRoot = absoluteRoot.lexically_normal();

    auto campaign = PuzzleCampaignFile::read(absoluteRoot, campaignId);
    if (!campaign) return core::Result<PuzzleCampaignSession>::failure(campaign.error());
    if (std::find(campaign.value().orderedLevelIds.begin(), campaign.value().orderedLevelIds.end(), activeLevelId)
        == campaign.value().orderedLevelIds.end())
        return core::Result<PuzzleCampaignSession>::failure(sessionError(core::ErrorCode::VersionMismatch,
            "Saved Puzzle level is not part of the requested campaign"));

    auto level = PuzzleLevelFile::read(absoluteRoot, activeLevelId);
    if (!level) return core::Result<PuzzleCampaignSession>::failure(level.error());
    auto authoredGame = PuzzleGame::create(level.value());
    if (!authoredGame) return core::Result<PuzzleCampaignSession>::failure(authoredGame.error());
    const auto authoredRules = authoredGame.value().snapshot();
    if (snapshot.width != authoredRules.width || snapshot.height != authoredRules.height
        || snapshot.tileKinds != authoredRules.tileKinds || snapshot.levelTarget != authoredRules.levelTarget)
        return core::Result<PuzzleCampaignSession>::failure(sessionError(core::ErrorCode::VersionMismatch,
            "Saved Puzzle rules do not match the authored campaign level"));
    if (snapshot.score < level.value().targetScore)
    {
        if (campaignEnded)
            return core::Result<PuzzleCampaignSession>::failure(sessionError(core::ErrorCode::ValidationError,
                "Saved campaign is marked ended before its active level target was reached"));
    }

    auto restored = PuzzleGame::restore(snapshot);
    if (!restored) return core::Result<PuzzleCampaignSession>::failure(restored.error());
    if (campaignEnded)
    {
        const auto next = resolvePuzzleCampaignTransition(campaign.value(), activeLevelId, snapshot.score);
        if (!next) return core::Result<PuzzleCampaignSession>::failure(next.error());
        if (next.value())
            return core::Result<PuzzleCampaignSession>::failure(sessionError(core::ErrorCode::VersionMismatch,
                "Saved campaign END state no longer matches the authored transition rules"));
    }

    PuzzleCampaignSession session(absoluteRoot, std::move(campaign).value(), std::move(level).value(),
        std::move(restored).value());
    session.state_ = campaignEnded ? PuzzleCampaignSessionState::Ended : PuzzleCampaignSessionState::Running;
    session.currentLevelComplete_ = !campaignEnded && snapshot.score >= session.currentLevel_.targetScore;
    return core::Result<PuzzleCampaignSession>::success(std::move(session));
}

core::Result<PuzzleMoveResult> PuzzleCampaignSession::swap(const GridPosition first, const GridPosition second)
{
    if (state_ != PuzzleCampaignSessionState::Running)
        return core::Result<PuzzleMoveResult>::failure(sessionError(core::ErrorCode::InvalidState,
            "Puzzle campaign has ended"));
    if (currentLevelComplete_)
        return core::Result<PuzzleMoveResult>::failure(sessionError(core::ErrorCode::InvalidState,
            "Complete the current Puzzle campaign level before making another move"));

    auto move = game_.swap(first, second);
    if (move && move.value().levelCompleted) currentLevelComplete_ = true;
    return move;
}

core::Result<std::optional<std::string>> PuzzleCampaignSession::completeCurrentLevel()
{
    if (state_ != PuzzleCampaignSessionState::Running || !currentLevelComplete_)
        return core::Result<std::optional<std::string>>::failure(sessionError(core::ErrorCode::InvalidState,
            "Puzzle campaign level completion has not been reached or the campaign has ended"));

    auto selected = resolvePuzzleCampaignTransition(campaign_, currentLevel_.id, game_.score());
    if (!selected) return core::Result<std::optional<std::string>>::failure(selected.error());
    if (!selected.value())
    {
        state_ = PuzzleCampaignSessionState::Ended;
        currentLevelComplete_ = false;
        return core::Result<std::optional<std::string>>::success(std::nullopt);
    }

    // Load and initialize into temporaries. Keep the current playable result intact if I/O or validation fails.
    auto nextLevel = PuzzleLevelFile::read(projectRoot_, *selected.value());
    if (!nextLevel) return core::Result<std::optional<std::string>>::failure(nextLevel.error());
    auto nextGame = PuzzleGame::create(nextLevel.value());
    if (!nextGame) return core::Result<std::optional<std::string>>::failure(nextGame.error());

    const auto nextLevelId = nextLevel.value().id;
    currentLevel_ = std::move(nextLevel).value();
    game_ = std::move(nextGame).value();
    currentLevelComplete_ = false;
    return core::Result<std::optional<std::string>>::success(nextLevelId);
}
}
