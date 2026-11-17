#pragma once

#include "astro/core/Result.hpp"
#include "astro/runtime/PuzzleLevel.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace astro::runtime
{
struct GridPosition
{
    std::size_t column = 0;
    std::size_t row = 0;
    friend bool operator==(const GridPosition&, const GridPosition&) = default;
};

struct PuzzleMoveResult
{
    bool accepted = false;
    std::size_t clearedTiles = 0;
    std::uint64_t pointsAwarded = 0;
    std::size_t cascades = 0;
    bool levelCompleted = false;
};

struct PuzzleSnapshot final
{
    std::size_t width = 0, height = 0, tileKinds = 0;
    std::uint64_t levelTarget = 0, score = 0, level = 0;
    std::vector<std::uint8_t> tiles;
    std::string randomState;
};

// Deterministic, dependency-free match-three rules. Tile IDs are small palette indices.
class PuzzleGame final
{
public:
    static core::Result<PuzzleGame> create(std::size_t width, std::size_t height,
        std::uint32_t seed, std::size_t tileKinds = 5, std::uint64_t levelTarget = 300);
    static core::Result<PuzzleGame> create(const PuzzleLevelDefinition& definition);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint8_t tileAt(GridPosition position) const;
    [[nodiscard]] std::uint64_t score() const noexcept { return score_; }
    [[nodiscard]] std::uint64_t level() const noexcept { return level_; }
    [[nodiscard]] std::uint64_t levelTarget() const noexcept { return levelTarget_; }
    [[nodiscard]] const std::vector<std::uint8_t>& tiles() const noexcept { return tiles_; }
    core::Result<PuzzleMoveResult> swap(GridPosition first, GridPosition second);
    [[nodiscard]] PuzzleSnapshot snapshot() const;
    static core::Result<PuzzleGame> restore(const PuzzleSnapshot& snapshot);

private:
    PuzzleGame(std::size_t width, std::size_t height, std::size_t tileKinds,
        std::uint64_t levelTarget, std::uint32_t seed);
    [[nodiscard]] std::vector<std::size_t> findMatches() const;
    [[nodiscard]] bool adjacent(GridPosition first, GridPosition second) const noexcept;
    [[nodiscard]] std::uint8_t randomTile();
    std::size_t width_;
    std::size_t height_;
    std::size_t tileKinds_;
    std::uint64_t levelTarget_;
    std::uint64_t score_ = 0;
    std::uint64_t level_ = 1;
    std::vector<std::uint8_t> tiles_;
    std::mt19937 random_;
};
}
