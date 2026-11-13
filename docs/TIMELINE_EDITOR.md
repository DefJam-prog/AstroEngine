# Timeline Editor — 06.3

Open **View → Timeline** in the optional Windows Editor. The panel edits one in-memory scalar track, lists its time/value keyframes, draws a curve, and shows the current sampled value.

- **Play / Pause** advances the playhead using the editor frame delta and stops at the last key.
- **Reset** moves to the first key and pauses.
- **Playhead** scrubs between the first and last key.
- **Add key at end** adds a key one second after the current final key, using the current sampled value.
- Edit Time/Value fields to update a key; Remove deletes a key while preserving at least one key.

The curve uses the same `AnimationTrack::sample` implementation as runtime sampling. Times must remain finite, non-negative and strictly increasing; the editor model limits a clip to 4096 keys, with time/value magnitude up to 1,000,000,000. Invalid edits are rejected and leave the previous key data intact.

## C++ example

```cpp
auto timeline = astro::runtime::AnimationTimeline::create({{0.0, 0.0}, {1.0, 1.0}});
if (!timeline) return;
if (!timeline.value().seek(0.5)) return;
const double value = timeline.value().sample(); // 0.5
timeline.value().play();
if (!timeline.value().advance(0.25)) return;
```

`AnimationTimelineTests.cpp` covers editing, invalid keys, limits, sampling, seek and playback. The editor smoke exercises the timeline panel.

## Limits

- There is one demonstration scalar track; no track list, property binding, event/action track, easing selection, animation state machine, Scene mutation, file save/load, Undo history or export integration.
- Playback is editor preview only. Closing the editor loses timeline edits.
- Curve interaction is through the playhead slider and keyframe fields; keys cannot be dragged directly on the graph.
- This panel does not claim a complete animation authoring pipeline.

`astro_runtime_animation` is a separate optional CMake target. Enable it in a non-editor runtime build using `-DASTRO_BUILD_ANIMATION_RUNTIME=ON`. Puzzle target compatibility is retained: enabling `ASTRO_BUILD_PUZZLE_RUNTIME` also builds and links the animation target.
