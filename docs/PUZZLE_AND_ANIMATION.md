# Puzzle rules, animation sampling, and audio cues (05.3)

## Match-three runtime model

`astro_runtime_puzzle` is backend-neutral and does not depend on SDL or a renderer. It is built when tests are enabled or with `-DASTRO_BUILD_PUZZLE_RUNTIME=ON`; otherwise this optional module is excluded. `PuzzleGame::create(width, height, seed, tileKinds, levelTarget)` creates a deterministic grid with dimensions of at least 3×3 and no more than 4096 cells. A move swaps orthogonally adjacent tiles. Swaps without a run of three or more equal tiles are rejected and rolled back. Valid moves clear all horizontal/vertical runs, award 10 points per cleared tile multiplied by cascade number, drop remaining tiles, refill with seeded tile IDs, and repeat until stable. `PuzzleMoveResult` reports whether the move was accepted, total cleared tiles, points, cascade count, and whether one or more score thresholds were crossed.

```cpp
auto game = astro::runtime::PuzzleGame::create(8, 8, 9123, 5, 300);
if (game) {
    auto move = game.value().swap({2, 3}, {3, 3});
    if (move && move.value().accepted)
        std::cout << "Score: " << game.value().score() << '\n';
}
```

The seed supports reproducible tests and replay setup; runtime state is not serialized, and no editor, level file, touch/mouse board, objective types, limited move count, special tiles, dead-board reshuffle, or visual puzzle board is implemented. This is an initial rules model, not a complete end-user puzzle game.

## Scalar animation tracks

`AnimationTrack::create` validates finite, strictly increasing keyframe times. `sample(seconds)` performs linear interpolation and clamps before/after the track; a non-finite sample time returns the first keyframe. It is a deterministic sampling primitive; there is no scene-property binding, easing library, event tracks, playback controller, serialization, or renderer integration yet.

## SDL audio cues

The optional `astro_runtime_sdl_audio` target uses the SDL3 audio stream API to queue short synthesized sine cues (`Select`, `Match`, `LevelUp`) on the default playback device. The Game UI demo tries to open audio outside smoke mode and reports a warning if unavailable; game UI remains usable without it. This is not asset playback, mixing, spatial sound, volume settings, or an audio authoring system. Default/headless builds do not require SDL3.

Optional build and tests:

```powershell
cmake -S . -B build/stack-spike -DASTRO_BUILD_GAME_UI_DEMO=ON -DBUILD_TESTING=ON
cmake --build build/stack-spike --parallel 4
ctest --test-dir build/stack-spike --output-on-failure
```

`runtime.puzzle.unit` tests deterministic board generation, input validation, swap resolution, refill, score and level progress, and animation interpolation. `runtime.audio.sdl.smoke` opens SDL's dummy audio output and queues an actual cue. It verifies API/device-stream flow, not audibility on physical speakers.
