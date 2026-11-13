#pragma once

#include "astro/runtime/CheckpointSystem.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace astro::save_editor
{
struct SaveGroup
{
    std::string id;
    std::set<scene::EntityId> entities;
};

struct SaveProfileDraft
{
    std::string id;
    std::uint32_t schemaVersion = 1;
    std::map<scene::EntityId, std::set<std::string>> fields;
    std::map<std::string, std::set<std::string>> groupFields;
};

// In-memory editor authoring model. Runtime profiles are always flattened to
// explicit entity/property pairs before SaveStore can consume them.
class SaveSystemDocument final
{
public:
    core::Result<void> createGroup(std::string id);
    core::Result<void> setGroupMember(std::string_view group, scene::EntityId entity, bool member);
    core::Result<void> createProfile(std::string id, std::uint32_t schemaVersion = 1);
    core::Result<void> setField(std::string_view profile, scene::EntityId entity,
        std::string property, bool selected);
    core::Result<void> setGroupField(std::string_view profile, std::string_view group,
        std::string property, bool selected);
    core::Result<void> setCheckpoint(runtime::CheckpointDefinition definition);
    core::Result<void> removeCheckpoint(scene::EntityId entity);
    core::Result<std::map<std::string, runtime::SaveProfile>> compileProfiles(const scene::Scene& scene) const;
    core::Result<std::vector<runtime::CheckpointDefinition>> validateCheckpoints(
        const scene::Scene& scene, const std::map<std::string, runtime::SaveProfile>& profiles) const;

    [[nodiscard]] const std::map<std::string, SaveGroup>& groups() const noexcept { return groups_; }
    [[nodiscard]] const std::map<std::string, SaveProfileDraft>& profiles() const noexcept { return profiles_; }
    [[nodiscard]] const std::map<scene::EntityId, runtime::CheckpointDefinition>& checkpoints() const noexcept { return checkpoints_; }

private:
    std::map<std::string, SaveGroup> groups_;
    std::map<std::string, SaveProfileDraft> profiles_;
    std::map<scene::EntityId, runtime::CheckpointDefinition> checkpoints_;
};
}
