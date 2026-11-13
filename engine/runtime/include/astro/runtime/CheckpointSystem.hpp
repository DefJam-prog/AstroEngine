#pragma once

#include "astro/core/EventBus.hpp"
#include "astro/runtime/SaveStore.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace astro::runtime
{
enum class CheckpointMode : std::uint8_t { RespawnOnly, SaveSelectedProfile };
enum class CheckpointRepeatMode : std::uint8_t { Repeatable, OneShot };
enum class CheckpointEventKind : std::uint8_t { Activated, Rejected, Respawned };
enum class RespawnRestoreMode : std::uint8_t { PositionOnly, SelectedProfile };

struct CheckpointArea2D
{
    double halfWidth = 1.0;
    double halfHeight = 1.0;
};

struct CheckpointDefinition
{
    scene::EntityId entity = 0;
    CheckpointArea2D area{};
    scene::Vec3 respawnPosition{};
    CheckpointMode mode = CheckpointMode::RespawnOnly;
    CheckpointRepeatMode repeatMode = CheckpointRepeatMode::Repeatable;
    std::string saveProfileId;
    std::size_t slot = 0;
    bool enabled = true;
    std::function<core::Result<bool>(const scene::Scene&, scene::EntityId actor)> condition;
};

struct CheckpointEvent
{
    std::uint64_t sequence = 0;
    CheckpointEventKind kind = CheckpointEventKind::Activated;
    scene::EntityId actor = 0;
    scene::EntityId checkpoint = 0;
    std::string diagnostic;
};

struct CheckpointSaveContext
{
    SaveStore* store = nullptr;
    const std::map<std::string, SaveProfile>* profiles = nullptr;
    std::string_view gameVersion;
};

struct CheckpointUpdateResult
{
    std::size_t activated = 0;
    std::size_t rejected = 0;
};

struct RespawnRules
{
    RespawnRestoreMode restoreMode = RespawnRestoreMode::PositionOnly;
    scene::Vec3 fallbackPosition{};
};

struct RespawnResult
{
    std::optional<scene::EntityId> checkpoint;
    scene::Vec3 position{};
    bool restoredSelectedProfile = false;
    std::size_t restoredProperties = 0;
    std::size_t missingEntities = 0;
    std::size_t missingComponents = 0;
};

// Runtime-only checkpoint triggers; register them against the isolated Play Scene.
class CheckpointSystem final
{
public:
    core::Result<void> registerCheckpoint(const scene::Scene& scene, CheckpointDefinition definition);
    core::Result<void> unregisterCheckpoint(scene::EntityId checkpoint);
    core::Result<void> setEnabled(scene::EntityId checkpoint, bool enabled);
    [[nodiscard]] std::optional<scene::EntityId> activeCheckpoint() const noexcept { return activeCheckpoint_; }

    core::Result<CheckpointUpdateResult> update(const scene::Scene& runtimeScene,
        scene::EntityId actor, core::EventBus& events, const CheckpointSaveContext* saveContext = nullptr);
    core::Result<RespawnResult> respawn(scene::Scene& runtimeScene, scene::EntityId actor,
        const RespawnRules& rules, core::EventBus& events,
        const CheckpointSaveContext* saveContext = nullptr);

private:
    struct RuntimeCheckpoint
    {
        CheckpointDefinition definition;
        std::set<scene::EntityId> actorsInside;
        bool consumed = false;
    };
    core::Result<void> publish(core::EventBus& events, CheckpointEventKind kind,
        scene::EntityId actor, scene::EntityId checkpoint, std::string diagnostic = {});
    std::map<scene::EntityId, RuntimeCheckpoint> checkpoints_;
    std::optional<scene::SceneId> sceneId_;
    std::optional<scene::EntityId> activeCheckpoint_;
    std::uint64_t nextSequence_ = 1;
};
}
