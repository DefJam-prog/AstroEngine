#pragma once

#include "astro/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astro::runtime
{
enum class PuzzleCampaignRuleKind
{
    ScoreAtLeast,
    Default
};

struct PuzzleCampaignTransition final
{
    std::string sourceLevelId;
    PuzzleCampaignRuleKind kind = PuzzleCampaignRuleKind::Default;
    std::uint64_t scoreThreshold = 0;
    // No target means terminal END, which is valid only for a Default rule.
    std::optional<std::string> targetLevelId;
    friend bool operator==(const PuzzleCampaignTransition&, const PuzzleCampaignTransition&) = default;
};

struct PuzzleCampaignDefinition final
{
    std::string id;
    std::string name;
    std::string startLevelId;
    // Vector order is the stable presentation order; it is independent of transitions.
    std::vector<std::string> orderedLevelIds;
    // File order is the rule priority for a source. Score rules precede its one Default rule.
    std::vector<PuzzleCampaignTransition> transitions;
    friend bool operator==(const PuzzleCampaignDefinition&, const PuzzleCampaignDefinition&) = default;
};

struct PuzzleCampaignLevelReference final
{
    std::string levelId;
    bool available = false;
    std::string diagnostic;
    friend bool operator==(const PuzzleCampaignLevelReference&, const PuzzleCampaignLevelReference&) = default;
};

struct PuzzleCampaignCatalogEntry final
{
    std::string id;
    std::optional<PuzzleCampaignDefinition> definition;
    std::string diagnostic;
    std::vector<PuzzleCampaignLevelReference> levelReferences;
    [[nodiscard]] bool readable() const noexcept { return definition.has_value(); }
    [[nodiscard]] bool referencesValid() const noexcept;
    friend bool operator==(const PuzzleCampaignCatalogEntry&, const PuzzleCampaignCatalogEntry&) = default;
};

inline constexpr std::size_t maxPuzzleCampaignLevels = 256;
inline constexpr std::size_t maxPuzzleCampaignTransitions = 4096;
inline constexpr std::size_t maxPuzzleCampaignFileBytes = 128 * 1024;

[[nodiscard]] core::Result<void> validatePuzzleCampaign(const PuzzleCampaignDefinition& campaign);
[[nodiscard]] core::Result<PuzzleCampaignDefinition> parsePuzzleCampaign(std::string_view text);
[[nodiscard]] core::Result<std::string> serializePuzzleCampaign(const PuzzleCampaignDefinition& campaign);
// Resolves one completion request only. A successful empty optional means the selected rule is terminal END.
[[nodiscard]] core::Result<std::optional<std::string>> resolvePuzzleCampaignTransition(
    const PuzzleCampaignDefinition& campaign, std::string_view completedLevelId, std::uint64_t finalScore);

// Project content references authored Puzzle levels; no player progress is stored here.
class PuzzleCampaignFile final
{
public:
    [[nodiscard]] static std::filesystem::path campaignsDirectory(const std::filesystem::path& projectRoot);
    [[nodiscard]] static core::Result<PuzzleCampaignDefinition> read(const std::filesystem::path& projectRoot,
        const std::string& campaignId);
    [[nodiscard]] static core::Result<std::vector<PuzzleCampaignCatalogEntry>> list(
        const std::filesystem::path& projectRoot);
    [[nodiscard]] static core::Result<std::filesystem::path> write(const std::filesystem::path& projectRoot,
        const PuzzleCampaignDefinition& campaign);
};
}
