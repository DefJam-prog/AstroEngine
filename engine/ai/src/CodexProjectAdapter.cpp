#include "astro/ai/CodexProjectAdapter.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace astro::ai
{
namespace
{
constexpr std::size_t kMaxProjectLabelBytes = 240;
constexpr std::size_t kMaxPlanSummaryBytes = 4096;
constexpr std::size_t kMaxStepIdBytes = 64;
constexpr std::size_t kMaxStepIntentBytes = 1024;
constexpr std::size_t kMaxPlanSteps = 256;

bool validUtf8(const std::string_view text)
{
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const auto first = static_cast<unsigned char>(text[offset]);
        if (first <= 0x7f) { ++offset; continue; }

        std::size_t continuationCount = 0;
        std::uint32_t codePoint = 0;
        std::uint32_t minimum = 0;
        if ((first & 0xe0) == 0xc0) { continuationCount = 1; codePoint = first & 0x1f; minimum = 0x80; }
        else if ((first & 0xf0) == 0xe0) { continuationCount = 2; codePoint = first & 0x0f; minimum = 0x800; }
        else if ((first & 0xf8) == 0xf0) { continuationCount = 3; codePoint = first & 0x07; minimum = 0x10000; }
        else return false;

        if (offset + continuationCount >= text.size()) return false;
        for (std::size_t index = 1; index <= continuationCount; ++index)
        {
            const auto next = static_cast<unsigned char>(text[offset + index]);
            if ((next & 0xc0) != 0x80) return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if (codePoint < minimum || codePoint > 0x10ffff
            || (codePoint >= 0xd800 && codePoint <= 0xdfff)) return false;
        offset += continuationCount + 1;
    }
    return true;
}

void appendComponent(SceneEntitySnapshot& entity, std::string typeId,
    const scene::ComponentId componentId, const std::uint32_t schemaVersion)
{
    entity.components.push_back({std::move(typeId), componentId, schemaVersion});
}
}

core::Result<CodexProjectContext> CodexProjectAdapter::readContext(
    std::string projectLabel, const scene::Scene& activeScene) const
{
    if (projectLabel.empty() || projectLabel.size() > kMaxProjectLabelBytes || !validUtf8(projectLabel))
        return core::Result<CodexProjectContext>::failure({core::ErrorCode::InvalidArgument,
            "Project label must contain valid UTF-8 between 1 and 240 bytes"});

    CodexProjectContext context;
    context.projectLabel = std::move(projectLabel);
    context.sceneId = activeScene.id();
    context.sceneName = activeScene.name();
    context.commands = api_.commandCatalog();
    context.entities.reserve(activeScene.entities().size());

    for (const auto& source : activeScene.entities())
    {
        SceneEntitySnapshot entity;
        entity.id = source.id;
        entity.name = source.name;
        entity.parent = source.parent;
        if (!source.components.empty()) entity.transform = source.components.front().value;
        for (const auto& component : source.components)
            appendComponent(entity, "Transform", component.id, component.schemaVersion);
        if (source.spriteRenderer)
            appendComponent(entity, "SpriteRenderer", source.spriteRenderer->id, source.spriteRenderer->schemaVersion);
        if (source.meshRenderer)
            appendComponent(entity, "MeshRenderer", source.meshRenderer->id, source.meshRenderer->schemaVersion);
        if (source.primitiveRenderer)
            appendComponent(entity, "PrimitiveRenderer", source.primitiveRenderer->id, source.primitiveRenderer->schemaVersion);
        if (source.camera3d)
            appendComponent(entity, "Camera3D", source.camera3d->id, source.camera3d->schemaVersion);
        if (source.directionalLight3d)
            appendComponent(entity, "DirectionalLight3D", source.directionalLight3d->id,
                source.directionalLight3d->schemaVersion);
        if (source.pointLight3d)
            appendComponent(entity, "PointLight3D", source.pointLight3d->id, source.pointLight3d->schemaVersion);
        for (const auto& component : source.scriptBehaviors)
            appendComponent(entity, "ScriptBehavior", component.id, component.schemaVersion);

        std::sort(entity.components.begin(), entity.components.end(), [](const auto& left, const auto& right) {
            if (left.typeId != right.typeId) return left.typeId < right.typeId;
            return left.componentId < right.componentId;
        });
        context.entities.push_back(std::move(entity));
    }
    std::sort(context.entities.begin(), context.entities.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return core::Result<CodexProjectContext>::success(std::move(context));
}

core::Result<void> CodexProjectAdapter::validatePlan(const CodexChangePlan& plan) const
{
    if (plan.schemaVersion != 1)
        return core::Result<void>::failure({core::ErrorCode::VersionMismatch,
            "Unsupported Codex change plan schema version"});
    if (plan.summary.empty() || plan.summary.size() > kMaxPlanSummaryBytes || !validUtf8(plan.summary))
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument,
            "Plan summary must contain valid UTF-8 between 1 and 4096 bytes"});
    if (plan.steps.empty() || plan.steps.size() > kMaxPlanSteps)
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument,
            "Plan must contain between 1 and 256 steps"});

    const auto catalog = api_.commandCatalog();
    std::unordered_set<std::string> stepIds;
    stepIds.reserve(plan.steps.size());
    for (const auto& step : plan.steps)
    {
        if (step.id.empty() || step.id.size() > kMaxStepIdBytes || !validUtf8(step.id)
            || !stepIds.insert(step.id).second)
            return core::Result<void>::failure({core::ErrorCode::InvalidArgument,
                "Plan step IDs must be unique and contain between 1 and 64 bytes"});
        if (step.intent.empty() || step.intent.size() > kMaxStepIntentBytes || !validUtf8(step.intent))
            return core::Result<void>::failure({core::ErrorCode::InvalidArgument,
                "Plan step intent must contain between 1 and 1024 UTF-8 bytes"});

        const auto command = std::find_if(catalog.begin(), catalog.end(), [&step](const auto& entry) {
            return entry.schema.id == step.commandId;
        });
        if (command == catalog.end())
            return core::Result<void>::failure({core::ErrorCode::NotFound,
                "Plan references an unavailable command: " + step.commandId});
        if (step.commandSchemaVersion != command->schema.version)
            return core::Result<void>::failure({core::ErrorCode::VersionMismatch,
                "Plan command schema version is stale: " + step.commandId});
    }
    return core::Result<void>::success();
}

core::Result<PropertyChangePreview> CodexProjectAdapter::previewSetProperty(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::SetScenePropertyPayload& payload) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "entity.set_property";
    });
    if (command == catalog.end())
        return core::Result<PropertyChangePreview>::failure({core::ErrorCode::NotFound,
            "Property preview requires the registered entity.set_property command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<PropertyChangePreview>::failure({core::ErrorCode::VersionMismatch,
            "Property preview command schema version is stale"});
    if (!command->transactional)
        return core::Result<PropertyChangePreview>::failure({core::ErrorCode::InvalidState,
            "Property preview requires a transactional command"});

    auto before = activeScene.getProperty(payload.entity, payload.property);
    if (!before)
        return core::Result<PropertyChangePreview>::failure(before.error());
    auto validation = activeScene.validateProperty(payload.entity, payload.property, payload.value);
    if (!validation)
        return core::Result<PropertyChangePreview>::failure(validation.error());

    PropertyChangePreview preview;
    preview.sceneId = activeScene.id();
    preview.entityId = payload.entity;
    preview.propertyId = payload.property;
    preview.before = std::move(before).value();
    preview.after = payload.value;
    preview.changesValue = preview.before != preview.after;
    return core::Result<PropertyChangePreview>::success(std::move(preview));
}

core::Result<SpriteRendererVisibilityPreview> CodexProjectAdapter::previewSpriteRendererVisibility(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::EntityId entity, const bool visible) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "entity.set_sprite_renderer";
    });
    if (command == catalog.end())
        return core::Result<SpriteRendererVisibilityPreview>::failure({core::ErrorCode::NotFound,
            "Sprite visibility preview requires the registered entity.set_sprite_renderer command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<SpriteRendererVisibilityPreview>::failure({core::ErrorCode::VersionMismatch,
            "Sprite visibility preview command schema version is stale"});
    if (!command->transactional)
        return core::Result<SpriteRendererVisibilityPreview>::failure({core::ErrorCode::InvalidState,
            "Sprite visibility preview requires a transactional command"});

    const auto* component = activeScene.spriteRenderer(entity);
    if (!component)
        return core::Result<SpriteRendererVisibilityPreview>::failure({core::ErrorCode::NotFound,
            "Sprite visibility preview requires an existing SpriteRenderer component"});
    const scene::SetSpriteRendererPayload payload{entity, component->texture, visible};
    const auto validation = api_.validate("entity.set_sprite_renderer", commandSchemaVersion, payload);
    if (!validation)
        return core::Result<SpriteRendererVisibilityPreview>::failure(validation.error());

    SpriteRendererVisibilityPreview preview;
    preview.sceneId = activeScene.id();
    preview.entityId = entity;
    preview.componentId = component->id;
    preview.texture = component->texture;
    preview.beforeVisible = component->visible;
    preview.afterVisible = visible;
    preview.changesValue = preview.beforeVisible != preview.afterVisible;
    return core::Result<SpriteRendererVisibilityPreview>::success(preview);
}

core::Result<PrimitiveCreationPreview> CodexProjectAdapter::previewCreatePrimitive(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::CreatePrimitiveEntityPayload& payload) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "entity.create_primitive";
    });
    if (command == catalog.end())
        return core::Result<PrimitiveCreationPreview>::failure({core::ErrorCode::NotFound,
            "Primitive creation preview requires the registered entity.create_primitive command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<PrimitiveCreationPreview>::failure({core::ErrorCode::VersionMismatch,
            "Primitive creation preview command schema is stale"});
    if (!command->transactional)
        return core::Result<PrimitiveCreationPreview>::failure({core::ErrorCode::InvalidState,
            "Primitive creation preview requires a transactional command"});

    const auto validation = api_.validate("entity.create_primitive", commandSchemaVersion, payload);
    if (!validation) return core::Result<PrimitiveCreationPreview>::failure(validation.error());
    const auto expectedEntityId = activeScene.nextEntityIdHint();
    if (expectedEntityId == 0)
        return core::Result<PrimitiveCreationPreview>::failure({core::ErrorCode::InvalidState,
            "Scene entity ID space is exhausted"});

    return core::Result<PrimitiveCreationPreview>::success({activeScene.id(), expectedEntityId,
        payload.name, payload.parent, payload.primitive, payload.visible});
}

core::Result<Camera3DAdditionPreview> CodexProjectAdapter::previewAddCamera3D(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::AddCamera3DPayload& payload) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "camera3d.add";
    });
    if (command == catalog.end())
        return core::Result<Camera3DAdditionPreview>::failure({core::ErrorCode::NotFound,
            "Camera3D addition preview requires the registered camera3d.add command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<Camera3DAdditionPreview>::failure({core::ErrorCode::VersionMismatch,
            "Camera3D addition preview command schema is stale"});
    if (!command->transactional)
        return core::Result<Camera3DAdditionPreview>::failure({core::ErrorCode::InvalidState,
            "Camera3D addition preview requires a transactional command"});

    const auto validation = api_.validate("camera3d.add", commandSchemaVersion, payload);
    if (!validation) return core::Result<Camera3DAdditionPreview>::failure(validation.error());
    return core::Result<Camera3DAdditionPreview>::success({activeScene.id(), payload.entity});
}

core::Result<Camera3DLensPreview> CodexProjectAdapter::previewSetCamera3DLens(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::SetCamera3DLensPayload& payload) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "camera3d.set_lens";
    });
    if (command == catalog.end())
        return core::Result<Camera3DLensPreview>::failure({core::ErrorCode::NotFound,
            "Camera3D lens preview requires the registered camera3d.set_lens command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<Camera3DLensPreview>::failure({core::ErrorCode::VersionMismatch,
            "Camera3D lens preview command schema is stale"});
    if (!command->transactional)
        return core::Result<Camera3DLensPreview>::failure({core::ErrorCode::InvalidState,
            "Camera3D lens preview requires a transactional command"});

    const auto* component = activeScene.camera3D(payload.entity);
    if (!component)
        return core::Result<Camera3DLensPreview>::failure({core::ErrorCode::NotFound,
            "Camera3D lens preview requires an existing Camera3D component"});
    const auto validation = api_.validate("camera3d.set_lens", commandSchemaVersion, payload);
    if (!validation) return core::Result<Camera3DLensPreview>::failure(validation.error());

    Camera3DLensPreview preview;
    preview.sceneId = activeScene.id();
    preview.entityId = payload.entity;
    preview.componentId = component->id;
    preview.beforeVerticalFovDegrees = component->verticalFovDegrees;
    preview.afterVerticalFovDegrees = payload.verticalFovDegrees;
    preview.beforeNearPlane = component->nearPlane;
    preview.afterNearPlane = payload.nearPlane;
    preview.beforeFarPlane = component->farPlane;
    preview.afterFarPlane = payload.farPlane;
    preview.active = component->active;
    preview.changesValue = preview.beforeVerticalFovDegrees != preview.afterVerticalFovDegrees
        || preview.beforeNearPlane != preview.afterNearPlane
        || preview.beforeFarPlane != preview.afterFarPlane;
    return core::Result<Camera3DLensPreview>::success(preview);
}

core::Result<Camera3DActiveSelectionPreview> CodexProjectAdapter::previewSetActiveCamera3D(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::SetActiveCamera3DPayload& payload) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "camera3d.set_active";
    });
    if (command == catalog.end())
        return core::Result<Camera3DActiveSelectionPreview>::failure({core::ErrorCode::NotFound,
            "Active Camera3D review requires the registered camera3d.set_active command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<Camera3DActiveSelectionPreview>::failure({core::ErrorCode::VersionMismatch,
            "Active Camera3D review command schema is stale"});
    if (!command->transactional)
        return core::Result<Camera3DActiveSelectionPreview>::failure({core::ErrorCode::InvalidState,
            "Active Camera3D review requires a transactional command"});

    const auto validation = api_.validate("camera3d.set_active", commandSchemaVersion, payload);
    if (!validation) return core::Result<Camera3DActiveSelectionPreview>::failure(validation.error());

    Camera3DActiveSelectionPreview preview;
    preview.sceneId = activeScene.id();
    preview.previousEntity = activeScene.activeCamera3D();
    if (payload.entity != 0) preview.targetEntity = payload.entity;
    if (preview.previousEntity)
    {
        const auto* current = activeScene.camera3D(*preview.previousEntity);
        if (!current || !current->active)
            return core::Result<Camera3DActiveSelectionPreview>::failure({core::ErrorCode::InvalidState,
                "Scene active Camera3D selection is inconsistent"});
        preview.previousComponentId = current->id;
    }
    if (preview.targetEntity)
    {
        const auto* target = activeScene.camera3D(*preview.targetEntity);
        if (!target)
            return core::Result<Camera3DActiveSelectionPreview>::failure({core::ErrorCode::NotFound,
                "Active Camera3D target component does not exist"});
        preview.targetComponentId = target->id;
    }
    preview.changesValue = preview.previousEntity != preview.targetEntity;
    return core::Result<Camera3DActiveSelectionPreview>::success(preview);
}

core::Result<TransformChangePreview> CodexProjectAdapter::previewSetTransform(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::SetTransformPayload& payload) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "entity.set_transform";
    });
    if (command == catalog.end())
        return core::Result<TransformChangePreview>::failure({core::ErrorCode::NotFound,
            "Transform preview requires the registered entity.set_transform command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<TransformChangePreview>::failure({core::ErrorCode::VersionMismatch,
            "Transform preview command schema version is stale"});
    if (!command->transactional)
        return core::Result<TransformChangePreview>::failure({core::ErrorCode::InvalidState,
            "Transform preview requires a transactional command"});

    const auto validation = api_.validate("entity.set_transform", commandSchemaVersion, payload);
    if (!validation) return core::Result<TransformChangePreview>::failure(validation.error());
    const auto* component = activeScene.transform(payload.entity);
    if (!component)
        return core::Result<TransformChangePreview>::failure({core::ErrorCode::NotFound,
            "Transform preview requires an existing Transform component"});

    TransformChangePreview preview;
    preview.sceneId = activeScene.id();
    preview.entityId = payload.entity;
    preview.componentId = component->id;
    preview.before = component->value;
    preview.after = payload.value;
    preview.changesValue = preview.before != preview.after;

    auto currentId = std::optional<scene::EntityId>{payload.entity};
    std::unordered_set<scene::EntityId> visited;
    visited.reserve(activeScene.entities().size());
    while (currentId)
    {
        if (!visited.insert(*currentId).second)
            return core::Result<TransformChangePreview>::failure({core::ErrorCode::InvalidState,
                "Transform preview cannot capture a cyclic hierarchy"});
        const auto* entity = activeScene.find(*currentId);
        const auto* transform = activeScene.transform(*currentId);
        if (!entity || !transform)
            return core::Result<TransformChangePreview>::failure({core::ErrorCode::InvalidState,
                "Transform preview cannot capture an incomplete hierarchy"});
        preview.hierarchy.push_back({entity->id, transform->id, entity->parent, transform->value});
        if (preview.hierarchy.size() > activeScene.entities().size())
            return core::Result<TransformChangePreview>::failure({core::ErrorCode::InvalidState,
                "Transform preview hierarchy exceeds the Scene entity count"});
        currentId = entity->parent;
    }
    return core::Result<TransformChangePreview>::success(std::move(preview));
}

core::Result<EntityParentChangePreview> CodexProjectAdapter::previewSetEntityParent(
    const scene::Scene& activeScene, const std::uint32_t commandSchemaVersion,
    const scene::SetEntityParentPayload& payload) const
{
    const auto catalog = api_.commandCatalog();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& entry) {
        return entry.schema.id == "entity.reparent";
    });
    if (command == catalog.end())
        return core::Result<EntityParentChangePreview>::failure({core::ErrorCode::NotFound,
            "Hierarchy preview requires the registered entity.reparent command"});
    if (commandSchemaVersion == 0 || commandSchemaVersion != command->schema.version)
        return core::Result<EntityParentChangePreview>::failure({core::ErrorCode::VersionMismatch,
            "Hierarchy preview command schema version is stale"});
    if (!command->transactional)
        return core::Result<EntityParentChangePreview>::failure({core::ErrorCode::InvalidState,
            "Hierarchy preview requires a transactional command"});

    const auto validation = api_.validate("entity.reparent", commandSchemaVersion, payload);
    if (!validation) return core::Result<EntityParentChangePreview>::failure(validation.error());
    const auto* targetEntity = activeScene.find(payload.entity);
    const auto* targetTransform = activeScene.transform(payload.entity);
    if (!targetEntity || !targetTransform)
        return core::Result<EntityParentChangePreview>::failure({core::ErrorCode::NotFound,
            "Hierarchy preview requires an existing entity with a Transform"});

    const auto captureChain = [&activeScene](std::optional<scene::EntityId> currentId)
        -> core::Result<std::vector<TransformHierarchySnapshot>> {
        std::vector<TransformHierarchySnapshot> chain;
        std::unordered_set<scene::EntityId> visited;
        visited.reserve(activeScene.entities().size());
        while (currentId)
        {
            if (!visited.insert(*currentId).second)
                return core::Result<std::vector<TransformHierarchySnapshot>>::failure({
                    core::ErrorCode::InvalidState, "Hierarchy preview cannot capture a cyclic chain"});
            const auto* entity = activeScene.find(*currentId);
            const auto* transform = activeScene.transform(*currentId);
            if (!entity || !transform)
                return core::Result<std::vector<TransformHierarchySnapshot>>::failure({
                    core::ErrorCode::InvalidState, "Hierarchy preview cannot capture an incomplete chain"});
            chain.push_back({entity->id, transform->id, entity->parent, transform->value});
            if (chain.size() > activeScene.entities().size())
                return core::Result<std::vector<TransformHierarchySnapshot>>::failure({
                    core::ErrorCode::InvalidState, "Hierarchy preview chain exceeds the Scene entity count"});
            currentId = entity->parent;
        }
        return core::Result<std::vector<TransformHierarchySnapshot>>::success(std::move(chain));
    };

    auto sourceHierarchy = captureChain(payload.entity);
    if (!sourceHierarchy) return core::Result<EntityParentChangePreview>::failure(sourceHierarchy.error());
    auto targetHierarchy = captureChain(payload.parent);
    if (!targetHierarchy) return core::Result<EntityParentChangePreview>::failure(targetHierarchy.error());

    std::unordered_map<scene::EntityId, std::vector<scene::EntityId>> children;
    children.reserve(activeScene.entities().size());
    for (const auto& entity : activeScene.entities())
        if (entity.parent) children[*entity.parent].push_back(entity.id);
    std::vector<scene::EntityId> subtreeIds{payload.entity};
    std::unordered_set<scene::EntityId> subtreeSet{payload.entity};
    for (std::size_t index = 0; index < subtreeIds.size(); ++index)
    {
        const auto found = children.find(subtreeIds[index]);
        if (found == children.end()) continue;
        for (const auto child : found->second)
            if (subtreeSet.insert(child).second) subtreeIds.push_back(child);
    }
    std::sort(subtreeIds.begin(), subtreeIds.end());

    EntityParentChangePreview preview;
    preview.sceneId = activeScene.id();
    preview.entityId = payload.entity;
    preview.transformComponentId = targetTransform->id;
    preview.beforeParent = targetEntity->parent;
    preview.afterParent = payload.parent;
    preview.localTransform = targetTransform->value;
    preview.sourceHierarchy = std::move(sourceHierarchy).value();
    preview.targetParentHierarchy = std::move(targetHierarchy).value();
    preview.affectedSubtree.reserve(subtreeIds.size());
    for (const auto id : subtreeIds)
    {
        const auto* entity = activeScene.find(id);
        const auto* transform = activeScene.transform(id);
        if (!entity || !transform)
            return core::Result<EntityParentChangePreview>::failure({core::ErrorCode::InvalidState,
                "Hierarchy preview cannot capture an affected entity without a Transform"});
        preview.affectedSubtree.push_back({entity->id, transform->id, entity->parent, transform->value});
    }
    preview.changesValue = preview.beforeParent != preview.afterParent;
    return core::Result<EntityParentChangePreview>::success(std::move(preview));
}
}
