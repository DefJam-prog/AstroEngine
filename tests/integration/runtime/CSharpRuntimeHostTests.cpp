#include "astro/assets/AssetRegistry.hpp"
#include "astro/runtime/CSharpScriptHost.hpp"
#include "astro/runtime/CSharpScriptRuntime.hpp"
#include "astro/runtime/RuntimeSession.hpp"

#define NOMINMAX
#include <windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
bool expect(const bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::wstring quoteArgument(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int runManagedHostChecks(const std::filesystem::path& root, const std::filesystem::path& dotnetRoot,
    const std::filesystem::path& sampleSource, const astro::assets::AssetId scriptAssetId)
{
    auto registryResult = astro::assets::AssetRegistry::open(root);
    bool ok = expect(registryResult.hasValue(), "open project asset registry for managed scripts");
    if (!registryResult) return 1;
    auto registry = std::move(registryResult).value();
    ok &= expect(registry.findScript(scriptAssetId) != nullptr, "persist and reload the project script AssetId");
    auto resolved = registry.resolveScript(scriptAssetId);
    const auto assembly = root / "Library" / "Scripts" / "AstroEngine.CSharpSample.dll";
    const auto runtimeConfig = root / "Library" / "Scripts" / "AstroEngine.CSharpSample.runtimeconfig.json";
    ok &= expect(resolved && resolved.value().assemblyPath == std::filesystem::canonical(assembly)
        && resolved.value().runtimeConfigPath == std::filesystem::canonical(runtimeConfig),
        "resolve the persisted AssetId to the real compiled assembly, runtimeconfig and type");
    auto missingScript = astro::runtime::CSharpScriptHost::loadScriptAsset(dotnetRoot, registry, {999999});
    ok &= expect(!missingScript && missingScript.error().code == astro::core::ErrorCode::NotFound,
        "reject an unregistered Script AssetId before hostfxr load");

    auto hostResult = astro::runtime::CSharpScriptHost::loadScriptAsset(dotnetRoot, registry, scriptAssetId);
    if (!expect(hostResult.hasValue(), "load C# managed component through the persisted Script AssetId"))
    {
        std::cerr << hostResult.error().message << '\n';
        return 1;
    }
    auto host = std::move(hostResult).value();

    astro::runtime::CSharpScriptUpdateV1 unsupported;
    unsupported.apiVersion = astro::runtime::CSharpScriptApiVersion + 1;
    auto unsupportedBytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(&unsupported), sizeof(unsupported));
    auto unsupportedResult = astro::runtime::validateCSharpScriptUpdateV1(unsupportedBytes);
    if (!expect(!unsupportedResult.hasValue() && unsupportedResult.error().code == astro::core::ErrorCode::VersionMismatch,
        "native API rejects an unsupported ABI version"))
        return 1;
    std::array<std::byte, 4> truncatedPayload{};
    if (!expect(!astro::runtime::validateCSharpScriptUpdateV1(truncatedPayload).hasValue(),
        "native API rejects a truncated ABI struct"))
        return 1;

    astro::scene::Scene authoring(8101, "C# Runtime Integration");
    const auto entityResult = authoring.createEntity("Scripted");
    if (!expect(entityResult.hasValue(), "create authoring entity")) return 1;
    const auto entityId = entityResult.value();
    astro::scene::Transform initial;
    initial.position.x = 4.0;
    if (!expect(authoring.setTransform(entityId, initial).hasValue(), "set authoring transform")) return 1;

    astro::runtime::RuntimeSession session;
    auto playResult = session.play(authoring, [&host, entityId](astro::scene::Scene& scene, double delta) {
        return host->fixedUpdate(scene, entityId, delta);
    }, {0.1, 0.5, 8});
    if (!expect(playResult.hasValue(), "enter isolated Play Mode")) return 1;
    auto tick = session.tick(0.3);
    ok &= expect(tick.hasValue() && tick.value().fixedStepCount == 3, "run three fixed managed updates");
    const auto* runtimeTransform = session.runtimeScene()->transform(entityId);
    ok &= expect(runtimeTransform && std::abs(runtimeTransform->value.position.x - 4.3) < 1e-9,
        "managed update modifies runtime scene through the fixed-step adapter");
    ok &= expect(authoring.transform(entityId)->value.position.x == 4.0, "authoring scene remains isolated");
    session.stop();
    ok &= expect(session.runtimeScene() == nullptr && authoring.transform(entityId)->value.position.x == 4.0,
        "Stop discards runtime script changes");

    const auto* registeredBinding = registry.findScript(scriptAssetId);
    const auto behavior = registeredBinding
        ? authoring.addScriptBehavior(entityId, scriptAssetId, registeredBinding->managedTypeName)
        : astro::core::Result<astro::scene::ComponentId>::failure(
            {astro::core::ErrorCode::NotFound, "registered sample binding is missing"});
    const auto disabledBehavior = authoring.addScriptBehavior(entityId, {999998}, "Missing.Disabled", false);
    ok &= expect(behavior && disabledBehavior, "author enabled and disabled ScriptBehavior components");
    std::size_t approvalRechecks = 0;
    auto scriptedRuntime = astro::runtime::CSharpScriptRuntime::load(dotnetRoot, registry, authoring,
        [&](const astro::assets::AssetId asset) -> astro::core::Result<void> {
            ++approvalRechecks;
            return asset == scriptAssetId ? astro::core::Result<void>::success()
                : astro::core::Result<void>::failure({astro::core::ErrorCode::ValidationError,
                    "unexpected ScriptBehavior asset"});
        });
    if (!expect(scriptedRuntime && scriptedRuntime.value()->componentCount() == 1 && approvalRechecks == 1,
        "load only enabled ScriptBehavior entries after a per-host approval recheck")) return 1;
    auto scriptSet = std::move(scriptedRuntime).value();
    astro::scene::Scene stoppedRuntime(0, "uninitialized");
    auto lifecyclePlay = session.playWithLifecycle(authoring,
        [&](astro::scene::Scene& scene) { return scriptSet->onCreate(scene); },
        [&](astro::scene::Scene& scene, const double delta) { return scriptSet->fixedUpdate(scene, delta); },
        [&](astro::scene::Scene& scene) {
            auto destroyed = scriptSet->onDestroy(scene);
            stoppedRuntime = scene;
            return destroyed;
        }, {0.1, 0.5, 8});
    ok &= expect(lifecyclePlay && session.runtimeScene()->transform(entityId)->value.position.y == 2.0,
        "OnCreate runs on the runtime copy before the session starts ticking");
    const auto lifecycleTick = session.tick(0.3);
    ok &= expect(lifecycleTick && lifecycleTick.value().fixedStepCount == 3
        && std::abs(session.runtimeScene()->transform(entityId)->value.position.x - 4.3) < 1e-9,
        "C# FixedUpdate runs once for each RuntimeSession fixed step");
    const auto lifecycleStop = session.stopChecked();
    ok &= expect(lifecycleStop && stoppedRuntime.transform(entityId)
            && stoppedRuntime.transform(entityId)->value.position.y == 2.0
            && stoppedRuntime.transform(entityId)->value.position.z == 3.0
            && authoring.transform(entityId)->value.position.y == 0.0
            && authoring.transform(entityId)->value.position.z == 0.0,
        "OnDestroy runs before Stop discards the runtime copy and authoring data stays isolated");

    auto refusedLoad = astro::runtime::CSharpScriptRuntime::load(dotnetRoot, registry, authoring,
        [](astro::assets::AssetId) -> astro::core::Result<void> {
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "approval changed before managed load"});
        });
    ok &= expect(!refusedLoad && session.state() == astro::runtime::RuntimeState::Stopped,
        "a failed approval recheck prevents C# host load and Play startup");

    auto missingSetScene = authoring;
    const auto missingBehavior = missingSetScene.addScriptBehavior(entityId, {999999}, "Missing.Script");
    auto incompleteSet = astro::runtime::CSharpScriptRuntime::load(dotnetRoot, registry, missingSetScene,
        [](astro::assets::AssetId) { return astro::core::Result<void>::success(); });
    ok &= expect(missingBehavior && !incompleteSet && session.state() == astro::runtime::RuntimeState::Stopped
        && session.runtimeScene() == nullptr,
        "a missing binding rejects the complete script set before any lifecycle callback or Play");

    auto missingEntity = host->fixedUpdate(authoring, 999999, 0.1);
    ok &= expect(!missingEntity.hasValue() && missingEntity.error().code == astro::core::ErrorCode::NotFound,
        "unknown runtime entity is rejected before entering managed code");
    auto invalidStep = host->fixedUpdate(authoring, entityId, 0.0);
    ok &= expect(!invalidStep.hasValue() && invalidStep.error().code == astro::core::ErrorCode::InvalidArgument,
        "invalid fixed timestep is rejected by the native API");

    astro::scene::Scene overflowScene(8102, "C# Transform Validation");
    const auto overflowEntity = overflowScene.createEntity("Overflow");
    ok &= expect(overflowEntity.hasValue(), "create transform validation fixture");
    if (overflowEntity)
    {
        astro::scene::Transform nearLimit;
        nearLimit.position.x = 1.7e308;
        ok &= expect(overflowScene.setTransform(overflowEntity.value(), nearLimit).hasValue(),
            "set large but finite validation fixture");
        auto overflow = host->fixedUpdate(overflowScene, overflowEntity.value(), 1.7e308);
        ok &= expect(!overflow.hasValue()
                && overflowScene.transform(overflowEntity.value())->value.position.x == 1.7e308,
            "non-finite C# Transform update is rejected without changing Scene data");
    }

    const auto source = root / "Scripts" / "SceneMotion.cs";
    std::ofstream append(source, std::ios::binary | std::ios::app);
    append << ' ';
    append.close();
    const auto stale = registry.resolveScript(scriptAssetId);
    ok &= expect(!stale && stale.error().code == astro::core::ErrorCode::InvalidState,
        "reject resolution after the source no longer matches the registered build");
    std::error_code error;
    std::filesystem::copy_file(sampleSource, source, std::filesystem::copy_options::overwrite_existing, error);
    ok &= expect(!error && registry.resolveScript(scriptAssetId),
        "restore only the test-owned script source and resolve it again");
    host.reset();
    return ok ? 0 : 1;
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 6 && std::wstring_view(argv[1]) == L"--run-managed")
    {
        wchar_t* end = nullptr;
        const auto id = std::wcstoull(argv[4], &end, 10);
        if (!end || *end != L'\0' || id == 0) return 2;
        return runManagedHostChecks(argv[2], argv[3], argv[5], {id});
    }
    if (argc != 5) return 2;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / (L"Astro CSharp Script Registry Ω " + std::to_wstring(unique));
    std::error_code error;
    std::filesystem::create_directories(root / "Scripts", error);
    if (error) return 1;
    const auto output = root / "Library" / "Scripts";
    std::filesystem::create_directories(output, error);
    if (error) { std::filesystem::remove_all(root, error); return 1; }
    const auto assembly = output / "AstroEngine.CSharpSample.dll";
    const auto runtimeConfig = output / "AstroEngine.CSharpSample.runtimeconfig.json";
    const auto source = root / "Scripts" / "SceneMotion.cs";
    std::filesystem::copy_file(argv[3], assembly, std::filesystem::copy_options::overwrite_existing, error);
    if (!error)
        std::filesystem::copy_file(argv[2], runtimeConfig, std::filesystem::copy_options::overwrite_existing, error);
    if (!error)
        std::filesystem::copy_file(argv[4], source, std::filesystem::copy_options::overwrite_existing, error);
    if (error) { std::filesystem::remove_all(root, error); return 1; }

    astro::assets::AssetId scriptAssetId;
    {
        auto registryResult = astro::assets::AssetRegistry::open(root);
        if (!expect(registryResult.hasValue(), "open project asset registry for managed scripts"))
        {
            std::filesystem::remove_all(root, error);
            return 1;
        }
        auto registry = std::move(registryResult).value();
        const std::string manifest = R"({"schemaVersion":1,"sdkVersion":"10.0.404","targetFramework":"net10.0","assemblyName":"AstroEngine.CSharpSample","scriptBindings":[{"sourcePath":"Scripts/SceneMotion.cs","managedTypeName":"AstroEngine.CSharpSample.SceneMotion"}]})";
        {
            std::ofstream output(root / ".astro" / "csharp-build.json", std::ios::binary | std::ios::trunc);
            output.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
        }
        const auto sourceBytes = readBytes(source);
        const auto assemblyBytes = readBytes(assembly);
        const auto runtimeConfigBytes = readBytes(runtimeConfig);
        const auto sourceHash = astro::assets::sha256Hex(sourceBytes);
        const auto manifestHash = astro::assets::sha256Hex(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size()));
        std::ostringstream state;
        state << "ASTRO_CSHARP_PROJECT_BUILD 1\nFINGERPRINT " << std::string(64, 'b')
            << "\nMANIFEST_SHA256 " << manifestHash << "\nSDK_VERSION \"10.0.404\"\n"
            << "TARGET_FRAMEWORK \"net10.0\"\nASSEMBLY_NAME \"AstroEngine.CSharpSample\"\n"
            << "ASSEMBLY_PATH \"Library/Scripts/AstroEngine.CSharpSample.dll\"\nASSEMBLY_SHA256 "
            << astro::assets::sha256Hex(assemblyBytes) << "\nRUNTIME_CONFIG_PATH \"Library/Scripts/AstroEngine.CSharpSample.runtimeconfig.json\"\n"
            << "RUNTIME_CONFIG_SHA256 " << astro::assets::sha256Hex(runtimeConfigBytes)
            << "\nSOURCE_COUNT 1\nSOURCE \"Scripts/SceneMotion.cs\" " << sourceHash
            << "\nBINDING_COUNT 1\nBINDING \"Scripts/SceneMotion.cs\" \"AstroEngine.CSharpSample.SceneMotion\"\nEND\n";
        {
            const auto text = state.str();
            std::ofstream output(root / "Library" / "Scripts" / "asset-bindings.state", std::ios::binary | std::ios::trunc);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            const auto stateHash = astro::assets::sha256Hex(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
            const auto buildStateJson = std::string("{\"schemaVersion\":2,\"assetStatePath\":\"Library/Scripts/asset-bindings.state\",\"assetStateSha256\":\"")
                + stateHash + "\"}\n";
            std::ofstream buildState(root / "Library" / "Scripts" / "build-state.json", std::ios::binary | std::ios::trunc);
            buildState.write(buildStateJson.data(), static_cast<std::streamsize>(buildStateJson.size()));
        }
        const auto report = registry.inspectScriptBuild();
        if (!expect(report.freshness == astro::assets::ScriptBuildFreshness::Current
                && report.requiresRegistration,
            "inspect a current project build handoff before registry synchronization"))
            return 1;
        auto registered = registry.registerScriptsFromBuild();
        if (!expect(registered.hasValue(), "register the compiled C# sample source and managed type"))
        {
            std::filesystem::remove_all(root, error);
            return 1;
        }
        if (!expect(registered.value().size() == 1 && !registry.inspectScriptBuild().requiresRegistration,
            "synchronize the explicit managed binding into the project asset index")) return 1;
        scriptAssetId = registered.value().front().id;
    }

    std::wstring commandLine = quoteArgument(argv[0]) + L" --run-managed " + quoteArgument(root.wstring())
        + L" " + quoteArgument(argv[1]) + L" " + std::to_wstring(scriptAssetId.value)
        + L" " + quoteArgument(argv[4]);
    std::vector<wchar_t> writableCommand(commandLine.begin(), commandLine.end());
    writableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(argv[0], writableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!created)
    {
        std::cerr << "FAIL: start the isolated managed test process (Windows error " << GetLastError() << ")\n";
        std::filesystem::remove_all(root, error);
        return 1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD childExitCode = 1;
    GetExitCodeProcess(process.hProcess, &childExitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    error.clear();
    std::filesystem::remove_all(root, error);
    const bool cleanupSucceeded = !error;
    if (!cleanupSucceeded)
        std::cerr << "FAIL: remove only the test-owned project (" << error.message() << ")\n";
    return childExitCode == 0 && cleanupSucceeded ? 0 : 1;
}
