#pragma once

#include "astro/core/EventBus.hpp"
#include "astro/scene/Scene.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace astro::runtime
{
enum class InteractionEventKind : std::uint8_t { Requested, Activated, Rejected };

struct InteractionEvent
{
    std::uint64_t sequence = 0;
    InteractionEventKind kind = InteractionEventKind::Requested;
    scene::EntityId actor = 0;
    scene::EntityId target = 0;
    std::string diagnostic;
};

using InteractionCallback = std::function<core::Result<void>(scene::Scene&,
    scene::EntityId actor, scene::EntityId target)>;

struct InteractableBehavior
{
    double activationRange = 2.0;
    bool enabled = true;
    InteractionCallback callback;
};

// Runtime-only bindings; behavior runs against the RuntimeSession Scene copy.
class InteractionSystem final
{
public:
    core::Result<void> registerInteractable(const scene::Scene& scene, scene::EntityId target,
        InteractableBehavior behavior);
    core::Result<void> unregisterInteractable(scene::EntityId target);
    core::Result<void> setEnabled(scene::EntityId target, bool enabled);
    [[nodiscard]] core::Result<void> canInteract(const scene::Scene& scene,
        scene::EntityId actor, scene::EntityId target) const;
    core::Result<void> requestInteraction(scene::Scene& runtimeScene, core::EventBus& events,
        scene::EntityId actor, scene::EntityId target);

private:
    core::Result<void> publish(core::EventBus& events, InteractionEventKind kind,
        scene::EntityId actor, scene::EntityId target, std::string diagnostic = {});
    std::map<scene::EntityId, InteractableBehavior> interactables_;
    std::optional<scene::SceneId> sceneId_;
    std::uint64_t nextEventSequence_ = 1;
};
}
