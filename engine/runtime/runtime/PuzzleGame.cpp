#include "astro/runtime/PuzzleGame.hpp"

#include <algorithm>
#include <limits>

namespace astro::runtime
{
namespace
{
core::Error puzzleError(const core::ErrorCode code, const char* message)
{
    return {code, message};
}
}

PuzzleGame::PuzzleGame(const std::size_t width, const std::size_t height, const std::size_t tileKinds,
    const std::uint64_t levelTarget, const std::uint32_t seed)
    : width_(width), height_(height), tileKinds_(tileKinds), levelTarget_(levelTarget),
      tiles_(width * height), random_(seed)
{
    for (std::size_t row = 0; row < height_; ++row)
        for (std::size_t column = 0; column < width_; ++column)
        {
            auto tile = randomTile();
            for (std::size_t attempt = 0; attempt < tileKinds_; ++attempt)
            {
                const bool rowMatch = column >= 2 && tiles_[row * width_ + column - 1] == tile
                    && tiles_[row * width_ + column - 2] == tile;
                const bool columnMatch = row >= 2 && tiles_[(row - 1) * width_ + column] == tile
                    && tiles_[(row - 2) * width_ + column] == tile;
                if (!rowMatch && !columnMatch) break;
                tile = randomTile();
            }
            tiles_[row * width_ + column] = tile;
        }
}

core::Result<PuzzleGame> PuzzleGame::create(const std::size_t width, const std::size_t height,
    const std::uint32_t seed, const std::size_t tileKinds, const std::uint64_t levelTarget)
{
    constexpr std::size_t maxCells = 4096;
    if (width < 3 || height < 3 || width > maxCells / height)
        return core::Result<PuzzleGame>::failure(puzzleError(core::ErrorCode::InvalidArgument, "Puzzle dimensions must be between 3 and 4096 cells"));
    if (tileKinds < 3 || tileKinds > 32 || levelTarget == 0)
        return core::Result<PuzzleGame>::failure(puzzleError(core::ErrorCode::InvalidArgument, "Puzzle requires 3..32 tile kinds and a positive level target"));
    return core::Result<PuzzleGame>::success(PuzzleGame(width, height, tileKinds, levelTarget, seed));
}

std::uint8_t PuzzleGame::tileAt(const GridPosition position) const
{
    if (position.column >= width_ || position.row >= height_) return std::numeric_limits<std::uint8_t>::max();
    return tiles_[position.row * width_ + position.column];
}

bool PuzzleGame::adjacent(const GridPosition first, const GridPosition second) const noexcept
{
    return (first.column == second.column && (first.row + 1 == second.row || second.row + 1 == first.row))
        || (first.row == second.row && (first.column + 1 == second.column || second.column + 1 == first.column));
}

std::uint8_t PuzzleGame::randomTile()
{
    return static_cast<std::uint8_t>(random_() % tileKinds_);
}

std::vector<std::size_t> PuzzleGame::findMatches() const
{
    std::vector<std::size_t> result;
    std::vector<bool> marked(tiles_.size(), false);
    for (std::size_t row = 0; row < height_; ++row)
    {
        for (std::size_t column = 0; column < width_;)
        {
            std::size_t end = column + 1;
            while (end < width_ && tiles_[row * width_ + end] == tiles_[row * width_ + column]) ++end;
            if (end - column >= 3)
                for (auto current = column; current < end; ++current) marked[row * width_ + current] = true;
            column = end;
        }
    }
    for (std::size_t column = 0; column < width_; ++column)
    {
        for (std::size_t row = 0; row < height_;)
        {
            std::size_t end = row + 1;
            while (end < height_ && tiles_[end * width_ + column] == tiles_[row * width_ + column]) ++end;
            if (end - row >= 3)
                for (auto current = row; current < end; ++current) marked[current * width_ + column] = true;
            row = end;
        }
    }
    for (std::size_t index = 0; index < marked.size(); ++index)
        if (marked[index]) result.push_back(index);
    return result;
}

core::Result<PuzzleMoveResult> PuzzleGame::swap(const GridPosition first, const GridPosition second)
{
    if (first.column >= width_ || first.row >= height_ || second.column >= width_ || second.row >= height_)
        return core::Result<PuzzleMoveResult>::failure(puzzleError(core::ErrorCode::InvalidArgument, "Puzzle move is outside the board"));
    if (!adjacent(first, second))
        return core::Result<PuzzleMoveResult>::failure(puzzleError(core::ErrorCode::InvalidArgument, "Puzzle tiles must be adjacent"));
    std::swap(tiles_[first.row * width_ + first.column], tiles_[second.row * width_ + second.column]);
    if (findMatches().empty())
    {
        std::swap(tiles_[first.row * width_ + first.column], tiles_[second.row * width_ + second.column]);
        return core::Result<PuzzleMoveResult>::success({});
    }

    PuzzleMoveResult move{true};
    while (true)
    {
        const auto matches = findMatches();
        if (matches.empty()) break;
        ++move.cascades;
        move.clearedTiles += matches.size();
        for (const auto index : matches) tiles_[index] = std::numeric_limits<std::uint8_t>::max();
        for (std::size_t column = 0; column < width_; ++column)
        {
            std::size_t writeRow = height_;
            for (std::size_t row = height_; row-- > 0;)
            {
                const auto value = tiles_[row * width_ + column];
                if (value == std::numeric_limits<std::uint8_t>::max()) continue;
                tiles_[(--writeRow) * width_ + column] = value;
            }
            while (writeRow > 0) tiles_[(--writeRow) * width_ + column] = randomTile();
        }
    }
    const auto multiplier = static_cast<std::uint64_t>(move.cascades);
    move.pointsAwarded = static_cast<std::uint64_t>(move.clearedTiles) * 10 * multiplier;
    if (score_ > std::numeric_limits<std::uint64_t>::max() - move.pointsAwarded)
        score_ = std::numeric_limits<std::uint64_t>::max();
    else score_ += move.pointsAwarded;
    while (level_ <= score_ / levelTarget_ && level_ < std::numeric_limits<std::uint64_t>::max())
    {
        ++level_;
        move.levelCompleted = true;
    }
    return core::Result<PuzzleMoveResult>::success(move);
}
}
