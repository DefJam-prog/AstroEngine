#include "astro/runtime/CheckpointSystem.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace astro::runtime
{
namespace
{
core::Error checkpointError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool finite(const scene::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool safeProfileId(const std::string_view value)
{
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
            || ch == '.' || ch == '_' || ch == '-';
    });
}
}

core::Result<void> CheckpointSystem::registerCheckpoint(const scene::Scene& scene,
    CheckpointDefinition definition)
{
    if (!definition.entity || !scene.find(definition.entity))
        return core::Result<void>::failure(checkpointError(core::ErrorCode::NotFound, "Checkpoint entity was not found"));
    if (!scene.transform(definition.entity))
        return core::Result<void>::failure(checkpointError(core::ErrorCode::ValidationError, "Checkpoint entity requires a Transform"));
    if (!std::isfinite(definition.area.halfWidth) || !std::isfinite(definition.area.halfHeight)
        || definition.area.halfWidth <= 0.0 || definition.area.halfHeight <= 0.0
        || definition.area.halfWidth > 1000000.0 || definition.area.halfHeight > 1000000.0
        || !finite(definition.respawnPosition))
        return core::Result<void>::failure(checkpointError(core::ErrorCode::ValidationError,
            "Checkpoint area and respawn position must contain finite, valid values"));
    if (definition.mode == CheckpointMode::SaveSelectedProfile)
    {
        if (!safeProfileId(definition.saveProfileId) || definition.slot >= 100)
            return core::Result<void>::failure(checkpointError(core::ErrorCode::ValidationError,
                "Save checkpoint requires a safe profile ID and slot below 100"));
    }
    else if (!definition.saveProfileId.empty())
        return core::Result<void>::failure(checkpointError(core::ErrorCode::ValidationError,
            "Respawn-only checkpoint cannot declare a save profile"));
    if ((definition.mode != CheckpointMode::RespawnOnly && definition.mode != CheckpointMode::SaveSelectedProfile)
        || (definition.repeatMode != CheckpointRepeatMode::Repeatable && definition.repeatMode != CheckpointRepeatMode::OneShot))
        return core::Result<void>::failure(checkpointError(core::ErrorCode::InvalidArgument, "Unknown checkpoint mode"));
    if (sceneId_ && *sceneId_ != scene.id())
        return core::Result<void>::failure(checkpointError(core::ErrorCode::InvalidArgument,
            "Checkpoint system cannot bind entities from multiple scenes"));
    if (checkpoints_.size() >= 100000 && !checkpoints_.contains(definition.entity))
        return core::Result<void>::failure(checkpointError(core::ErrorCode::ValidationError, "Runtime checkpoint limit reached"));
    const auto id = definition.entity;
    if (!checkpoints_.emplace(id, RuntimeCheckpoint{std::move(definition)}).second)
        return core::Result<void>::failure(checkpointError(core::ErrorCode::AlreadyExists, "Checkpoint entity is already registered"));
    sceneId_ = scene.id();
    return core::Result<void>::success();
}

core::Result<void> CheckpointSystem::unregisterCheckpoint(const scene::EntityId checkpoint)
{
    if (!checkpoints_.erase(checkpoint))
        return core::Result<void>::failure(checkpointError(core::ErrorCode::NotFound, "Checkpoint is not registered"));
    if (activeCheckpoint_ == checkpoint) activeCheckpoint_.reset();
    if (checkpoints_.empty()) sceneId_.reset();
    return core::Result<void>::success();
}

core::Result<void> CheckpointSystem::setEnabled(const scene::EntityId checkpoint, const bool enabled)
{
    const auto found = checkpoints_.find(checkpoint);
    if (found == checkpoints_.end())
        return core::Result<void>::failure(checkpointError(core::ErrorCode::NotFound, "Checkpoint is not registered"));
    found->second.definition.enabled = enabled;
    return core::Result<void>::success();
}

core::Result<void> CheckpointSystem::publish(core::EventBus& events, const CheckpointEventKind kind,
    const scene::EntityId actor, const scene::EntityId checkpoint, std::string diagnostic)
{
    if (nextSequence_ == std::numeric_limits<std::uint64_t>::max())
        return core::Result<void>::failure(checkpointError(core::ErrorCode::InternalError, "Checkpoint event sequence exhausted"));
    return events.publish(CheckpointEvent{nextSequence_++, kind, actor, checkpoint, std::move(diagnostic)});
}

core::Result<CheckpointUpdateResult> CheckpointSystem::update(const scene::Scene& runtimeScene,
    const scene::EntityId actor, core::EventBus& events, const CheckpointSaveContext* saveContext)
{
    if (!sceneId_ || *sceneId_ != runtimeScene.id())
        return core::Result<CheckpointUpdateResult>::failure(checkpointError(core::ErrorCode::InvalidArgument,
            "Checkpoint system is bound to a different Scene"));
    const auto* actorTransform = runtimeScene.transform(actor);
    if (!runtimeScene.find(actor) || !actorTransform)
        return core::Result<CheckpointUpdateResult>::failure(checkpointError(core::ErrorCode::NotFound,
            "Checkpoint actor or its Transform was not found"));

    CheckpointUpdateResult result;
    for (auto& [checkpointId, checkpoint] : checkpoints_)
    {
        const auto& definition = checkpoint.definition;
        const auto* checkpointTransform = runtimeScene.transform(checkpointId);
        if (!runtimeScene.find(checkpointId) || !checkpointTransform)
        {
            checkpoint.actorsInside.erase(actor);
            continue;
        }
        const auto& center = checkpointTransform->value.position;
        const auto& position = actorTransform->value.position;
        const bool inside = definition.enabled
            && std::abs(position.x - center.x) <= definition.area.halfWidth
            && std::abs(position.y - center.y) <= definition.area.halfHeight;
        if (!inside)
        {
            checkpoint.actorsInside.erase(actor);
            continue;
        }
        if (!checkpoint.actorsInside.insert(actor).second) continue;

        std::string rejection;
        if (checkpoint.consumed)
            rejection = "One-shot checkpoint has already been activated";
        else if (definition.condition)
        {
            try
            {
                auto condition = definition.condition(runtimeScene, actor);
                if (!condition) rejection = condition.error().message;
                else if (!condition.value()) rejection = "Checkpoint condition was not met";
            }
            catch (const std::exception& exception) { rejection = std::string("Checkpoint condition threw: ") + exception.what(); }
            catch (...) { rejection = "Checkpoint condition threw an unknown exception"; }
        }

        if (rejection.empty() && definition.mode == CheckpointMode::SaveSelectedProfile)
        {
            if (!saveContext || !saveContext->store || !saveContext->profiles)
                rejection = "Checkpoint requires a SaveStore and profile collection";
            else
            {
                const auto profile = saveContext->profiles->find(definition.saveProfileId);
                if (profile == saveContext->profiles->end()) rejection = "Checkpoint SaveProfile was not found";
                else if (profile->second.id() != definition.saveProfileId) rejection = "Checkpoint SaveProfile ID does not match its registry key";
                else
                {
                    auto saved = saveContext->store->save(definition.slot, runtimeScene, profile->second, saveContext->gameVersion);
                    if (!saved) rejection = saved.error().message;
                }
            }
        }

        if (!rejection.empty())
        {
            ++result.rejected;
            const auto notified = publish(events, CheckpointEventKind::Rejected, actor, checkpointId, std::move(rejection));
            if (!notified) return core::Result<CheckpointUpdateResult>::failure(notified.error());
            continue;
        }

        activeCheckpoint_ = checkpointId;
        if (definition.repeatMode == CheckpointRepeatMode::OneShot) checkpoint.consumed = true;
        ++result.activated;
        const auto notified = publish(events, CheckpointEventKind::Activated, actor, checkpointId);
        if (!notified) return core::Result<CheckpointUpdateResult>::failure(notified.error());
    }
    return core::Result<CheckpointUpdateResult>::success(result);
}

core::Result<RespawnResult> CheckpointSystem::respawn(scene::Scene& runtimeScene,
    const scene::EntityId actor, const RespawnRules& rules, core::EventBus& events,
    const CheckpointSaveContext* saveContext)
{
    if (sceneId_ && *sceneId_ != runtimeScene.id())
        return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::InvalidArgument,
            "Checkpoint system is bound to a different Scene"));
    if (!finite(rules.fallbackPosition))
        return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::ValidationError, "Respawn fallback position must be finite"));
    if (!runtimeScene.find(actor) || !runtimeScene.transform(actor))
        return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::NotFound, "Respawn actor or Transform was not found"));

    const RuntimeCheckpoint* active = nullptr;
    if (activeCheckpoint_)
    {
        const auto found = checkpoints_.find(*activeCheckpoint_);
        if (found != checkpoints_.end()) active = &found->second;
    }
    if (rules.restoreMode == RespawnRestoreMode::SelectedProfile
        && (!active || active->definition.mode != CheckpointMode::SaveSelectedProfile))
        return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::InvalidState,
            "Selected-profile respawn requires an active checkpoint that saves a profile"));
    if (rules.restoreMode != RespawnRestoreMode::PositionOnly && rules.restoreMode != RespawnRestoreMode::SelectedProfile)
        return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::InvalidArgument, "Unknown respawn restore mode"));

    auto candidate = runtimeScene;
    RespawnResult result;
    result.checkpoint = activeCheckpoint_;
    result.position = active ? active->definition.respawnPosition : rules.fallbackPosition;
    if (rules.restoreMode == RespawnRestoreMode::SelectedProfile)
    {
        if (!saveContext || !saveContext->store || !saveContext->profiles)
            return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::InvalidState,
                "Selected-profile respawn requires a SaveStore and profiles"));
        const auto profile = saveContext->profiles->find(active->definition.saveProfileId);
        if (profile == saveContext->profiles->end())
            return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::NotFound, "Checkpoint SaveProfile was not found"));
        if (profile->second.id() != active->definition.saveProfileId)
            return core::Result<RespawnResult>::failure(checkpointError(core::ErrorCode::ValidationError,
                "Checkpoint SaveProfile ID does not match its registry key"));
        auto loaded = saveContext->store->load(active->definition.slot, candidate, profile->second, saveContext->gameVersion);
        if (!loaded) return core::Result<RespawnResult>::failure(loaded.error());
        result.restoredSelectedProfile = true;
        result.restoredProperties = loaded.value().appliedProperties;
        result.missingEntities = loaded.value().missingEntities;
        result.missingComponents = loaded.value().missingComponents;
    }

    auto transform = candidate.transform(actor)->value;
    transform.position = result.position;
    const auto placed = candidate.setTransform(actor, transform);
    if (!placed) return core::Result<RespawnResult>::failure(placed.error());
    runtimeScene = std::move(candidate);
    const auto notified = publish(events, CheckpointEventKind::Respawned, actor,
        result.checkpoint.value_or(0));
    if (!notified) return core::Result<RespawnResult>::failure(notified.error());
    return core::Result<RespawnResult>::success(std::move(result));
}
}
