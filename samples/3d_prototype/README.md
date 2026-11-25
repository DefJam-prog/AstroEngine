# Optional 3D BGFX smoke

When `ASTRO_BUILD_RUNTIME_BGFX_3D=ON`, the optional executable includes the `astro_runtime_bgfx_scene3d` packet consumer. The sample smoke tests the disposable RuntimeSession Scene; the Editor also uses the adapter for eligible Play content in Game View, with the Editor owning its context and framebuffer. Unsupported packet/material paths fall back to the existing Editor renderer. Adapter material limits and lifecycle are documented in [3D rendering](../../docs/3D_RENDERING.md).

This Windows x64/MSVC sample validates the backend-neutral 3D camera/mesh prototype against the local BGFX D3D11 stack.

## Procedural cube smoke

Enable `ASTRO_BUILD_STACK_SPIKE=ON` and `ASTRO_BUILD_RENDER3D_BGFX_SMOKE=ON`. Build target `astro_render3d_bgfx_smoke`; run:

```powershell
& '.\build\07.1e-scene-release\samples\3d_prototype\astro_render3d_bgfx_smoke.exe' --smoke
```

The existing procedural cube mode remains available with `ASTRO_BUILD_GLTF_IMPORTER=OFF`.

## Scene Mesh preview smoke

Enable `ASTRO_BUILD_GLTF_IMPORTER=ON` as well. Run the CTest `runtime.render3d.scene.bgfx.smoke` or:

```powershell
ctest --test-dir build/07.1e-scene-release -R "^runtime\\.render3d\\.scene\\.bgfx\\.smoke$" -V
```

CTest copies the checked-in Scene/glTF/material fixture into a unique temporary project under the ignored build tree. The app registers its model and material, loads the serialized Scene v7, resolves the visible MeshRenderer, selected primitive and project material override, then renders it in a D3D11 window and verifies BGFX screenshot readback. The temporary fixture copy is removed after the smoke. The sample does not change the fixture or other project assets.

## Material tint smoke

With the importer enabled, CTest `runtime.render3d.material.bgfx.smoke` invokes `--material-smoke --project-root <temporary-project>`. It resolves the Scene's registered material override and multiplies its linear `baseColorFactor` into the mesh's existing vertex-color bytes before BGFX upload. The fixture uses a red-dominant factor; image checks require red-dominant readback pixels, broad coverage and in-window bounds. This only proves the factor reaches the existing flat vertex-color draw path.

This is not the Editor Scene/Game View renderer. The preview draws one primitive at origin and ignores Scene transforms/hierarchy. The material mode consumes only `baseColorFactor`; it is a flat tint diagnostic, not PBR. Normals/UV are absent and glTF primitives with other vertex attributes remain rejected. Metallic/roughness, emissive, alpha modes, texture roles, image decode/upload, lighting and shadows are ignored. It accepts at most 65,536 vertices and 196,608 indices and uses a 16-bit BGFX index buffer. Pixel checks verify broad foreground coverage, tint color dominance and bounds, not exact image output.

## Base-color texture smoke

With the optional importer and BGFX sample enabled, run CTest `runtime.render3d.base_color_texture.bgfx.smoke`. It invokes `--texture-smoke --project-root <temporary-project>`, imports the fixture PNG through AssetRegistry, resolves the material's `BASE_COLOR` image reference and `texCoord=0`, decodes the verified Library cache through WIC, then uploads BGRA8 pixels and UV0 to a dedicated BGFX D3D11 sampling pass. The test verifies screenshot coverage and four broad color families. A separate `runtime.image.wic.integration` verifies exact fixture dimensions and representative BGRA values, plus rejection of corrupt/missing files.

The decoder is Windows WIC-only, bounded to 16,384 × 16,384 and 16 Mi pixels. This prototype uses a white factor to isolate texture sampling; it does not multiply `baseColorFactor`, convert sRGB, use glTF sampler/texture transforms, render other roles, or integrate with Editor Scene/Game View. Pixel checks are thresholds, not exact image goldens. The temporary test project is copied under the ignored build tree and removed after the smoke.

## Linear base-color factor smoke

CTest `runtime.render3d.base_color_factor.bgfx.smoke` runs the same registered image/UV0 path with factor `(0.65, 0.60, 0.80, 1)`. `prepareLinearBaseColorBgra8` decodes image RGB from sRGB to linear, multiplies the linear factor and stores a temporary linear BGRA8 upload buffer; it multiplies alpha directly. BGFX uses the D3D11 sRGB backbuffer, and the smoke compares readback with expected encoded values `(212, 203, 232)` within bounded ranges. The source PNG and registry cache are unchanged. `runtime.base_color_texture.unit` checks transfer values, rounding, alpha and invalid payloads/factors.

This validates CPU preparation plus BGFX sampling/output, not hardware sRGB texture sampling. Per-material CPU preparation and 8-bit linear quantization remain prototype limitations; no Editor/Game View integration or production texture cache is provided.
