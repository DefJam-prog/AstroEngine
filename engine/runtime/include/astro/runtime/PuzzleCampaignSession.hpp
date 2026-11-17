#pragma once

#include "astro/runtime/PuzzleCampaign.hpp"
#include "astro/runtime/PuzzleGame.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace astro::runtime
{
enum class PuzzleCampaignSessionState
{
    Running,
    Ended
};

// Loads authored levels into independent PuzzleGame instances as one campaign advances.
class PuzzleCampaignSession final
{
public:
    [[nodiscard]] static core::Result<PuzzleCampaignSession> start(
        const std::filesystem::path& projectRoot, const std::string& campaignId);
    [[nodiscard]] static core::Result<PuzzleCampaignSession> resume(
        const std::filesystem::path& projectRoot, const std::string& campaignId,
        const std::string& activeLevelId, const PuzzleSnapshot& snapshot, bool campaignEnded);

    [[nodiscard]] PuzzleCampaignSessionState state() const noexcept { return state_; }
    [[nodiscard]] const PuzzleCampaignDefinition& campaign() const noexcept { return campaign_; }
    [[nodiscard]] const PuzzleLevelDefinition& currentLevel() const noexcept { return currentLevel_; }
    [[nodiscard]] const PuzzleGame& game() const noexcept { return game_; }
    [[nodiscard]] bool completionPending() const noexcept { return currentLevelComplete_; }

    [[nodiscard]] core::Result<PuzzleMoveResult> swap(GridPosition first, GridPosition second);
    // Completes only after PuzzleGame reports that the authored level's target score was reached.
    // Returns the newly loaded level ID, or an empty optional after the campaign reaches END.
    [[nodiscard]] core::Result<std::optional<std::string>> completeCurrentLevel();

private:
    PuzzleCampaignSession(std::filesystem::path projectRoot, PuzzleCampaignDefinition campaign,
        PuzzleLevelDefinition currentLevel, PuzzleGame game);

    std::filesystem::path projectRoot_;
    PuzzleCampaignDefinition campaign_;
    PuzzleLevelDefinition currentLevel_;
    PuzzleGame game_;
    PuzzleCampaignSessionState state_ = PuzzleCampaignSessionState::Running;
    bool currentLevelComplete_ = false;
};
}
