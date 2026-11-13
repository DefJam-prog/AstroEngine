#include "astro/runtime/AnimationTrack.hpp"
#include "astro/runtime/PuzzleGame.hpp"

#include <cmath>
#include <cstdlib>
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
    check(!PuzzleGame::create(2, 4, 1), "reject boards smaller than 3x3");
    check(!PuzzleGame::create(65, 64, 1), "bound board cell allocation");
    check(!PuzzleGame::create(4, 4, 1, 2), "require at least three tile kinds");

    auto first = PuzzleGame::create(8, 8, 9123, 5, 10);
    auto same = PuzzleGame::create(8, 8, 9123, 5, 10);
    check(first && same && first.value().tiles() == same.value().tiles(), "seed produces a stable initial board");
    if (!first || !same) return EXIT_FAILURE;
    for (const auto tile : first.value().tiles()) check(tile < 5, "board tile is in configured palette");
    check(first.value().tileAt({99, 99}) == UINT8_MAX, "out-of-range tile query returns invalid sentinel");

    auto unchanged = first.value().tiles();
    check(!first.value().swap({0, 0}, {4, 4}), "reject non-adjacent swap");
    check(first.value().tiles() == unchanged, "invalid swap leaves board unchanged");

    bool foundRejected = false;
    for (std::size_t row = 0; row < 8 && !foundRejected; ++row)
        for (std::size_t column = 0; column < 8 && !foundRejected; ++column)
            for (const GridPosition offset : {GridPosition{1, 0}, GridPosition{0, 1}})
            {
                const GridPosition other{column + offset.column, row + offset.row};
                if (other.column >= 8 || other.row >= 8) continue;
                auto candidate = PuzzleGame::create(8, 8, 9123, 5, 10);
                const auto board = candidate.value().tiles();
                const auto move = candidate.value().swap({column, row}, other);
                if (move && !move.value().accepted)
                {
                    foundRejected = true;
                    check(candidate.value().tiles() == board && candidate.value().score() == 0,
                        "non-matching adjacent swap is rolled back without scoring");
                }
            }
    check(foundRejected, "seeded board contains a non-matching adjacent swap");

    bool foundMatch = false;
    for (std::size_t row = 0; row < 8 && !foundMatch; ++row)
        for (std::size_t column = 0; column < 8 && !foundMatch; ++column)
            for (const GridPosition offset : {GridPosition{1, 0}, GridPosition{0, 1}})
            {
                const GridPosition other{column + offset.column, row + offset.row};
                if (other.column >= 8 || other.row >= 8) continue;
                auto candidate = PuzzleGame::create(8, 8, 9123, 5, 10);
                const auto move = candidate.value().swap({column, row}, other);
                if (move && move.value().accepted)
                {
                    foundMatch = true;
                    check(move.value().clearedTiles >= 3 && move.value().pointsAwarded >= 30,
                        "valid swap clears a line and awards points");
                    check(move.value().cascades >= 1 && move.value().levelCompleted,
                        "resolve cascades and report level completion");
                    check(candidate.value().score() == move.value().pointsAwarded
                        && candidate.value().level() >= 2, "score and level state update");
                    for (const auto tile : candidate.value().tiles()) check(tile < 5, "refill restores valid tiles");
                }
            }
    check(foundMatch, "seeded board contains a valid move");

    auto track = AnimationTrack::create({{0.0, 0.0}, {1.0, 10.0}, {2.0, 0.0}});
    check(static_cast<bool>(track), "create ordered scalar animation");
    if (track)
    {
        check(track.value().duration() == 2.0, "duration follows last keyframe");
        check(track.value().sample(-1.0) == 0.0 && track.value().sample(3.0) == 0.0,
            "sample clamps to endpoints");
        check(std::abs(track.value().sample(0.5) - 5.0) < 1e-9
            && std::abs(track.value().sample(1.5) - 5.0) < 1e-9, "sample linearly interpolates keyframes");
        check(track.value().sample(NAN) == 0.0, "non-finite sample time returns first keyframe");
    }
    check(!AnimationTrack::create({}), "reject empty animation");
    check(!AnimationTrack::create({{0.0, 1.0}, {0.0, 2.0}}), "reject duplicate keyframe times");
    check(!AnimationTrack::create({{0.0, NAN}}), "reject non-finite animation values");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
