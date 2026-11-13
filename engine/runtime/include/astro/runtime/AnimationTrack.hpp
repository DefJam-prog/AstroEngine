#pragma once

#include "astro/core/Result.hpp"

#include <vector>
#include <utility>

namespace astro::runtime
{
struct AnimationKeyframe
{
    double timeSeconds = 0.0;
    double value = 0.0;
};

// Samples a scalar track with linear interpolation and endpoint clamping.
class AnimationTrack final
{
public:
    static core::Result<AnimationTrack> create(std::vector<AnimationKeyframe> keys);
    [[nodiscard]] double duration() const noexcept { return keys_.back().timeSeconds; }
    [[nodiscard]] double sample(double timeSeconds) const noexcept;
private:
    explicit AnimationTrack(std::vector<AnimationKeyframe> keys) : keys_(std::move(keys)) {}
    std::vector<AnimationKeyframe> keys_;
};
}
