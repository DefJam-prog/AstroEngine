# Проверки и критерии готовности функций

### 07.1h — Scene View visual smoke and multi-mesh diagnostics

Run from VS 2022 x64 Developer PowerShell with the existing importer-enabled configuration and local dependency caches:

```powershell
cmake --build build/07.1f-editor3d-release --target astro_editor --parallel 8
& build/07.1f-editor3d-release/editor/astro_editor.exe --smoke
ctest --test-dir build/07.1f-editor3d-release `
  -R "^(editor\.scene_view_camera3d\.unit|editor\.localization\.unit|editor\.smoke|runtime\.render3d\.(bgfx|scene\.bgfx)\.smoke)$" --output-on-failure
ctest --test-dir build/07.1f-editor3d-release --output-on-failure
```

The smoke uses only its generated temporary project and the checked-in triangle fixture. It contains two registered/drawn meshes plus one nonzero but unregistered model AssetId. Assertions require exactly two 3D submissions in the last frame, one mesh diagnostic with the expected missing-registration text, Frame All center `x=4`, and at least 32 near-white pixels in a normalized crop of the actual Scene View viewport. The observed run measured 11,900 pixels. The criterion detects missing/blank geometry but allows driver rasterization variation; it is not a visual golden. CTest focused passed **5/5 in 4.85 s**; full Release suite passed **50/50 in 9.47 s**. Pre-change baseline for this substage: full CTest **50/50 in 14.44 s**. Logs: `build/07.1h-prechange-build-20261117.log`, `build/07.1h-prechange-ctest-20261117.log`, `build/07.1h-final-build-20261117.log`, `build/07.1h-editor-smoke-20261117.log`, `build/07.1h-focused-ctest-20261117.log`, `build/07.1h-final-ctest-20261117.log`. Windows x64/D3D11 only; this does not validate other GPU drivers or platforms.

### 07.1g — Scene View camera and framing

Run in VS 2022 x64 Developer PowerShell. The importer/dependency caches are local build inputs; configure does not download them:

```powershell
cmake -S . -B build/07.1f-editor3d-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON -DASTRO_BUILD_EDITOR=ON -DASTRO_BUILD_STACK_SPIKE=ON `
  -DASTRO_BUILD_GLTF_IMPORTER=ON -DASTRO_BUILD_RENDER3D_BGFX_SMOKE=ON `
  -DASTRO_GLTF_DEPS_ROOT="$((Get-Location).Path)/build/07.1b-deps" `
  -DASTRO_SPIKE_DEPS="$((Get-Location).Path)/build/spike-deps" `
  -DASTRO_DOTNET_ROOT="$((Get-Location).Path)/build/04.5-dotnet-sdk"
cmake --build build/07.1f-editor3d-release --parallel 8
ctest --test-dir build/07.1f-editor3d-release `
  -R "^(editor\.scene_view_camera3d\.unit|editor\.localization\.unit|editor\.smoke|runtime\.render3d\.(bgfx|scene\.bgfx)\.smoke)$" --output-on-failure
ctest --test-dir build/07.1f-editor3d-release --output-on-failure
```

The focused selection passed **5/5 in 5.02 s**; the final complete Release suite passed **50/50 in 9.82 s**. The Editor smoke reported 139 3D mesh submissions, transformed child world origin `(2.75,0,0)`, and successful `frameAll=1` at camera distance `5.36656`; raw output is in `build/07.1g-editor-smoke-20261117.log`. The Editor smoke requires mesh submission, a transformed child origin, and successful Frame All. Unit coverage exercises default pose, orbit, pan, zoom limits, valid/invalid bounds and aspect ratio, extreme values, and preservation of camera state on rejected input. Logs: `build/07.1g-final-build-20261117.log` and `build/07.1g-final-ctest-20261117.log`. The smoke is not an image-golden test; macOS/iOS/Android and importer-off rebuild were not run in this substage. Build output has three existing MSVC C4189 unused-local warnings in C# metadata smoke code.

### 07.1f — Editor Scene View Mesh preview

From VS 2022 x64 Developer PowerShell with existing `build/spike-deps`, `build/07.1b-deps`, and local `build/04.5-dotnet-sdk` caches:

```powershell
cmake -S . -B build/07.1f-editor3d-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON -DASTRO_BUILD_EDITOR=ON -DASTRO_BUILD_STACK_SPIKE=ON `
  -DASTRO_BUILD_GLTF_IMPORTER=ON -DASTRO_BUILD_RENDER3D_BGFX_SMOKE=ON `
  -DASTRO_GLTF_DEPS_ROOT="$((Get-Location).Path)/build/07.1b-deps" `
  -DASTRO_SPIKE_DEPS="$((Get-Location).Path)/build/spike-deps" `
  -DASTRO_DOTNET_ROOT="$((Get-Location).Path)/build/04.5-dotnet-sdk"
cmake --build build/07.1f-editor3d-release --parallel 8
ctest --test-dir build/07.1f-editor3d-release `
  -R "^(editor\.smoke|runtime\.render3d\.(bgfx|scene\.bgfx)\.smoke)$" --output-on-failure
ctest --test-dir build/07.1f-editor3d-release --output-on-failure
```

Windows x64/MSVC Release configure and build succeeded. Focused BGFX/Editor smoke passed **3/3**; full CTest passed **49/49 in 9.90 s**. A direct Editor smoke reported D3D11, 140 frames, 139 Scene View render frames, **139 3D mesh submissions**, and transformed child origin `(2.75,0,0)` after parent scale/local offset. The importer-off `build/06.53-editor-release` was rebuilt; focused `editor.localization.unit` + `editor.smoke` passed **2/2 in 3.45 s**. Logs: `build/07.1f-reconfigure-20261117.log`, `build/07.1f-build-20261117.log`, `build/07.1f-final-build-20261117.log`, `build/07.1f-final-ctest-20261117.log`, `build/07.1f-importer-off-build-20261117.log`.

The smoke uses the checked-in triangle fixture copied into an isolated temporary project; it does not open or edit the user's project. It checks hierarchy-transform composition and BGFX submission, not pixel goldens or performance. Camera controls/framing, mesh diagnostics for multiple simultaneous failures, materials, normals, lighting, picking, Game View/runtime, and non-Windows backends remain outside this slice. Editor build emitted three C4189 warnings for existing metadata diagnostic smoke locals; no new build error remained.

### 07.1e — Standalone Scene Mesh BGFX smoke

From VS 2022 x64 Developer PowerShell with the existing local `build/spike-deps` and `build/07.1b-deps` caches:

```powershell
cmake -S . -B build/07.1e-scene-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON -DASTRO_BUILD_STACK_SPIKE=ON `
  -DASTRO_BUILD_GLTF_IMPORTER=ON -DASTRO_BUILD_RENDER3D_BGFX_SMOKE=ON `
  -DASTRO_GLTF_DEPS_ROOT="$((Get-Location).Path)/build/07.1b-deps" `
  -DASTRO_SPIKE_DEPS="$((Get-Location).Path)/build/spike-deps"
cmake --build build/07.1e-scene-release --parallel 8
ctest --test-dir build/07.1e-scene-release `
  -R "^runtime\.render3d\.(bgfx|scene\.bgfx)\.smoke$" --output-on-failure
ctest --test-dir build/07.1e-scene-release --output-on-failure
```

Windows x64 Release: clean configure succeeded; after fixing a compile-time vector iteration error, the sample rebuilt successfully. Focused procedural+Scene BGFX smoke passed **2/2**. Verbose Scene smoke reported D3D11, 32 frames, foreground pixels **24,642**, color buckets **1**, bounds `(369,210)–(590,430)`, and `scene_mesh=1 model_asset=1 mesh_index=0 primitive_index=0 visible=1`. The full configured suite passed **45/45** (initial full run 12.96 s; final post-regeneration run **6.10 s**). A separate importer-off BGFX sample build/test passed **1/1**; headless build remained up to date and contained no 3D/import/sample target. Logs: `build/07.1e-configure-20261117.log`, `build/07.1e-build-20261117.log`, `build/07.1e-rebuild-20261117.log`, `build/07.1e-focused-ctest-20261117.log`, `build/07.1e-scene-visual-output-20261117.log`, `build/07.1e-full-ctest-20261117.log`, `build/07.1e-final-configure-20261117.log`, `build/07.1e-final-build-20261117.log`, `build/07.1e-final-ctest-20261117.log`, `build/07.1e-importer-off-build-20261117.log`, `build/07.1e-importer-off-ctest-20261117.log`, and `build/07.1e-headless-build-20261117.log`.

This smoke is Windows x64 D3D11 only and uses an isolated fixture. Pixel coverage/bounds are coarse thresholds, not a golden image. The shader is the existing BGFX cube sample shader; preview geometry is capped at 65,536 vertices/196,608 indices and drawn without back-face culling, at origin without Scene transforms. Editor/Game View, runtime, multiple objects and real project resource integration are not verified.

### 07.1d — Scene MeshRenderer resolution

The opt-in Windows x64 Release configuration reuses the verified 07.1b local dependency cache:

```powershell
cmake --build build/07.1c-release-clean --parallel 8
ctest --test-dir build/07.1c-release-clean `
  -R "^runtime\.render3d\.gltf\.import\.integration$" --output-on-failure
ctest --test-dir build/07.1c-release-clean --output-on-failure
```

The glTF integration test covers successful resolution of a persisted Scene component; unknown/zero AssetId; unsupported component schema; out-of-range mesh and primitive selectors; changed model source; and changed external buffer while the top-level glTF file remains unchanged. Explicit reimport/register restores successful resolution without changing the model AssetId. It also checks selected triangle geometry, name and visibility state. Clean Windows x64 Release build passed **159/159** actions; final focused test passed **1/1 in 0.29 s** and full CTest passed **42/42 in 2.51 s**. The headless Release tree was reconfigured and built with importer/testing disabled; it reported no work and target help contained neither `astro_gltf` nor `astro_runtime_render3d`. Logs: `build/07.1d-clean-configure-20261117.log`, `build/07.1d-clean-build-20261117.log`, `build/07.1d-final-build-20261117.log`, `build/07.1d-final-focused-ctest-20261117.log`, `build/07.1d-final-full-ctest-20261117.log`, `build/07.1d-headless-configure-20261117.log`, `build/07.1d-headless-build-20261117.log`. Cross-platform, sanitizer, visual-renderer and persistent-cache behavior are not covered; the resolver validates current import inputs on demand and does not submit rendering work.

### 07.1c — Scene model asset references

From VS 2022 x64 Developer PowerShell with the locally prepared 07.1b dependency cache:

```powershell
cmake -S . -B build/07.1c-release-clean -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON -DASTRO_BUILD_GLTF_IMPORTER=ON `
  -DASTRO_GLTF_DEPS_ROOT="$((Get-Location).Path)/build/07.1b-deps"
cmake --build build/07.1c-release-clean --parallel 8
ctest --test-dir build/07.1c-release-clean `
  -R "^(assets\.registry\.integration|scene\.file\.integration|scene\.command\.integration|prefab\.integration|runtime\.render3d\.gltf\.import\.integration)$" `
  --output-on-failure
ctest --test-dir build/07.1c-release-clean --output-on-failure
```

Windows x64/MSVC Release clean build completed **158/158** actions. Focused asset/Scene/prefab/import integration passed **5/5** in 2.22 s; after removing temporary failure-location diagnostics, the final full suite passed **42/42 in 7.94 s**. A fresh default headless Release build with `BUILD_TESTING=OFF` and `ASTRO_BUILD_GLTF_IMPORTER=OFF` completed **32/32** actions. Tests cover Asset index v1/v2 read and v3 write/reopen, stable model AssetId, source modification/missing diagnostics, Scene v5→v6 read and v6 round-trip, MeshRenderer command validation and undo/redo, prefab-capture rejection, and glTF import-to-registry flow. Logs: `build/07.1c-clean-configure-20261117.log`, `build/07.1c-clean-build-20261117.log`, `build/07.1c-clean-focused-ctest-20261117.log`, `build/07.1c-final-build-20261117.log`, `build/07.1c-final-ctest-20261117.log`, `build/07.1c-headless-configure-20261117.log`, `build/07.1c-headless-build-20261117.log`.

No macOS/mobile build, fuzzing or sanitizer run was performed. The integration verifies model external-buffer changes via explicit reimport; AssetRegistry does not yet keep normalized dependency records for each buffer. The checks do not test visual 3D rendering or prefab persistence of MeshRenderer.

### 07.1b — bounded glTF/GLB importer

The opt-in Windows x64 Release configuration uses pinned local sources only; configure fails if the expected fastgltf commit or simdjson/simdutf hashes differ. The CMake integration explicitly sets fastgltf's automatic dependency download options OFF. From VS 2022 x64 Developer PowerShell, after the approved local staging step:

```powershell
cmake -S . -B build/07.1b-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON -DASTRO_BUILD_GLTF_IMPORTER=ON `
  -DASTRO_GLTF_DEPS_ROOT="$((Get-Location).Path)/build/07.1b-deps"
cmake --build build/07.1b-release --parallel 8
ctest --test-dir build/07.1b-release -R "runtime.render3d.gltf.import.integration" --output-on-failure
ctest --test-dir build/07.1b-release --output-on-failure
```

Integration tests cover embedded data, GLB, external Unicode/percent-encoded buffer paths, preserved source data, repeat import/cache hit, cache corruption rebuild, traversal (literal and encoded), remote URIs, required unsupported extensions, invalid indices/attributes, malformed JSON, out-of-budget source/buffer sizes, and cache link rejection/preservation where supported. Windows x64 Release build completed 95/95 Ninja actions; focused test passed 1/1 (0.25 s); full CTest passed 42/42 (8.92 s), then passed 42/42 again in 2.41 s. A separate default headless Release configure/build passed with `ASTRO_BUILD_GLTF_IMPORTER=OFF`; target help contained no `astro_gltf`. Logs are `build/07.1b-release-full-build-20261117.log`, `build/07.1b-release-full-ctest-20261117.log`, `build/07.1b-release-full-ctest-final-20261117.log`, `build/07.1b-focused-build-final-20261117.log`, `build/07.1b-headless-configure-20261117.log` and `build/07.1b-headless-build-20261117.log`. The earlier baseline was a different configuration with 46/46 tests; test counts should not be compared as though the target sets were identical.

No fuzz/sanitizer campaign or non-Windows build was run. Path validation is not race-safe against hostile concurrent path replacement. Scope/remaining limits: [3D guide](3D_RENDERING.md#bounded-gltfglb-geometry-import-071b), [report](reports/07.1b-bounded-gltf-importer.md).

### 07.1a — 3D camera/mesh and bgfx smoke

Windows x64 Release configuration uses the existing `build/06.53-editor-release` tree. Configure from the VS 2022 x64 Developer Command Prompt:

```powershell
cmake -S . -B build/06.53-editor-release -DASTRO_BUILD_RENDER3D=ON -DASTRO_BUILD_RENDER3D_BGFX_SMOKE=ON
cmake --build build/06.53-editor-release --parallel 8
ctest --test-dir build/06.53-editor-release -R "runtime.render3d.(camera_mesh.unit|bgfx.smoke)" --output-on-failure
ctest --test-dir build/06.53-editor-release --output-on-failure
```

The camera/mesh unit test has no platform or window dependency. The bgfx smoke is opt-in, Windows x64/MSVC/D3D11 only, and requires the prepared `build/spike-deps` SDL/bgfx tree and its `vs_cubes.bin`/`fs_cubes.bin`; CMake copies only those runtime shader files into the build tree. It requests a backbuffer readback and verifies foreground pixel count, color-bucket diversity and bounded image region. It does not save an image or compare exact pixels against a golden reference. It reports bgfx CPU frame-interval p50/p95; the isolated VSync cube result is not a budget. Three runs each found 85,155 foreground pixels, 169 color buckets and bounds 280,135–658,498. Median p50 was 16.735 ms (range 16.701–16.764 ms); median p95 was 32.824 ms (range 32.055–34.120 ms). After readback, Windows Release build completed 161/161 Ninja actions and full CTest passed 46/46 in 13.94 s. Earlier WSL Release source state passed 40/40 in 0.77 s; WSL was not rerun after the Windows-only sample change.

## 04.15 — C# Code workspace

Unit test `editor.csharp_code_workspace.unit` checks UTF-8 Cyrillic round-trip, dirty/save state, nested file creation, extension/path traversal rejection and parsing of error/warning file+line locations. `editor.smoke` uses its own temporary project to create, save and reopen a UTF-8 script and checks that the Code workspace window renders. `editor.localization.unit` verifies the new UI labels. The existing `runtime.csharp_project_build.integration` still performs the actual offline .NET build and invalid-C# compiler case; Editor smoke does not synthesize clicks or execute a project build. No test claims sandbox isolation.

Текущие полные результаты Windows x64: Debug build successful, CTest **44/44** (9.66 с); Release + C# host build successful, CTest **46/46** (38.29 с), включая offline C# build integration (29.28 с). Логи: `build/04.15-{debug,release}-{build,ctest}.log`; команды и ограничения: [отчёт 04.15](reports/04.15-csharp-code-workspace.md).

## 04.16 — C# build manifest settings

`editor.csharp_build_manifest.unit` checks schema/types, UTF-8 round-trip, unsupported versions/framework, unknown/duplicate keys, path traversal, missing source bindings and preservation of the previous manifest when validation fails. `editor.smoke` writes and rereads settings inside its isolated temporary project. Release `runtime.csharp_project_build.integration` exercises the same manifest through the offline .NET build pipeline. UI clicks/typing are not synthesized. Full Debug/Release results and logs: [04.16 report](reports/04.16-csharp-build-manifest-editor.md).

## 04.17 — C# build-to-registry diagnostics

`assets.registry.integration` checks a current manifest/build handoff with an unregistered type, a registered type that differs from the handoff, explicit repair through `registerScriptsFromBuild()`, and stable AssetId preservation. Existing checks cover stale handoff hashes. `editor.localization.unit` covers the Russian mismatch diagnostic; `editor.smoke` confirms the Project C# panel renders. Refreshing diagnostics does not load a C# assembly; CLR type/entrypoint existence is checked only during explicit approved Play loading. Full Debug/Release results and logs: [04.17 report](reports/04.17-csharp-binding-diagnostics.md).

## Запланированная приёмка встроенного редактора C# (P15)

Это критерии будущей функции, не фактические проверки текущей сборки. Unit/integration tests должны покрыть UTF-8 сохранение и повторное открытие `.cs`, compiler diagnostics и ошибки bindings; Editor smoke — открытие/редактирование файла, несохранённые правки и переход от ошибки к строке; runtime smoke — запуск назначенного скрипта в Play Mode и изоляцию редактируемой сцены после Stop. Build/export regression должен подтвердить, что Editor не попадает в игру и проекты без скриптов не тянут необязательный scripting host. Тесты безопасности должны подтвердить, что AI не собирает и не исполняет изменённый код без необходимых разрешений/подтверждения; наличие .NET runtime само по себе не является sandbox.

## 04.5 — C# native host prototype

До изменений C++ baseline `build/stack-spike`: build successful; полный CTest **42/42**, 7.80 s. После реализации та же конфигурация: build successful; **42/42**, 7.53 s. Отдельная Release-конфигурация с `ASTRO_BUILD_CSHARP_RUNTIME=ON`, `BUILD_TESTING=ON`: C++ и C# sample build successful; полный CTest **36/36**, финальный повтор 0.87 s (первый полный запуск после сборки 12.88 s), включая `runtime.csharp_host.integration` (0.19 s). Тест проверяет загрузку `UnmanagedCallersOnly`, validation timestep, три fixed steps, обновление runtime Transform, неизменность authoring Scene после Play/Stop и отклонение неверного payload.

SDK 10.0.404 был распакован локально по разрешению пользователя. Для воспроизведения команды и ограничения см. [отчёт 04.5](reports/04.5-csharp-runtime-prototype.md). C# host доступен только Windows x64 MSVC и остаётся выключенным по умолчанию. Тест не проверяет произвольный пользовательский API, редакторскую UX, sandbox, hot reload, package/export или мобильные цели.

## 04.9 — C# project build pipeline

The C# Release configuration adds `runtime.csharp_project_build.integration` when PowerShell 7 is available. It compiles two isolated project copies with the pinned local SDK and cleared NuGet sources; assembly bytes match across different project paths. It also checks CLI exit codes for fresh/stale output, source-change rebuilds, immutable prior generations, preservation of the last successful build-state after invalid C#, and output hash tampering. Test fixtures use a Cyrillic project path and are removed only from their own unique CMake build test directory after each run.

Before changes in this turn: Debug **42/42** (8.44 s), Release+C# host **36/36** (1.84 s). After implementation: Debug **42/42** (9.09 s); Release+C# host **37/37** (22.99 s), including `runtime.csharp_project_build.integration` (20.98 s). The focused build test performs real offline .NET builds including a deliberate compiler failure. It needs the local SDK and PowerShell 7; if `pwsh` is not found, CMake does not register this integration test. It does not test Editor commands/UI, AssetRegistry registration of produced assemblies, runtime lifecycle, sandboxing, or package/export. Logs are recorded under `build/04.9-{debug,release}-{build,ctest}.log`.

## 04.10 — C# build handoff and project diagnostics

Baseline before edits: Debug build/tests **42/42** (8.23 s); Release with C# host **37/37** (24.14 s). Final Windows x64/MSVC x64 checks: Debug build succeeded and CTest **42/42** passed (8.28 s); Release + C# host build succeeded and CTest **37/37** passed (20.83 s). No tests were skipped. `assets.registry.integration` validates current handoff hashes, explicit registration, digest tampering detection, and stable IDs; `runtime.csharp_host.integration` consumes a handoff before loading the real sample assembly; the PowerShell build integration exercises generated schema v2 state; `editor.smoke` verifies the Project panel/status path. `editor.localization.unit` verifies all new UI labels have Russian text.

Builds used the installed Visual Studio x64 environment via `VsDevCmd.bat`. Logs are saved at `build/04.8-debug-clean/04.10-build.log`, `build/04.8-debug-clean/04.10-ctest.log`, `build/04.8-release-clean/04.10-build.log`, and `build/04.8-release-clean/04.10-ctest.log`. The test verifies project/build consistency, not trust against a malicious user who can replace both the builder's state and its digest. The selected CLR type is not reflected until the explicit host-load call. No lifecycle/sandbox security is claimed.

## 04.11 — C# Play approval policy (design-only)

No runtime behavior or tests were added in this substage. Before documentation edits, the Debug and Release builds completed and the baseline suites passed: Debug **42/42** (7.90 s) and Release+C# **37/37** (22.34 s). CMake/CTest 4.4.4, Ninja 1.13.2, PowerShell 7 were available; MSVC requires the installed Visual Studio x64 developer environment. After the documentation-only edits, `git diff --check` is the applicable validation. The decision's future test acceptance is specified in `CSHARP_SCRIPTING_API.md`; it is not evidence that an approval dialog, permission gate, runtime lifecycle, or sandbox exists. Logs: `build/04.11-baseline-{debug,release}-{build,ctest}.log`.

## 04.6 — versioned C# Scene bridge

Baseline до изменений: `build/stack-spike` — build up to date и CTest **42/42** (7.35 s); `build/04.5-csharp-host` — build up to date и CTest **36/36** (1.12 s). `runtime.csharp_host.integration` теперь выполняет C# fixed update с ABI v1 и проверяет callbacks чтения/записи Transform на runtime Scene copy. Дополнительно проверяются native rejection неверной ABI version и размера, неизвестный EntityId, нулевой timestep, отказ записи переполненного (infinite) Transform без изменения предыдущего состояния и Play/Stop isolation. Focused integration после изменений: **1/1 passed** (0.18 s); полный C# Release CTest **36/36** (1.10 s); исходная `stack-spike` сборка и CTest **42/42** (7.08 s). См. [отчёт 04.6](reports/04.6-versioned-csharp-runtime-api.md).

## 04.7 — persistent ScriptBehavior

`scene.unit` покрывает выдачу/обновление/удаление ScriptBehavior, устойчивый ComponentId, allocator, обязательный Script AssetId и managed type name. `scene.file.integration` проверяет UTF-8 v5 round-trip нескольких привязок (включая disabled), продолжение allocator, миграцию v4 prefab metadata и отказ неизвестной версии компонента/нулевого AssetId. `prefab.integration` подтверждает, что capture поддерева с ScriptBehavior отклоняется вместо молчаливой потери данных. Runtime lifecycle, ScriptBehavior editor commands/UI, asset resolver, sandbox, API compatibility across releases, package/export и Android/iOS этим набором не проверяются.

Перед изменениями baseline: `build/stack-spike` — Debug build successful, **42/42**; `build/04.5-csharp-host` — Release build successful, **36/36**. Incremental каталоги после изменения публичного `Entity` layout показали повторяемые heap corruption/segfault в несвязанных тестах. Это устранилось полной пересборкой в новых каталогах без очистки старых: Debug `build/04.7-debug-clean` — build successful, **42/42** (35.55 s); Release + C# host `build/04.7-release-clean` — build successful, **36/36** (7.01 s). Логи сохранены в `build/04.7-debug-clean-{configure,build,ctest}.log` и `build/04.7-release-clean-{configure,build,ctest}.log`. Команды воспроизведения приведены в [отчёте 04.7](reports/04.7-persistent-script-behavior.md). Отдельная полная чистая сборка важна при изменении публичного Scene/Entity layout.

## 04.8 — script asset resolution

`assets.registry.integration` проверяет общий image/script AssetId counter, чтение v1 без записи и явную миграцию на v2, Unicode source/type, stale hash после правки `.cs`, отсутствующий assembly, traversal paths, стабильный ID при обновлении type mapping. `runtime.csharp_host.integration` создаёт временный Unicode project, регистрирует и повторно открывает Script AssetId, разрешает его в настоящую C# sample assembly, загружает type и `FixedUpdate` через hostfxr, а затем проверяет прежний Transform/RuntimeSession контракт. Host test запускается дочерним процессом для освобождения загруженной CLR assembly перед очисткой временного каталога.

До изменений: Debug **42/42** (8.23 s), Release+C# host **36/36** (1.72 s). После изменений чистая Debug-сборка/CTest: **42/42** (39.32 s); чистая Release+C# host: **36/36** (8.18 s). После финального изменения управления test process focused C# integration снова прошёл **1/1**. Логи и точные команды: [отчёт 04.8](reports/04.8-project-script-asset-registry.md).

## Фактические проверки 01.1

Добавлены unit-проверки `core.unit`: `Result<T>/void`, уровни Logger, фильтрация и sink, конфигурация UTF-8/BOM/CRLF, ошибки отсутствующего ключа/синтаксиса/дубликатов, сохранение старого состояния после неудачного load, round-trip, валидация и EngineContext. Результаты этого подэтапа: [отчёт 01.1](reports/01.1.md).

## Фактические проверки 01.2

`core.unit` дополнен проверками детерминированного топологического порядка, reverse shutdown, запрета повторного lifecycle, отсутствующей/циклической/дублирующей зависимости, rollback после ошибки initialize и продолжения shutdown после ошибки одного модуля. Полный CTest после изменений: 5/5, включая headless smoke и регрессию stack spike; подробности — [отчёт 01.2](reports/01.2.md).

## Фактические проверки 01.3

`core.unit` проверяет typed dispatch, изоляцию типов событий, subscribe/unsubscribe во время dispatch, исключения callbacks, fixed-step count/interpolation, catch-up cap и dropped-time accounting, clamp большого delta, паузу, неверные значения и порядок/error пути module tick. Полный CTest после изменений: 5/5; headless и stack-spike smoke сохранились. Подробности: [отчёт 01.3](reports/01.3.md).

## Фактические проверки 01.4

`core.unit` проверяет регистрацию, ID/schema version, точные payload/result типы, validation-only без execute, отклонение до выполнения, типизированный результат, void-команду, отсутствующий ID, дублирование, ошибки и исключения validator/executor, audit entries. Полный CTest после изменений: 5/5; headless и stack-spike smoke сохранились. Подробности: [отчёт 01.4](reports/01.4.md).

## Фактические проверки 01.5

`core.unit` проверяет атомарное применение списка шагов, rollback префикса при ошибке, очистку истории при ошибке rollback, Undo в обратном и Redo в прямом порядке, redo branch invalidation, восстановление состояния при ошибках Undo/Redo, transactional command через CommandRegistry и undo barrier non-transactional команды. Полный CTest: 5/5; подробности: [отчёт 01.5](reports/01.5.md).

## Фактические проверки 00.2

## Фактические проверки 00.3

- Чистая baseline Windows x64 Debug сборка MSVC: успешна; CTest 3/3.
- Spike SDL3/bgfx/ImGui: сборка успешна; CTest 4/4. `stack_spike.smoke` автоматически проверяет 90 кадров, активный docking, клавиатурное событие, resize окна и наличие draw calls.
- Ручной запуск: SDL 3.5.0, Dear ImGui 1.93.1 WIP, bgfx Direct3D 11; процесс рендерил 18 409 кадров, UI зарегистрировал клавиши и клики. Скриншот/ручное перетаскивание docking не подтверждены.
- Подробные команды, конфигурация инструментов, ревизии и ограничения: [reports/00.3.md](reports/00.3.md).

- `pwsh -NoProfile -File tests/scripts/process_runner.tests.ps1`: 5/5 проверок — stdout/stderr, ненулевой код, отсутствующая программа, тайм-аут, Unicode-аргумент с пробелами. Эти тесты проверяют инфраструктуру процессов, а не сборку движка.
- `pwsh -NoProfile -File scripts/verify.ps1`: JSON и логи созданы, exit 2 / incomplete. Конфигурация, сборка, CTest и форматирование blocked из-за отсутствия инструментов; статический анализ skipped.
- `pwsh -NoProfile -File tests/scripts/report.tests.ps1 -ReportPath <путь-к-report.json>`: 9/9 проверок схемы, исходных хэшей, этапов, итогового статуса и наличия логов прошли на реальном отчёте.
- Добавлены CTest-сценарии headless.help и headless.invalid, сохранён headless.startup. Выполненных C++/CTest-тестов: 0; компилятор и CMake/CTest недоступны. Обработка сбоев вывода/исключений приложения ещё не проверена исполнением.

См. [reports/00.2.md](reports/00.2.md). Сценарии успешной C++-сборки и запуска clang-format/clang-tidy пока не подтверждены. Подмены build-инструментов для заявления об успешной сборке не используются.

Исторически, при выполнении 00.1, CTest smoke-тест `headless.startup` ещё не был запущен из-за отсутствия toolchain. Сейчас он исполняется в регрессионном наборе. Тест проверяет запуск приложения, нулевой код завершения, отсутствие stderr и обязательную информацию о запуске; проверки не зависят от assert/NDEBUG.

Команда: `ctest --preset windows-debug` после конфигурации и сборки по [BUILD_GUIDE.md](BUILD_GUIDE.md). Результаты первоначальной проверки среды: [reports/00.1.md](reports/00.1.md). Теперь unit-тесты ядра и Scene Model, integration-команд и smoke-тесты редактора/runtime выполняются; сохранение сцен и игровые системы ещё не реализованы.

## Проверки каждого этапа

| Уровень | Область |
| --- | --- |
| Unit | Ядро и Scene Model |
| Integration | Команды; сериализация сцен появится после реализации |
| Smoke | Редактор и runtime |
| Regression | Ранее готовые функции |

Для каждого этапа указывать результаты этих групп проверок. Если соответствующая подсистема ещё отсутствует или проверку выполнить невозможно, явно записывать причину; отсутствие теста не считать успехом. Новые функции покрываются подходящими тестами, ранее готовые проверяются на регрессии.

Проверки дополняют критерии приёмки из [ROADMAP.md](ROADMAP.md). Учитывать чистую сборку, Windows-пути, Unicode, UTF-8 и отсутствие сети. В текущей среде CMake/CTest/Ninja/MSVC доступны из Visual Studio 2022 Developer PowerShell x64. Чистые сборки и другие платформы проверяются отдельно и не считаются покрытыми инкрементальным тестовым каталогом.

## Графика

Использовать визуальные эталонные снимки с допустимыми отклонениями. Метрика сравнения и численные допуски пока не определены. Для воспроизводимости фиксировать тестовую сцену, настройки и окружение захвата; сохранять результат сравнения и выявленные отличия. Изменение эталона не должно скрывать необъяснённую регрессию.

## Производительность

Использовать фиксированные тестовые сцены и измерять FPS/frame time, память, загрузку и размер сборки. Фиксировать окружение, настройки и метод измерения, чтобы сравнения были воспроизводимыми.

Целевые бюджеты утверждаются отдельно. Пока бюджет не утверждён, измерение является наблюдением, а не подтверждением достижения целевой производительности.

## Сохранения

Обязательны round-trip сохранения/загрузки, миграции, прерывание записи, неверная версия, отсутствующие объекты, повторная загрузка и проверка checkpoint.

Проверки дополняют [SAVE_SYSTEM.md](SAVE_SYSTEM.md): выборочная запись, повреждённые файлы, резервное восстановление, смерть/respawn, перезапуск игры и стабильные runtime-ID динамических объектов. Проверять сохранность выбранных данных и отсутствие исключённых полей в сохранении.

## Безопасность и зависимости

Проверять валидацию путей, границы памяти, обработку недоверенных ассетов и скриптов, права AI и лицензии сторонних библиотек. Конкретные средства анализа и тестирования выбираются при реализации соответствующего подэтапа; они пока не установлены и не настроены.

Для AI учитывать ограничения из [AI_INTEGRATION.md](AI_INTEGRATION.md). Проверка лицензий документируется отдельно от результатов исполняемых тестов. Наличие проверок не гарантирует отсутствие ошибок или уязвимостей.

## Обязательная документация

Целевой комплект документов:

- README.md в корне проекта.
- docs/ARCHITECTURE.md.
- docs/ROADMAP.md.
- docs/STATUS.md.
- docs/DECISIONS.md.
- docs/TESTING.md.
- docs/COMMAND_API.md.
- docs/FILE_FORMATS.md.
- docs/SAVE_SYSTEM.md.
- docs/TEMPLATE_GUIDE.md.
- docs/BUILD_GUIDE.md.

Это перечень обязательных документов, а не утверждение об их полноте. COMMAND_API создан в подэтапе 01.4; FILE_FORMATS и TEMPLATE_GUIDE пока отсутствуют. Остальные документы также должны получать содержательное наполнение по мере реализации соответствующих функций.

## Готовность функции и отчёт

Каждая готовая функция должна иметь документацию пользователя, пример, тест и известные ограничения. Одного описания запланированной функции недостаточно.

В отчёте подэтапа указывать изменённые файлы, работающее поведение, команды и окружение проверки, результаты тестов, пропущенные проверки с причинами, известные проблемы и следующий рекомендуемый подэтап. После отчёта остановиться до ручного подтверждения пользователя.

## Подэтап 02.1
Добавлен optional astro_editor: окно, docking, меню и project.astro. CTest 6/6 на момент завершения 02.1. Команды и ограничения: [отчёт 02.1](reports/02.1.md).

### Проверка безопасности project.astro (исправление 02.1)

`editor.smoke` проверяет round-trip UTF-8 имени с Ω, отказ создавать проект в существующем каталоге с контрольным файлом без изменения его содержимого и сохранение текущего проекта после открытия файла с неподдерживаемой версией. Это интеграционная smoke-проверка редактора, а не отдельный unit-тест формата.

## Подэтап 02.2

`scene.unit` проверяет UTF-8 имена, создание сущностей и parent links, отклонение пустых имён, неизвестных родителей, циклов и удаления родителя без явного recursive-флага, а также удаление поддерева без висячих ссылок. `editor.smoke` проверяет модель с root/child entity, окно редактора, docking Scene View/Hierarchy и графический вывод. Полный CTest после изменений: 7/7. Сохранение сцены и визуальный image-diff пока не реализованы.

## Подэтап 02.3

`scene.unit` проверяет предел длины и invalid UTF-8 при создании сущностей. `scene.command.integration` проверяет схему Name/ID, default/required/read-only metadata, validation-only без изменения модели, command execution, UTF-8 round-trip, неверный тип/ID/версию, пустое/слишком длинное/некорректное значение и отсутствие частичного изменения при отказе. `editor.smoke` проверяет регистрацию команды и применение тестового редактирования через `CommandRegistry`, а также docking и рендеринг Inspector. Регрессия полного CTest: 8/8. Ручное редактирование мышью и image-diff не выполнялись.

## Подэтап 02.4 — сцены и редактор

Интеграционный тест `scene.file.integration` проверяет UTF-8 round-trip, иерархию, продолжение ID после удаления, атомарную замену существующего файла, неверную версию, отсутствующего родителя, цикл, лишние данные, повреждённый UTF-8 и отсутствующий файл. `editor.smoke` проверяет сохранение/открытие, Undo/Redo и то, что открытие файла с неподдерживаемой версией не заменяет активную сцену. Это автоматический smoke, не ручная проверка визуального поведения GUI.

Проверенная Windows x64 конфигурация: Visual Studio 2022 MSVC, Debug, локальные SDL3/bgfx/ImGui. Команды из Developer PowerShell:

```powershell
cmake --build build/stack-spike --parallel 4
ctest --test-dir build/stack-spike --output-on-failure
```

Последний прогон: сборка успешна (`ninja: no work to do`), CTest 9/9 passed. Сериализация не проверяет принудительное прерывание записи и не содержит миграций/резервной копии.

## Подэтап 03.2 — Asset Registry и импорт

`assets.hash.unit` проверяет SHA-256 по эталонным векторам пустой строки и `abc`. `assets.registry.integration` создаёт отдельный временный project и проверяет PNG/JPEG metadata, UTF-8 source path, stable ID и индекс после reopen, CRLF, dedup повторного импорта, явный reimport при смене содержимого, missing source report, восстановление повреждённого cache entry, rejection traversal/unsupported/corrupt PNG, missing ID и неизменность исходного файла/повреждённого индекса. Symlink cache проверяется, если ОС разрешает создать ссылку без дополнительной привилегии. Полный CTest после реализации: 11/11.

## Подэтап 03.1 — Transform и миграция схемы

`scene.unit` проверяет обязательный Transform, default scale, Component ID lookup, принятие конечных значений и отклонение бесконечности. `scene.file.integration` проверяет точный round-trip Transform и Component ID, чтение Astro Scene v1 с default Transform, запись мигрированного документа как v2 с сохранением нового ID, и диагностику неизвестного компонента. Полный регрессионный CTest: 9/9.

## 03.3 результаты

Windows x64, MSVC 14.44, Debug, BGFX D3D11: `cmake --build build/stack-spike --parallel 4` успешно; CTest 11/11 успешно (`build/03.3-build.log`, `build/03.3-ctest.log`). Покрыты SpriteRenderer command validation/Undo/Redo и Scene v3 serialization. `editor.smoke` импортирует собственный PNG, проверяет WIC decode/BGFX texture upload и scene round-trip, плюс запуск UI и project/scene regression. Визуальный pixel-diff эталон не снимается.

## 03.4 результаты

Полная Windows x64/MSVC Debug сборка успешна; CTest 13/13. `prefab.unit` покрывает локальные ID, parent, root и hierarchy cycle validation. `prefab.integration` проверяет file round-trip, nested instantiate, graph cycle rejection без изменения Scene, Scene v4 persistence, command Undo/Redo, prefab source update, сохранение EntityId и override, повторное update после загрузки. См. `build/03.4-build.log` и `build/03.4-ctest.log`. Нет отдельного editor smoke для prefab authoring, так как UI в 03.4 не добавлялся.

## 03.5 результаты

`assets.registry.integration` теперь проверяет graph save/load, cycle rejection, closure, упаковку reachable assets, повторное использование cache, ремонт повреждённого артефакта и различие target tag. Полная MSVC Debug сборка прошла; CTest 13/13. `build/03.5-build.log` и `build/03.5-ctest.log`. Не проверялись межплатформенная сборка, декодирование/перекодирование текстур и визуальные golden images: код сохраняет байты исходных image payload.



## 04.1 результаты

`core.unit` покрывает fixed-step TimeService и paused manual step. `runtime.session.unit` проверяет копирование Scene, неизменность authoring state, переходы, число шагов, Pause/Resume/Step/Stop, ошибки Result и исключения callback. Windows x64/MSVC Debug сборка успешна; полный CTest: 14/14. `editor.smoke` прошёл регрессионно, но Play UI не реализован.

## 04.2 результаты

`input.mapping.unit` проверяет bindings, alternative sources, signed axes, deadzone, digital edge transitions, frame reset, transient mouse delta, invalid controls and device clearing. `input.sdl.adapter.smoke` injects SDL3 key, mouse, wheel, gamepad button/axis, removal, and window-focus-loss events without physical hardware. Windows x64/MSVC Debug build successful; full CTest **16/16**. `editor.smoke` passed regression. Physical keyboard/mouse/controller interaction and non-Windows adapters were not tested.

## 04.3 результаты

`camera.rig.unit` проверяет Custom pose, mode presets, target follow, frame-delta smoothing, zero/invalid delta, bounds, zoom clamp, rotation inheritance, missing target and invalid settings without changing pose. Полный CTest — 17/17; `editor.smoke`/graphics regression прошли, но camera image output отсутствует, так как renderer ещё не подключён.

## 04.4 результаты

`runtime.interaction.integration` проверяет Requested/Activated/Rejected event order/sequence, registered/duplicate/missing behaviors, enabled-state, in/out-of-range activation, callback mutation isolation on Runtime Scene, Result error and thrown exception diagnostics. Полный Windows x64/MSVC CTest — 18/18; editor/graphics smoke прошли regression, но визуальный interaction test не выполнялся.

## 05.1 результаты

`runtime.physics2d.integration` проверяет gravity/fixed-step movement, solid floor correction, velocity stop, Enter/Stay/Exit sequence, trigger overlap without correction, invalid values, static velocity rejection, one-SceneId binding, wrong-scene rejection and authoring/runtime Scene isolation. Полный Windows x64/MSVC CTest — 19/19; editor/graphics smoke regressions passed. Нагрузочное масштабирование, CCD/tunneling scenarios and visual collision rendering are not tested or implemented. Logs: `build/05.1-build.log`, `build/05.1-ctest.log`.

## 05.2 результаты

`runtime.game_ui.unit` проверяет экранные действия, UTF-8/ID validation, HUD CRUD, bool/number/choice settings, диапазоны, choices и typed setting event. `runtime.game_ui.integration` проверяет запуск/пауза/возобновление RuntimeSession через UI model, fixed-step suppression, переходы Settings и возврат в меню без изменения authoring Scene. `runtime.game_ui.visual.smoke` создаёт скрытое SDL3 окно, рисует шесть экранных кадров, проверяет ImGui vertices и сравнивает main-menu BMP с эталоном: MAE ≤1.5/255; не более 1% пикселей могут отличаться более чем на 12/255 по каналу. Полный Windows x64/MSVC CTest с `ASTRO_BUILD_GAME_UI_DEMO=ON` и включёнными graphics smoke: **22/22**. Отдельная чистая headless-конфигурация (без Editor/SDL3/ImGui) собралась с нуля и прошла **18/18** тестов; в ней работают два новых теста backend-neutral модели, а graphics smoke отсутствуют. Другие экраны не имеют pixel-golden эталонов; ручной UX review не выполнялся. Логи: `build/05.2-baseline-build.log`, `build/05.2-baseline-ctest.log`, `build/05.2-configure.log`, `build/05.2-build.log`, `build/05.2-ctest.log`, `build/05.2-headless-configure.log`, `build/05.2-headless-build.log`, `build/05.2-headless-ctest.log`.

## 05.3 результаты

`runtime.puzzle.unit` проверяет границы размера/палитры, детерминированный board, invalid moves без мутации, legal match resolution, cascades, refill, points/level thresholds; также проверяет animation keyframe validation, endpoint clamping и linear interpolation. `runtime.audio.sdl.smoke` открывает SDL3 dummy playback stream и отправляет синтезированный cue. Полная Windows x64/MSVC Debug сборка `build/stack-spike` успешна; CTest **24/24** прошёл (включая `stack_spike.smoke`, `runtime.game_ui.visual.smoke`, `runtime.audio.sdl.smoke`, `editor.smoke`). Чистый headless configure/build без SDL/ImGui прошёл; CTest **19/19**. Базовая проверка перед изменением: 22/22. Физические speakers, visual puzzle interaction, другие audio backends и pixel-golden для игровой доски не проверялись. Логи: `build/05.3-build.log`, `build/05.3-ctest.log`, `build/05.3-headless-configure.log`, `build/05.3-headless-build.log`, `build/05.3-headless-ctest.log`.

## 05.4 результаты

`runtime.save.integration` проверяет explicit field selection/исключения, UTF-8 round-trip, numeric/bool values, slot bounds/metadata, reopen store, repeated load idempotence, profile fingerprint/version и game version rejection without Scene mutation, missing entities/components, bad checksum, unsupported format version, backup fallback and orphan temporary file. Windows test additionally blocks destination replacement by a handle without delete-sharing, then confirms the prior committed slot remains loadable. Baseline MSVC x64 Debug was clean: optional profile CTest **24/24**, headless **19/19**. After implementation, optional profile build and full CTest **25/25**; headless build and CTest **20/20**. No process was killed during actual disk I/O; interruption was simulated by a blocked atomic replacement and an abandoned partial temp artifact. Schema migrations remain deferred; v1 rejects unsupported versions. Checkpoint/respawn behavior is covered by 05.5 below. Logs: `build/05.4-baseline-build.log`, `build/05.4-baseline-ctest.log`, `build/05.4-baseline-headless-build.log`, `build/05.4-baseline-headless-ctest.log`, `build/05.4-build.log`, `build/05.4-ctest.log`, `build/05.4-headless-build.log`, `build/05.4-headless-ctest.log`.

## 05.5 результаты

`runtime.checkpoint.integration` проверяет XY AABB boundary, actor entry/exit, edge-triggering, one-shot consumption, condition rejection/retry after re-entry, duplicate/missing registrations, active checkpoint selection/unregister, fallback respawn, and authoring/runtime Scene isolation. It also verifies SaveSelectedProfile activation requires a valid context, saves only that profile's fields, restores selected values to the Play Scene before assigning the spawn point, and distinguishes PositionOnly from SelectedProfile respawn. Baseline MSVC x64 Debug: optional **25/25**, headless **20/20**. Final Windows x64/MSVC build and CTest: optional **26/26**, headless **21/21**. A separate production configuration with `BUILD_TESTING=OFF` and `ASTRO_BUILD_CHECKPOINT_RUNTIME=ON` compiled `astro_runtime_checkpoint` successfully without SDL/ImGui. No rendered checkpoint area, physical collision trigger, health/inventory component, process death detection, sound, or editor workflow was tested or implemented. Logs: `build/05.5-baseline-build.log`, `build/05.5-baseline-ctest.log`, `build/05.5-baseline-headless-build.log`, `build/05.5-baseline-headless-ctest.log`, `build/05.5-build.log`, `build/05.5-ctest.log`, `build/05.5-headless-build.log`, `build/05.5-headless-ctest.log`, `build/05.5-runtime-only-configure.log`, `build/05.5-runtime-only-build.log`.

## 05.6 результаты

Baseline before changes, Windows x64/MSVC Debug: optional profile CTest **26/26**; headless **21/21**. `ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE` requires Windows x64/MSVC and `ASTRO_BUILD_GAME_UI_DEMO=ON`; it packages only `astro_game_ui_demo.exe`, `README-runtime.txt`, and SDL3/Dear ImGui license texts. `runtime.windows.package.clean_start` creates a ZIP, extracts it to a fresh unique directory, checks these files and launches the extracted executable with `--smoke` from that directory. Post-change Debug package profile CTest: **24/24**, including archive smoke. Post-change headless CTest: **21/21**. A separate Release configuration with `BUILD_TESTING=OFF` generated the ZIP, and the same clean-start script passed against it. Release archive size is 1,652,875 bytes and it contains no DLL entries. Logs: `build/05.6-package-configure.log`, `build/05.6-package-build.log`, `build/05.6-package-smoke.log`, `build/05.6-package-ctest.log`, `build/05.6-package-final.log`, `build/05.6-headless-regression.log`, `build/05.6-headless-final.log`, `build/05.6-release-configure.log`, `build/05.6-release-package.log`, `build/05.6-release-final.log`, `build/05.6-release-smoke.log`.

## 06.1 результаты

Windows x64/MSVC Debug baseline перед изменениями был чистым: optional/editor CTest **26/26**, headless **21/21**. После изменений `cmake --build build/stack-spike --parallel 4` прошёл; полный optional/editor CTest — **28/28** (`build/06.1-editor-ctest.log`). Headless build прошёл, CTest — **23/23** (`build/06.1-headless-final.log`). Отдельная Release-конфигурация с `BUILD_TESTING=OFF`, Editor/SDL выключенными и `ASTRO_BUILD_VISUAL_LOGIC=ON` собрала `astro_visual_logic` (`build/06.1-logic-runtime-build.log`). Unit-тесты проверяют типы, валидацию, лимиты, циклы и удаление узлов; integration — детерминированное исполнение, пошаговый trace, ошибку деления на ноль и неполные графы. `editor.smoke` проверяет отображение панели и вычисление встроенного графа; ручная работа мышью по меню не автоматизирована. Граф не сохраняется, поэтому persistence/regression-тестов формата пока нет. Логи baseline: `build/06.1-baseline-editor.log`, `build/06.1-baseline-headless.log`; итоговая сборка: `build/06.1-build.log`.

## 06.2 результаты

Windows x64/MSVC Debug baseline после локальной фиксации 06.1: editor CTest **28/28**, headless **23/23**. После добавления UI document/editor: editor build успешен, полный CTest **30/30** (`build/06.2-editor-ctest.log`); headless build успешен, полный CTest **25/25** (`build/06.2-headless-ctest.log`). `ui.document.unit` проверяет темы, responsive resolution на разных viewports, bounds/UTF-8/element deletion; `ui.binding.integration` проверяет текстовые и видимые Scene bindings, обновление значения, несовместимые ссылки и отсутствие частичных обновлений при stale reference. `editor.smoke` проверяет создание preview model, responsive coordinates, binding refresh и наличие UI Editor panel в отрисованном окне. Pixel-golden для UI Editor и ручная мышиная UX проверка не выполнялись. Нет save/load тестов, так как UI document file persistence не реализована. Logs: `build/06.2-build.log`, `build/06.2-editor-ctest.log`, `build/06.2-headless-build.log`, `build/06.2-headless-ctest.log`.

## 06.3 результаты

Baseline Windows x64/MSVC Debug до изменений: optional/editor CTest **30/30**, headless **25/25**. После изменений Editor build successful, full CTest **31/31**; headless build successful, full CTest **26/26**. `runtime.animation.unit` checks scalar track editing, strict time ordering, finite/magnitude/count bounds, rejected-edit preservation, sample/seek/play/pause/end-stop. Existing `camera.rig.unit` tests cover preset configuration/follow behavior; `editor.smoke` also verifies Timeline panel rendering and ThirdPerson target/pose setup. A separate Release configuration with `BUILD_TESTING=OFF`, `ASTRO_BUILD_ANIMATION_RUNTIME=ON`, and Editor/game UI disabled built `astro_runtime_animation` without SDL/bgfx/ImGui. On the first full CTest invocation, the existing `stack_spike.smoke` did not receive its synthetic key event (`keys=0`); the isolated retry passed, followed by the full 31/31 pass. No pixel-golden or manual camera-view comparison exists because the renderer is not connected. Timeline-to-Scene binding, persistence and game export are not implemented. Logs: `build/06.3-baseline-editor-ctest.log`, `build/06.3-baseline-headless-ctest.log`, `build/06.3-editor-build.log`, `build/06.3-editor-ctest-first.log`, `build/06.3-stack-spike-retry.log`, `build/06.3-editor-ctest.log`, `build/06.3-headless-build.log`, `build/06.3-headless-ctest.log`, `build/06.3-animation-only-configure.log`, `build/06.3-animation-only-build.log`.

## 06.4 результаты

Windows x64/MSVC Debug baseline before edits: editor CTest **31/31**, headless **26/26**. Rebuilt `build/stack-spike`; final editor CTest **33/33** including `save.editor.unit`, `save.editor.integration`, and the Editor smoke; rebuilt `build/05.3-headless`; final headless CTest **28/28**. Unit coverage checks explicit group membership/rules, field flattening, unsupported properties, missing groups, and checkpoint validation. Integration uses actual `SaveStore` and `CheckpointSystem` in a uniquely created OS temp directory, exercises checkpoint entry and selected-profile restore on a Scene copy, then removes only its owned directory. Editor smoke creates a profile/group/checkpoint, runs the restore preview, and confirms that the Save System panel is rendered. A `BUILD_TESTING=OFF` Release configuration successfully built `astro_save_editor` and its runtime dependencies; full `astro_editor` linking could not run because this dependency cache lacks `bgfxRelease.lib`. A manual mouse-driven walkthrough and visual golden comparison were not performed. Profile/group/checkpoint authoring is not persisted; the restore preview supports a profile-backed test only when `entity.name` is selected for the chosen test actor. The initial integration run caught that Scene property commands use a different property key than Save v1's storage key; the isolated test harness was corrected to mutate the copied Play Scene directly, and both final suites passed. Final logs: `build/06.4-editor-build-final.log`, `build/06.4-editor-ctest-final.log`, `build/06.4-headless-build-final.log`, `build/06.4-headless-ctest-final.log`; production configure/build: `build/06.4-editor-production-build.log`, `build/06.4-save-editor-production-build.log`.

## 2D Play foundation before 06.5

Windows 11 x64/MSVC Debug: `build/stack-spike` rebuilt; Editor CTest **35/35**, including `runtime.render2d.unit` and `runtime.2d.playground.smoke`. The smoke uses SDL3's software renderer, reads back a player-color pixel, synthesizes movement through `InputMapping`, checks one fixed update, pause/no-update, one manual step, and Scene-copy isolation. `build/05.3-headless` rebuilt without the SDL Playground; CTest **29/29**. A separate Release configuration with `BUILD_TESTING=OFF` and `ASTRO_BUILD_RENDER2D=ON` built `astro_runtime_render2d` without SDL/bgfx/ImGui. Pixel verification is a single-color smoke check; no scene asset rendering, text/glyph rendering, GPU performance, full visual golden, or non-Windows backend was tested. Logs: `build/06.5-foundation-editor-build-final.log`, `build/06.5-foundation-editor-ctest-final.log`, `build/06.5-foundation-headless-build-final.log`, `build/06.5-foundation-headless-ctest-final.log`, `build/06.5-foundation-render2d-only-build.log`.

## Hierarchical transforms and project preview follow-up

`runtime.scene.render2d.integration` checks multi-level XY transform composition, including rotated non-uniform parent scale (affine/shear output), negative determinant, local child offset, hidden components, missing assets, invalid pixels-per-unit, and atomic draw-list failure. `runtime.2d.project.preview.smoke` creates an isolated serialized project, invokes the normal Windows-Unicode CLI against a Unicode project path, renders its cached PNG through WIC/SDL, checks path traversal rejection, and verifies SHA-256 stability for its scene, asset source, cache, and registry index. Windows x64/MSVC Debug: Editor/optional CTest **37/37**, headless regression **30/30**. Release production targets `astro_runtime_render2d` and `astro_scene_render2d` build with SDL disabled. Logs: `build/06.7-hierarchy-preview-build.log`, `build/06.7-hierarchy-preview-editor-ctest.log`, `build/06.7-hierarchy-preview-headless-build.log`, `build/06.7-hierarchy-preview-headless-ctest.log`, `build/06.7-hierarchy-preview-renderer-only-build.log`, and `build/06.7-project-preview-security-ctest.log`. WIC rendering was tested on Windows only; no non-Windows decoder or GPU visual golden is covered.

## Editor Scene/Game View renderer integration

Before changes, `build/stack-spike` build succeeded without work and CTest passed **37/37**. After changes, `astro_editor` rebuilt successfully and the full CTest suite passed **37/37**, including `editor.smoke`. The smoke creates an isolated project with a cached PNG SpriteRenderer, exercises a copied RuntimeSession scene, and requires successful Scene View and Game View render frames. In a separate `BUILD_TESTING=OFF` Release configuration, `astro_scene_render2d`, `astro_runtime_sdl_render2d`, and `astro_runtime_sdl_wic_image` all built successfully. View rendering uses an SDL software surface uploaded each frame to bgfx; no visual golden, performance measurement, or full production Release Editor link was run. Logs: `build/06.8-editor-build.log`, `build/06.8-editor-ctest.log`, `build/06.8-production-renderer-targets.log`.

## Scene/Game View CPU baseline

Run `build/stack-spike/editor/astro_editor.exe --benchmark`. It creates an isolated temporary project and synthetic scenes with 1, 100 and 1000 visible SpriteRenderer components sharing the same cached 1x1 PNG. Each view is 960x540; each scenario has 8 warm-up and 40 measured frames. Times are CPU elapsed time for Canvas preparation, SDL software draw, and bgfx texture upload submission (`bgfx::copy` + `updateTexture2D`); they do **not** include GPU completion or full UI composition. 1000 sprites: Scene 7.995 ms p50 / 9.122 ms p95; Game 7.378 / 9.148 ms. Two full view uploads represent 4,147,200 bytes/frame or 237.305 MiB/s at 60 FPS. This synthetic baseline is not a final performance target. CTest after benchmark addition: **37/37**. Logs: `build/06.9-renderer-benchmark-build.log`, `build/06.9-renderer-benchmark-results.log`, `build/06.9-renderer-benchmark-ctest.log`.

## Direct bgfx Scene/Game View path

The same command now benchmarks transient geometry submission into both bgfx framebuffer views. It reports Canvas/vertex preparation and API submission CPU time separately, and reads asynchronous bgfx GPU frame timer samples when the driver exposes them. In the final 1000-sprite run, Scene p50/p95 is **4.809/5.789 ms**; Game is **4.782/5.618 ms**. Paired GPU frame timing is **0.074/0.135 ms p50/p95** (40 samples in this run). No full-frame host upload occurs; transient sprite vertex/index data for two 1000-sprite views is 184,000 bytes/frame. The paired p50 CPU sums to 9.591 ms versus the previous 15.373 ms baseline. This excludes UI composition and vsync, and uses one repeated 1x1 texture on D3D11 Debug. Full CTest: **37/37**. Log: `build/06.10-gpu-renderer-benchmark.log`.

## 06.11 visual and dense-scene checks

`editor.smoke` creates an isolated three-sprite scene with distinct PNG AssetIds: opaque red, half-alpha green and opaque blue. It requests two window sizes during the 90-frame run, then requests a bgfx backbuffer TGA. The test reads the capture and requires at least 8 pixels in each expected color class (BGRA opaque red/blue and green blended over the `#121822` view clear color). Capture and project are deleted from their unique temporary directory after validation. This is a color-predicate regression, not a fixed full-frame golden or per-view diff.

Run `build/stack-spike/editor/astro_editor.exe --benchmark` for 1, 100, 1000, 5000 and 10000 SpriteRenderer entities at 960x540. Runs through 1000 use 8 warm-up + 40 measured frames; 5000/10000 use 4 + 12 to keep stress checks bounded. Result log: `build/06.11-dense-benchmark.log`. Windows x64/MSVC Debug D3D11 at 10k: Scene p50/p95 **132.871/144.285 ms**, Game **130.929/137.712 ms**, paired GPU timer **0.108/0.113 ms**. CPU geometry preparation dominates and misses a 60 FPS budget at that load; results are hardware/build-specific, not approved performance targets.

Release check: `cmake --build build/06.4-editor-production --target astro_scene_render2d astro_runtime_render2d --parallel 4` passed. Full `astro_editor` link was attempted but cannot complete until the local bgfx cache provides `bgfxRelease.lib`; it currently contains only `bgfxDebug.lib`. Logs: `build/06.11-release-build.log` (Editor link blocked), `build/06.11-release-renderer-build.log` (renderer targets passed).

## 06.12 dense-scene transform profiling and Release benchmark

The benchmark prints p50 stage times for Scene composition, camera bounds, vertex/index geometry generation, transient allocation/copy and bgfx submission. Before optimization, 10k Debug Scene composition was 136.750 ms of 142.218 ms total; Game composition was 135.800 ms of 141.326 ms. `Scene::transform()` performed a linear `Scene::find()` for every sprite, creating O(N²) work. After replacing this with a per-call EntityId index and vector-backed cached parent world matrices, and adding exact zero-rotation/axis-aligned math fast paths, the same Debug benchmark reported Scene/Game totals 26.086/24.348 ms p50 at 10k. Existing hierarchy/affine integration tests plus full CTest passed **37/37** (`build/06.12-optimized-debug-ctest.log`).

For a complete local Release build, use an MSVC Developer Command Prompt and first build the already checked-out bgfx dependency solution:

```powershell
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" build\spike-deps\bgfx\.build\projects\vs2022\bgfx.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
cmake --build build\06.4-editor-production --target astro_editor --parallel 4
build\06.4-editor-production\editor\astro_editor.exe --smoke
build\06.4-editor-production\editor\astro_editor.exe --benchmark
```

This produces `bgfxRelease.lib`, `bimgRelease.lib`, `bimg_decodeRelease.lib` and `bxRelease.lib` in the local dependency cache. The final optimized Release D3D11 run at 10k measured Scene **4.913/7.184 ms p50/p95**, Game **4.682/7.240 ms**, paired GPU frame **0.114/0.195 ms**. The sum of view CPU p50 is 9.596 ms; it excludes ImGui composition and vsync. Release Editor smoke passed, but Release CTest was configured off. Results/logs: `build/06.12-baseline-debug-benchmark.log`, `build/06.12-profile-stages.log`, `build/06.12-optimized-debug-benchmark.log`, `build/06.12-final-release-benchmark.log`, `build/06.12-final-release-smoke.log`, and `build/06.12-bgfx-release-dependencies.log`.

## 06.13 representative full-frame Release profile

Run `build\06.4-editor-production\editor\astro_editor.exe --benchmark`. In addition to 1/100/1000/5000/10000 sprite stress profiles, the command creates an isolated temporary project with chunked tiles, alpha overlays and mixed world/effects. Each representative scene uses 24 warm-up and 48 measured editor frames. The full host frame includes SDL event polling, Editor UI construction, Scene and Game framebuffer views, and `bgfx::frame()`; the report also separates Editor CPU before bgfx, Scene/Game CPU, bgfx frame call, BGFX frame interval and host wall time. VSync remains on, so total wall time estimates the observed interactive frame cadence and must not be read as uncapped throughput. BGFX latency counters can be unavailable on the active driver; the BGFX frame timer alone is not a pure GPU execution measurement.

Recorded Windows x64/MSVC Release D3D11 values: host wall p50/p95 **16.672/17.518 ms** chunked tile world, **16.634/17.319 ms** alpha overdraw, **16.652/17.551 ms** mixed world/effects. `editor_cpu_before_bgfx_frame` p50 is **1.89–1.98 ms**, and the combined two-view CPU p50 is **0.26–0.41 ms**. The difference is primarily `bgfx::frame()` waiting under VSync (**14.61–14.76 ms p50**). These generated scenarios have 328–384 sprites and 8 textures; there are no tracked real project scenes in this checkout. Full command output: `build/06.13-release-representative-full-frame.log`.

Post-change Debug validation: Editor target builds, but the all-target build failed while linking `astro_2d_playground` (`LNK1104 astro_runtime_sdl_render2d.lib`; the archive exists under `engine\runtime` while the emitted link command searches relative to the build root). CTest then reported **35/37 passed**: playground smoke could not start because its executable was not produced, and project-preview smoke failed for the same missing executable. The other 35 tests, including Editor smoke, passed. This generated link-path issue blocks a clean all-target Debug regression result; do not count it as a successful 37/37 run. Release Editor build and the complete benchmark command succeeded. Logs: `build/06.13-release-profile-build-final.log`, `build/06.13-release-representative-full-frame.log`.

## 06.14 Debug link fix and real-scene profile availability

The 06.13 CTest issue was corrected in `engine/runtime/CMakeLists.txt`: SDL runtime renderer and WIC adapter targets now build when Editor tests enable the 2D playground fixture, even if standalone `ASTRO_BUILD_2D_PLAYGROUND` remains off. This makes CMake emit the actual target paths under `engine\runtime` instead of bare unresolved library names.

```powershell
cmake -S . -B build/stack-spike
cmake --build build/stack-spike --parallel 4
ctest --test-dir build/stack-spike --output-on-failure -j 4
```

On Windows x64/MSVC Debug, configure and all-target build passed; CTest passed **37/37** in **6.23 seconds**. Logs: `build/06.14-debug-full-build.log`, `build/06.14-debug-full-ctest.log`.

No serialized project scene or project file (`*.scene`, `*.astro-scene`, `*.astroproj`, `*.astro-project`) was found in the checkout or under `C:\Users\zheka\Desktop\gpt`. Consequently, this step did not claim a real-project profile and did not substitute the generated benchmark scenes. To measure real content, make the project directory or scene file available in the workspace and identify the entry scene.

## 06.5 Puzzle snapshot unit and regression

`runtime.puzzle.unit` now round-trips a `PuzzleGame` snapshot, checks board metadata and progress, resumes a second copy and compares legal/rejected move outcomes and resulting random refills, and rejects an incomplete snapshot. The snapshot includes the `std::mt19937` state so restore does not restart the refill sequence from the initial seed. On Windows x64/MSVC Debug, target build and focused unit passed; full build and CTest passed **37/37** in **9.05 seconds**. Logs: `build/06.5-puzzle-snapshot-build.log`, `build/06.5-full-build.log`.

This verifies only in-memory runtime snapshot/restore. Project serialization, SaveStore slots/profile selection, Editor template creation and playable GUI smoke remain future T01 work.

## 06.6 SaveStore custom-field compatibility

`runtime.save.integration` checks that explicit `string`/`double`/`bool` custom profile keys serialize to `ASTRO_SAVE 2`, round-trip (including UTF-8), and contribute to slot preview counts. It rejects unsafe/duplicate profile keys, missing/extra keys, type mismatch and non-finite values. It verifies a changed custom selection is rejected without changing the Scene or caller map, and the existing Scene-only save/load coverage continues to exercise v1. On Windows x64/MSVC Debug, focused save integration passed **1/1**; full build/CTest for this slice: **37/37**. Logs: `build/06.6-save-custom-fields-focused.log`, `build/06.6-save-custom-fields-build.log`, `build/06.6-save-custom-fields-ctest.log`.

The 06.6 test did not cover PuzzleSnapshot serialization, schema migration, editor authoring, or process-restart smoke. PuzzleSnapshot-to-slot coverage is recorded below for 06.7.

## 06.7 PuzzleSnapshot SaveStore integration

`runtime.puzzle.save.integration` uses the optional `astro_runtime_puzzle_save` adapter. It saves and restores a progressed board including score, level, dimensions, tile palette and RNG stream through the explicitly selected `puzzle.snapshot` field. It also round-trips a selected Scene property and another custom value, compares the deterministic next move/refill, rejects an unselected key, wrong selected type, unsupported snapshot version and malformed tile payload, and verifies payload failure does not publish the candidate Scene. Windows x64/MSVC Debug focused result: **1/1 passed**; baseline before changes was **37/37**. Full post-change build/CTest results are recorded in [report 06.7](reports/06.7-puzzle-save-adapter.md).

The payload is version 1 with no migration path yet. This does not test app process restart/export packaging or editor authoring.

## 06.15 Puzzle Game View smoke

`editor.smoke` first captures the existing Scene/Game sprite visual regression, then starts the optional Puzzle mode in Game View. It verifies that the board is drawable, queues a deterministic legal neighboring-cell swap, advances it through a fixed runtime step, pauses and confirms queued input does not mutate Puzzle state, executes exactly one `Step`, resumes, and checks that the editor's authored Scene remains unchanged. The graphical test runs against bgfx/D3D11 on the Windows test machine; it does not verify other render backends or physical mouse input.

On Windows x64/MSVC Debug, the focused Editor smoke passed **1/1** in **3.28 seconds**; full CTest passed **38/38** in **7.98 seconds**. Build and test logs: `build/06.15-puzzle-game-view-build.log`, `build/06.15-puzzle-game-view-focused.log`, `build/06.15-puzzle-game-view-full-ctest.log`. Commands are recorded in [report 06.15](reports/06.15-puzzle-game-view.md).

## 06.16 Puzzle SaveStore Game View smoke

The same `editor.smoke` opens a SaveStore in its temporary smoke project, uses the Editor's explicitly selected `puzzle.snapshot` profile, saves the current progressed board, changes it with a legal move, loads the selected slot and checks exact PuzzleSnapshot equality before continuing the Pause/Step test. This exercises the same manual save/load handlers used by the UI. The test directory is removed with the smoke-owned temporary project; it never accesses the user's SDL preference directory.

On Windows x64/MSVC Debug, full build passed; focused `editor.smoke` passed **1/1** in **3.61 seconds**; full CTest passed **38/38** in **7.95 seconds**. Logs: `build/06.16-puzzle-save-view-build.log`, `build/06.16-puzzle-save-view-focused.log`, `build/06.16-puzzle-save-view-full-build.log`, `build/06.16-puzzle-save-view-full-ctest.log`. See [report 06.16](reports/06.16-puzzle-save-game-view.md).

## 06.17 Project-scoped Puzzle save settings smoke

`editor.smoke` checks that a missing sidecar produces stable project-specific defaults, two project roots resolve to distinct subdirectories, UTF-8 profile/path settings round-trip through `.astro/puzzle-save.settings`, and a `../outside` update is rejected without replacing the previously valid settings. The Game View's Puzzle Save Settings section is opened and asserted during smoke. The following save/load check uses the project profile and the isolated smoke-owned slot directory, then verifies PuzzleSnapshot restore and existing Pause/Step behavior. The user SDL preference directory is never touched by smoke.

Baseline on Windows x64/MSVC Debug before changes: build passed, CTest **38/38** in **7.88 s**. After changes: Editor build and full build passed; focused `editor.smoke` **1/1** in **3.43 s**; full CTest **38/38** in **8.01 s**. Logs: `build/06.17-project-puzzle-settings-baseline-build.log`, `build/06.17-project-puzzle-settings-baseline-ctest.log`, `build/06.17-project-puzzle-settings-build.log`, `build/06.17-project-puzzle-settings-focused.log`, `build/06.17-project-puzzle-settings-full-build.log`, `build/06.17-project-puzzle-settings-full-ctest.log`. See [report 06.17](reports/06.17-project-puzzle-save-settings.md).

## 06.18 Puzzle authored level tests

`runtime.puzzle.unit` checks exact initial-board creation and invalid tile rejection. `runtime.puzzle.level.integration` writes/reads a versioned project level with UTF-8 name and exact board equality; it rejects traversal IDs, invalid UTF-8, incomplete boards, malformed files and unknown versions. `editor.smoke` keeps existing SaveStore/Pause/Step coverage, opens the authoring panel, round-trips an isolated project level and launches its exact board. Authored-level progress Save/Load is intentionally disabled while its level ID is not part of the slot profile.

Baseline Windows x64/MSVC Debug build was already current and CTest passed **38/38** in **72.75 s**. After changes, full build passed, focused Puzzle unit/file integration/Editor smoke passed **3/3** in **3.15 s**, and full CTest passed **39/39** in **6.13 s**. Logs: `build/06.18-puzzle-level-build.log`, `build/06.18-puzzle-level-focused-ctest.log`, `build/06.18-puzzle-level-full-ctest.log`. See [report 06.18](reports/06.18-puzzle-level-authoring.md).
## 06.19 Authored-level progress isolation

`runtime.puzzle.save.integration` verifies the selected identity key and snapshot can be saved; a wrong expected level ID returns `VersionMismatch` without changing the caller Scene; matching load restores the snapshot and does not expose the internal identity field as arbitrary custom state; unsafe IDs are rejected. `editor.smoke` saves an authored level, verifies the level-specific `PuzzleLevels/<ID>` namespace, rejects a different active ID without changing the board, and restores the matching slot. These tests use only a unique smoke-owned temporary directory; the user's SDL preference directory is not touched. On Windows x64/MSVC Debug, focused tests passed **2/2** and full CTest passed **39/39** in **5.35 s**. See [06.19 report](reports/06.19-level-scoped-progress.md) for exact commands and logs.
## 06.20 Puzzle level catalog

`runtime.puzzle.level.integration` covers deterministic catalog order, valid/malformed entries, create-only collision behavior, duplicate content and source preservation, ID rename, removal of the old path only after publication, and rename collision preservation. `editor.smoke` uses a temporary project to exercise catalog refresh/open plus duplicate and rename before launching the level and continuing the existing per-level save/load smoke. No user project is modified. Windows x64/MSVC Debug focused result: **3/3** in **3.36 s**; full CTest: **39/39** in **6.99 s**. Commands and logs are recorded in [06.20 report](reports/06.20-puzzle-level-catalog.md).

## 06.22 Puzzle campaign parser and project-file API

`runtime.puzzle.campaign.integration` validates UTF-8 parse/serialize round-trip, explicit presentation order and rule order, version/condition/record/count/index strictness, malformed UTF-8, file-size bound, invalid and duplicate IDs, start membership, threshold validity/order, defaults and reachability/cycle rules. Project integration creates real temporary Puzzle levels and verifies campaign read/write, missing-reference rejection, ID/filename match, traversal rejection, and replacement. Related regression tests: `runtime.puzzle.unit`, `runtime.puzzle.level.integration`, and `runtime.puzzle.save.integration`.

Atomic-write failure injection, level-rename reference protection, and a v1 migration (none is defined) remain untested. The test owns and removes a unique temporary directory. See [06.22 report](reports/06.22-puzzle-campaign-parser.md).

## 06.23 Puzzle campaign runtime transition resolver

The campaign integration test covers score resolution at the highest and next lower threshold, DEFAULT fallback for a score below all thresholds, terminal END, rejection of a completed ID outside the campaign, and one-edge semantics (resolving a destination requires a separate call). Resolver tests are pure data checks; they do not launch/load levels. Editor authoring and automatic progression orchestration remain unimplemented. Windows x64/MSVC Debug focused Puzzle tests passed **4/4**; full CTest passed **40/40** in **9.00 s**. Exact commands: [06.23 report](reports/06.23-puzzle-campaign-resolver.md).

## 06.24 Puzzle campaign runtime session

`runtime.puzzle.campaign.integration` creates real authored level files and a campaign in a unique temporary project. It checks START loading, invalid early completion, move forwarding and target completion, score-threshold transition into the selected level, DEFAULT loading, and END behavior (retain final board/score, reject more moves/completion). It also removes the next-level test asset after session start and verifies a failed load preserves the current completed game; after restoring the fixture, retry loads the target. A new session explicitly starts at START after END. This does not load engine Scenes, write progress, or exercise Editor UI. Exact focused/full commands and results: [06.24 report](reports/06.24-puzzle-campaign-session.md).

## 06.25 Puzzle campaign Game View smoke

`editor.smoke` creates two authored levels and a campaign under its unique temporary project, launches it through the same campaign-start handler used by the Game View button, queues legal tile input, and advances it through the `RuntimeSession` fixed-step callback. The smoke invokes the shared Continue handler to verify next-level loading, then verifies `DEFAULT END`, final-board retention, and rejection of further moves. Campaign board rendering must occur in the view. The smoke is programmatic and does not synthesize physical mouse clicks on ImGui buttons. Campaign file authoring and progress persistence were added in later substages 06.27 and 06.31.

Windows x64/MSVC Debug: `astro_editor` build passed; focused `editor.smoke` passed **1/1** in **3.12 s**; all-target build reported `ninja: no work to do`; full CTest passed **40/40** in **5.79 s**. Exact report and commands: [06.25](reports/06.25-puzzle-campaign-game-view.md).

## 06.26 Campaign browser and link diagnostics

`runtime.puzzle.campaign.integration` verifies sorted catalog enumeration of valid and malformed campaign files, per-reference valid/missing status, and retention of a structurally valid campaign definition when a referenced level asset is absent. Existing strict `PuzzleCampaignFile::read`/runtime start behavior continues to reject missing references. `editor.smoke` opens the browser against a temporary project and renders a selected broken link diagnostic; it also exercises campaign launch/progression independently. No project content is modified by enumeration.

Windows x64/MSVC Debug: focused `runtime.puzzle.campaign.integration` and `editor.smoke` passed **2/2** in **3.04 s**; all-target build reported `ninja: no work to do`; full CTest passed **40/40** in **7.94 s**. Exact commands: [06.26 report](reports/06.26-puzzle-campaign-browser.md).

## 06.27 Campaign authoring and level rename links

`runtime.puzzle.level.integration` creates a valid campaign referencing an authored level and verifies rename refusal names the campaign and preserves source/destination files. After the test-owned campaign is updated away, rename succeeds. A malformed campaign also blocks rename, and the source stays intact. `runtime.puzzle.campaign.integration` remains the strict campaign parser/file regression. `editor.smoke` checks the campaign browser and authoring section are drawn and retains the earlier missing-link and Game View progression checks. This smoke is programmatic and does not synthesize ImGui clicks; UI save interaction is not end-to-end click tested.

Focused Debug tests passed **3/3** in **6.40 s**; the final full Debug CTest passed **40/40** in **5.35 s** after the last UI adjustment. Build and exact commands are recorded in [06.27 report](reports/06.27-puzzle-campaign-authoring-rename.md). Tests use an isolated temporary project; no user campaign or level files are changed.
# 06.28 Локализация Windows Editor

`editor.localization.unit` проверяет English fallback, наличие русских переводов у прямых видимых строк ImGui, стабильный ID окна, чтение/запись обоих языков и отказ на неизвестной версии/языке. `editor.smoke` запускает Editor на русском и проверяет, что окно настроек отрисовано. Smoke не изменяет реальный SDL user preference. Windows x64/MSVC Debug полный CTest: **41/41** за **6.31 s**; полный лог: `build/localization-full-ctest.log`. Это не проверка перевода сообщений движка, драйвера, SDL Game UI demo или текста пользователя. Руководство: [LOCALIZATION.md](LOCALIZATION.md), подробности: [отчёт](reports/06.28-editor-russian-localization.md).

# 06.29 Runtime UI и диагностики

`runtime.game_ui.localization.unit` проверяет стабильные `en`/`ru`, переводы меню/настроек, известные ошибки GameUiModel/adapter, безопасное копирование произвольного UTF-8 string_view и сохранение неизвестного текста с русской меткой. Изменённый `runtime.game_ui.visual.smoke` запускает прежний английский эталон и программно переключает экран Settings на русский и обратно. `editor.localization.unit` проверяет русскую метку неизвестной диагностики, кириллический путь и отсутствие повторной метки на уже русской строке. Focused Debug CTest: **4/4** за **5.41 s** до последней Unicode-path проверки; итоговый полный Debug CTest: **42/42** за **8.66 s**. Точный лог и детали: [отчёт 06.29](reports/06.29-runtime-and-diagnostics-localization.md).

# 06.30 Runtime language preference

`runtime.game_ui.localization.unit` round-trips English/Russian preferences under a Unicode directory, defaults a missing file to English, rejects malformed versions and unsupported language enum values, and checks translated diagnostic templates retain system details. `runtime.game_ui.visual.smoke` exercises the actual setting-change event writer in a unique temporary directory, reads back both language selections and removes only the directory it created. The smoke must not read or overwrite the real SDL user preference. Full results are in [report 06.30](reports/06.30-runtime-language-persistence.md).

## 05.7 — Puzzle в runtime Game UI

`runtime.game_ui.puzzle.smoke` starts the actual SDL/ImGui demo with a hidden window, starts `RuntimeSession`, rejects a non-adjacent selection without changing score, finds a legal move deterministically on copied board state, and submits that pair through the demo's tile selection handler. It checks the Gameplay render generated draw vertices, the existing HUD score matches PuzzleGame, Pause/Resume/return-to-menu transition correctly, and the authoring Scene remains unchanged. `runtime.game_ui.localization.unit` checks the Russian board instruction; the previous `runtime.game_ui.visual.smoke` remains a regression for the menu golden. The smoke does not synthesize OS mouse events or verify a puzzle-board image golden. Windows x64/MSVC Debug full build succeeded; final CTest **47/47** in **7.17 s**. Logs: `build/05.7-debug-full-build.log`, `build/05.7-debug-full-ctest.log`; implementation details: [report 05.7](reports/05.7-puzzle-game-ui.md).

## 05.8 — Authored campaign in standalone Game UI

`runtime.game_ui.campaign.smoke` launches the checked-in project at `samples/ПазлКампания` through the Unicode command-line path, starts a `PuzzleCampaignSession`, queues accepted tile selections and advances them through `RuntimeSession::tick`. It reaches the first target, verifies the explicit continuation loads the authored `finale` level, reaches the second target, follows `END`, and checks the final level and score remain available. The existing `runtime.puzzle.campaign.integration` remains the deeper test for threshold/default resolution and failed-file-load preservation. The smoke does not synthesize OS mouse clicks or persist campaign progress. Baseline before changes: Debug CTest **47/47** (6.12 s). Final Windows x64/MSVC Debug full build succeeded; CTest **48/48** (7.68 s). Focused Puzzle/campaign/Game UI/localization tests: **4/4**. Direct smoke output: `[game-ui-campaign-smoke] start=intro next=finale end=retained score=180 ok`. Logs: `build/05.8-baseline-build.log`, `build/05.8-baseline-ctest.log`, `build/05.8-debug-full-build.log`, `build/05.8-debug-full-ctest.log`; details: [report 05.8](reports/05.8-runtime-puzzle-campaign.md).

## 05.9 — Manual standalone campaign progress slots

`runtime.puzzle.campaign.save.integration` uses isolated Unicode project and user-data directories. It checks explicit profile selection, midlevel deterministic snapshot round-trip, selected Scene field restoration, wrong campaign/profile-schema rejection without partial Scene publication, pending Continue restoration, transition to a second authored level, terminal END round-trip, and changed authored board-rule rejection. `runtime.game_ui.campaign.smoke` exercises the standalone host's actual SaveStore path with the checked-in read-only sample: save/restore a target-complete first level, continue, reach END, then save/restore END. Both tests remove only their own temporary directories. The smoke does not synthesize OS mouse clicks or run a separately launched restart process. Profile migration is not implemented.

## 06.31 — Editor Game View campaign progress slots

`editor.smoke` drives the Game View campaign handlers against a unique smoke project: it starts a campaign, manually saves slot 4, changes progress through a legal fixed-step move, verifies that loading an empty slot fails without changing progress, then loads slot 4 and compares the restored Puzzle snapshot and campaign level. It also checks the expected project-specific user-data path. `runtime.puzzle.campaign.save.integration` remains the deeper adapter/format test for campaign identity, level, END, authored-rule validation and non-mutating failure behavior. The smoke exercises the same handlers as the UI but does not synthesize physical ImGui clicks.

Windows x64/MSVC Release baseline before changes: complete build succeeded; CTest **52/52** in **54.99 s**. After changes, `astro_editor`, `astro_editor_localization_tests`, and `astro_puzzle_campaign_save_tests` built successfully; focused `editor.smoke`, `editor.localization.unit`, and `runtime.puzzle.campaign.save.integration` passed **3/3** in **3.64 s**. Full post-change Release build succeeded and full CTest passed **52/52** in **31.42 s**. Logs: `build/06.31-baseline-release-build.log`, `build/06.31-baseline-release-ctest.log`, `build/06.31-release-build.log`, `build/06.31-final-release-build.log`, and `build/06.31-final-release-ctest.log`.

## 06.33 — Campaign save namespace identity

The `editor.smoke` fixture verifies `resolvePuzzleCampaignSaveDirectory` without creating save files: a campaign ID change resolves to a separate path, an unsafe ID is rejected, and a copied Unicode `.astro/puzzle-save.settings` sidecar resolves to the same namespace as its source. The fixture deletes only its own relocation directory and never touches SDL user data. `runtime.puzzle.campaign.save.integration` remains the adapter regression; this substage adds no migration/copy operation.

Pre-change Release baseline: `astro_editor`/campaign save test targets were up to date, and `editor.smoke` plus `runtime.puzzle.campaign.save.integration` passed **2/2**, **3.92 s**. Final focused Release tests passed **2/2**, **3.37 s**. The full Release build succeeded (`ninja: no work to do` after target rebuild); full CTest passed **52/52**, **53.64 s**. Logs: `build/06.33-baseline-build.log`, `build/06.33-baseline-ctest.log`, `build/06.33-release-build.log`, `build/06.33-release-ctest.log`, `build/06.33-final-release-build.log`, `build/06.33-final-release-ctest.log`.

## 06.32 — Live campaign save/load controls in Editor

Manual Windows x64/MSVC Release UI verification used a unique copied sample project under `%TEMP%`, leaving `samples/ПазлКампания` untouched. The actual Editor window started `starter`, saved slot 0, accepted a legal adjacent swap (score **0→180**), and loaded slot 0; the UI returned to `intro`, score 0, and the original tile arrangement. The `slot-0.astro-save` file was confirmed under the project's hashed SDL user-data path, outside project files. The first UI observation found the default Game View too small, with campaign Save/Load controls clipped. The interactive default was increased and the action row reflowed. The first post-edit smoke run failed its pixel-color check after the unconditional size override changed capture geometry; size override is now omitted for `--smoke` and `--benchmark`.

Before edits, Release target build was current and the three focused tests passed **3/3** in **3.46 s**. After the final layout fix, Release build succeeded and focused `editor.smoke`, `editor.localization.unit`, and `runtime.puzzle.campaign.save.integration` passed **3/3** in **3.48 s**. The complete Release build succeeded (`ninja: no work to do` after target rebuild); full CTest passed **52/52** in **50.85 s**. Logs: `build/06.32-baseline-build.log`, `build/06.32-baseline-ctest.log`, `build/06.32-release-build.log`, `build/06.32-release-ctest.log`, `build/06.32-final-release-build.log`, and `build/06.32-final-release-ctest.log`.

Baseline before edits: Windows x64/MSVC Debug build succeeded and CTest **48/48** (6.42 s), logs `build/05.9-baseline-build.log` and `build/05.9-baseline-ctest.log`. Final commands and full regression results are recorded in [report 05.9](reports/05.9-campaign-progress-save.md).

## 05.6 package refresh after 05.9

This earlier refresh packaged the executable, README and SDL3/Dear ImGui license texts without project files. Its results are retained in [report 05.6 refresh](reports/05.6-package-refresh-after-05.9.md); the later 05.10 package is self-contained for the checked-in example campaign.

## 05.10 — Self-contained Windows runtime campaign package

Baseline before changes: Release build was up to date; package-profile CTest **45/45** in **3.47 s**. The new Release package-profile CTest passed **45/45** in **8.22 s**; focused `runtime.windows.package.clean_start` passed **1/1** in **1.24 s**. Its clean extraction checked executable, relative launcher, README, license texts, and the campaign plus both level files, then ran menu smoke and the bundled campaign save/restore smoke. The exact produced ZIP was also extracted to a separate clean directory; its campaign run reported `save=restore-pending end=save-restore`, and the `.cmd` launcher passed manually when invoked with a different current working directory. The source sample was not modified by test runs. CTest invokes the packaged executable directly for reliable process argument handling; the launcher behavior has a separate manual check. Full commands, artifact SHA-256, paths and logs are in [report 05.10](reports/05.10-self-contained-campaign-package.md).

## 09.1 Local AI Editor API

`scene.command.integration` now checks API version 1.0, deterministic catalog ordering, exact registered Scene command count and metadata fields, plus typed validate/execute through the API facade and the existing Undo/Redo/audit path. Before changes, Debug build was up to date and full CTest passed **42/42** in **7.45 s**. After changes, `cmake --build build\stack-spike --target astro_scene_command_tests --parallel 4` passed and focused CTest `-R "^scene\.command\.integration$"` passed **1/1**. Full regression CTest is run after documentation updates; final result appears in [report 09.1](reports/09.1.md). The test exercises the in-process typed API only; there is no JSON/network transport or security-boundary test.

# 04.12 C# Editor approval gate

The Editor-only `CSharpPlayApproval` captures the current project, Scene, enabled script components, registered AssetIds/types/source SHA-256, C# build fingerprint, and output file paths/SHA-256 values. A prompt is shown before starting ordinary isolated Play. Allow recaptures through AssetRegistry and accepts only an identical snapshot; Cancel and invalid/missing/stale bindings do not grant. Unit coverage verifies disabled scripts are excluded, enabled scripts fail closed when no build exists, empty snapshots are rejected, source/output changes invalidate approval, and revoke clears state. `assets.registry.integration` also captures and approves a valid Scene binding against the test's fresh registered build and verified output hashes. `editor.smoke` and `editor.localization.unit` cover Editor startup/visible translation catalog, but do not synthesize a click through the modal. No C# host load/callback occurs in this stage.

Windows x64/MSVC Debug and Release CTest results are recorded in [04.12 report](reports/04.12-csharp-editor-approval.md).

## 04.13 — C# RuntimeSession lifecycle

`runtime.session.unit` verifies lifecycle order, authored Scene isolation, failed-OnCreate rollback, and reported OnDestroy failures. `runtime.csharp_host.integration` compiles and loads the real sample project binding, verifies the per-unique-asset approval recheck, enabled/disabled component scheduling, OnCreate/FixedUpdate/OnDestroy effects, startup refusal and isolation. `editor.smoke` exercises the standard Editor startup with the optional C# runtime built; it does not synthesize clicks through the approval modal.

Verified on Windows x64/MSVC Release with Editor, Game UI demo, Stack Spike and `ASTRO_BUILD_CSHARP_RUNTIME=ON`: full CTest **45/45** in **39.51 s**; focused lifecycle/approval/localization/Game UI/Editor smoke **7/7** in **4.72 s**. Full Release command (from VS x64 Developer environment):

```powershell
ctest --test-dir build/04.8-release-clean -C Release --output-on-failure
```

Debug with `ASTRO_BUILD_CSHARP_RUNTIME=OFF`, Editor/Game UI/Stack Spike ON: full CTest **43/43** in **15.62 s**. A Debug build with the optional C# host enabled fails to link the SDK-pack `libnethost.lib` because MSVC reports `_ITERATOR_DEBUG_LEVEL` 0 versus 2. Thus the C# host integration is verified in Release only; other platforms and sandboxing are not verified. Build/test logs: `build/04.13-release-rebuild.log`, `build/04.13-release-focused-ctest.log`, `build/04.13-release-ctest.log`, `build/04.13-debug-build.log`, `build/04.13-debug-ctest.log`.

## 04.14 — ScriptBehavior Editor authoring

`scene.command.integration` covers the three typed script component commands, zero/malformed payload rejection, multiple bindings, disabled-by-default, enable/disable, and add/remove Undo/Redo preserving ComponentId and exact component-list order. `editor.smoke` checks the Inspector authoring section is rendered, creates an isolated disabled test binding through `script_behavior.add`, saves and reloads Scene v5, and checks the binding. `editor.localization.unit` checks all new visible strings have Russian translations. `runtime.csharp_host.integration` remains in the Release focused regression to confirm the adjacent lifecycle still passes.

Baseline before edits: Debug full CTest **43/43** (13.95 s); Release+C# full CTest **45/45** (43.76 s). After edits, MSVC x64 Debug build passed; focused tests **3/3** (7.26 s); full CTest **43/43** (27.06 s). Release Editor+C# host build passed; focused tests **4/4** (4.27 s); full CTest **45/45** (46.57 s).

Commands from Visual Studio 2022 x64 Developer environment:

```powershell
cmake --build build/04.8-debug-clean --config Debug
ctest --test-dir build/04.8-debug-clean -C Debug --output-on-failure `
  -R 'scene\.command\.integration|editor\.localization\.unit|editor\.smoke'
ctest --test-dir build/04.8-debug-clean -C Debug --output-on-failure
cmake --build build/04.8-release-clean --config Release
ctest --test-dir build/04.8-release-clean -C Release --output-on-failure `
  -R 'runtime\.csharp_host\.integration|scene\.command\.integration|editor\.localization\.unit|editor\.smoke'
ctest --test-dir build/04.8-release-clean -C Release --output-on-failure
```

The Editor smoke does not automate mouse clicks or exercise a live-project registered-script combo. It verifies the same command path used by the UI, visible Inspector section, and Scene v5 persistence. Adding a disabled reference is allowed for repair/authoring diagnostics; enabling from Inspector requires an exact current registry/build match. The command layer itself has no project Registry dependency, and Play approval remains the runtime execution boundary. Logs: `build/04.14-{baseline-debug-build,baseline-debug-ctest,baseline-release-build,baseline-release-ctest,debug-build,debug-focused-ctest,debug-ctest,release-build,release-focused-ctest,release-ctest}.log`.

## 04.18 — C# metadata and isolation design assessment

Documentation-only assessment; no code or tests were added. Before changes, Windows x64 Debug and Release builds succeeded and the full CTest suites passed: Debug **45/45** (9.67 s), Release **47/47** (37.74 s). Logs: `build/04.18-debug-before-{build,ctest}.log` and `build/04.18-release-before-{build,ctest}.log`. These are baseline results, not tests of a new validator or sandbox. Future worker acceptance tests are listed in [report 04.18](reports/04.18-csharp-metadata-isolation-assessment.md).

## 04.19 — C# metadata-validator worker prototype

Standalone command used for the focused test (offline NuGet sources):

```powershell
$repo = (Get-Location).Path
pwsh -NoProfile -File tests/integration/runtime/CSharpMetadataValidatorTests.ps1 `
  -RepositoryRoot $repo `
  -DotnetRoot (Join-Path $repo 'build/04.5-dotnet-sdk') `
  -ValidatorProject (Join-Path $repo 'tools/csharp/metadata-validator/AstroEngine.CSharpMetadataValidator.csproj') `
  -WorkParent (Join-Path $repo 'build/04.19-focused-tests')
```

The script builds a fixture and worker using local .NET SDK 10.0.404, then tests valid and invalid ABI bindings, Unicode type lookup, unknown type, malformed and >64 MiB PE rejection, absence of module initializer execution, AppContainer denial of an outside sentinel and loopback connection, and Job Object timeout termination. The final focused run passed. Full Windows x64 regression commands:

```powershell
cmake --build build/04.8-debug-clean
ctest --test-dir build/04.8-debug-clean --output-on-failure
cmake --build build/04.8-release-clean
ctest --test-dir build/04.8-release-clean --output-on-failure
```

Results after implementation: Debug **45/45** (7.76 s); Release+C# **48/48** (47.51 s), including `runtime.csharp_metadata_validator.integration` (20.18 s). The full CTest logs are `build/04.19-debug-{build,ctest}.log` and `build/04.19-release-{build,ctest}.log`. This checks the prototype on this Windows x64 host; it is not a general AppContainer/hostile-PE security certification. More details: [report 04.19](reports/04.19-csharp-metadata-validator-worker.md).

## 04.20 — Project-panel C# metadata diagnostics

The Windows-only Editor path test covers canonical project assembly path, Unicode project/type names, stale builds, traversal, unregistered bindings, and rejection of a DLL symlink when the host permits creating one. The validator integration additionally calls `inspect-many` with valid, ABI-invalid and absent types in one worker session. Release Editor smoke asserts that the metadata diagnostics panel is present when the validator is built. It does not click the button or inspect a real project binding interactively.

Run from a PowerShell session with Visual Studio Build Tools installed; the Developer Command Prompt initializes the MSVC and Windows SDK include paths:

```powershell
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
cmd /s /c "`"$vsDevCmd`" -arch=x64 && cmake --build build/04.8-debug-clean -j 8"
ctest --test-dir build/04.8-debug-clean --output-on-failure
cmd /s /c "`"$vsDevCmd`" -arch=x64 && cmake --build build/04.8-release-clean -j 8"
ctest --test-dir build/04.8-release-clean --output-on-failure
```

On this Windows x64 host, after the final 04.20 code: Debug build succeeded and full CTest **46/46** passed (13.25 s); Release+C# build succeeded and full CTest **49/49** passed (62.68 s). Release included `runtime.csharp_metadata_validator.integration` (26.53 s), the new path-policy unit test, and Editor smoke. Before this substage the same build trees had Debug **45/45** and Release **48/48** passing. Captured logs: `build/04.20-debug-{build,ctest}.log` and `build/04.20-release-{build,ctest}.log`. Results are specific to this Windows host; UI click automation, non-Windows behavior, adversarial PE security certification, and large-project latency are not covered. See [report 04.20](reports/04.20-csharp-project-metadata-diagnostics.md).

## 04.21 — C# metadata batch profile

The metadata integration test builds a temporary Unicode fixture with 64 ABI-compatible managed types, verifies all 64 in one isolated request, checks the response remains below 32 KiB, and verifies that a 65-type request is rejected before worker staging. The Editor unit test covers splitting a 256-type plan into 64-type batches and the 8 KiB UTF-8 name budget.

To reproduce the Release benchmark (generates a dependency-free temporary 256-type ABI fixture under `build`, then measures full CLI/worker wall time including runtime staging):

```powershell
pwsh -NoProfile -File tools/csharp/metadata-validator/Benchmark.ps1 `
  -DotnetRoot build/04.5-dotnet-sdk `
  -ValidatorAssembly build/04.8-release-clean/tools/csharp/metadata-validator/publish/AstroEngine.CSharpMetadataValidator.dll `
  -WorkParent build/04.21-benchmark-final `
  -Repetitions 3
```

The synthetic fixture isolates validator overhead and resembles the managed binding ABI; it is not a full game project and excludes project dependencies. A one-type check of the actual `AstroEngine.CSharpSample.SceneMotion` assembly was also measured separately. Benchmark output and workspaces are under `build/04.21-*`.

Results on this Windows x64 host: pre-change Debug **46/46** (10.77 s), Release+C# **49/49** (61.30 s); final Debug **46/46** (10.24 s), Release+C# **49/49** (65.63 s). Release metadata integration passed in 29.06 s and Editor smoke passed in 2.99 s. The generated 64-type Unicode fixture passed with every type valid; 65 types were rejected before worker creation. The release benchmark medians for 1/16/64/128/256 types were **1.214/1.365/1.346/2.568/5.525 s** across 1/1/1/2/4 worker batches. The actual one-type sample check median was **1.016 s** over three runs. Detailed logs and limits: [report 04.21](reports/04.21-csharp-metadata-batch-performance.md).

Captured CTest logs: `build/04.21-debug-before-ctest.log`, `build/04.21-release-before-ctest.log`, `build/04.21-debug-ctest.log`, `build/04.21-release-ctest.log`. Build logs: `build/04.21-debug-before-build.log`, `build/04.21-release-before-build.log`, `build/04.21-debug-build.log`, and `build/04.21-release-build.log`.

## 04.22 — Доступный C# sample и отчёт 128 KiB

Фактический доступный project assembly содержит один тип. Команда для пяти Release-замеров:

```powershell
$dotnet = 'build/04.5-dotnet-sdk/dotnet.exe'
$validator = 'build/04.8-release-clean/tools/csharp/metadata-validator/publish/AstroEngine.CSharpMetadataValidator.dll'
$assembly = 'build/04.8-release-clean/engine/runtime/csharp_sample/managed/AstroEngine.CSharpSample.dll'
1..5 | ForEach-Object {
  $timer = [Diagnostics.Stopwatch]::StartNew()
  & $dotnet $validator inspect --dotnet-root 'build/04.5-dotnet-sdk' `
    --assembly $assembly --type 'AstroEngine.CSharpSample.SceneMotion'
  $timer.Stop()
  "elapsedMs=$([Math]::Round($timer.Elapsed.TotalMilliseconds, 1))"
}
```

Large-report UX smoke (Release):

```powershell
build/04.8-release-clean/editor/astro_editor.exe --smoke
```

The smoke-only fixture is exactly 131,072 bytes. It asserts a scrollable report area and a maximum measured wrapped-text draw below 250 ms. The run on this Windows x64 host reported 3.2743 ms. It does not automate clicks, judge visual readability, or simulate real validator diagnostics. The sample profile returned 282 bytes; five wall times were 2068.5, 1456.1, 1373.7, 1465.1 and 1450.9 ms (median 1456.1 ms). Since this is only a one-type sample, obtain a representative user project before drawing large-project performance conclusions.

Read-only search also found a separate local Unity 6.5.2 project. Its 13,824-byte cached `Assembly-CSharp.dll` is dated 2026-08-18 22:41; `IsometricCameraController.cs` is newer (23:11 that day), so the assembly is stale for that source. Five `inspect-many` attempts for `IsometricCameraController` and `IsoTerrainBootstrap` took 1192.0, 1130.2, 1121.1, 1170.3, and 1356.5 ms (median 1170.3 ms), but all exited 1: one type was absent from the stale assembly; the other lacks AstroEngine ABI methods. These are rejected-assembly diagnostics, not successful binding validation or a large-project benchmark. No Unity build was run, to avoid changing that project's generated Library state. Log: `build/04.22-unity-profile.log`. The 04.22 measurement is closed with these caveats; Unity compatibility needs its own approved scope.

Before implementation, Debug CTest was **46/46** (9.82 s) and Release+C# **49/49** (64.94 s). After implementation: Debug build passed and CTest **46/46** (9.48 s); Release+C# build passed and CTest **49/49** (66.31 s). Logs: `build/04.22-debug-{build,ctest}.log`, `build/04.22-release-{build,ctest}.log`, `build/04.22-csharp-sample-profile.log`, `build/04.22-ui-smoke.log`. See [report 04.22](reports/04.22-csharp-metadata-project-ux.md), [D087](DECISIONS.md#d087--04-22-c-metadata-profile-and-large-report-ux).
# 06.34 — Campaign save transfer boundary assessment

This is a documentation-only design assessment. Source inspection confirmed that `PuzzleCampaignSaveAdapter` stores and strictly checks campaign identity, while the existing project namespace resolver does not perform file I/O. No user save directories were enumerated or changed. Before implementation, tests should use an isolated temporary Unicode source/target fixture and cover read-only preview, absent slots, valid and backup-recovered slots, corrupt/unsupported records, target conflicts, path traversal/reparse-point rejection, unchanged source hashes, and stale-preview revalidation. No migration behavior is claimed or tested here.

Pre-change check: `ctest --test-dir build/04.8-release-clean --output-on-failure -R '^(runtime\.puzzle\.campaign\.save\.integration|editor\.smoke)$'` passed **2/2** in **3.04 s**. This existing configured Release tree was used only for read-only tests; no source or build reconfiguration was done. Test logs from the prior 06.33 implementation remain recorded in [06.33 report](reports/06.33-campaign-save-identity.md).

## 06.35 — Read-only campaign save transfer preview

`runtime.puzzle.campaign.save.integration` uses only its isolated Unicode fixture. It checks that read-only slot inspection does not create a missing directory; the preview covers slots 0–9 and reports empty, valid, primary-corrupt/backup-recovered, corrupt and unsupported slots, a foreign campaign ID, destination conflicts, source bytes and unexpected artifacts. It rejects `..` paths and unsafe campaign IDs, conditionally exercises symlink rejection if the host permits creating a symlink, does not create an absent target directory, and compares primary/backup bytes before and after repeated preview calls. No user save directory is used. The preview does not validate Puzzle snapshot semantics against the live campaign project, and no copy operation exists.

Windows x64/MSVC Release build used the VS 2022 Developer Environment; final full CTest passed **52/52** in **57.08 s**. The fixture reported that directory symlink creation was unavailable, so symlink rejection was not exercised on this host. Logs: `build/06.35-release-build.log`, `build/06.35-release-ctest.log`.

## 06.36 — Editor read-only campaign save transfer preview

The Editor smoke creates an isolated project/user-data fixture, presents the same-ID transfer preview through the `Puzzle Save Settings` UI path, verifies the valid slot result, confirms that the proposed target folder remains absent, and compares the source slot size before and after preview. Localization coverage checks the new English/Russian UI strings and hidden ImGui IDs. No user's AppData directory is used by smoke.

Pre-change Release baseline: full Release build completed with `ninja: no work to do`; `runtime.puzzle.campaign.save.integration` and `editor.smoke` passed **2/2** in **3.38 s**. The focused post-change run (`runtime.puzzle.campaign.save.integration`, `editor.localization.unit`, `editor.smoke`) passed **3/3** in **3.67 s**. Final full Release build completed successfully (`ninja: no work to do`) and full CTest passed **52/52** in **78.53 s**. Logs: `build/06.36-baseline-build.log`, `build/06.36-release-build.log`, `build/06.36-release-ctest.log`.

## 06.37 — Campaign save transfer preflight

The campaign save integration fixture uses isolated Unicode temporary folders. It covers an unchanged preview returning `Ready` without file/directory writes; a same-size change to a still-valid source slot returning `StaleSource`; a destination directory appearing after preview returning `StaleDestination`; an unchanged target collision returning `DestinationConflict`; and invalid slots/unexpected artifacts returning `SourceNotTransferable`. A zero-byte target file and a directory at an expected slot path both count as collisions. Tests compare source primary/backup bytes before and after read-only preflight operations. No user's AppData or project saves are used.

Baseline Release build completed (`ninja: no work to do`); focused baseline CTest passed **3/3** in **3.30 s**. Final post-change campaign integration, Editor smoke and localization passed **3/3** in **3.58 s**. First full build exposed a locked Editor executable from the prior UI inspection; after closing that exact process, linking exposed a stale Editor object because the existing Ninja dependency database had no header dependencies for `editor/main.cpp`. Touching that source file (without changing contents) forced compilation/relink. Refreshed Editor smoke passed **1/1** in **3.16 s**. Final full Release build passed and linked the changed targets; full CTest passed **52/52** in **54.46 s**. Logs: `build/06.37-focused-build.log`, `build/06.37-editor-rebuild.log`, `build/06.37-release-build.log`, `build/06.37-release-ctest.log`.

## 06.38 — Campaign save copy transaction

The integration fixture verifies that no confirmation creates no target; a confirmed copy writes primary and backup bytes exactly; revalidation rejects stale input and static conflicts; an exclusive-create conflict introduced by the progress callback is not overwritten; cancellation after one file rolls back created files and empty directories; rollback reports a competing file/directory that cannot be removed; and the source primary/backup bytes remain unchanged after success and failures. All writes target isolated Unicode temporary directories. No real project or user AppData save was read or modified.

Pre-change Release build completed (`ninja: no work to do`); focused campaign save, Editor smoke and localization passed **3/3** in **3.15 s**. Final focused run passed **3/3** in **3.47 s**. Full Release build completed successfully; initial full CTest exposed the known stale `editor/main.cpp` object after a public header change. After touching only its timestamp, relinking Editor and confirming `editor.smoke` **1/1** in **3.06 s**, the final full CTest passed **52/52** in **59.88 s**. Logs: `build/06.38-baseline-build.log`, `build/06.38-focused-build.log`, `build/06.38-editor-rebuild.log`, `build/06.38-release-build.log`, `build/06.38-release-ctest.log`.

## 06.39 — Editor confirmation and copy result

Editor smoke now opens the campaign copy confirmation dialog on a unique temporary fixture, verifies source/target and summary are rendered, invokes the same explicit-confirm handler used by the button, and checks the copied slot bytes against the still-present source. The runtime integration test continues to cover stale previews, conflicts, cancellation, rollback and incomplete-rollback artifact reporting. Localization unit checks all new Editor labels. No real project saves or AppData directories are read or written by these tests.

Release baseline before editing: full Editor target build up to date; focused campaign integration, Editor smoke and localization passed **3/3** in **3.64 s**. Initial implementation run exposed a missing translation-coverage wrapper for a `%s` diagnostic and smoke cleanup that did not yet include its new target fixture; both were corrected. Final focused build succeeded and focused tests passed **3/3** in **3.43 s**. Full Release build succeeded (`ninja: no work to do` after focused relink); full CTest passed **52/52** in **57.97 s**. Logs: `build/06.39-focused-build.log`, `build/06.39-release-build.log`, `build/06.39-release-ctest.log`.

## 06.40 — Campaign save transfer interruption journal

The integration test starts a child copy process with a Unicode source/target path. The child creates the real journal, publishes the first slot artifact, and exits immediately from the progress callback without running destructors or rollback. The parent then previews the same target and checks the journal identifies the first target as `Complete` and the second as `Missing`. A fresh copy is rejected as `RecoveryRequired`; acknowledgement without confirmation is refused. Confirmed acknowledgement removes only the journal, preserves all slot files, and a subsequent preview still reports a target conflict. A separate handled-cancellation case verifies ordinary rollback removes the journal and created target directory. Editor smoke renders the recovery state using an isolated temporary journal and verifies it is visible in the UI; no save slot is cleaned by smoke. All fixtures use isolated temporary folders; no user save data is read or modified.

Release baseline before edits: full build was up to date; full CTest passed **52/52** in **49.91 s**. Focused final CTest (`runtime.puzzle.campaign.save.integration`, `editor.localization.unit`, `editor.smoke`) passed **3/3** in **3.40 s**. Final full Release build completed (`ninja: no work to do` after focused relink); CTest passed **52/52** in **64.97 s**. Logs: `build/06.40-baseline-build.log`, `build/06.40-baseline-ctest.log`, `build/06.40-focused-build.log`, `build/06.40-focused-ctest.log`, `build/06.40-release-build.log`, `build/06.40-release-ctest.log`.

## 06.41 — Resume interrupted campaign save transfer

Integration coverage uses process interruption in isolated Unicode temp directories. It verifies resume refusal without explicit confirmation; successful resume skips one complete primary and copies only the missing backup; source changes after preview return `StaleSource`; a target modified after preview returns `RecoveryRequired`, preserves those changed bytes, leaves the other target missing, and retains the journal. Existing journal acknowledgement and no-overwrite behavior remain covered. Editor smoke renders the recovery state and the enabled resume action; it does not press the action against real user data. No real project or AppData save path is accessed.

Baseline: Release build up to date; focused CTest passed **3/3** in **3.67 s**; full previous substage passed **52/52**. An initial rebuild from plain PowerShell could not locate MSVC standard headers because the shell lacked the Visual Studio developer environment; retry in the VS x64 developer environment succeeded. Focused final build and tests passed **3/3** in **3.78 s**. Full Release build succeeded and CTest passed **52/52** in **53.14 s**. `git diff --check` reported no whitespace errors (Git printed existing LF-to-CRLF working-copy notices). Logs: `build/06.41-baseline-build.log`, `build/06.41-baseline-focused-ctest.log`, `build/06.41-focused-build.log`, `build/06.41-focused-ctest.log`, `build/06.41-release-build.log`, `build/06.41-release-ctest.log`.

## 06.42 — Non-destructive partial target handling

The recovery integration test starts with an actual child-process interruption, then substitutes a shorter file whose bytes match a prefix of the approved source. A fresh recovery preview must classify it as `Partial`. Confirmed resume returns `DestinationConflict` and preserves the exact partial bytes, the missing companion artifact, and the journal. The Editor smoke uses an isolated three-byte partial file and verifies the recovery panel reports the partial condition and disables resume. No cleanup/repair method is called and no user project/save directories are accessed.

Baseline Release build was up to date and CTest passed **52/52** in **53.30 s**. Focused Release build/tests passed **3/3** in **4.09 s**. Final Release build completed (`ninja: no work to do` after focused relink); full CTest passed **52/52** in **50.62 s**. Final `git diff --check` exited 0 with no whitespace errors; Git printed LF-to-CRLF working-copy notices. Logs: `build/06.42-baseline-build.log`, `build/06.42-baseline-ctest.log`, `build/06.42-focused-build.log`, `build/06.42-focused-ctest.log`, `build/06.42-release-build.log`, `build/06.42-release-ctest.log`, `build/06.42-diff-check.log`.

## 06.43 — Invalid recovery journal diagnostics

Runtime integration tests check four isolated journal cases: unsupported version, truncated v1, over-limit journal size, and a directory occupying the journal path. Each case returns its typed status (`Malformed`, `Oversized`, or `Unsafe`), gives a diagnostic, yields no guessed artifact list, and blocks copy, resume and acknowledgement. A slot-looking candidate file and the journal/path occupant remain byte-for-byte intact. Editor smoke renders a malformed record and candidate file, verifies they remain unchanged and confirms the UI receives an invalid record with no inferred artifacts. Localization unit coverage checks all new status and explanation labels. No recovery or cleanup is invoked and no user data path is read.

Baseline Release build was up to date; full CTest passed **52/52** in **53.30 s**. Focused Release build/tests passed **3/3** in **4.24 s**. Final Release build succeeded (`ninja: no work to do` after the focused build compiled changed targets); full CTest passed **52/52** in **52.83 s**. Final `git diff --check` exited 0 with no whitespace errors; Git printed LF-to-CRLF working-copy notices. Logs: `build/06.43-baseline-build.log`, `build/06.43-baseline-ctest.log`, `build/06.43-focused-build.log`, `build/06.43-focused-ctest.log`, `build/06.43-release-build.log`, `build/06.43-release-ctest.log`, and `build/06.43-diff-check.log`.

## 06.44 — Explicit archive of malformed transfer journal (historical behavior, superseded by 06.45)

Integration tests verify refusal without confirmation, exact-byte archive plus retirement of a supported malformed journal, refusal when a different file occupies the derived archive path, and refusal when the source journal changed after preview. Slot-looking candidate save files remain byte-for-byte intact in every case. Editor smoke opens the archive confirmation for an isolated malformed journal, uses the same archive action, verifies the original journal was retired only after an exact archive appeared, and checks candidate save bytes remain unchanged. Unsafe, oversized, unreadable and valid journals are ineligible for this action.

Baseline Release build was up to date; full CTest passed **52/52** in **50.28 s**. Focused Release build/tests passed **3/3** in **4.22 s**. Final Release build succeeded and full CTest passed **52/52** in **53.55 s**. Final `git diff --check` exited 0 without whitespace errors; Git printed LF-to-CRLF working-copy notices. Logs: `build/06.44-baseline-build.log`, `build/06.44-baseline-ctest.log`, `build/06.44-focused-build.log`, `build/06.44-focused-ctest.log`, `build/06.44-release-build.log`, `build/06.44-release-ctest.log`, and `build/06.44-diff-check.log`.

## 06.45 — Archive copy preserves malformed source journal

Integration tests verify refusal without confirmation, exact-byte archive creation while retaining the exact original and candidate save, idempotent repeat using the displayed recovery preview, refusal to replace an archive collision, and refusal when the source changed after review. Editor smoke opens the confirmation for an isolated malformed journal and verifies both archive and original bytes remain exact while the candidate save is untouched. The invalid journal remains present, so transfers stay blocked. Tests do not exercise simultaneous processes or prove cross-platform locking; no lock/retirement mechanism was added. All fixtures are isolated; user/project save directories are not used.

Baseline Release build was up to date and CTest passed **52/52 in 57.57 s**. Focused Release build/tests passed **3/3 in 4.85 s**. After the final runtime diagnostic text edit, Release rebuilt the affected runtime and dependent targets successfully; full CTest passed **52/52 in 56.39 s**. `git diff --check` exited 0; Git printed existing LF-to-CRLF working-copy notices. Logs: `build/06.45-baseline-build.log`, `build/06.45-baseline-ctest.log`, `build/06.45-focused-build.log`, `build/06.45-focused-ctest.log`, `build/06.45-release-build.log`, `build/06.45-release-ctest.log`, and `build/06.45-diff-check.log`.

## 06.46 — Safe acknowledgement of a valid recovery journal

The valid-journal integration case now checks explicit confirmation, stale exact bytes, and—on Windows—a competing open writer handle. While that handle is held, acknowledgement must fail closed and preserve the journal; after release, the same reviewed journal is removed by the handle that supplied the verified bytes. Candidate slot artifacts remain unchanged, and an existing partial slot remains a conflict. On non-Windows builds the API is expected to fail closed; that path was not run in this Windows environment. No POSIX lock/unlink implementation is enabled.

Baseline Release build was up to date and full CTest passed **52/52 in 52.12 s**. Final focused Release CTest passed **2/2 in 3.94 s** (`runtime.puzzle.campaign.save.integration`, `editor.smoke`); final Release build succeeded and full CTest passed **52/52 in 55.72 s**. `git diff --check` exited 0 with existing LF-to-CRLF working-copy notices. The first focused run exposed an incremental-build ABI mismatch after a temporary public struct layout addition; that field was removed to preserve layout, the affected runtime/test targets were rebuilt, and the focused plus full suites then passed. Logs: `build/06.46-baseline-build.log`, `build/06.46-baseline-ctest.log`, `build/06.46-focused-build.log`, `build/06.46-focused-ctest.log`, `build/06.46-release-build.log`, `build/06.46-release-ctest.log`, and `build/06.46-diff-check.log`.

## 06.47 — Cross-platform campaign journal acknowledgement

Integration coverage verifies that acknowledgement during an active copy cannot acquire the journal lease; a separate POSIX process holds `flock` while acknowledgement is attempted and proves refusal/preservation; Windows uses a competing `LockFileEx` range lock and proves refusal/preservation. After contention ends, acknowledgement removes the valid journal while preserving all slot artifacts. Existing tests also cover explicit confirmation, stale journal bytes, interrupted copy/resume, partial destination preservation, and cancellation rollback. Tests use isolated temporary paths; no project or user save data is accessed.

Windows x64 Release build succeeded; focused campaign-save integration passed **1/1**; full CTest passed **52/52 in 50.71 s**. A first full invocation after Windows restart used a stale `ASTRO_PWSH_EXECUTABLE` in the ignored CMake cache and marked two PowerShell tests Not Run; the cache was pointed at the currently available `pwsh.exe` and the final full suite passed. POSIX Release build succeeded in `/root/astroengine-06.47-posix-release`; focused campaign-save integration passed **1/1**; full CTest passed **39/39 in 0.56 s**. The POSIX race test uses a real separate process. Logs: `build/06.47-win-focused-build.log`, `build/06.47-win-release-build.log`, `build/06.47-win-release-ctest.log`, `build/06.47-posix-release-build.log`, and `build/06.47-posix-release-ctest.log`.

Limit: POSIX `flock` is advisory and protects only against other participants that honor the same lock protocol. Path replacement by hostile local processes and durability across sudden power loss are outside this guarantee.

## 06.48 — Malformed journal archive lock

The archive integration case holds a competing lock on the malformed source journal. Windows uses a second handle with `LockFileEx`; POSIX uses a separate child process with `flock`. Archive confirmation must fail while that lock is held, create no archive, and preserve the exact malformed source plus a candidate slot file. After releasing the lock, the existing archive-copy, exact verification, repeated-copy, and collision behavior must still pass. No source journal is deleted and no save slot is changed.

Baseline focused campaign-save integration passed **1/1** on Windows x64 Release and WSL Ubuntu 24.04 Release before changes. After changes, focused integration passed **1/1** on each target. Full Release CTest passed **52/52 in 72.12 s** on Windows and **39/39 in 1.08 s** on WSL Ubuntu. Logs: `build/06.48-win-release-build.log`, `build/06.48-win-release-ctest.log`, `build/06.48-posix-baseline-build.log`, `build/06.48-posix-baseline-ctest.log`, `build/06.48-posix-release-build.log`, and `build/06.48-posix-release-ctest.log`. Windows focused baseline passed directly before changes; its result was captured in the tool output, not a saved log file.

## 06.49 — Partial archive residue assessment

The integration case first creates a valid archive in its isolated temp fixture, truncates that archive to model the final-path residue of an interrupted write, then retries. Expected behavior: retry fails closed, leaves the truncated archive and malformed source bytes unchanged, and does not touch the candidate save. The test restores the exact archive bytes in the temporary fixture and confirms the existing idempotent archive path still works. This simulates a persisted partial file; it does not kill a process during an actual write or simulate power loss.

Baseline focused campaign-save integration passed **1/1** on Windows and **1/1** on WSL Ubuntu before edits. After adding the fixture check, focused integration passed **1/1** on both. Full Release CTest passed **52/52 in 62.87 s** on Windows and **39/39 in 0.96 s** on WSL Ubuntu. Logs: `build/06.49-win-release-build.log`, `build/06.49-win-release-ctest.log`, `build/06.49-posix-release-build.log`, and `build/06.49-posix-release-ctest.log`.
### 06.50 staged journal archive publication

The `PuzzleCampaignSaveAdapterTests` integration fixture verifies exact-byte staging before publication, successful no-replace archive retry while an unrelated stale staging-looking file is preserved, refusal to replace a pre-existing partial or conflicting final archive, and preservation of the malformed source journal plus candidate slot bytes. Existing platform-specific active-lease contention checks remain in the same executable. This is deterministic filesystem-state coverage, not process-kill or power-loss durability testing. Windows and POSIX full CTest results are recorded in [the 06.50 report](reports/06.50-staged-journal-archive.md).

### 06.51 archive process-interruption coverage

`runtime.puzzle.campaign.save.integration` launches its own executable as a child with a private in-process hook, waits for a marker at the selected archive boundary, then terminates the child (`TerminateProcess` on Windows, `SIGKILL` on POSIX). Cases stop after a partial staging write, after the staged bytes have been flushed and verified but before publication, and after publication but before staging cleanup. The parent verifies final-path absence/presence, exact content, platform-specific staging residue, successful retry, and byte-for-byte preservation of the malformed source and candidate slot file. The test seam is in a private runtime header and adds no public API/environment control. The child wait is bounded at ten seconds and the CTest timeout is 60 seconds. This checks process termination only; it does not simulate a kernel crash, power failure or storage-controller cache loss. Results: [06.51 report](reports/06.51-archive-interruption-tests.md).

### 06.52 legacy final-archive conflict policy (assessment)

The policy assessment is read-only: final-path reuse requires exact bounded journal bytes. A different occupant—including a regular truncated file, a matching prefix, an unreadable file, directory or reparse point—cannot be attributed safely to an interrupted archive. Future diagnostics should be tested with each class, show the exact path/type/size/access/comparison result, avoid following links and preserve all bytes. “Prefix match” is evidence about contents only, not provenance. No new runtime behavior or test was added in 06.52; the pre-assessment Release baseline passed 40/40 on Windows and 39/39 on WSL Ubuntu. See [06.52 report](reports/06.52-legacy-archive-conflict-assessment.md).

### 06.53 read-only archive conflict diagnostics

`runtime.puzzle.campaign.save.integration` covers a missing final path, an exact archive, a different occupant, a shorter matching prefix, a 4097-byte oversized occupant, a directory and (where symlink creation is permitted) a symbolic link. It checks exact byte size/comparison categories and proves the source journal, candidate slot and link referent are unchanged. The Editor smoke runs the archive action on an isolated fixture, verifies exact journal bytes, then changes only that temporary archive fixture into a sentinel conflict and checks that Editor inspection reports it without changing the journal or slot. No smoke touches project/player data. Comparison results do not establish file ownership.

Windows Release: `cmake --build build/06.53-editor-release --parallel 8` from the VS 2022 x64 Developer Command Prompt; `ctest --test-dir build/06.53-editor-release --output-on-failure` passed **44/44 in 7.54 s**. WSL Ubuntu Release: `cmake --build ~/astroengine-build-06.50 --parallel 8`; `ctest --test-dir ~/astroengine-build-06.50 --output-on-failure` passed **39/39 in 1.03 s**. Focused runtime integration and Editor smoke passed **2/2 in 4.19 s** on Windows; POSIX focused integration passed **1/1 in 0.29 s**. Logs: `build/06.53-editor-build.log`, `build/06.53-runtime-test-build.log`, `build/06.53-windows-ctest.log`, and `~/astroengine-build-06.50/06.53-ctest.log`. See [06.53 report](reports/06.53-archive-conflict-diagnostics.md).

### 06.54 copy-aside assessment

This substage changes policy documentation only; no implementation or test was added. Before edits, the Windows Release Editor build completed successfully (**154/154 Ninja tasks**, existing C4189 unused-local warnings, no errors) and full CTest passed **44/44 in 14.95 s**. WSL Ubuntu Release build reported `ninja: no work to do` and full CTest passed **39/39 in 0.80 s**. Logs: `build/06.54-preflight-windows-build.log`, `build/06.54-preflight-windows-ctest.log`, and `~/astroengine-build-06.50/06.54-preflight-ctest.log`.

06.54 documented the candidate policy; 06.55 implements it. Integration tests prove cancel/no-confirm creates no output, only a bounded readable regular file is eligible, output is byte-exact and adjacent, no-replace collisions fail, links/reparse points are not followed, and source/journal/slot remain unchanged. They also cover stale source and child-process termination before/after publication with expected staging/final artifacts. Editor smoke uses only an isolated fixture and exercises the confirmation operation. The copied data remains unverified and does not clear the original conflict. These tests do not establish provenance, kernel-crash/power-loss durability or safety against arbitrary hostile writers. See [06.55 report](reports/06.55-conflict-copy.md) and [D118](DECISIONS.md#d118--explicit-bounded-copy-aside-for-archive-conflicts-0655).

### 06.56 Unicode path and manual confirmation-dialog review

`runtime.puzzle.campaign.save.integration` additionally uses `std::filesystem::path` with the Unicode parent directory `Проверка копии Ω`; on success it verifies that generated source/destination share that parent and that copy, conflicting occupant, malformed journal, and candidate slot retain their expected exact bytes. Windows and WSL/POSIX focused tests both pass.

For visual inspection, launch `build/06.53-editor-release/editor/astro_editor.exe --smoke-inspect-copy-dialog`. This test-only mode creates an isolated temporary Editor project, opens the copy confirmation and waits after the smoke reaches it. Inspect source/destination, byte count and warning, then click **Cancel separate copy**. Cancellation validates that no destination was created and the fixture source/journal/slot remain unchanged; the process then exits successfully and removes only its own smoke directory. It does not touch the user's project or player saves. Windows manual UI inspection completed with exit code 0. The actual Editor was visually reviewed only on this Windows host; WSL coverage is automated integration, not visual.


### 06.57 macOS/POSIX portability assessment

The assessment inspected the current POSIX copy-aside code and Apple primary API documentation; it did not compile or execute on macOS, and adds no tests. Existing verification is the pre-edit baseline only: Windows Release Editor build completed 154/154 Ninja actions (three existing C4189 warnings), CTest 44/44 in 14.36 s; WSL Ubuntu Release build reported no work, CTest 39/39 in 1.07 s. Before claiming macOS support, run the campaign-save integration on a macOS host and APFS. Record `fsync(file)` and `fsync(directory)` outcomes separately, exercise hard-link no-replace publish, Unicode names, symlink rejection, lock contention, collisions and child-process interruption. These tests do not prove kernel-crash or power-loss durability. See [06.57 assessment](reports/06.57-posix-macos-portability-assessment.md).

### 06.58 C# unit-test clock formatting portability

The user reported that the Mac build stopped because `std::to_string` was ambiguous for `file_time_type::duration::rep` in `CSharpCodeWorkspaceTests.cpp` and `CSharpBuildManifestTests.cpp`. The count is now explicitly converted to `long long` before formatting. Windows Release build completed; focused tests passed 2/2 in 0.44 s and full CTest passed 44/44 in 13.33 s. WSL Ubuntu Release build recompiled and linked both changed tests; focused tests passed 2/2 in 0.07 s and full CTest passed 39/39 in 3.92 s. A subsequent logged rerun reported no build work, focused 2/2 in 0.03 s and full 39/39 in 0.92 s. Logs: `build/06.58-windows-focused-ctest.log`, `build/06.58-windows-full-ctest.log`, `build/06.58-macos-fix-windows-build.log`, `~/astroengine-build-06.50/06.58-build.log`, `~/astroengine-build-06.50/06.58-focused-ctest.log`, `~/astroengine-build-06.50/06.58-full-ctest.log`. The Mac must rebuild and rerun CTest to confirm Apple Clang/libc++ specifically; no such run is available from this host. See [06.58 report](reports/06.58-macos-test-portability-fix.md).

### 07.1 3D foundation assessment

This is a documentation/dependency assessment, not a code feature. Windows Release build succeeded after Ninja recovered its local log; CTest passed 44/44 in 14.56 s. WSL Ubuntu Release build reported no work; CTest passed 39/39 in 5.51 s. No new tests were needed because no implementation changed. The next code prototype must add unit tests for camera/projection and mesh validation plus an optional-renderer visual smoke; glTF URI/path safety and physics integration need separate tests before those adapters are enabled. See [07.1 report](reports/07.1-3d-foundation-assessment.md).

### 07.1b glTF/GLB parser and dependency assessment

Documentation-only assessment; no parser or fixtures were downloaded and no importer/test code was added. Local `build/spike-deps` contains cgltf only as part of bgfx's third-party tree; it is not approved for untrusted project assets given the current public memory-safety reports against cgltf 1.15. fastgltf v1.0.1 is C++20/MIT and is the recommended candidate for a guarded optional prototype. Its default CMake flags can download simdjson 5.1.0 and simdutf 9.3.3; any trial must disable implicit downloads or explicitly pre-stage exact sources/hashes in ignored build storage. The importer needs byte/element limits, project-root URI confinement, unsupported-required-extension rejection, cache integrity/atomicity checks, hostile fixtures and fuzz/sanitizer coverage where available. Existing Windows Release build/CTest are unchanged from 07.1a: 161/161 actions and 46/46 tests in 13.94 s. See [assessment](reports/07.1b-gltf-importer-dependency-assessment.md) and [D124](DECISIONS.md#d124--bounded-gltf-parser-candidate-and-dependency-gate-071b).

### 07.1i Editor 3D frame profile

Windows x64 Release only; `astro_gltf` and the Editor Scene View 3D pass must be enabled. In a Visual Studio Developer PowerShell:

```powershell
& '.\build\07.1f-editor3d-release\editor\astro_editor.exe' --benchmark-3d
ctest --test-dir 'build/07.1f-editor3d-release' -R '^editor\.scene_view3d\.benchmark\.smoke$' --output-on-failure
ctest --test-dir 'build/07.1f-editor3d-release' --output-on-failure
```

The Editor creates a temporary project from the checked-in render3d fixture, builds four grid scenes with 1/100/500/1000 visible meshes, discards 24 warm-up frames, then reports 48 measured frames per scene. Metrics include CPU mesh preparation and submission, Scene 3D CPU phase, complete Editor wall frame, Editor CPU before `bgfx::frame`, CPU time in that call, BGFX GPU full-frame timer, derived frame rate, fixture import/setup, process working/private memory, EXE size and draw counts. The BGFX GPU timer is the full window frame, includes VSync effects, and does not isolate the Scene View 3D pass. `frame_rate_from_wall_p50` is derived from wall p50 and is not uncapped throughput. No numeric performance threshold is asserted.

Verified 2026-11-17 on Windows x64 Release / D3D11: final build reported no pending work; profile produced all four scenarios across three direct runs; focused profile smoke passed **1/1 in 5.90 s**; full CTest passed **51/51 in 15.60 s**. Across runs, 1000-mesh Scene 3D CPU p50 was 1.4192–1.6042 ms and complete Editor wall-frame p50 was 16.6509–16.6791 ms. Logs: `build/07.1i-final-build-20261117.log`, `build/07.1i-benchmark-run1-20261117.log`, `build/07.1i-benchmark-run2-20261117.log`, `build/07.1i-benchmark-run3-20261117.log`, `build/07.1i-benchmark-smoke-ctest-20261117.log`, `build/07.1i-final-ctest-20261117.log`. See [07.1i report](reports/07.1i-editor-3d-frame-profile.md).
