#pragma once

#include "astro/core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace astro::editor
{
struct ProjectInfo
{
    std::string name;
};

struct PuzzleSaveProjectSettings final
{
    std::string profileId = "puzzle-preview";
    std::uint32_t profileVersion = 1;
    std::string userDataSubdirectory;
    friend bool operator==(const PuzzleSaveProjectSettings&, const PuzzleSaveProjectSettings&) = default;
};

astro::core::Result<ProjectInfo> readProject(const std::filesystem::path& file);
astro::core::Result<ProjectInfo> createProject(const std::filesystem::path& directory, const std::string& name);
PuzzleSaveProjectSettings defaultPuzzleSaveProjectSettings(const std::filesystem::path& projectRoot);
astro::core::Result<PuzzleSaveProjectSettings> readPuzzleSaveProjectSettings(
    const std::filesystem::path& projectRoot);
astro::core::Result<void> writePuzzleSaveProjectSettings(const std::filesystem::path& projectRoot,
    const PuzzleSaveProjectSettings& settings);
astro::core::Result<std::filesystem::path> resolvePuzzleCampaignSaveDirectory(
    const std::filesystem::path& userDataRoot, const PuzzleSaveProjectSettings& settings,
    std::string_view campaignId);
}
