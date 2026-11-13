#include "astro/runtime/InteractionSystem.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace astro::runtime
{
namespace
{
core::Error interactionError(core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}
}

core::Result<void> InteractionSystem::registerInteractable(const scene::Scene& scene,
    const scene::EntityId target, InteractableBehavior behavior)
{
    if (!target || !std::isfinite(behavior.activationRange) || behavior.activationRange < 0.0
        || behavior.activationRange > 1000000.0 || !behavior.callback)
        return core::Result<void>::failure(interactionError(core::ErrorCode::ValidationError,
            "Interactable requires a valid target, callback, and finite range from 0 to 1000000"));
    if (!scene.find(target))
        return core::Result<void>::failure(interactionError(core::ErrorCode::NotFound, "Interactable target entity was not found"));
    if (sceneId_ && *sceneId_ != scene.id())
        return core::Result<void>::failure(interactionError(core::ErrorCode::InvalidArgument,
            "Interaction system cannot bind entities from multiple scenes"));
    if (interactables_.size() >= 100000 && !interactables_.contains(target))
        return core::Result<void>::failure(interactionError(core::ErrorCode::ValidationError, "Runtime interaction limit reached"));
    if (!interactables_.emplace(target, std::move(behavior)).second)
        return core::Result<void>::failure(interactionError(core::ErrorCode::AlreadyExists, "Target already has an interactable behavior"));
    sceneId_ = scene.id();
    return core::Result<void>::success();
}

core::Result<void> InteractionSystem::unregisterInteractable(const scene::EntityId target)
{
    if (!interactables_.erase(target))
        return core::Result<void>::failure(interactionError(core::ErrorCode::NotFound, "Interactable behavior was not found"));
    if (interactables_.empty()) sceneId_.reset();
    return core::Result<void>::success();
}

core::Result<void> InteractionSystem::setEnabled(const scene::EntityId target, const bool enabled)
{
    const auto found = interactables_.find(target);
    if (found == interactables_.end())
        return core::Result<void>::failure(interactionError(core::ErrorCode::NotFound, "Interactable behavior was not found"));
    found->second.enabled = enabled;
    return core::Result<void>::success();
}

core::Result<void> InteractionSystem::canInteract(const scene::Scene& scene,
    const scene::EntityId actor, const scene::EntityId target) const
{
    if (!sceneId_ || *sceneId_ != scene.id())
        return core::Result<void>::failure(interactionError(core::ErrorCode::InvalidArgument,
            "Interaction system is bound to a different scene"));
    if (!actor || !target)
        return core::Result<void>::failure(interactionError(core::ErrorCode::InvalidArgument, "Actor and target IDs must be non-zero"));
    const auto* actorEntity = scene.find(actor);
    const auto* targetEntity = scene.find(target);
    if (!actorEntity || !targetEntity)
        return core::Result<void>::failure(interactionError(core::ErrorCode::NotFound, "Actor or target entity was not found"));
    const auto behavior = interactables_.find(target);
    if (behavior == interactables_.end())
        return core::Result<void>::failure(interactionError(core::ErrorCode::NotFound, "Target has no interactable behavior"));
    if (!behavior->second.enabled)
        return core::Result<void>::failure(interactionError(core::ErrorCode::InvalidState, "Target interaction is disabled"));
    const auto* actorTransform = scene.transform(actor);
    const auto* targetTransform = scene.transform(target);
    if (!actorTransform || !targetTransform)
        return core::Result<void>::failure(interactionError(core::ErrorCode::InvalidState, "Actor or target has no Transform"));
    const auto& a = actorTransform->value.position;
    const auto& b = targetTransform->value.position;
    const auto distance = std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    if (distance > behavior->second.activationRange)
        return core::Result<void>::failure(interactionError(core::ErrorCode::ValidationError, "Target is outside its interaction range"));
    return core::Result<void>::success();
}

core::Result<void> InteractionSystem::publish(core::EventBus& events,
    const InteractionEventKind kind, const scene::EntityId actor, const scene::EntityId target,
    std::string diagnostic)
{
    if (nextEventSequence_ == 0)
        return core::Result<void>::failure(interactionError(core::ErrorCode::InternalError, "Runtime interaction event sequence exhausted"));
    InteractionEvent event{nextEventSequence_++, kind, actor, target, std::move(diagnostic)};
    return events.publish(event);
}

core::Result<void> InteractionSystem::requestInteraction(scene::Scene& runtimeScene,
    core::EventBus& events, const scene::EntityId actor, const scene::EntityId target)
{
    if (!sceneId_ || *sceneId_ != runtimeScene.id())
        return core::Result<void>::failure(interactionError(core::ErrorCode::InvalidArgument,
            "Interaction system is bound to a different runtime scene"));
    if (!actor || !target || !runtimeScene.find(actor) || !runtimeScene.find(target))
        return core::Result<void>::failure(interactionError(core::ErrorCode::NotFound,
            "Interaction request requires existing actor and target entities"));
    auto published = publish(events, InteractionEventKind::Requested, actor, target);
    if (!published) return published;

    const auto eligible = canInteract(runtimeScene, actor, target);
    if (!eligible)
    {
        const auto rejected = publish(events, InteractionEventKind::Rejected, actor, target, eligible.error().message);
        if (!rejected)
            return core::Result<void>::failure(interactionError(rejected.error().code,
                eligible.error().message + "\n" + rejected.error().message));
        return eligible;
    }

    // Copy before publishing/calling user code: callbacks may unregister themselves.
    const auto behavior = interactables_.at(target);
    core::Result<void> activated = core::Result<void>::failure(
        interactionError(core::ErrorCode::InternalError, "Interaction callback did not complete"));
    try { activated = behavior.callback(runtimeScene, actor, target); }
    catch (const std::exception& exception)
    {
        activated = core::Result<void>::failure(interactionError(core::ErrorCode::InternalError,
            std::string("Interaction callback threw: ") + exception.what()));
    }
    catch (...)
    {
        activated = core::Result<void>::failure(interactionError(core::ErrorCode::InternalError,
            "Interaction callback threw an unknown exception"));
    }
    if (!activated)
    {
        const auto rejected = publish(events, InteractionEventKind::Rejected, actor, target, activated.error().message);
        if (!rejected)
            return core::Result<void>::failure(interactionError(rejected.error().code,
                activated.error().message + "\n" + rejected.error().message));
        return activated;
    }
    return publish(events, InteractionEventKind::Activated, actor, target);
}
}
