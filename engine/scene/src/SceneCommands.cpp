#include "astro/scene/Scene.hpp"

#include <algorithm>
#include <limits>
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

struct PrimitiveEntityRestore
{
    Entity entity;
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

core::Result<std::vector<EntityId>> normalizedReparentRoots(const Scene& scene,
    std::vector<EntityId> entities)
{
    if (entities.empty() || entities.size() > scene.entities().size())
        return core::Result<std::vector<EntityId>>::failure({core::ErrorCode::InvalidArgument,
            "Reparent selection must contain between one and the Scene entity count IDs"});
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    for (const auto id : entities)
    {
        if (!scene.find(id) || !scene.transform(id))
            return core::Result<std::vector<EntityId>>::failure({core::ErrorCode::NotFound,
                "Every selected reparent entity and its Transform must exist"});
    }

    std::vector<EntityId> roots;
    roots.reserve(entities.size());
    for (const auto id : entities)
    {
        const auto* entity = scene.find(id);
        auto ancestor = entity ? entity->parent : std::nullopt;
        bool selectedAncestor = false;
        while (ancestor)
        {
            if (std::binary_search(entities.begin(), entities.end(), *ancestor))
            {
                selectedAncestor = true;
                break;
            }
            const auto* parent = scene.find(*ancestor);
            ancestor = parent ? parent->parent : std::nullopt;
        }
        if (!selectedAncestor) roots.push_back(id);
    }
    if (roots.empty())
        return core::Result<std::vector<EntityId>>::failure({core::ErrorCode::InvalidArgument,
            "Reparent selection has no top-level entity"});
    return core::Result<std::vector<EntityId>>::success(std::move(roots));
}

core::Result<void> validateReparentRoots(const Scene& scene, const std::vector<EntityId>& roots,
    const std::optional<EntityId> parent)
{
    Scene candidate = scene;
    if (parent && !candidate.find(*parent))
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Parent entity does not exist"});
    for (const auto id : roots)
    {
        const auto* entity = candidate.find(id);
        if (!entity)
            return core::Result<void>::failure({core::ErrorCode::NotFound,
                "Every selected reparent entity must exist"});
        if (entity->parent == parent) continue;
        auto result = candidate.reparent(id, parent);
        if (!result) return result;
    }
    return core::Result<void>::success();
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
    auto transform = commands.registerTransactionalCommand<SetTransformPayload, void>(
        {"entity.set_transform", 1, "Set the full local-to-parent Transform of an existing entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"position_x", core::CommandFieldType::Number, true, "Local position X."},
            {"position_y", core::CommandFieldType::Number, true, "Local position Y."},
            {"position_z", core::CommandFieldType::Number, true, "Local position Z."},
            {"rotation_degrees_x", core::CommandFieldType::Number, true, "Local Euler rotation X in degrees."},
            {"rotation_degrees_y", core::CommandFieldType::Number, true, "Local Euler rotation Y in degrees."},
            {"rotation_degrees_z", core::CommandFieldType::Number, true, "Local Euler rotation Z in degrees."},
            {"scale_x", core::CommandFieldType::Number, true, "Local scale X."},
            {"scale_y", core::CommandFieldType::Number, true, "Local scale Y."},
            {"scale_z", core::CommandFieldType::Number, true, "Local scale Z."}}, "void"},
        [&scene](const SetTransformPayload& payload) {
            Scene candidate = scene;
            return candidate.setTransform(payload.entity, payload.value);
        },
        [&scene](core::EngineContext&, const SetTransformPayload& payload,
            core::CommandTransaction& transaction) -> core::Result<void> {
            const auto* component = scene.transform(payload.entity);
            if (!component)
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity does not have a Transform component"});
            const auto before = component->value;
            const auto componentId = component->id;
            return transaction.addStep("set local entity Transform",
                [&scene, payload, componentId] {
                    const auto* current = scene.transform(payload.entity);
                    if (!current || current->id != componentId)
                        return core::Result<void>::failure({core::ErrorCode::InvalidState,
                            "Transform component identity changed before applying the command"});
                    return scene.setTransform(payload.entity, payload.value);
                },
                [&scene, entity = payload.entity, componentId, before] {
                    const auto* current = scene.transform(entity);
                    if (!current || current->id != componentId)
                        return core::Result<void>::failure({core::ErrorCode::InvalidState,
                            "Transform component identity changed before undo"});
                    return scene.setTransform(entity, before);
                });
        });
    if (!transform) return transform;
    auto transforms = commands.registerTransactionalCommand<SetEntityTransformsPayload, void>(
        {"entity.set_transforms", 1,
            "Set local Transforms on several entities as one transaction.", {
            {"assignments", core::CommandFieldType::TransformAssignmentArray, true,
                "Structured entries of stable EntityId and complete local Transform; duplicate IDs are rejected."}}, "void"},
        [&scene](const SetEntityTransformsPayload& payload) {
            if (payload.assignments.empty() || payload.assignments.size() > scene.entities().size())
                return core::Result<void>::failure({core::ErrorCode::InvalidArgument,
                    "Transform assignments must contain between one and the Scene entity count entries"});
            std::vector<EntityId> ids;
            ids.reserve(payload.assignments.size());
            for (const auto& assignment : payload.assignments) ids.push_back(assignment.entity);
            std::sort(ids.begin(), ids.end());
            if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
                return core::Result<void>::failure({core::ErrorCode::InvalidArgument,
                    "Transform assignments must not contain duplicate EntityIds"});
            Scene candidate = scene;
            for (const auto& assignment : payload.assignments)
            {
                auto result = candidate.setTransform(assignment.entity, assignment.value);
                if (!result) return result;
            }
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const SetEntityTransformsPayload& payload,
            core::CommandTransaction& transaction) -> core::Result<void> {
            for (const auto& assignment : payload.assignments)
            {
                const auto* component = scene.transform(assignment.entity);
                if (!component)
                    return core::Result<void>::failure({core::ErrorCode::NotFound,
                        "Transform assignment target or its Transform component does not exist"});
                const auto before = component->value;
                const auto after = assignment.value;
                const auto componentId = component->id;
                if (before == after) continue;
                auto step = transaction.addStep("set local Transform for entity " + std::to_string(assignment.entity),
                    [&scene, assignment, before, componentId] {
                        const auto* current = scene.transform(assignment.entity);
                        if (!current || current->id != componentId || current->value != before)
                            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                                "Transform assignment target changed before applying the group"});
                        return scene.setTransform(assignment.entity, assignment.value);
                    },
                    [&scene, assignment, before, componentId] {
                        const auto* current = scene.transform(assignment.entity);
                        if (!current || current->id != componentId || current->value != assignment.value)
                            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                                "Transform assignment target changed before group Undo"});
                        return scene.setTransform(assignment.entity, before);
                    });
                if (!step) return step;
            }
            return core::Result<void>::success();
        });
    if (!transforms) return transforms;
    auto createPrimitive = commands.registerTransactionalCommand<CreatePrimitiveEntityPayload, EntityId>(
        {"entity.create_primitive", 1, "Create a Scene entity with a built-in 3D primitive.", {
            {"name", core::CommandFieldType::Utf8String, true, "Entity name."},
            {"parent", core::CommandFieldType::UnsignedInteger, false, "Optional parent entity ID."},
            {"primitive", core::CommandFieldType::UnsignedInteger, true,
                "Built-in kind: Cube=1, Plane=2, Sphere=3, Cylinder=4."},
            {"visible", core::CommandFieldType::Boolean, true, "Initial visibility of the component."}}, "EntityId"},
        [&scene](const CreatePrimitiveEntityPayload& payload) {
            Scene candidate = scene;
            const auto entity = candidate.createEntity(payload.name, payload.parent);
            if (!entity) return core::Result<void>::failure(entity.error());
            return candidate.setPrimitiveRenderer(entity.value(), payload.primitive, payload.visible);
        },
        [&scene](core::EngineContext&, const CreatePrimitiveEntityPayload& payload,
            core::CommandTransaction& transaction) -> core::Result<EntityId> {
            const auto stableId = scene.nextEntityIdHint();
            auto restore = std::make_shared<std::optional<PrimitiveEntityRestore>>();
            auto step = transaction.addStep("create primitive entity",
                [&scene, payload, stableId, restore] {
                    if (*restore)
                        return scene.restoreEntity((*restore)->entity, (*restore)->insertionIndex);
                    const auto created = scene.createEntity(payload.name, payload.parent);
                    if (!created) return core::Result<void>::failure(created.error());
                    if (created.value() != stableId)
                    {
                        (void)scene.erase(created.value());
                        return core::Result<void>::failure({core::ErrorCode::InvalidState,
                            "Scene entity ID changed between validation and execution"});
                    }
                    const auto assigned = scene.setPrimitiveRenderer(created.value(), payload.primitive, payload.visible);
                    if (!assigned) { (void)scene.erase(created.value()); return assigned; }
                    const auto* entity = scene.find(created.value());
                    if (!entity)
                    {
                        (void)scene.erase(created.value());
                        return core::Result<void>::failure({core::ErrorCode::InternalError,
                            "Created primitive entity could not be read back"});
                    }
                    restore->emplace(PrimitiveEntityRestore{*entity, scene.entities().size() - 1});
                    return core::Result<void>::success();
                },
                [&scene, stableId] { return scene.erase(stableId); });
            if (!step) return core::Result<EntityId>::failure(step.error());
            return core::Result<EntityId>::success(stableId);
        });
    if (!createPrimitive) return createPrimitive;
    auto deleteSubtree = commands.registerTransactionalCommand<DeleteEntitySubtreePayload, void>(
        {"entity.delete_subtree", 1, "Delete an entity and all descendants as one undoable Scene operation.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the subtree root."}}, "void"},
        [&scene](const DeleteEntitySubtreePayload& payload) {
            Scene candidate = scene;
            return candidate.erase(payload.entity, true);
        },
        [&scene](core::EngineContext&, const DeleteEntitySubtreePayload& payload,
            core::CommandTransaction& transaction) {
            auto beforeEntities = std::make_shared<std::vector<Entity>>(scene.entities());
            auto beforePrefabs = std::make_shared<std::vector<PrefabInstanceInfo>>(scene.prefabInstances());
            return transaction.addStep("delete entity subtree",
                [&scene, payload] { return scene.erase(payload.entity, true); },
                [&scene, beforeEntities, beforePrefabs] {
                    return scene.restoreSnapshot(*beforeEntities, *beforePrefabs);
                });
        });
    if (!deleteSubtree) return deleteSubtree;
    auto reparent = commands.registerTransactionalCommand<SetEntityParentPayload, void>(
        {"entity.reparent", 1,
            "Reparent an entity while preserving its local Transform; world transforms remain hierarchy-derived.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the entity to move."},
            {"parent", core::CommandFieldType::UnsignedInteger, true,
                "New parent entity ID; zero means the Scene root."}}, "void"},
        [&scene](const SetEntityParentPayload& payload) {
            Scene candidate = scene;
            return candidate.reparent(payload.entity, payload.parent);
        },
        [&scene](core::EngineContext&, const SetEntityParentPayload& payload,
            core::CommandTransaction& transaction) {
            const auto* entity = scene.find(payload.entity);
            const auto* transform = scene.transform(payload.entity);
            if (!entity || !transform)
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity or its Transform component does not exist"});
            const auto previousParent = entity->parent;
            const auto componentId = transform->id;
            return transaction.addStep("reparent entity and preserve local Transform",
                [&scene, payload, previousParent, componentId] {
                    const auto* currentEntity = scene.find(payload.entity);
                    const auto* currentTransform = scene.transform(payload.entity);
                    if (!currentEntity || !currentTransform || currentTransform->id != componentId
                        || currentEntity->parent != previousParent)
                        return core::Result<void>::failure({core::ErrorCode::InvalidState,
                            "Entity hierarchy or Transform identity changed before reparenting"});
                    return scene.reparent(payload.entity, payload.parent);
                },
                [&scene, payload, previousParent, componentId] {
                    const auto* currentEntity = scene.find(payload.entity);
                    const auto* currentTransform = scene.transform(payload.entity);
                    if (!currentEntity || !currentTransform || currentTransform->id != componentId
                        || currentEntity->parent != payload.parent)
                        return core::Result<void>::failure({core::ErrorCode::InvalidState,
                            "Entity hierarchy or Transform identity changed before undoing reparent"});
                    return scene.reparent(payload.entity, previousParent);
                });
        });
    if (!reparent) return reparent;
    auto reparentMany = commands.registerTransactionalCommand<SetEntityParentsPayload, void>(
        {"entity.reparent_many", 1,
            "Reparent several top-level selected entities as one transaction while preserving local Transforms.", {
            {"entities", core::CommandFieldType::UnsignedIntegerArray, true,
                "Stable EntityIds to move; selected descendants of another selected entity are normalized away."},
            {"parent", core::CommandFieldType::UnsignedInteger, true,
                "New parent EntityId; zero means the Scene root."}}, "void"},
        [&scene](const SetEntityParentsPayload& payload) {
            auto roots = normalizedReparentRoots(scene, payload.entities);
            if (!roots) return core::Result<void>::failure(roots.error());
            return validateReparentRoots(scene, roots.value(), payload.parent);
        },
        [&scene](core::EngineContext&, const SetEntityParentsPayload& payload,
            core::CommandTransaction& transaction) {
            auto roots = normalizedReparentRoots(scene, payload.entities);
            if (!roots) return core::Result<void>::failure(roots.error());
            auto valid = validateReparentRoots(scene, roots.value(), payload.parent);
            if (!valid) return valid;
            for (const auto id : roots.value())
            {
                const auto* entity = scene.find(id);
                const auto* transform = scene.transform(id);
                if (!entity || !transform)
                    return core::Result<void>::failure({core::ErrorCode::NotFound,
                        "Selected reparent entity or its Transform does not exist"});
                if (entity->parent == payload.parent) continue;
                const auto previousParent = entity->parent;
                const auto componentId = transform->id;
                auto step = transaction.addStep("reparent selected entity " + std::to_string(id),
                    [&scene, id, previousParent, componentId, parent = payload.parent] {
                        const auto* currentEntity = scene.find(id);
                        const auto* currentTransform = scene.transform(id);
                        if (!currentEntity || !currentTransform || currentTransform->id != componentId
                            || currentEntity->parent != previousParent)
                            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                                "Selected entity hierarchy or Transform identity changed before group reparent"});
                        return scene.reparent(id, parent);
                    },
                    [&scene, id, previousParent, componentId, parent = payload.parent] {
                        const auto* currentEntity = scene.find(id);
                        const auto* currentTransform = scene.transform(id);
                        if (!currentEntity || !currentTransform || currentTransform->id != componentId
                            || currentEntity->parent != parent)
                            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                                "Selected entity hierarchy or Transform identity changed before group Undo"});
                        return scene.reparent(id, previousParent);
                    });
                if (!step) return step;
            }
            return core::Result<void>::success();
        });
    if (!reparentMany) return reparentMany;
    auto primitive = commands.registerTransactionalCommand<SetPrimitiveRendererPayload, void>(
        {"entity.set_primitive_renderer", 1, "Add or replace built-in primitive geometry on an existing entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"primitive", core::CommandFieldType::UnsignedInteger, true, "Built-in kind: Cube=1, Plane=2, Sphere=3, Cylinder=4."},
            {"visible", core::CommandFieldType::Boolean, true, "Initial visibility of the component."}}, "void"},
        [&scene](const SetPrimitiveRendererPayload& payload) {
            Scene candidate = scene;
            return candidate.setPrimitiveRenderer(payload.entity, payload.primitive, payload.visible);
        },
        [&scene](core::EngineContext&, const SetPrimitiveRendererPayload& payload,
            core::CommandTransaction& transaction) {
            const auto* current = scene.primitiveRenderer(payload.entity);
            const auto before = current ? std::optional<PrimitiveRendererComponent>{*current} : std::nullopt;
            auto after = std::make_shared<std::optional<PrimitiveRendererComponent>>();
            return transaction.addStep("set PrimitiveRenderer",
                [&scene, payload, after] {
                    if (*after) return scene.restorePrimitiveRenderer(payload.entity, **after);
                    auto result = scene.setPrimitiveRenderer(payload.entity, payload.primitive, payload.visible);
                    if (!result) return result;
                    const auto* added = scene.primitiveRenderer(payload.entity);
                    if (!added) return core::Result<void>::failure({core::ErrorCode::InternalError,
                        "PrimitiveRenderer could not be read back after assignment"});
                    after->emplace(*added);
                    return core::Result<void>::success();
                },
                [&scene, payload, before] {
                    return before ? scene.restorePrimitiveRenderer(payload.entity, *before)
                        : scene.removePrimitiveRenderer(payload.entity);
                });
        });
    if (!primitive) return primitive;
    auto primitiveMaterial = commands.registerTransactionalCommand<SetPrimitiveMaterialOverridePayload, void>(
        {"entity.set_primitive_material_override", 1,
            "Assign or clear a registered project material override on a built-in primitive.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the primitive entity."},
            {"material", core::CommandFieldType::AssetId, true,
                "Project material AssetId; zero clears the override."}}, "void"},
        [&scene](const SetPrimitiveMaterialOverridePayload& payload) {
            if (payload.material.value == std::numeric_limits<std::uint64_t>::max())
                return core::Result<void>::failure({core::ErrorCode::ValidationError,
                    "Primitive material AssetId is invalid"});
            Scene candidate = scene;
            return candidate.setPrimitiveMaterialOverride(payload.entity, payload.material);
        },
        [&scene](core::EngineContext&, const SetPrimitiveMaterialOverridePayload& payload,
            core::CommandTransaction& transaction) {
            const auto* current = scene.primitiveRenderer(payload.entity);
            if (!current) return core::Result<void>::failure({core::ErrorCode::NotFound,
                "Entity does not have a PrimitiveRenderer"});
            const auto previous = current->materialOverride;
            return transaction.addStep("set primitive material override",
                [&scene, payload] { return scene.setPrimitiveMaterialOverride(payload.entity, payload.material); },
                [&scene, entity = payload.entity, previous] {
                    return scene.setPrimitiveMaterialOverride(entity, previous);
                });
        });
    if (!primitiveMaterial) return primitiveMaterial;
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

    auto materialOverride = commands.registerTransactionalCommand<SetMeshMaterialOverridePayload, void>(
        {"entity.set_mesh_material_override", 1,
            "Set or clear the project material override on an existing MeshRenderer primitive.", {
                {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
                {"material", core::CommandFieldType::AssetId, true,
                    "Project material AssetId; zero clears the override."}}, "void"},
        [&scene](const SetMeshMaterialOverridePayload& payload) {
            if (!scene.find(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (!scene.meshRenderer(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity does not contain a MeshRenderer"});
            if (payload.material.value == std::numeric_limits<std::uint64_t>::max())
                return core::Result<void>::failure({core::ErrorCode::ValidationError,
                    "Material override AssetId is invalid"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const SetMeshMaterialOverridePayload& payload,
            core::CommandTransaction& transaction) -> core::Result<void> {
            const auto* current = scene.meshRenderer(payload.entity);
            if (!current)
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity does not contain a MeshRenderer"});
            const auto before = *current;
            auto after = std::make_shared<std::optional<MeshRendererComponent>>();
            return transaction.addStep("set MeshRenderer material override",
                [&scene, payload, after] {
                    if (*after) return scene.restoreMeshRenderer(payload.entity, **after);
                    auto changed = scene.setMeshMaterialOverride(payload.entity, payload.material);
                    if (!changed) return changed;
                    const auto* updated = scene.meshRenderer(payload.entity);
                    if (!updated)
                        return core::Result<void>::failure({core::ErrorCode::InternalError,
                            "Material override command could not read back its MeshRenderer"});
                    after->emplace(*updated);
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity, before] {
                    return scene.restoreMeshRenderer(entity, before);
                });
        });
    if (!materialOverride) return materialOverride;

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

    auto addCamera = commands.registerTransactionalCommand<AddCamera3DPayload, void>(
        {"camera3d.add", 1, "Add an inactive perspective Camera3D to an existing entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."}}, "void"},
        [&scene](const AddCamera3DPayload& payload) {
            const auto* entity = scene.find(payload.entity);
            if (!entity) return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (entity->camera3d)
                return core::Result<void>::failure({core::ErrorCode::AlreadyExists, "Entity already contains a Camera3D"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const AddCamera3DPayload& payload, core::CommandTransaction& transaction) {
            auto added = std::make_shared<std::optional<Camera3DComponent>>();
            return transaction.addStep("add Camera3D",
                [&scene, payload, added] {
                    if (*added) return scene.restoreCamera3D(payload.entity, **added);
                    auto result = scene.setCamera3D(payload.entity);
                    if (!result) return result;
                    const auto* camera = scene.camera3D(payload.entity);
                    if (!camera) return core::Result<void>::failure({core::ErrorCode::InternalError,
                        "Camera3D add command could not read back its component"});
                    added->emplace(*camera);
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity] { return scene.removeCamera3D(entity); });
        });
    if (!addCamera) return addCamera;

    auto removeCamera = commands.registerTransactionalCommand<RemoveCamera3DPayload, void>(
        {"camera3d.remove", 1, "Remove the Camera3D from an existing entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."}}, "void"},
        [&scene](const RemoveCamera3DPayload& payload) {
            if (!scene.find(payload.entity)) return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (!scene.camera3D(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not contain a Camera3D"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const RemoveCamera3DPayload& payload, core::CommandTransaction& transaction) {
            const auto before = *scene.camera3D(payload.entity);
            return transaction.addStep("remove Camera3D",
                [&scene, payload] { return scene.removeCamera3D(payload.entity); },
                [&scene, entity = payload.entity, before] { return scene.restoreCamera3D(entity, before); });
        });
    if (!removeCamera) return removeCamera;

    auto setCameraLens = commands.registerTransactionalCommand<SetCamera3DLensPayload, void>(
        {"camera3d.set_lens", 1, "Set validated perspective-camera field of view and clipping planes.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"vertical_fov_degrees", core::CommandFieldType::Number, true, "Vertical field of view in degrees."},
            {"near_plane", core::CommandFieldType::Number, true, "Positive near clipping distance."},
            {"far_plane", core::CommandFieldType::Number, true, "Far clipping distance greater than near."}}, "void"},
        [&scene](const SetCamera3DLensPayload& payload) {
            if (!scene.find(payload.entity)) return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (!scene.camera3D(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not contain a Camera3D"});
            Scene candidate = scene;
            return candidate.setCamera3D(payload.entity, payload.verticalFovDegrees, payload.nearPlane, payload.farPlane);
        },
        [&scene](core::EngineContext&, const SetCamera3DLensPayload& payload, core::CommandTransaction& transaction) {
            const auto before = *scene.camera3D(payload.entity);
            auto after = std::make_shared<std::optional<Camera3DComponent>>();
            return transaction.addStep("set Camera3D lens",
                [&scene, payload, after] {
                    if (*after) return scene.restoreCamera3D(payload.entity, **after);
                    auto result = scene.setCamera3D(payload.entity, payload.verticalFovDegrees,
                        payload.nearPlane, payload.farPlane);
                    if (!result) return result;
                    after->emplace(*scene.camera3D(payload.entity));
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity, before] { return scene.restoreCamera3D(entity, before); });
        });
    if (!setCameraLens) return setCameraLens;

    auto setActiveCamera = commands.registerTransactionalCommand<SetActiveCamera3DPayload, void>(
        {"camera3d.set_active", 1, "Select one active Camera3D; entity 0 clears the active selection.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Camera entity ID, or zero to clear."}}, "void"},
        [&scene](const SetActiveCamera3DPayload& payload) {
            if (payload.entity != 0 && !scene.camera3D(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not contain a Camera3D"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const SetActiveCamera3DPayload& payload, core::CommandTransaction& transaction) {
            const auto previous = scene.activeCamera3D();
            return transaction.addStep("set active Camera3D",
                [&scene, payload, previous] {
                    if (payload.entity != 0) return scene.setCamera3DActive(payload.entity, true);
                    return previous ? scene.setCamera3DActive(*previous, false) : core::Result<void>::success();
                },
                [&scene, previous, target = payload.entity] {
                    if (previous) return scene.setCamera3DActive(*previous, true);
                    if (target != 0) return scene.setCamera3DActive(target, false);
                    return core::Result<void>::success();
                });
        });
    if (!setActiveCamera) return setActiveCamera;

    auto addDirectionalLight = commands.registerTransactionalCommand<AddDirectionalLight3DPayload, void>(
        {"light3d.add_directional", 1, "Add a default enabled DirectionalLight3D.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."}}, "void"},
        [&scene](const AddDirectionalLight3DPayload& payload) {
            const auto* entity = scene.find(payload.entity);
            if (!entity) return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (entity->directionalLight3d)
                return core::Result<void>::failure({core::ErrorCode::AlreadyExists,
                    "Entity already contains DirectionalLight3D"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const AddDirectionalLight3DPayload& payload,
            core::CommandTransaction& transaction) {
            auto added = std::make_shared<std::optional<DirectionalLight3DComponent>>();
            return transaction.addStep("add DirectionalLight3D",
                [&scene, payload, added] {
                    if (*added) return scene.restoreDirectionalLight3D(payload.entity, **added);
                    auto result = scene.setDirectionalLight3D(payload.entity);
                    if (!result) return result;
                    added->emplace(*scene.directionalLight3D(payload.entity));
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity] { return scene.removeDirectionalLight3D(entity); });
        });
    if (!addDirectionalLight) return addDirectionalLight;

    auto setDirectionalLight = commands.registerTransactionalCommand<SetDirectionalLight3DPayload, void>(
        {"light3d.set_directional", 1, "Set validated linear RGB, intensity, and enabled state.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"color", core::CommandFieldType::NumberArray, true, "Three linear RGB channels in [0,1]."},
            {"intensity", core::CommandFieldType::Number, true, "Unitless intensity multiplier in [0,64]."},
            {"enabled", core::CommandFieldType::Boolean, true, "Whether the light is included in render packets."}}, "void"},
        [&scene](const SetDirectionalLight3DPayload& payload) {
            if (!scene.directionalLight3D(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity does not contain DirectionalLight3D"});
            Scene candidate = scene;
            return candidate.setDirectionalLight3D(payload.entity, payload.color, payload.intensity, payload.enabled);
        },
        [&scene](core::EngineContext&, const SetDirectionalLight3DPayload& payload,
            core::CommandTransaction& transaction) {
            const auto before = *scene.directionalLight3D(payload.entity);
            auto after = std::make_shared<std::optional<DirectionalLight3DComponent>>();
            return transaction.addStep("set DirectionalLight3D",
                [&scene, payload, after] {
                    if (*after) return scene.restoreDirectionalLight3D(payload.entity, **after);
                    auto result = scene.setDirectionalLight3D(payload.entity, payload.color,
                        payload.intensity, payload.enabled);
                    if (!result) return result;
                    after->emplace(*scene.directionalLight3D(payload.entity));
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity, before] {
                    return scene.restoreDirectionalLight3D(entity, before);
                });
        });
    if (!setDirectionalLight) return setDirectionalLight;

    auto removeDirectionalLight = commands.registerTransactionalCommand<RemoveDirectionalLight3DPayload, void>(
        {"light3d.remove_directional", 1, "Remove DirectionalLight3D from an entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."}}, "void"},
        [&scene](const RemoveDirectionalLight3DPayload& payload) {
            if (!scene.directionalLight3D(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity does not contain DirectionalLight3D"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const RemoveDirectionalLight3DPayload& payload,
            core::CommandTransaction& transaction) {
            const auto before = *scene.directionalLight3D(payload.entity);
            return transaction.addStep("remove DirectionalLight3D",
                [&scene, payload] { return scene.removeDirectionalLight3D(payload.entity); },
                [&scene, entity = payload.entity, before] {
                    return scene.restoreDirectionalLight3D(entity, before);
                });
        });
    if (!removeDirectionalLight) return removeDirectionalLight;

    auto addPointLight = commands.registerTransactionalCommand<AddPointLight3DPayload, void>(
        {"light3d.add_point", 1, "Add a default enabled PointLight3D.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."}}, "void"},
        [&scene](const AddPointLight3DPayload& payload) {
            const auto* entity = scene.find(payload.entity);
            if (!entity) return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (entity->pointLight3d)
                return core::Result<void>::failure({core::ErrorCode::AlreadyExists,
                    "Entity already contains PointLight3D"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const AddPointLight3DPayload& payload,
            core::CommandTransaction& transaction) {
            auto added = std::make_shared<std::optional<PointLight3DComponent>>();
            return transaction.addStep("add PointLight3D",
                [&scene, payload, added] {
                    if (*added) return scene.restorePointLight3D(payload.entity, **added);
                    auto result = scene.setPointLight3D(payload.entity);
                    if (!result) return result;
                    added->emplace(*scene.pointLight3D(payload.entity));
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity] { return scene.removePointLight3D(entity); });
        });
    if (!addPointLight) return addPointLight;

    auto setPointLight = commands.registerTransactionalCommand<SetPointLight3DPayload, void>(
        {"light3d.set_point", 1, "Set validated linear RGB, intensity, range, and enabled state.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."},
            {"color", core::CommandFieldType::NumberArray, true, "Three linear RGB channels in [0,1]."},
            {"intensity", core::CommandFieldType::Number, true, "Unitless intensity multiplier in [0,64]."},
            {"range", core::CommandFieldType::Number, true, "Finite light range in scene units [0.1,10000]."},
            {"enabled", core::CommandFieldType::Boolean, true, "Whether the light is included in render packets."}}, "void"},
        [&scene](const SetPointLight3DPayload& payload) {
            if (!scene.pointLight3D(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity does not contain PointLight3D"});
            Scene candidate = scene;
            return candidate.setPointLight3D(payload.entity, payload.color, payload.intensity,
                payload.range, payload.enabled);
        },
        [&scene](core::EngineContext&, const SetPointLight3DPayload& payload,
            core::CommandTransaction& transaction) {
            const auto before = *scene.pointLight3D(payload.entity);
            auto after = std::make_shared<std::optional<PointLight3DComponent>>();
            return transaction.addStep("set PointLight3D",
                [&scene, payload, after] {
                    if (*after) return scene.restorePointLight3D(payload.entity, **after);
                    auto result = scene.setPointLight3D(payload.entity, payload.color, payload.intensity,
                        payload.range, payload.enabled);
                    if (!result) return result;
                    after->emplace(*scene.pointLight3D(payload.entity));
                    return core::Result<void>::success();
                },
                [&scene, entity = payload.entity, before] {
                    return scene.restorePointLight3D(entity, before);
                });
        });
    if (!setPointLight) return setPointLight;

    auto removePointLight = commands.registerTransactionalCommand<RemovePointLight3DPayload, void>(
        {"light3d.remove_point", 1, "Remove PointLight3D from an entity.", {
            {"entity", core::CommandFieldType::UnsignedInteger, true, "Stable ID of the target entity."}}, "void"},
        [&scene](const RemovePointLight3DPayload& payload) {
            if (!scene.pointLight3D(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound,
                    "Entity does not contain PointLight3D"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const RemovePointLight3DPayload& payload,
            core::CommandTransaction& transaction) {
            const auto before = *scene.pointLight3D(payload.entity);
            return transaction.addStep("remove PointLight3D",
                [&scene, payload] { return scene.removePointLight3D(payload.entity); },
                [&scene, entity = payload.entity, before] {
                    return scene.restorePointLight3D(entity, before);
                });
        });
    if (!removePointLight) return removePointLight;

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
