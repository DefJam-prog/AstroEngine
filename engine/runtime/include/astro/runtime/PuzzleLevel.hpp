#pragma once

#include "astro/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace astro::runtime
{
struct PuzzleLevelDefinition final
{
    std::string id = "level-001";
    std::string name = "Puzzle Level";
    std::size_t width = 8;
    std::size_t height = 8;
    std::size_t tileKinds = 5;
    std::uint32_t seed = 9123;
    std::uint64_t targetScore = 300;
    std::vector<std::uint8_t> initialTiles;
    friend bool operator==(const PuzzleLevelDefinition&, const PuzzleLevelDefinition&) = default;
};

[[nodiscard]] core::Result<void> validatePuzzleLevel(const PuzzleLevelDefinition& level);

struct PuzzleLevelCatalogEntry final
{
    std::string id;
    std::optional<std::string> name;
    std::string diagnostic;
    [[nodiscard]] bool readable() const noexcept { return name.has_value(); }
    friend bool operator==(const PuzzleLevelCatalogEntry&, const PuzzleLevelCatalogEntry&) = default;
};

// Project-authored files live under <project>/Puzzle/Levels and are not player saves.
class PuzzleLevelFile final
{
public:
    static core::Result<std::filesystem::path> write(const std::filesystem::path& projectRoot,
        const PuzzleLevelDefinition& level);
    // Create-only publication never replaces an existing level file.
    static core::Result<std::filesystem::path> createNew(const std::filesystem::path& projectRoot,
        const PuzzleLevelDefinition& level);
    static core::Result<PuzzleLevelDefinition> read(const std::filesystem::path& projectRoot,
        const std::string& levelId);
    static core::Result<std::vector<PuzzleLevelCatalogEntry>> list(const std::filesystem::path& projectRoot);
    static core::Result<std::filesystem::path> duplicate(const std::filesystem::path& projectRoot,
        const std::string& sourceId, std::string newId, std::string newName);
    // Rename requires a distinct, unused ID. The old file is removed only after the new file is published.
    static core::Result<std::filesystem::path> rename(const std::filesystem::path& projectRoot,
        const std::string& sourceId, std::string newId);
    [[nodiscard]] static std::filesystem::path levelsDirectory(const std::filesystem::path& projectRoot);
};
}
