# Optional 3D BGFX smoke

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

CTest copies the checked-in Scene/glTF fixture into a unique temporary project under the ignored build tree. The app registers its model, loads the serialized Scene v6, resolves the visible MeshRenderer and selected primitive, then renders it in a D3D11 window and verifies BGFX screenshot readback. The temporary fixture copy is removed after the smoke. The sample does not change the fixture or other project assets.

This is not the Editor Scene/Game View renderer. The preview draws one primitive at origin; it ignores Scene transforms/hierarchy, materials and textures. It accepts at most 65,536 vertices and 196,608 indices and uses a 16-bit BGFX index buffer. Pixel checks verify broad foreground coverage and bounds, not exact image output.
