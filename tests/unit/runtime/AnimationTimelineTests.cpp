#include "astro/runtime/AnimationTimeline.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
template<typename T>
void check(const T& condition, const char* message)
{
    if (!static_cast<bool>(condition)) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::runtime;
    auto created = AnimationTimeline::create({{0.0, 0.0}, {1.0, 10.0}, {2.0, 0.0}});
    check(created, "create a validated scalar timeline");
    if (!created) return EXIT_FAILURE;
    auto timeline = std::move(created).value();
    check(timeline.sample() == 0.0 && timeline.startTime() == 0.0 && timeline.endTime() == 2.0,
        "initialize the playhead at the first key");
    check(timeline.seek(0.5) && timeline.sample() == 5.0, "seek and linearly sample between keys");
    check(timeline.seek(-10.0) && timeline.playhead() == 0.0, "clamp seek to the first key");
    check(!timeline.seek(std::numeric_limits<double>::infinity()),
        "reject a non-finite playhead");

    check(timeline.addKeyframe({1.5, 5.0}), "add and order a keyframe");
    check(timeline.keyframes().size() == 4 && timeline.keyframes()[2].timeSeconds == 1.5,
        "maintain chronological key order");
    const auto beforeInvalid = timeline.keyframes();
    check(!timeline.addKeyframe({1.5, 99.0}), "reject duplicate key times");
    check(!timeline.addKeyframe({std::numeric_limits<double>::infinity(), 1.0}), "reject invalid time before ordering keys");
    check(!timeline.addKeyframe({3.0, 1'000'000'001.0}), "reject key values outside supported range");
    check(timeline.keyframes().size() == beforeInvalid.size()
        && timeline.keyframes()[2].value == beforeInvalid[2].value, "preserve track on rejected key insertion");
    check(!timeline.updateKeyframe(0, {1.5, 2.0}), "reject update that duplicates an adjacent key");
    check(timeline.updateKeyframe(1, {0.8, 8.0}), "update a key without breaking order");
    check(!timeline.updateKeyframe(99, {3.0, 1.0}), "reject missing key index");
    check(timeline.removeKeyframe(2) && timeline.keyframes().size() == 3, "remove a keyframe");

    check(timeline.seek(0.0), "reset playhead before playback");
    timeline.play();
    check(timeline.isPlaying(), "start playback");
    check(timeline.advance(0.5) && timeline.playhead() == 0.5, "advance playback by elapsed time");
    check(!timeline.advance(-1.0), "reject negative playback delta");
    check(timeline.advance(100.0) && !timeline.isPlaying() && timeline.playhead() == timeline.endTime(),
        "stop playback at the final key");
    timeline.play();
    check(timeline.playhead() == timeline.startTime(), "replay from the beginning after reaching the end");
    timeline.pause();
    const auto pausedAt = timeline.playhead();
    check(timeline.advance(1.0) && timeline.playhead() == pausedAt, "do not advance while paused");
    check(timeline.removeKeyframe(1) && timeline.removeKeyframe(1), "reduce the track to one keyframe");
    check(!timeline.removeKeyframe(0), "retain at least one keyframe");
    check(!AnimationTimeline::create({}), "reject an empty timeline");
    check(!AnimationTimeline::create(std::vector<AnimationKeyframe>(4097, {0.0, 0.0})),
        "reject timelines beyond the keyframe count limit");
    check(failures == 0, "all animation timeline tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
