#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/runtime/CSharpScriptHost.hpp"
#include "astro/scene/Scene.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace astro::runtime
{
// Per-Play ScriptBehavior schedule. It is an in-process trusted-code runner, not a sandbox.
class CSharpScriptRuntime final
{
public:
    using BeforeLoadCheck = std::function<core::Result<void>(assets::AssetId)>;

    [[nodiscard]] static core::Result<std::unique_ptr<CSharpScriptRuntime>> load(
        const std::filesystem::path& dotnetRoot, const assets::AssetRegistry& registry,
        const scene::Scene& authoringScene, BeforeLoadCheck beforeEachHostLoad);

    CSharpScriptRuntime(const CSharpScriptRuntime&) = delete;
    CSharpScriptRuntime& operator=(const CSharpScriptRuntime&) = delete;
    CSharpScriptRuntime(CSharpScriptRuntime&&) = delete;
    CSharpScriptRuntime& operator=(CSharpScriptRuntime&&) = delete;

    [[nodiscard]] core::Result<void> onCreate(scene::Scene& runtimeScene);
    [[nodiscard]] core::Result<void> fixedUpdate(scene::Scene& runtimeScene, double fixedDeltaSeconds);
    [[nodiscard]] core::Result<void> onDestroy(scene::Scene& runtimeScene);
    [[nodiscard]] std::size_t componentCount() const noexcept { return components_.size(); }

private:
    CSharpScriptRuntime() = default;
    struct Component
    {
        scene::EntityId entity = 0;
        scene::ComponentId component = 0;
        std::shared_ptr<CSharpScriptHost> host;
    };

    [[nodiscard]] core::Result<void> destroyCreated(scene::Scene& runtimeScene);
    std::vector<Component> components_;
    std::size_t createdCount_ = 0;
};
}
