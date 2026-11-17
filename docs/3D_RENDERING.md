# 3D camera and mesh prototype (07.1a)

This prototype establishes a small backend-neutral data contract and one optional graphics smoke. It is a foundation experiment, not a scene-rendering feature.

## Current API

Include `astro/render3d/Camera3D.hpp` for `Camera3D`, `Vec3`, `Mat4`, and `ClipDepthRange`. The camera uses a right-handed view: +Y is up and the default camera looks from +Z toward the origin along -Z. `viewMatrix()` validates finite, non-degenerate pose vectors. `projectionMatrix(aspect, depthRange)` validates FOV and near/far planes; the default clip-depth range is [0, 1], while `NegativeOneToOne` provides the OpenGL-style range. `Mat4::values` is column-major.

Include `astro/render3d/MeshData.hpp` for `MeshData::create(vertices, triangleIndices)`. The validator rejects empty/oversized buffers, incomplete triangles, non-finite positions and out-of-range indices. Vertex colors are four 8-bit channels. `MeshData::makeColoredCube()` is a deterministic 8-vertex/12-triangle sample.

Example:

```cpp
astro::render3d::Camera3D camera;
auto view = camera.viewMatrix();
auto projection = camera.projectionMatrix(16.0f / 9.0f);
auto cube = astro::render3d::MeshData::makeColoredCube();
if (!view || !projection) return; // surface the Result error in production code
```

The matrices are used by the optional bgfx sample; production renderer adapters should verify their backend clip-space convention before using another graphics API.

## Build and smoke

`astro_runtime_render3d` is independent of SDL/bgfx. With testing enabled it builds the portable camera/mesh unit test. The graphics sample currently requires the existing Windows x64/MSVC SDL3/bgfx cache and D3D11 shaders; it does not download dependencies:

With `BUILD_TESTING=OFF` and the 3D options left disabled, the ordinary headless build does not add `astro_runtime_render3d` or require bgfx. This separation was checked using a fresh WSL Ubuntu build directory.

```powershell
# From a VS 2022 x64 Developer Command Prompt
cmake -S . -B build/06.53-editor-release `
  -DASTRO_BUILD_RENDER3D=ON `
  -DASTRO_BUILD_RENDER3D_BGFX_SMOKE=ON
cmake --build build/06.53-editor-release --parallel 8
ctest --test-dir build/06.53-editor-release `
  -R "runtime.render3d.(camera_mesh.unit|bgfx.smoke)" --output-on-failure
```

The window target reads the bgfx example's `vs_cubes.bin` and `fs_cubes.bin` from `build/spike-deps` and copies them into its build directory. Its `--smoke` mode requests a bgfx backbuffer readback, checks a minimum foreground-pixel count, color diversity and an in-bounds cube region, and reports CPU frame-interval p50/p95. Three Windows D3D11 runs each found 85,155 foreground pixels, 169 color buckets and bounds 280,135–658,498; median p50 was 16.735 ms (range 16.701–16.764 ms) and median p95 was 32.824 ms (range 32.055–34.120 ms) with VSync. It does not save an image or compare exact pixels against a golden screenshot. These intervals are a single-cube smoke baseline, not full frame time or a performance budget.

## Limits

- No mesh/camera Scene components, Scene format changes, asset registry integration or Editor Scene/Game View integration.
- No normals, UVs, materials, lighting, depth picking, 3D Scene transforms or 3D physics. The bounded static glTF/GLB prototype below imports positions and triangle topology only.
- The bgfx smoke is Windows D3D11 only; no macOS/Android/iOS graphics path was built.
- The smoke checks coarse visible-pixel presence, not exact image output, graphics quality or approved performance budgets.
- The timing workload is one rotating colored cube with VSync; it is a baseline, not a representative engine frame or target budget.

## Bounded glTF/GLB geometry import (07.1b)

`astro_gltf` is a separate opt-in CMake target. Its public header is `astro/render3d/GltfImporter.hpp`:

```cpp
auto imported = astro::render3d::GltfImporter::importFile(
    projectRoot, std::filesystem::path("Assets/Models/crate.glb"));
if (!imported) {
    report(imported.error().message);
    return;
}
for (const auto& mesh : imported.value().meshes) {
    for (const auto& primitive : mesh.primitives) {
        // primitive is validated backend-neutral MeshData (positions + triangle indices)
    }
}
```

The source path must be project-relative under `Assets/` and end in `.gltf` or `.glb`. External buffer URIs must be plain relative local paths that resolve to regular files under `Assets/`; data URIs are supported by the parser for embedded buffers. Import reads but never rewrites source files. Validated outputs are cached under `Library/Derived/gltf`; cache entries use a content-derived name and checksummed schema, and are rebuilt if malformed. Limits currently include 8 MiB source JSON/container, 64 MiB per external buffer, 128 MiB total external buffers, 128 meshes, 256 primitives, 1,000,000 vertices, 3,000,000 indices, and a 64 MiB cache artifact.

Enable it explicitly after staging the pinned dependencies in the ignored `build/07.1b-deps` directory:

```powershell
cmake -S . -B build/07.1b-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON -DASTRO_BUILD_GLTF_IMPORTER=ON `
  -DASTRO_GLTF_DEPS_ROOT="$((Get-Location).Path)/build/07.1b-deps"
cmake --build build/07.1b-release --parallel 8
ctest --test-dir build/07.1b-release -R "runtime.render3d.gltf.import.integration" --output-on-failure
```

The implementation supports static triangle primitives with a float `POSITION` VEC3 accessor and optional unsigned triangle indices. Non-indexed triangles are converted to indexed `MeshData`. It rejects sparse accessors, unsupported vertex attributes, morph targets, Draco, unsupported required extensions, invalid indices and out-of-budget data. Names and mesh grouping are retained. Normals, UVs, tangents, colors, materials, textures/images, node transforms/hierarchy, animation, skinning, morphs, physics and Scene/AssetRegistry/Editor integration are not provided. This is a prototype, not general glTF compatibility.

The filesystem checks reject traversal and links/reparse points, but use path inspection followed by ordinary file opens; they do not prevent TOCTOU attacks from a hostile process concurrently replacing path components. The parser is not claimed vulnerability-free. Fuzzing/sanitizers and macOS/mobile builds have not been run. See [07.1b verification report](reports/07.1b-bounded-gltf-importer.md).

## Scene model asset reference (07.1c)

`astro::scene::MeshRendererComponent` stores a stable model `AssetId`, mesh index, primitive index and visibility. `entity.set_mesh_renderer` is a typed transactional Scene command with candidate validation and stable component identity across undo/redo. Scene v6 serializes the component; the loader still accepts v1–v5, where the component is absent. `GltfImporter::importAndRegister` first runs the bounded importer and only then registers the source in Asset index v3. The registry verifies the main model source size/hash and retains the import content digest. External glTF buffers are revalidated when this importer method runs; they are not independently represented in the registry dependency graph.

This slice does not draw MeshRenderer in Editor/Game View, add geometry to prefab serialization (prefab capture rejects the component), or import node transforms, normals, UVs, materials, textures or animation. Clean Windows x64 Release build completed 158/158 actions; focused integration passed 5/5, full CTest passed 42/42 in 7.94 s, and the default importer-off headless Release build completed 32/32 actions. See [07.1c report](reports/07.1c-scene-model-asset-reference.md).

## Scene MeshRenderer resolution (07.1d)

`astro/render3d/SceneMeshResolver.hpp` provides `resolveMeshRenderer(const AssetRegistry&, const MeshRendererComponent&)`. It validates component schema/AssetId, resolves the registered top-level source through AssetRegistry, asks the bounded importer for current geometry, compares the current importer content digest with the registered digest, and checks both `meshIndex` and `primitiveIndex`. On success it returns the selected `MeshData`, mesh name, stable model ID, selectors and visibility as a backend-neutral value. Invalid IDs/selectors fail with a diagnostic; changed source or external-buffer content fails as stale until explicit import and registration. Geometry is moved out of the import result to avoid an additional vertex/index buffer copy.

This resolves data for a future renderer but does not submit draw calls or connect to Editor/Game View. It revalidates import inputs on each call and adds no independent per-buffer dependency graph. Release verification is in [TESTING.md](TESTING.md#071d-scene-meshrenderer-resolution) and the [07.1d report](reports/07.1d-scene-mesh-resolution.md).

## Standalone Scene Mesh BGFX preview (07.1e)

The optional Windows x64 `astro_render3d_bgfx_smoke` sample has a `--scene-smoke --project-root <path>` mode when both `ASTRO_BUILD_RENDER3D_BGFX_SMOKE` and `ASTRO_BUILD_GLTF_IMPORTER` are enabled. It opens a test-owned project with a serialized Scene v6 `MeshRenderer`, explicitly imports/registers the `.gltf`, resolves the component through `resolveMeshRenderer`, uploads the selected primitive to a BGFX vertex/index buffer and submits a D3D11 draw. The CTest wrapper copies checked-in fixtures into a unique temporary directory, verifies the reported selected AssetId/indices and pixel readback, then removes only that temporary directory. The ordinary procedural cube smoke remains available with importer OFF.

This preview is a narrow graphics integration test, not an Editor/Game View or runtime renderer. It draws one visible primitive at the origin and does not apply the Scene Transform. The smoke adapter caps preview geometry at 65,536 vertices and 196,608 triangle indices to use 16-bit BGFX indices. The fixture has constant vertex color, so its pixel check expects visible foreground coverage and bounds rather than multiple color buckets or an exact golden image. See [sample instructions](../samples/3d_prototype/README.md), [CTest instructions](TESTING.md#071e-standalone-scene-mesh-bgfx-smoke), and the [07.1e report](reports/07.1e-scene-bgfx-preview.md).

## Editor Scene View Mesh preview (07.1f)

When `astro_gltf` is built, Editor's Scene View adds a BGFX 3D pass after its existing 2D Scene pass. Visible `MeshRenderer` components resolve through the project AssetRegistry/importer and draw their selected primitive into the same color target, with a depth attachment. Game View has no 3D integration in this step. Transform composition traverses parent links from root to entity and applies local position, Euler rotation and scale; invalid or cyclic/missing hierarchy data skips that mesh and displays a Scene View diagnostic. BGFX vertex/index buffers are retained per entity while its model/mesh/primitive selection stays stable; removed/invisible entities release their buffers. `Reload 3D meshes` releases the cache so the importer is run again.

The preview uses an editor orbit camera with a 60-degree vertical field of view. Right-drag orbits, middle-drag pans, the wheel zooms, and **Frame All** fits the world bounds of visible mesh geometry; mouse input is read only while over the rendered viewport. Bounds are built from transformed local-AABB corners, including hierarchy transforms. Camera input is finite-checked and bounded. Geometry still uses position and vertex-color attributes only, no back-face culling, and a 65,536 vertex/196,608 index cap for 16-bit indices. Normals, UVs, textures/materials, lighting, 3D gizmos, picking, automatic file watching, and Game View/runtime integration are unsupported. Reimport/register modified glTF inputs in the project and use Reload to refresh cached geometry. Unit and Editor smoke tests validate camera operations, rejected bounds, draw submission, hierarchy origin and Frame All application; there is no viewport pixel-golden test. Windows x64 D3D11 only; see [TESTING.md](TESTING.md#071g-scene-view-camera-and-framing), the [07.1g report](reports/07.1g-editor-scene-camera.md), and the [07.1f integration report](reports/07.1f-editor-scene-mesh-preview.md).

The 07.1h Editor smoke uses two valid mesh instances and one unresolved AssetId. It verifies two draws on the final frame, preserves rendering while reporting the missing reference, and measures the normalized Scene View crop from a BGFX swapchain screenshot. The test requires at least 32 near-white pixels for the white test mesh; it observed 11,900 on the current Windows D3D11 driver. This is a coarse blank-render detector, not an exact image baseline or cross-driver quality check. See [TESTING.md](TESTING.md#071h-scene-view-visual-smoke-and-mesh-diagnostics), [07.1h report](reports/07.1h-editor-scene-visual-smoke.md), and [D131](DECISIONS.md#d131--scene-view-visual-smoke-and-mesh-diagnostics-071h).

## Editor 3D frame profile (07.1i)

Run `astro_editor.exe --benchmark-3d` from a Release build with the optional glTF importer. The command makes a temporary project based on the existing triangle glTF fixture and constructs four fixed visible-mesh grids (1, 100, 500, 1000 entities), grouped under hierarchy roots. Each scenario discards 24 warm-up frames and measures 48 frames. The report separates CPU mesh preparation and draw submission from complete Editor frame wall time; it also includes the time spent before and inside `bgfx::frame`, BGFX full-frame GPU timer, derived frame rate, fixture import/scene construction, process working/private memory, executable bytes and draw counts. The Editor is VSync-limited on the validated Windows D3D11 configuration; `bgfx_full_frame_gpu_timer` covers the entire window frame and must not be read as an isolated 3D pass cost. This is a repeatable local diagnostic and CTest smoke, not a portable benchmark or a performance budget. On the current host, three runs reported 1000-mesh Scene 3D CPU p50 of 1.4192–1.6042 ms and complete Editor wall-frame p50 of 16.6509–16.6791 ms. Full logs and commands are in [TESTING.md](TESTING.md#071i-editor-3d-frame-profile) and [the 07.1i report](reports/07.1i-editor-3d-frame-profile.md).
