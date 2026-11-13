#include "astro/runtime/AnimationTrack.hpp"

#include <algorithm>
#include <cmath>

namespace astro::runtime
{
core::Result<AnimationTrack> AnimationTrack::create(std::vector<AnimationKeyframe> keys)
{
    if (keys.empty()) return core::Result<AnimationTrack>::failure({core::ErrorCode::InvalidArgument, "Animation requires at least one keyframe"});
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        if (!std::isfinite(keys[index].timeSeconds) || !std::isfinite(keys[index].value)
            || keys[index].timeSeconds < 0.0 || (index && keys[index - 1].timeSeconds >= keys[index].timeSeconds))
            return core::Result<AnimationTrack>::failure({core::ErrorCode::ValidationError, "Animation keyframes must be finite, non-negative and strictly time-ordered"});
    }
    return core::Result<AnimationTrack>::success(AnimationTrack(std::move(keys)));
}

double AnimationTrack::sample(const double timeSeconds) const noexcept
{
    if (!std::isfinite(timeSeconds)) return keys_.front().value;
    if (timeSeconds <= keys_.front().timeSeconds) return keys_.front().value;
    if (timeSeconds >= keys_.back().timeSeconds) return keys_.back().value;
    const auto next = std::upper_bound(keys_.begin(), keys_.end(), timeSeconds,
        [](const double time, const AnimationKeyframe& key) { return time < key.timeSeconds; });
    const auto& previous = *(next - 1);
    const auto alpha = (timeSeconds - previous.timeSeconds) / (next->timeSeconds - previous.timeSeconds);
    return previous.value + (next->value - previous.value) * alpha;
}
}
