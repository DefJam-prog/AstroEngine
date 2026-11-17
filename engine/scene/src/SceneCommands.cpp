#include "astro/scene/Scene.hpp"

#include <algorithm>
#include <memory>
#include <optional>

namespace astro::scene
{
namespace
{
struct ScriptBehaviorRestore
{
    ScriptBehaviorComponent component;
    std::size_t insertionIndex = 0;
};

std::optional<ScriptBehaviorRestore> findScriptBehavior(const Scene& scene,
    const EntityId entityId, const ComponentId componentId)
{
    const auto* entity = scene.find(entityId);
    if (!entity) return std::nullopt;
    for (std::size_t index = 0; index < entity->scriptBehaviors.size(); ++index)
    {
        const auto& component = entity->scriptBehaviors[index];
        if (component.id == componentId) return ScriptBehaviorRestore{component, index};
    }
    return std::nullopt;
}
}

core::Result<void> registerSceneCommands(core::CommandRegistry& commands, Scene& scene)
{
    auto property = commands.registerTransactionalCommand<SetScenePropertyPayload, void>(
        {"entity.set_property", 1, "Set one schema-validated property on an existing scene entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"property", core::CommandFieldType::Utf8String, true, "Editable property ID; currently only 'name'."},
            {"value", core::CommandFieldType::StringOrUnsignedInteger, true,
                "Typed value. For 'name', use a non-empty valid UTF-8 string up to 240 bytes."}}, "void"},
        [&scene](const SetScenePropertyPayload& payload) {
            return scene.validateProperty(payload.entity, payload.property, payload.value);
        },
        [&scene](core::EngineContext&, const SetScenePropertyPayload& payload, core::CommandTransaction& transaction) {
            auto previous = scene.getProperty(payload.entity, payload.property);
            if (!previous) return core::Result<void>::failure(previous.error());
            const auto property = payload.property;
            const auto before = std::move(previous).value();
            const auto after = payload.value;
            return transaction.addStep("set entity property",
                [&scene, entity = payload.entity, property, after] {
                    return scene.setProperty(entity, property, after);
                },
                [&scene, entity = payload.entity, property, before] {
                    return scene.setProperty(entity, property, before);
                });
        });
    if (!property) return property;
    auto mesh = commands.registerTransactionalCommand<SetMeshRendererPayload, void>(
        {"entity.set_mesh_renderer", 1, "Add or replace the MeshRenderer reference on an existing entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"model", core::CommandFieldType::AssetId, true, "Non-zero model AssetId from the project registry."},
            {"mesh_index", core::CommandFieldType::UnsignedInteger, true, "Imported mesh index."},
            {"primitive_index", core::CommandFieldType::UnsignedInteger, true, "Primitive index within the imported mesh."},
            {"visible", core::CommandFieldType::Boolean, true, "Initial visibility of the component."}}, "void"},
        [&scene](const SetMeshRendererPayload& payload) {
            Scene candidate = scene;
            return candidate.setMeshRenderer(payload.entity, payload.model,
                payload.meshIndex, payload.primitiveIndex, payload.visible);
        },
        [&scene](core::EngineContext&, const SetMeshRendererPayload& payload,
            core::CommandTransaction& transaction) -> core::Result<void> {
            const auto* existing = scene.meshRenderer(payload.entity);
            const auto before = existing ? std::optional<MeshRendererComponent>{*existing} : std::nullopt;
            auto after = std::make_shared<std::optional<MeshRendererComponent>>();
            return transaction.addStep("set MeshRenderer",
                [&scene, payload, after] {
                    if (*after) return scene.restoreMeshRenderer(payload.entity, **after);
                    auto changed = scene.setMeshRenderer(payload.entity, payload.model,
                        payload.meshIndex, payload.primitiveIndex, payload.visible);
                    if (!changed) return changed;
                    const auto* component = scene.meshRenderer(payload.entity);
                    if (!component)
                        return core::Result<void>::failure({core::ErrorCode::InternalError,
                            "Set MeshRenderer could not read back its component"});
                    after->emplace(*component);
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity, before] {
                    if (before) return scene.restoreMeshRenderer(entity, *before);
                    return scene.removeMeshRenderer(entity);
                });
        });
    if (!mesh) return mesh;

    auto sprite = commands.registerTransactionalCommand<SetSpriteRendererPayload, void>(
        {"entity.set_sprite_renderer", 1, "Add or replace the SpriteRenderer component on an existing entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"texture", core::CommandFieldType::AssetId, true, "Non-zero texture asset ID."},
            {"visible", core::CommandFieldType::Boolean, true, "Initial visibility of the component."}}, "void"},
        [&scene](const SetSpriteRendererPayload& payload) {
            if (!scene.find(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (payload.texture.value == 0)
                return core::Result<void>::failure({core::ErrorCode::ValidationError, "Texture AssetId must not be zero"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const SetSpriteRendererPayload& payload, core::CommandTransaction& transaction) {
            const auto* existing = scene.spriteRenderer(payload.entity);
            const auto before = existing ? std::optional<SpriteRendererComponent>{*existing} : std::nullopt;
            return transaction.addStep("set SpriteRenderer",
                [&scene, payload] { return scene.setSpriteRenderer(payload.entity, payload.texture, payload.visible); },
                [&scene, entity = payload.entity, before] {
                    if (before) return scene.setSpriteRenderer(entity, before->texture, before->visible);
                    return scene.removeSpriteRenderer(entity);
                });
        });
    if (!sprite) return sprite;

    auto addScript = commands.registerTransactionalCommand<AddScriptBehaviorPayload, void>(
        {"script_behavior.add", 1, "Add a disabled ScriptBehavior binding to an existing entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"script_asset", core::CommandFieldType::AssetId, true, "Registered project script AssetId."},
            {"managed_type", core::CommandFieldType::Utf8String, true, "Managed type selected from the project registry."}},
            "component_id"},
        [&scene](const AddScriptBehaviorPayload& payload) {
            if (!scene.find(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            Scene candidate = scene;
            const auto added = candidate.addScriptBehavior(payload.entity, payload.scriptAsset,
                payload.managedTypeName, false);
            return added ? core::Result<void>::success() : core::Result<void>::failure(added.error());
        },
        [&scene](core::EngineContext&, const AddScriptBehaviorPayload& payload,
            core::CommandTransaction& transaction) -> core::Result<void> {
            auto restored = std::make_shared<std::optional<ScriptBehaviorRestore>>();
            const auto step = transaction.addStep("add ScriptBehavior",
                [&scene, payload, restored] {
                    if (*restored)
                        return scene.restoreScriptBehavior(payload.entity, (*restored)->component,
                            (*restored)->insertionIndex);
                    const auto added = scene.addScriptBehavior(payload.entity, payload.scriptAsset,
                        payload.managedTypeName, false);
                    if (!added) return core::Result<void>::failure(added.error());
                    const auto* entity = scene.find(payload.entity);
                    if (!entity || entity->scriptBehaviors.empty())
                        return core::Result<void>::failure({core::ErrorCode::InternalError,
                            "Added ScriptBehavior could not be read back"});
                    restored->emplace(ScriptBehaviorRestore{entity->scriptBehaviors.back(),
                        entity->scriptBehaviors.size() - 1});
                    return core::Result<void>::success();
                },
                [&scene, payload, restored] {
                    if (!*restored)
                        return core::Result<void>::failure({core::ErrorCode::InternalError,
                            "ScriptBehavior undo record is missing"});
                    return scene.removeScriptBehavior(payload.entity, (*restored)->component.id);
                });
            return step;
        });
    if (!addScript) return addScript;

    auto setScriptEnabled = commands.registerTransactionalCommand<SetScriptBehaviorEnabledPayload, void>(
        {"script_behavior.set_enabled", 1, "Enable or disable an existing ScriptBehavior component.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"component", core::CommandFieldType::UnsignedInteger, true, "Stable ScriptBehavior component ID."},
            {"enabled", core::CommandFieldType::Boolean, true, "Whether the component participates in Play."}}, "void"},
        [&scene](const SetScriptBehaviorEnabledPayload& payload) {
            if (!scene.find(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            const auto previous = findScriptBehavior(scene, payload.entity, payload.component);
            if (!previous)
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "ScriptBehavior component does not exist on the entity"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const SetScriptBehaviorEnabledPayload& payload,
            core::CommandTransaction& transaction) {
            const auto previous = findScriptBehavior(scene, payload.entity, payload.component);
            if (!previous)
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "ScriptBehavior component does not exist on the entity"});
            const auto before = previous->component;
            return transaction.addStep("set ScriptBehavior enabled",
                [&scene, payload, before] {
                    return scene.updateScriptBehavior(payload.entity, payload.component,
                        before.scriptAsset, before.managedTypeName, payload.enabled);
                },
                [&scene, payload, before] {
                    return scene.updateScriptBehavior(payload.entity, payload.component,
                        before.scriptAsset, before.managedTypeName, before.enabled);
                });
        });
    if (!setScriptEnabled) return setScriptEnabled;

    return commands.registerTransactionalCommand<RemoveScriptBehaviorPayload, void>(
        {"script_behavior.remove", 1, "Remove a ScriptBehavior component from an entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"component", core::CommandFieldType::UnsignedInteger, true, "Stable ScriptBehavior component ID."}}, "void"},
        [&scene](const RemoveScriptBehaviorPayload& payload) {
            if (!scene.find(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (!findScriptBehavior(scene, payload.entity, payload.component))
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "ScriptBehavior component does not exist on the entity"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const RemoveScriptBehaviorPayload& payload,
            core::CommandTransaction& transaction) {
            const auto restore = findScriptBehavior(scene, payload.entity, payload.component);
            if (!restore)
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "ScriptBehavior component does not exist on the entity"});
            return transaction.addStep("remove ScriptBehavior",
                [&scene, payload] { return scene.removeScriptBehavior(payload.entity, payload.component); },
                [&scene, payload, restore] {
                    return scene.restoreScriptBehavior(payload.entity, restore->component, restore->insertionIndex);
                });
        });
}
}
