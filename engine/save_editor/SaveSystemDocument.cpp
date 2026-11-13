#include "astro/save_editor/SaveSystemDocument.hpp"

#include <algorithm>

namespace astro::save_editor
{
namespace
{
core::Error invalid(std::string message)
{
    return {core::ErrorCode::ValidationError, std::move(message)};
}

bool supportedOnEntity(const scene::Scene& scene, const scene::EntityId entity, const std::string_view property)
{
    using namespace runtime::SaveProperty;
    if (!scene.find(entity)) return false;
    if (property == entityName) return true;
    if (property == spriteVisible) return scene.spriteRenderer(entity) != nullptr;
    return property == positionX || property == positionY || property == positionZ
        || property == rotationX || property == rotationY || property == rotationZ
        || property == scaleX || property == scaleY || property == scaleZ
        ? scene.transform(entity) != nullptr : false;
}
}

core::Result<void> SaveSystemDocument::createGroup(std::string id)
{
    auto valid = runtime::SaveProfile::create(id);
    if (!valid) return core::Result<void>::failure(valid.error());
    if (!groups_.emplace(id, SaveGroup{id, {}}).second)
        return core::Result<void>::failure({core::ErrorCode::AlreadyExists, "Save group already exists"});
    return core::Result<void>::success();
}

core::Result<void> SaveSystemDocument::setGroupMember(const std::string_view group,
    const scene::EntityId entity, const bool member)
{
    auto found = groups_.find(std::string(group));
    if (found == groups_.end()) return core::Result<void>::failure({core::ErrorCode::NotFound, "Save group was not found"});
    if (!entity) return core::Result<void>::failure(invalid("Entity ID must be nonzero"));
    if (member) found->second.entities.insert(entity);
    else found->second.entities.erase(entity);
    return core::Result<void>::success();
}

core::Result<void> SaveSystemDocument::createProfile(std::string id, const std::uint32_t schemaVersion)
{
    auto valid = runtime::SaveProfile::create(id, schemaVersion);
    if (!valid) return core::Result<void>::failure(valid.error());
    SaveProfileDraft draft; draft.id = id; draft.schemaVersion = schemaVersion;
    if (!profiles_.emplace(id, std::move(draft)).second)
        return core::Result<void>::failure({core::ErrorCode::AlreadyExists, "Save profile already exists"});
    return core::Result<void>::success();
}

core::Result<void> SaveSystemDocument::setField(const std::string_view profile, const scene::EntityId entity,
    std::string property, const bool selected)
{
    auto found = profiles_.find(std::string(profile));
    if (found == profiles_.end()) return core::Result<void>::failure({core::ErrorCode::NotFound, "Save profile was not found"});
    auto probe = runtime::SaveProfile::create(found->second.id, found->second.schemaVersion);
    if (!probe) return core::Result<void>::failure(probe.error());
    auto valid = probe.value().addField(entity, property);
    if (!valid) return valid;
    auto& fields = found->second.fields[entity];
    if (selected) fields.insert(std::move(property));
    else
    {
        fields.erase(property);
        if (fields.empty()) found->second.fields.erase(entity);
    }
    return core::Result<void>::success();
}

core::Result<void> SaveSystemDocument::setGroupField(const std::string_view profile,
    const std::string_view group, std::string property, const bool selected)
{
    auto p = profiles_.find(std::string(profile));
    if (p == profiles_.end()) return core::Result<void>::failure({core::ErrorCode::NotFound, "Save profile was not found"});
    if (!groups_.contains(std::string(group))) return core::Result<void>::failure({core::ErrorCode::NotFound, "Save group was not found"});
    auto probe = runtime::SaveProfile::create(p->second.id, p->second.schemaVersion);
    if (!probe) return core::Result<void>::failure(probe.error());
    auto valid = probe.value().addField(1, property);
    if (!valid) return valid;
    auto& fields = p->second.groupFields[std::string(group)];
    if (selected) fields.insert(std::move(property));
    else
    {
        fields.erase(property);
        if (fields.empty()) p->second.groupFields.erase(std::string(group));
    }
    return core::Result<void>::success();
}

core::Result<void> SaveSystemDocument::setCheckpoint(runtime::CheckpointDefinition definition)
{
    if (!definition.entity) return core::Result<void>::failure(invalid("Checkpoint entity ID must be nonzero"));
    checkpoints_.insert_or_assign(definition.entity, std::move(definition));
    return core::Result<void>::success();
}

core::Result<void> SaveSystemDocument::removeCheckpoint(const scene::EntityId entity)
{
    if (!checkpoints_.erase(entity))
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Checkpoint was not found"});
    return core::Result<void>::success();
}

core::Result<std::map<std::string, runtime::SaveProfile>> SaveSystemDocument::compileProfiles(const scene::Scene& scene) const
{
    std::map<std::string, runtime::SaveProfile> result;
    for (const auto& [id, draft] : profiles_)
    {
        auto created = runtime::SaveProfile::create(id, draft.schemaVersion);
        if (!created) return core::Result<std::map<std::string, runtime::SaveProfile>>::failure(created.error());
        auto profile = std::move(created).value();
        const auto add = [&](const scene::EntityId entity, const std::string& property) -> core::Result<void> {
            if (!supportedOnEntity(scene, entity, property))
                return core::Result<void>::failure(invalid("Selected save property is unavailable on entity " + std::to_string(entity) + ": " + property));
            const auto existing = profile.fields().find(entity);
            if (existing != profile.fields().end()
                && std::find(existing->second.begin(), existing->second.end(), property) != existing->second.end())
                return core::Result<void>::success();
            return profile.addField(entity, property);
        };
        for (const auto& [entity, properties] : draft.fields)
            for (const auto& property : properties)
            {
                auto added = add(entity, property);
                if (!added) return core::Result<std::map<std::string, runtime::SaveProfile>>::failure(added.error());
            }
        for (const auto& [groupId, properties] : draft.groupFields)
        {
            const auto group = groups_.find(groupId);
            if (group == groups_.end()) return core::Result<std::map<std::string, runtime::SaveProfile>>::failure({core::ErrorCode::NotFound, "Profile references a missing save group"});
            for (const auto entity : group->second.entities)
                for (const auto& property : properties)
                {
                    auto added = add(entity, property);
                    if (!added) return core::Result<std::map<std::string, runtime::SaveProfile>>::failure(added.error());
                }
        }
        result.emplace(id, std::move(profile));
    }
    return core::Result<std::map<std::string, runtime::SaveProfile>>::success(std::move(result));
}

core::Result<std::vector<runtime::CheckpointDefinition>> SaveSystemDocument::validateCheckpoints(
    const scene::Scene& scene, const std::map<std::string, runtime::SaveProfile>& profiles) const
{
    runtime::CheckpointSystem validator;
    std::vector<runtime::CheckpointDefinition> result;
    for (const auto& [id, definition] : checkpoints_)
    {
        if (id != definition.entity) return core::Result<std::vector<runtime::CheckpointDefinition>>::failure(invalid("Checkpoint key does not match entity ID"));
        if (definition.mode == runtime::CheckpointMode::SaveSelectedProfile
            && !profiles.contains(definition.saveProfileId))
            return core::Result<std::vector<runtime::CheckpointDefinition>>::failure({core::ErrorCode::NotFound, "Checkpoint references a missing SaveProfile"});
        auto valid = validator.registerCheckpoint(scene, definition);
        if (!valid) return core::Result<std::vector<runtime::CheckpointDefinition>>::failure(valid.error());
        result.push_back(definition);
    }
    return core::Result<std::vector<runtime::CheckpointDefinition>>::success(std::move(result));
}
}
