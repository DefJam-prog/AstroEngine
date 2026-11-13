#pragma once

#include "astro/runtime/AnimationTrack.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace astro::runtime
{
// Editable scalar clip with deterministic linear sampling and simple playback.
class AnimationTimeline final
{
public:
    [[nodiscard]] static core::Result<AnimationTimeline> create(std::vector<AnimationKeyframe> keyframes);
    [[nodiscard]] const std::vector<AnimationKeyframe>& keyframes() const noexcept { return keyframes_; }
    [[nodiscard]] double startTime() const noexcept { return keyframes_.front().timeSeconds; }
    [[nodiscard]] double endTime() const noexcept { return keyframes_.back().timeSeconds; }
    [[nodiscard]] double playhead() const noexcept { return playhead_; }
    [[nodiscard]] bool isPlaying() const noexcept { return playing_; }
    [[nodiscard]] double sample() const noexcept { return sampleAt(playhead_); }
    [[nodiscard]] double sampleAt(double timeSeconds) const noexcept { return track_.sample(timeSeconds); }

    [[nodiscard]] core::Result<void> addKeyframe(AnimationKeyframe keyframe);
    [[nodiscard]] core::Result<void> updateKeyframe(std::size_t index, AnimationKeyframe keyframe);
    [[nodiscard]] core::Result<void> removeKeyframe(std::size_t index);
    [[nodiscard]] core::Result<void> seek(double timeSeconds);
    void play() noexcept;
    void pause() noexcept { playing_ = false; }
    void reset() noexcept;
    [[nodiscard]] core::Result<void> advance(double deltaSeconds);

private:
    AnimationTimeline(std::vector<AnimationKeyframe> keyframes, AnimationTrack track);
    [[nodiscard]] core::Result<void> replace(std::vector<AnimationKeyframe> keyframes);

    std::vector<AnimationKeyframe> keyframes_;
    AnimationTrack track_;
    double playhead_ = 0.0;
    bool playing_ = false;
};
}
