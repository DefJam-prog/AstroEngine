# Minimal 2D Play and rendering foundation

This prototype is a prerequisite experiment for later 2D game templates. It is
not a template library and does not complete stage 06.5.

## Runtime canvas

`astro_runtime_render2d` provides backend-neutral `Canvas2D` rectangle and
sprite commands with stable layer ordering. `Camera2D` projects world
coordinates into a pixel viewport with center, pixels-per-unit, and bounded
zoom. Inputs are validated for finite values, positive dimensions, viewport
limits, and command-count limits. `astro_scene_render2d` converts visible root
`SpriteRenderer` components into sprite commands using dimensions from the
project's `AssetRegistry`; XY position/scale and Z rotation are applied. Child
sprites are rejected until hierarchy transforms are composed.

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
project; floor/platform geometry remains colored rectangles. Move the sprite
with **WASD** or **arrow keys**. Press
**P** to pause/resume, **R** to restart from the authoring Scene, and **Esc** to
close. The preview Scene is code-defined and uses colored geometry.

To configure it on Windows, use Visual Studio 2022 Developer Command Prompt
x64 and the prepared local dependency cache at `build/spike-deps`. Current CMake
reuses the `stack_spike` bootstrap, so enabling this sample also checks for
bgfx/bx/bimg and Dear ImGui even though the 2D preview itself links SDL3 and
WIC only:

```powershell
cmake -S . -B build/2d-playground -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON -DASTRO_BUILD_2D_PLAYGROUND=ON
cmake --build build/2d-playground --target astro_2d_playground --parallel 4
ctest --test-dir build/2d-playground -R 'runtime\.(render2d\.unit|2d\.playground\.smoke)' --output-on-failure
```

The executable is `build/2d-playground/samples/2d_playground/astro_2d_playground.exe`.

## Verification and boundaries

`runtime.render2d.unit` covers layer-independent projection, camera/viewport
validation, invalid rectangles, the command limit, and clearing the frame.
`runtime.2d.playground.smoke` creates a hidden SDL software-renderer window,
injects one movement action, checks fixed-step movement, pause and single-step,
checks that the authoring Scene was not changed, and reads back a rendered
pixel to verify the decoded sprite color. `runtime.scene.render2d.integration`
checks asset dimensions, transform mapping, visibility, missing references,
and rejection of uncomposed child transforms.

This path has no editor-integrated Play controls, serialized level files,
collision or character controller, GPU batching, or cross-platform image
decoder. It currently renders root sprites only and does not compose hierarchy
transforms. The SDL renderer is an adapter prototype; it does not finalize the
engine-wide graphics backend. These gaps still block the full Puzzle,
Platformer, and Top-Down templates described in
[GAME_TEMPLATES.md](GAME_TEMPLATES.md).
