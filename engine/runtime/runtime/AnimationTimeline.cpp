#include "astro/runtime/AnimationTimeline.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace astro::runtime
{
namespace
{
constexpr double maximumTimelineMagnitude = 1'000'000'000.0;

bool validTimelineValues(const std::vector<AnimationKeyframe>& keyframes)
{
    return keyframes.size() <= 4096
        && std::all_of(keyframes.begin(), keyframes.end(), [](const AnimationKeyframe& keyframe) {
            return std::isfinite(keyframe.timeSeconds) && std::isfinite(keyframe.value)
                && std::abs(keyframe.timeSeconds) <= maximumTimelineMagnitude
                && std::abs(keyframe.value) <= maximumTimelineMagnitude;
        });
}
}

AnimationTimeline::AnimationTimeline(std::vector<AnimationKeyframe> keyframes, AnimationTrack track)
    : keyframes_(std::move(keyframes)), track_(std::move(track)), playhead_(keyframes_.front().timeSeconds)
{}

core::Result<AnimationTimeline> AnimationTimeline::create(std::vector<AnimationKeyframe> keyframes)
{
    if (!validTimelineValues(keyframes))
        return core::Result<AnimationTimeline>::failure({core::ErrorCode::ValidationError,
            "Timeline requires at most 4096 finite keys within supported magnitude"});
    auto track = AnimationTrack::create(keyframes);
    if (!track) return core::Result<AnimationTimeline>::failure(track.error());
    return core::Result<AnimationTimeline>::success(AnimationTimeline(std::move(keyframes), std::move(track).value()));
}

core::Result<void> AnimationTimeline::replace(std::vector<AnimationKeyframe> keyframes)
{
    if (!validTimelineValues(keyframes))
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "Timeline requires at most 4096 finite keys within supported magnitude"});
    auto track = AnimationTrack::create(keyframes);
    if (!track) return core::Result<void>::failure(track.error());
    keyframes_ = std::move(keyframes);
    track_ = std::move(track).value();
    playhead_ = std::clamp(playhead_, startTime(), endTime());
    return core::Result<void>::success();
}

core::Result<void> AnimationTimeline::addKeyframe(const AnimationKeyframe keyframe)
{
    if (keyframes_.size() >= 4096)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Animation keyframe limit reached"});
    if (!std::isfinite(keyframe.timeSeconds) || !std::isfinite(keyframe.value)
        || keyframe.timeSeconds < 0.0 || std::abs(keyframe.timeSeconds) > maximumTimelineMagnitude
        || std::abs(keyframe.value) > maximumTimelineMagnitude)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Animation keyframe is outside supported limits"});
    auto candidate = keyframes_;
    candidate.push_back(keyframe);
    std::sort(candidate.begin(), candidate.end(), [](const auto& left, const auto& right) {
        return left.timeSeconds < right.timeSeconds;
    });
    return replace(std::move(candidate));
}

core::Result<void> AnimationTimeline::updateKeyframe(const std::size_t index, const AnimationKeyframe keyframe)
{
    if (index >= keyframes_.size())
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Animation keyframe index was not found"});
    auto candidate = keyframes_;
    candidate[index] = keyframe;
    return replace(std::move(candidate));
}

core::Result<void> AnimationTimeline::removeKeyframe(const std::size_t index)
{
    if (index >= keyframes_.size())
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Animation keyframe index was not found"});
    if (keyframes_.size() <= 1)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "Animation timeline requires at least one keyframe"});
    auto candidate = keyframes_;
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(index));
    return replace(std::move(candidate));
}

core::Result<void> AnimationTimeline::seek(const double timeSeconds)
{
    if (!std::isfinite(timeSeconds))
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument, "Timeline playhead must be finite"});
    playhead_ = std::clamp(timeSeconds, startTime(), endTime());
    return core::Result<void>::success();
}

void AnimationTimeline::play() noexcept
{
    if (playhead_ >= endTime()) playhead_ = startTime();
    playing_ = true;
}

void AnimationTimeline::reset() noexcept
{
    playing_ = false;
    playhead_ = startTime();
}

core::Result<void> AnimationTimeline::advance(const double deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument, "Timeline delta must be finite and non-negative"});
    if (!playing_) return core::Result<void>::success();
    const double remaining = endTime() - playhead_;
    if (deltaSeconds >= remaining)
    {
        playhead_ = endTime();
        playing_ = false;
    }
    else playhead_ += deltaSeconds;
    return core::Result<void>::success();
}
}
