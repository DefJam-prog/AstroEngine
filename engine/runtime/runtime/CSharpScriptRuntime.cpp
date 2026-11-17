#include "astro/runtime/CSharpScriptRuntime.hpp"

#include <exception>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace astro::runtime
{
namespace
{
core::Error runtimeError(core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}
}

core::Result<std::unique_ptr<CSharpScriptRuntime>> CSharpScriptRuntime::load(
    const std::filesystem::path& dotnetRoot, const assets::AssetRegistry& registry,
    const scene::Scene& authoringScene, BeforeLoadCheck beforeEachHostLoad)
{
    if (dotnetRoot.empty() || !beforeEachHostLoad)
        return core::Result<std::unique_ptr<CSharpScriptRuntime>>::failure(runtimeError(
            core::ErrorCode::InvalidArgument, "C# Play requires a .NET root and an approval recheck before host load"));

    auto result = std::unique_ptr<CSharpScriptRuntime>(new CSharpScriptRuntime());
    std::map<assets::AssetId, std::shared_ptr<CSharpScriptHost>> loadedHosts;
    for (const auto& entity : authoringScene.entities())
    {
        for (const auto& behavior : entity.scriptBehaviors)
        {
            if (!behavior.enabled) continue;
            if (behavior.id == 0 || behavior.scriptAsset.value == 0)
                return core::Result<std::unique_ptr<CSharpScriptRuntime>>::failure(runtimeError(
                    core::ErrorCode::ValidationError, "Enabled ScriptBehavior has an invalid component or AssetId"));

            auto host = loadedHosts.find(behavior.scriptAsset);
            if (host == loadedHosts.end())
            {
                const auto* binding = registry.findScript(behavior.scriptAsset);
                if (!binding || binding->managedTypeName != behavior.managedTypeName)
                    return core::Result<std::unique_ptr<CSharpScriptRuntime>>::failure(runtimeError(
                        core::ErrorCode::ValidationError, "Enabled ScriptBehavior does not match a registered managed type"));
                core::Result<void> approved = core::Result<void>::failure(runtimeError(
                    core::ErrorCode::InternalError, "Approval recheck did not complete"));
                try { approved = beforeEachHostLoad(behavior.scriptAsset); }
                catch (const std::exception& exception)
                {
                    approved = core::Result<void>::failure(runtimeError(core::ErrorCode::InternalError,
                        std::string("C# approval recheck threw an exception: ") + exception.what()));
                }
                catch (...)
                {
                    approved = core::Result<void>::failure(runtimeError(core::ErrorCode::InternalError,
                        "C# approval recheck threw an unknown exception"));
                }
                if (!approved) return core::Result<std::unique_ptr<CSharpScriptRuntime>>::failure(approved.error());

                auto loaded = CSharpScriptHost::loadScriptAsset(dotnetRoot, registry, behavior.scriptAsset);
                if (!loaded) return core::Result<std::unique_ptr<CSharpScriptRuntime>>::failure(loaded.error());
                auto sharedHost = std::shared_ptr<CSharpScriptHost>(std::move(loaded).value());
                host = loadedHosts.emplace(behavior.scriptAsset, std::move(sharedHost)).first;
            }
            result->components_.push_back({entity.id, behavior.id, host->second});
        }
    }
    return core::Result<std::unique_ptr<CSharpScriptRuntime>>::success(std::move(result));
}

core::Result<void> CSharpScriptRuntime::onCreate(scene::Scene& runtimeScene)
{
    if (createdCount_ != 0)
        return core::Result<void>::failure(runtimeError(core::ErrorCode::InvalidState,
            "ScriptBehavior OnCreate has already run for this Play session"));
    for (const auto& component : components_)
    {
        auto created = component.host->onCreate(runtimeScene, component.entity);
        if (!created)
        {
            const auto original = created.error();
            const auto rollback = destroyCreated(runtimeScene);
            if (!rollback)
                return core::Result<void>::failure(runtimeError(original.code,
                    original.message + "; OnCreate rollback reported: " + rollback.error().message));
            return core::Result<void>::failure(original);
        }
        ++createdCount_;
    }
    return core::Result<void>::success();
}

core::Result<void> CSharpScriptRuntime::fixedUpdate(scene::Scene& runtimeScene,
    const double fixedDeltaSeconds)
{
    if (createdCount_ != components_.size())
        return core::Result<void>::failure(runtimeError(core::ErrorCode::InvalidState,
            "ScriptBehavior FixedUpdate requires every OnCreate to succeed"));
    for (const auto& component : components_)
    {
        auto updated = component.host->fixedUpdate(runtimeScene, component.entity, fixedDeltaSeconds);
        if (!updated) return updated;
    }
    return core::Result<void>::success();
}

core::Result<void> CSharpScriptRuntime::destroyCreated(scene::Scene& runtimeScene)
{
    std::optional<core::Error> firstError;
    while (createdCount_ > 0)
    {
        --createdCount_;
        const auto& component = components_[createdCount_];
        auto destroyed = component.host->onDestroy(runtimeScene, component.entity);
        if (!destroyed && !firstError) firstError = destroyed.error();
    }
    if (firstError)
        return core::Result<void>::failure(runtimeError(firstError->code,
            "One or more ScriptBehavior OnDestroy callbacks failed: " + firstError->message));
    return core::Result<void>::success();
}

core::Result<void> CSharpScriptRuntime::onDestroy(scene::Scene& runtimeScene)
{
    return destroyCreated(runtimeScene);
}
}
