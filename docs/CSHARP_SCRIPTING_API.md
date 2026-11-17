# C# runtime API prototype

Status: experimental Windows x64 prototype, introduced in 04.5–04.6. ABI v1 is implemented for one synchronous fixed-step entry point and Transform access. It is not a production compatibility promise.

## Version 1 ABI

The native contract is declared in `engine/runtime/include/astro/runtime/CSharpScriptHost.hpp`:

- `CSharpScriptTransformV1`: struct size, API version, then nine `double` values for position, Euler rotation in degrees, and scale.
- `CSharpScriptApiV1`: struct size/version, invocation-local user data, and `getTransform` / `setTransform` callbacks.
- `CSharpScriptUpdateV1`: struct size/version, stable-in-scene EntityId, fixed delta seconds, and pointer to the API table.
- All IDs are `uint64_t`; sizes and versions are `uint32_t`; callbacks return `int32_t` status codes. Structures use sequential native layout. The C# sample mirrors this layout with `[StructLayout(LayoutKind.Sequential)]`.

`CSharpScriptHost::fixedUpdate(runtimeScene, entityId, fixedDeltaSeconds)` constructs the callback table and update context on the stack and synchronously invokes the loaded managed `FixedUpdate` entry point. Native validation requires exact v1 structure size, supported version, an existing entity, and a finite positive timestep. The C# entry point repeats size/version/context validation before calling callbacks.

The bridge exposes only the owning entity's Transform read/write for this prototype. `setTransform` routes through `Scene::setTransform`, so non-finite or otherwise invalid values return an error and do not replace the prior Transform. It does not expose arbitrary memory, file access, the full Scene object, generic property access, or CommandRegistry operations.

Callback table and user-data pointers are valid only during the synchronous `FixedUpdate` call. A script must not retain them or call callbacks later/on another thread. The invocation uses the caller's RuntimeSession fixed-step thread; this prototype defines no multithreaded script scheduling or callback reentrancy.

## Compatibility and failures

V1 requires exact structure sizes and exact version `1`; the prototype rejects unknown versions and truncated or extended structures. There is no forward compatibility or migration yet. Any incompatible layout requires a new version and a compatibility decision before distribution. `CSharpScriptStatus` defines callback results: success, invalid context, version mismatch, invalid entity, invalid transform, and callback failure. The host reports a nonzero managed return through `core::Result<void>`.

The ABI is currently Windows x64 only and remains coupled to the experimental .NET `hostfxr` loader. Consumers must use the native declarations or the documented C# mirror; it is not yet a public plugin SDK.

## ScriptBehavior boundary

Stage 04.7 adds a persistent Scene authoring component: Scene v5 `astro.ScriptBehavior` records a stable `AssetId`, an explicit managed type name, enabled state, ComponentId, and component schema version 1. In 04.8 the project AssetRegistry adds script records to the shared AssetId index (v2) and can resolve a registered AssetId to its source, managed type, assembly, and runtimeconfig. 04.10 connects the project build handoff to that registry: Project shows freshness and binding diagnostics and offers an explicit registration action. Since 04.13, `CSharpScriptHost::loadScriptAsset` resolves lifecycle entry points and `CSharpScriptRuntime` schedules enabled components through RuntimeSession after Editor approval.

Registration expects existing UTF-8 `.cs` source below project `Scripts/` and existing build output `.dll`/`.runtimeconfig.json` below ignored `Library/Scripts/`. In 04.9, `tools/csharp/build-project.ps1` provides a project-local offline build for `Scripts/**/*.cs`; it pins the SDK/framework in `.astro/csharp-build.json`, validates UTF-8 input and path boundaries, writes immutable generation outputs under `Library/Scripts/<AssemblyName>/`, and records the active generation in `Library/Scripts/build-state.json`. In 04.10, optional `scriptBindings` entries explicitly select the managed type for each source; the builder also publishes `Library/Scripts/asset-bindings.state` and stores its SHA-256 in build-state schema v2. `AssetRegistry::inspectScriptBuild()` reports Missing/Current/Stale/Invalid, validates manifest, complete source set and hashes of assembly/runtimeconfig, and reports registry synchronization state. `registerScriptsFromBuild()` registers all selected types atomically when explicitly called and preserves IDs for existing source paths. The Project panel provides Refresh and Register current C# build actions. It does not compile, load or execute managed code while scanning the project. Actual type/entry-point validation happens only when `CSharpScriptHost::loadScriptAsset` is explicitly called. Assembly simple name is inferred from the DLL filename and the host entry method remains fixed to `FixedUpdate` for this prototype.

Create `.astro/csharp-build.json` in a project:

```json
{
  "schemaVersion": 1,
  "sdkVersion": "10.0.404",
  "targetFramework": "net10.0",
  "assemblyName": "MyGameScripts",
  "scriptBindings": [
    { "sourcePath": "Scripts/SceneMotion.cs", "managedTypeName": "MyGame.SceneMotion" }
  ]
}
```

`scriptBindings` is optional. Each `sourcePath` must exactly match a compiled source below `Scripts/`; a source may be bound at most once. The managed type name must be nonempty UTF-8 (up to 512 bytes) without whitespace/control characters. The builder validates the binding's shape and source membership, not whether that type exists in the compiled assembly.

With PowerShell 7 and the pinned SDK already available locally, build and check freshness:

```powershell
pwsh -NoProfile -File tools/csharp/build-project.ps1 `
  -ProjectRoot "C:\path\to\project" `
  -DotnetRoot "C:\path\to\dotnet-sdk"
pwsh -NoProfile -File tools/csharp/build-project.ps1 `
  -ProjectRoot "C:\path\to\project" `
  -DotnetRoot "C:\path\to\dotnet-sdk" -Check
```

`-Check` exits 0 when source/manifest/outputs match, 3 when a rebuild is needed and 2 for invalid configuration. Build/restore failure exits 1 and preserves the prior build-state record. After building, open the project in Editor, use **Project → Refresh C# status**, inspect diagnostics, then choose **Register current C# build** to synchronize selected bindings. Generated sources/intermediates, handoff, and immutable outputs are kept under ignored `Library/Scripts`; the current prototype does not prune older generations. Restore uses a checked-in NuGet config with all package sources cleared, so project dependencies cannot be added through packages in this stage. The command is a local compiler invocation, not a sandbox or approval boundary.

The 04.13 runtime lifecycle resolves the entire enabled set before callbacks, preserving Scene entity insertion order and ScriptBehavior order within each entity. Each unique AssetId gets one host after a fresh per-load Editor snapshot/consent check; its component instances each receive `OnCreate`, `FixedUpdate` per fixed step, and `OnDestroy` in reverse successful creation order. Disabled behaviors receive no calls. Missing bindings, host entry points, callback failures, or failed approval abort Play with diagnostics. `RuntimeSession::play` remains source-compatible; Editor uses `playWithLifecycle` and `stopChecked` to report teardown errors. Component-based `loadScriptAsset` now requires `OnCreate`, `FixedUpdate`, and `OnDestroy`; the older generic `CSharpScriptHost::load` remains a single-entry API.

Managed component ABI v1 is static and callback-oriented, not a managed object/instance model. A bound type must expose methods with the required signatures:

```csharp
[UnmanagedCallersOnly]
public static int OnCreate(IntPtr payload, int payloadSize);
[UnmanagedCallersOnly]
public static int FixedUpdate(IntPtr payload, int payloadSize);
[UnmanagedCallersOnly]
public static int OnDestroy(IntPtr payload, int payloadSize);
```

`OnCreate` and `OnDestroy` receive `CSharpScriptLifecycleV1`; `FixedUpdate` receives the existing `CSharpScriptUpdateV1`. Each payload contains a temporary callback table exposing the current entity's Transform only. Keep callbacks synchronous and do not retain payload or callback pointers after return. This is a source compatibility change for projects using `loadScriptAsset`: add both lifecycle methods and rebuild/register the project before Play. No Scene file migration is needed because the serialized ScriptBehavior schema did not change.

If an `OnCreate` fails, the runtime calls `OnDestroy` for previously created components in reverse order and discards the candidate runtime Scene. Stop attempts every reverse-order `OnDestroy`, discards runtime state even if callbacks fail, and returns the first diagnostic through `stopChecked`. A failed fixed update faults Play; Editor stops and revokes consent. Assembly loading is process-wide and unload is not guaranteed. Managed module initializers may execute as part of assembly loading before component callbacks; the engine cannot defer or roll back such CLR behavior.

## Security and platforms

The C# runtime is in-process: user scripts have the process privileges and are not sandboxed. Do not load untrusted scripts. Native AOT, Android/iOS hosting, export/runtime packaging, runtime distribution terms, license notices, crash containment, and AI permissions have not been validated. `.NET 10` remains a prototype candidate only.

### Play approval policy (04.11 policy, Editor gate in 04.12–04.13)

Before Play, the Editor requires one direct user confirmation for the enabled `ScriptBehavior` set; its dialog lists project/Scene, each exact AssetId/source/type, and current build fingerprint/output hashes. Consent applies only to that set and one Play session and is not persisted. Stop/new Play and project/Scene replacement revoke it. The Editor recaptures and compares the snapshot on Allow and the runtime checks it again immediately before every unique host load. Cancel, stale/missing/invalid binding, changed snapshot, or load failure aborts the entire set before component lifecycle callbacks. AI/CLI/build completion cannot grant consent; AI-requested Play waits for direct user action. Hot reload is excluded; disk edits apply next session.

The Editor implements the consent prompt and ephemeral snapshot gate in `editor/CSharpPlayApproval.*`; `AssetRegistry::inspectScriptBuild()` exposes verified output paths/hashes. Allow recaptures and compares the snapshot. In 04.13 the runtime manager calls the gate immediately before loading each unique assembly; stale approval aborts the entire set before component lifecycle callbacks. Stop/new Play/project/Scene replacement/Faulted revoke approval. The low-level host API itself remains trusted in-process API without a permission check. The gate is an accidental-execution policy, not a sandbox: loaded C# has process privileges and may access OS APIs, files, and network available to that process. No remembered trust option is planned.

Current tests cover disabled-script exclusion, invalid/missing bindings, per-unique-asset approval recheck, real sample `OnCreate`/`FixedUpdate`/`OnDestroy`, reverse cleanup, startup rollback, Scene isolation and RuntimeSession teardown. Editor smoke and localization tests pass, but mouse/keyboard interaction with the consent modal is not synthesized. The integration test directly injects an approval callback; the Editor flow itself recaptures hashes before load. There is no sandbox, process crash containment, managed instance state, hot reload, unload guarantee, non-Windows host, or production permission boundary. Test-only approval does not create a production auto-grant path.

## Tests

`assets.registry.integration` covers shared image/script IDs, index migration, UTF-8 paths/type names, stale source detection, missing outputs, traversal, fresh handoff and registration. `runtime.csharp_host.integration` loads the real sample lifecycle through hostfxr and verifies approval rechecks, enabled/disabled scheduling, callback effects, rollback and Scene isolation. `scene.command.integration` covers ScriptBehavior add/enable/remove, validation and stable-ID/order Undo/Redo. `editor.smoke` checks Inspector authoring is present, adds a disabled test binding through the same typed command, and saves/reopens Scene v5. The smoke does not synthesize ImGui clicks or test the selection combo against a live project build. `runtime.csharp_project_build.integration` covers deterministic project build, freshness, source changes, output generations and tampering. These tests do not verify Code Editor authoring, sandboxing or security isolation. See reports [04.6](reports/04.6-versioned-csharp-runtime-api.md), [04.7](reports/04.7-persistent-script-behavior.md), [04.8](reports/04.8-project-script-asset-registry.md), [04.9](reports/04.9-csharp-project-build.md), [04.10](reports/04.10-csharp-build-registry-diagnostics.md), [04.13](reports/04.13-csharp-runtime-lifecycle.md), and [04.14](reports/04.14-scriptbehavior-editor-authoring.md).

## 04.15 — Code workspace

`editor.csharp_code_workspace.unit` covers UTF-8 source I/O, dirty tracking, path confinement and compiler-diagnostic location parsing. `editor.smoke` creates and reopens a source file only within its unique temporary project; no user project or sample script is modified. Build diagnostics are produced by the existing PowerShell/.NET project builder and mapped through its deterministic `PathMap`. Editor smoke confirms the panel renders but does not synthesize keyboard/mouse editing, execute the build button, or test a graphical caret screenshot. The workspace is not a security sandbox.

## 04.16 — Project build settings

The C# Code workspace exposes project-owned settings stored at `.astro/csharp-build.json`. The Editor form can pin an installed/configured .NET 10.0.x SDK, set the assembly name, and map script source paths under `Scripts/` to managed type names. `targetFramework` is fixed to `net10.0` in this prototype. The existing offline builder reads this same manifest when building; unsaved form edits do not silently change a build.

The parser rejects duplicate/unknown keys, wrong JSON types, unsupported schema/framework values, invalid source paths and references to missing scripts. Saving validates the project and writes through a temporary sibling file before replacement. A malformed existing manifest remains intact until the user confirms replacement in the Editor. Tests cover parser/validation and programmatic Editor smoke in a temporary project; actual pointer/key interaction is not automated. This Windows prototype does not offer SDK/framework portability or approve .NET 10 for production. See [04.16 report](reports/04.16-csharp-build-manifest-editor.md).

## 04.17 — Build binding consistency diagnostics

The Project panel compares each current build handoff binding against `.astro/assets.index`. A binding row reports whether it is registered and identifies a missing registration, managed type mismatch, source hash mismatch, or assembly/runtimeconfig output mismatch. Sources with no selected type and registered records outside the current manifest remain visible as project diagnostics. Use **Register current C# build** only after a successful, current build; records for the same source keep their AssetId.

This check does not inspect CLR metadata or execute the assembly. It cannot prove that a type or `OnCreate`/`FixedUpdate`/`OnDestroy` methods exist. Those entry points are checked only by explicit approved Play loading, which can run CLR module initializers in-process. Refreshing project diagnostics is metadata-only. See D082 and [04.17 report](reports/04.17-csharp-binding-diagnostics.md).

# Metadata inspection and isolation status (04.19)

The Project panel's build/registry diagnostics intentionally do not load the managed assembly, so they do not prove that a declared CLR type or entry point exists. Do not add `Assembly.Load`, `AssemblyLoadContext`, or reflection to the Editor scan path. `AssemblyLoadContext` is an assembly-loading mechanism, not a security boundary. `MetadataLoadContext` can inspect metadata without running the inspected assembly, but this does not make a metadata parser safe for hostile input; low-level `System.Reflection.Metadata` documentation explicitly warns that it is not designed for untrusted input. Attribute constructors must never be invoked during inspection; if attributes are needed, inspect their metadata representation only.

There is now a Windows x64 validator at `tools/csharp/metadata-validator`. It inspects explicit assembly/type metadata using `System.Reflection.Metadata` from .NET 10, inside a temporary AppContainer with no network capabilities, narrow staging ACLs, a 512 MiB Job Object limit, bounded operation time and protocol. The Project panel's **Inspect registered C# types** action invokes it only for a fresh build and registered bindings, in the background. The Editor validates the canonical assembly path beneath project `Library/Scripts` and rejects reparse points. One worker request accepts up to 64 types and 8 KiB of UTF-8 names; a project inspection can include up to 256 types split across workers. Each worker response remains bounded to 32 KiB; the scrollable Editor report aggregates up to 128 KiB and indicates truncation. Runtime staging is still repeated per batch. A synthetic 256-type ABI fixture measured about 1.35 s median for one 64-type batch and 5.53 s for four batches. This does not profile dependencies or the user's actual project and is not a security certification for arbitrary hostile assemblies. The parser API itself warns that it is not designed for untrusted input; the in-process Play host continues to be trusted-code-only. See [D086](DECISIONS.md#d086--c-metadata-validation-batch-limits-0421), [04.21 report](reports/04.21-csharp-metadata-batch-performance.md), [D085](DECISIONS.md#d085--project-panel-c-metadata-diagnostics-0420), [04.20 report](reports/04.20-csharp-project-metadata-diagnostics.md), [D084](DECISIONS.md#d084--windows-c-metadata-validator-worker-prototype-0419), and [report 04.19](reports/04.19-csharp-metadata-validator-worker.md).

Manual CLI (run with the repository's local .NET 10 SDK/runtime): `dotnet AstroEngine.CSharpMetadataValidator.dll inspect --dotnet-root <dotnet-root> --assembly <path-to-dll> --type <Namespace.Type>`. For a single isolated batch use `inspect-many` and repeat `--type` up to 64 times, keeping total UTF-8 type-name data at or below 8 KiB. This command is Windows x64 only. The Editor integration does not alter the runtime host ABI. See [benchmark instructions](TESTING.md#04.21--c-metadata-batch-profile).

The runtime host implemented through 04.13 remains in-process for compatibility and executes with Editor/user privileges. Consent is an explicit execution UX gate, not isolation. Do not use it for untrusted scripts. A future out-of-process runtime requires a separately versioned IPC contract and opt-in migration; ABI v1 remains unchanged until that work is approved.
