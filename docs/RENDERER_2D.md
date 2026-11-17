# Minimal 2D Play and rendering foundation

This prototype is a prerequisite experiment for later 2D game templates. It is
not a template library and does not complete stage 06.5.

## Runtime canvas

`astro_runtime_render2d` provides backend-neutral `Canvas2D` rectangle and
sprite commands with stable layer ordering. `Camera2D` projects world
coordinates into a pixel viewport with center, pixels-per-unit, and bounded
zoom. Inputs are validated for finite values, positive dimensions, viewport
limits, and command-count limits. `astro_scene_render2d` composes the full
parent chain with XY translation, XY scale, and Z rotation, then emits the
sprite's four world-space corners. SDL's textured geometry path preserves shear
from nested rotations and non-uniform scale.

`astro_runtime_sdl_render2d` adapts these commands to SDL3. Sprite commands use
a caller-supplied `AssetId -> SDL_Texture*` resolver. The optional Windows
`SDLImageTextureCache` reads the validated PNG/JPEG cache through WIC, bounds
decoded image dimensions, and owns SDL textures while borrowing the renderer
and registry. Future platforms need a decoder adapter of their own. The
backend-neutral render commands have no SDL dependency and can be enabled with
`ASTRO_BUILD_RENDER2D=ON`.

## Play preview

The optional `astro_2d_playground` executable is a small visible example of the
path: SDL3 window and input adapter, backend-neutral input actions,
`RuntimeSession` fixed-step updates on a copied Scene, Canvas2D commands, then
SDL3 drawing. Its visible root `SpriteRenderer` displays a small generated
sample image imported through AssetRegistry into a unique temporary sample
project; floor/platform geometry remains colored rectangles. Move the sample
sprite with **WASD** or **arrow keys**. Press
**P** to pause/resume, **R** to restart from the authoring Scene, and **Esc** to
close. With no arguments the executable runs this sample. To preview an
existing Astro Scene from a project:

```powershell
& '.\build\2d-playground\samples\2d_playground\astro_2d_playground.exe' `
  --project 'D:\Games\MyProject' --scene 'Scenes\Main.scene'
```

`--scene` is a relative path resolved under the canonical project root. The
preview requires existing `Assets`, `Library/Assets`, `.astro`, and
`.astro/assets.index`; it rejects traversal and symlinks for those directories
and the registry. It loads Astro Scene v1–v4 and auto-fits visible SpriteRenderer
bounds. It reads the project scene, index, source images, and cache without
writing them; missing/stale cached images report an error instead of being
reimported. Generic project preview currently has no gameplay input binding;
the standalone sample controls its own `Player` entity.

To configure it on Windows, use Visual Studio 2022 Developer Command Prompt
x64 and the prepared local dependency cache at `build/spike-deps`. Current CMake
reuses the `stack_spike` bootstrap, so enabling this sample also checks for
bgfx/bx/bimg and Dear ImGui even though the 2D preview itself links SDL3 and
WIC only:

```powershell
cmake -S . -B build/2d-playground -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON -DASTRO_BUILD_2D_PLAYGROUND=ON
cmake --build build/2d-playground --target astro_2d_playground --parallel 4
ctest --test-dir build/2d-playground -R 'runtime\.(render2d\.unit|scene\.render2d\.integration|2d\.playground\.smoke|2d\.project\.preview\.smoke)' --output-on-failure
```

The executable is `build/2d-playground/samples/2d_playground/astro_2d_playground.exe`.

## Verification and boundaries

`runtime.render2d.unit` covers layer-independent projection, camera/viewport
validation, invalid rectangle/sprite geometry, command limits, point projection,
and clearing the frame. `runtime.scene.render2d.integration` covers nested
translation/rotation/non-uniform scale, affine corner output, visibility,
missing assets, and all-or-nothing draw-list updates.
`runtime.2d.playground.smoke` creates a hidden SDL software-renderer window,
injects one movement action, checks fixed-step movement, pause and single-step,
checks that the authoring Scene was not changed, and reads back a rendered
pixel to verify the decoded sprite color. `runtime.2d.project.preview.smoke`
creates an owned temporary project, moves it to a Unicode path, opens the
serialized Scene through the real command line, verifies a rendered PNG pixel,
hash-checks the scene/asset/index/cache for read-only behavior, and confirms
that an outside-project scene path is rejected.

The Windows editor connects its Rendered Scene View and Game View directly to
bgfx framebuffers. Scene View keeps a separate Hierarchy map tab. Game View
provides Play/Pause/Resume/Step/Stop and renders RuntimeSession's isolated Scene
copy while playing; when stopped it previews the authoring Scene. Each view
composes the same backend-neutral scene sprite commands, builds transient
position/UV/color vertex and index buffers, samples the Editor's loaded bgfx
asset textures, and displays its independent BGRA8 framebuffer attachment via
ImGui. Consecutive sprites with the same texture are submitted together;
viewports auto-fit visible sprite bounds and clamp to 1280x720. No software
surface or full-frame host upload is used in the Editor path. The SDL software
adapter remains available to standalone/runtime clients.

Run `build/stack-spike/editor/astro_editor.exe --benchmark` to compare the
current direct-GPU submission with synthetic 1/100/1000/5000/10000 sprite
scenes. The benchmark reports Scene composition, camera bounds, vertex geometry,
transient allocation/copy and bgfx submission separately. The 10k D3D11 Debug
total is ~24–26 ms CPU p50 per view after replacing quadratic Transform lookup
with indexed/cached world matrices. Optimized Release measures ~5.5–6.0 ms per
view. These measurements omit ImGui composition and vsync. Editor smoke checks
mixed textures, alpha blending, window resize and captured output with color
predicates rather than a device-specific full-frame golden. Results:
[report 06.12](reports/06.12-dense-scene-geometry.md), [report
06.11](reports/06.11-bgfx-visual-dense-release.md), [report
06.10](reports/06.10-bgfx-framebuffer-renderer.md) and [D044](DECISIONS.md#d044--render-scenegame-view-directly-into-bgfx-framebuffers).

The Release benchmark also includes three generated 2D patterns (chunked tiles,
alpha overdraw, mixed world/effects) and times the complete host-side Editor
frame. On the recorded Windows D3D11 system, observed p50/p95 is about
16.6/17.3–17.6 ms with VSync enabled; only about 1.9 ms p50 is Editor CPU work
before `bgfx::frame()`, with the frame call waiting about 14.6 ms. BGFX latency
counters were unavailable, so this paced frame time is not a pure GPU duration
or uncapped throughput estimate. See [06.13 profile](reports/06.13-representative-frame-profile.md)
and [D047](DECISIONS.md#d047--report-the-complete-vsync-limited-editor-frame-separately-from-view-cpu-cost-0613).

The Scene View currently renders SpriteRenderer components only; it does not
draw colliders, prefab handles, selection outlines, or other editor overlays.
Ordinary Game View Play also renders SpriteRenderer components and its generic
fixed-step callback remains empty. If `astro_runtime_puzzle` is built, the
additional **Play Puzzle** action runs a built-in match-three preview in Game
View: click two neighboring cells to queue a swap, which resolves during the
next fixed runtime step. Pause suppresses moves; Step applies one queued move.
This mode is isolated from the authored Scene and does not create a serialized
Puzzle component, level, or template. Its `editor.smoke` verifies board draw,
legal moves, pause/step behavior and Scene isolation. The WIC adapter is Windows-only.
There are no serialized level files, collision/character controller, GPU
general instancing/batching, X/Y-axis 3D rotations, Z-position sorting, or cross-platform image
decoder. These gaps still block the full Puzzle, Platformer, and Top-Down templates described in
[GAME_TEMPLATES.md](GAME_TEMPLATES.md).
