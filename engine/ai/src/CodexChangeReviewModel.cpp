#include "astro/ai/CodexChangeReviewModel.hpp"

#include <utility>

namespace astro::ai
{
core::Result<void> CodexChangeReviewModel::preparePropertyChange(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    scene::SetScenePropertyPayload payload, const std::uint32_t commandSchemaVersion)
{
    pending_.reset();
    pendingSpriteRenderer_.reset();
    pendingPrimitive_.reset();
    pendingCamera3D_.reset();
    pendingCamera3DLens_.reset();
    pendingCamera3DActiveSelection_.reset();
    pendingTransform_.reset();
    pendingEntityParent_.reset();
    auto preview = adapter.previewSetProperty(activeScene, commandSchemaVersion, payload);
    if (!preview) return core::Result<void>::failure(preview.error());

    pending_ = PendingPropertyChange{std::move(payload), std::move(preview).value(), commandSchemaVersion,
        projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::prepareSpriteRendererVisibilityChange(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    const scene::EntityId entity, const bool visible, const std::uint32_t commandSchemaVersion)
{
    pending_.reset();
    pendingSpriteRenderer_.reset();
    pendingPrimitive_.reset();
    pendingCamera3D_.reset();
    pendingCamera3DLens_.reset();
    pendingCamera3DActiveSelection_.reset();
    pendingTransform_.reset();
    pendingEntityParent_.reset();
    auto preview = adapter.previewSpriteRendererVisibility(activeScene, commandSchemaVersion, entity, visible);
    if (!preview) return core::Result<void>::failure(preview.error());

    pendingSpriteRenderer_ = PendingSpriteRendererVisibilityChange{
        {entity, preview.value().texture, visible}, std::move(preview).value(), commandSchemaVersion,
        projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::preparePrimitiveCreation(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    scene::CreatePrimitiveEntityPayload payload, const std::uint32_t commandSchemaVersion)
{
    pending_.reset();
    pendingSpriteRenderer_.reset();
    pendingPrimitive_.reset();
    pendingCamera3D_.reset();
    pendingCamera3DLens_.reset();
    pendingCamera3DActiveSelection_.reset();
    pendingTransform_.reset();
    pendingEntityParent_.reset();
    auto preview = adapter.previewCreatePrimitive(activeScene, commandSchemaVersion, payload);
    if (!preview) return core::Result<void>::failure(preview.error());
    pendingPrimitive_ = PendingPrimitiveCreationChange{std::move(payload), std::move(preview).value(),
        commandSchemaVersion, projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::prepareCamera3DAddition(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    scene::AddCamera3DPayload payload, const std::uint32_t commandSchemaVersion)
{
    pending_.reset();
    pendingSpriteRenderer_.reset();
    pendingPrimitive_.reset();
    pendingCamera3D_.reset();
    pendingCamera3DLens_.reset();
    pendingCamera3DActiveSelection_.reset();
    pendingTransform_.reset();
    pendingEntityParent_.reset();
    auto preview = adapter.previewAddCamera3D(activeScene, commandSchemaVersion, payload);
    if (!preview) return core::Result<void>::failure(preview.error());
    pendingCamera3D_ = PendingCamera3DAdditionChange{payload, preview.value(), commandSchemaVersion,
        projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::prepareCamera3DLensChange(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    scene::SetCamera3DLensPayload payload, const std::uint32_t commandSchemaVersion)
{
    pending_.reset();
    pendingSpriteRenderer_.reset();
    pendingPrimitive_.reset();
    pendingCamera3D_.reset();
    pendingCamera3DLens_.reset();
    pendingCamera3DActiveSelection_.reset();
    pendingTransform_.reset();
    pendingEntityParent_.reset();
    auto preview = adapter.previewSetCamera3DLens(activeScene, commandSchemaVersion, payload);
    if (!preview) return core::Result<void>::failure(preview.error());
    pendingCamera3DLens_ = PendingCamera3DLensChange{payload, std::move(preview).value(),
        commandSchemaVersion, projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::prepareCamera3DActiveSelectionChange(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    scene::SetActiveCamera3DPayload payload, const std::uint32_t commandSchemaVersion)
{
    pending_.reset();
    pendingSpriteRenderer_.reset();
    pendingPrimitive_.reset();
    pendingCamera3D_.reset();
    pendingCamera3DLens_.reset();
    pendingCamera3DActiveSelection_.reset();
    pendingTransform_.reset();
    pendingEntityParent_.reset();
    auto preview = adapter.previewSetActiveCamera3D(activeScene, commandSchemaVersion, payload);
    if (!preview) return core::Result<void>::failure(preview.error());
    pendingCamera3DActiveSelection_ = PendingCamera3DActiveSelectionChange{
        payload, std::move(preview).value(), commandSchemaVersion, projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::prepareTransformChange(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    scene::SetTransformPayload payload, const std::uint32_t commandSchemaVersion)
{
    cancel();
    auto preview = adapter.previewSetTransform(activeScene, commandSchemaVersion, payload);
    if (!preview) return core::Result<void>::failure(preview.error());
    pendingTransform_ = PendingTransformChange{std::move(payload), std::move(preview).value(),
        commandSchemaVersion, projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::prepareEntityParentChange(
    const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
    scene::SetEntityParentPayload payload, const std::uint32_t commandSchemaVersion)
{
    cancel();
    auto preview = adapter.previewSetEntityParent(activeScene, commandSchemaVersion, payload);
    if (!preview) return core::Result<void>::failure(preview.error());
    pendingEntityParent_ = PendingEntityParentChange{std::move(payload), std::move(preview).value(),
        commandSchemaVersion, projectIdentity, sceneIdentity};
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::confirmAndApply(
    core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity)
{
    if (pendingPrimitive_ || pendingCamera3D_ || pendingCamera3DLens_ || pendingCamera3DActiveSelection_
        || pendingEntityParent_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "This reviewed operation requires its result-specific confirmation method"});
    if (!pending_ && !pendingSpriteRenderer_ && !pendingTransform_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "There is no pending change preview to confirm"});
    const auto& expectedProject = pending_ ? pending_->projectIdentity
        : pendingSpriteRenderer_ ? pendingSpriteRenderer_->projectIdentity : pendingTransform_->projectIdentity;
    const auto& expectedScene = pending_ ? pending_->sceneIdentity
        : pendingSpriteRenderer_ ? pendingSpriteRenderer_->sceneIdentity : pendingTransform_->sceneIdentity;
    const auto expectedSceneId = pending_ ? pending_->preview.sceneId
        : pendingSpriteRenderer_ ? pendingSpriteRenderer_->preview.sceneId : pendingTransform_->preview.sceneId;
    if (expectedProject != projectIdentity || expectedScene != sceneIdentity || expectedSceneId != activeScene.id())
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Project or scene changed after preview; create a new preview before applying"});

    core::Result<void> applied = core::Result<void>::failure({core::ErrorCode::InvalidState,
        "Pending change preview is invalid"});
    if (pending_)
    {
        auto current = adapter.previewSetProperty(activeScene, pending_->commandSchemaVersion, pending_->payload);
        if (!current) return core::Result<void>::failure(current.error());
        if (current.value().before != pending_->preview.before
            || current.value().after != pending_->preview.after || !current.value().changesValue)
            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                "Scene property changed after preview; review a fresh preview before applying"});
        applied = api.execute<scene::SetScenePropertyPayload, void>(
            "entity.set_property", pending_->commandSchemaVersion, pending_->payload);
    }
    else if (pendingSpriteRenderer_)
    {
        auto current = adapter.previewSpriteRendererVisibility(activeScene,
            pendingSpriteRenderer_->commandSchemaVersion, pendingSpriteRenderer_->payload.entity,
            pendingSpriteRenderer_->payload.visible);
        if (!current) return core::Result<void>::failure(current.error());
        const auto& expected = pendingSpriteRenderer_->preview;
        if (current.value().componentId != expected.componentId
            || current.value().texture != expected.texture
            || current.value().beforeVisible != expected.beforeVisible
            || current.value().afterVisible != expected.afterVisible || !current.value().changesValue)
            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                "SpriteRenderer changed after preview; review a fresh preview before applying"});
        applied = api.execute<scene::SetSpriteRendererPayload, void>(
            "entity.set_sprite_renderer", pendingSpriteRenderer_->commandSchemaVersion,
            pendingSpriteRenderer_->payload);
    }
    else
    {
        auto current = adapter.previewSetTransform(activeScene,
            pendingTransform_->commandSchemaVersion, pendingTransform_->payload);
        if (!current) return core::Result<void>::failure(current.error());
        const auto& expected = pendingTransform_->preview;
        if (current.value().sceneId != expected.sceneId || current.value().entityId != expected.entityId
            || current.value().componentId != expected.componentId
            || current.value().before != expected.before || current.value().after != expected.after
            || current.value().hierarchy != expected.hierarchy || !current.value().changesValue)
            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                "Transform or its parent chain changed after preview; review a fresh preview before applying"});
        applied = api.execute<scene::SetTransformPayload, void>(
            "entity.set_transform", pendingTransform_->commandSchemaVersion, pendingTransform_->payload);
        if (!applied) return applied;
        const auto* transform = activeScene.transform(expected.entityId);
        if (!transform || transform->id != expected.componentId || transform->value != expected.after)
            return core::Result<void>::failure({core::ErrorCode::InternalError,
                "Transform command did not apply the reviewed local values while preserving component identity"});
    }
    if (!applied) return applied;
    cancel();
    return core::Result<void>::success();
}

core::Result<scene::EntityId> CodexChangeReviewModel::confirmAndCreatePrimitive(
    core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity)
{
    if (!pendingPrimitive_)
        return core::Result<scene::EntityId>::failure({core::ErrorCode::InvalidState,
            "There is no pending primitive creation preview to confirm"});
    const auto& pending = *pendingPrimitive_;
    if (pending.projectIdentity != projectIdentity || pending.sceneIdentity != sceneIdentity
        || pending.preview.sceneId != activeScene.id()
        || pending.preview.expectedEntityId != activeScene.nextEntityIdHint())
        return core::Result<scene::EntityId>::failure({core::ErrorCode::InvalidState,
            "Project or scene changed after preview; create a new preview before applying"});

    auto current = adapter.previewCreatePrimitive(activeScene, pending.commandSchemaVersion, pending.payload);
    if (!current) return core::Result<scene::EntityId>::failure(current.error());
    if (current.value().expectedEntityId != pending.preview.expectedEntityId
        || current.value().name != pending.preview.name || current.value().parent != pending.preview.parent
        || current.value().primitive != pending.preview.primitive
        || current.value().visible != pending.preview.visible)
        return core::Result<scene::EntityId>::failure({core::ErrorCode::InvalidState,
            "Primitive creation proposal changed after preview; review a fresh preview before applying"});

    auto created = api.execute<scene::CreatePrimitiveEntityPayload, scene::EntityId>(
        "entity.create_primitive", pending.commandSchemaVersion, pending.payload);
    if (!created) return created;
    if (created.value() != pending.preview.expectedEntityId)
        return core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError,
            "Primitive command returned an entity ID different from the reviewed preview"});
    pendingPrimitive_.reset();
    return created;
}

core::Result<void> CodexChangeReviewModel::confirmAndAddCamera3D(
    core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity)
{
    if (!pendingCamera3D_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "There is no pending Camera3D addition preview to confirm"});
    const auto& pending = *pendingCamera3D_;
    if (pending.projectIdentity != projectIdentity || pending.sceneIdentity != sceneIdentity
        || pending.preview.sceneId != activeScene.id()
        || pending.preview.entityId != pending.payload.entity)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Project or scene changed after preview; create a new preview before applying"});

    auto current = adapter.previewAddCamera3D(activeScene, pending.commandSchemaVersion, pending.payload);
    if (!current) return core::Result<void>::failure(current.error());
    if (current.value().sceneId != pending.preview.sceneId
        || current.value().entityId != pending.preview.entityId)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Camera3D target changed after preview; review a fresh preview before applying"});

    auto added = api.execute<scene::AddCamera3DPayload, void>(
        "camera3d.add", pending.commandSchemaVersion, pending.payload);
    if (!added) return added;
    const auto* component = activeScene.camera3D(pending.payload.entity);
    if (!component || component->active)
        return core::Result<void>::failure({core::ErrorCode::InternalError,
            "Camera3D add command did not create the reviewed inactive component"});
    pendingCamera3D_.reset();
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::confirmAndSetCamera3DLens(
    core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity)
{
    if (!pendingCamera3DLens_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "There is no pending Camera3D lens preview to confirm"});
    const auto& pending = *pendingCamera3DLens_;
    if (pending.projectIdentity != projectIdentity || pending.sceneIdentity != sceneIdentity
        || pending.preview.sceneId != activeScene.id())
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Project or scene changed after preview; create a new preview before applying"});

    auto current = adapter.previewSetCamera3DLens(activeScene, pending.commandSchemaVersion, pending.payload);
    if (!current) return core::Result<void>::failure(current.error());
    const auto& expected = pending.preview;
    if (current.value().sceneId != expected.sceneId || current.value().entityId != expected.entityId
        || current.value().componentId != expected.componentId
        || current.value().beforeVerticalFovDegrees != expected.beforeVerticalFovDegrees
        || current.value().beforeNearPlane != expected.beforeNearPlane
        || current.value().beforeFarPlane != expected.beforeFarPlane
        || current.value().afterVerticalFovDegrees != expected.afterVerticalFovDegrees
        || current.value().afterNearPlane != expected.afterNearPlane
        || current.value().afterFarPlane != expected.afterFarPlane
        || current.value().active != expected.active || !current.value().changesValue)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Camera3D changed after preview; review a fresh lens preview before applying"});

    auto applied = api.execute<scene::SetCamera3DLensPayload, void>(
        "camera3d.set_lens", pending.commandSchemaVersion, pending.payload);
    if (!applied) return applied;
    const auto* component = activeScene.camera3D(pending.payload.entity);
    if (!component || component->id != expected.componentId || component->active != expected.active
        || component->verticalFovDegrees != expected.afterVerticalFovDegrees
        || component->nearPlane != expected.afterNearPlane || component->farPlane != expected.afterFarPlane)
        return core::Result<void>::failure({core::ErrorCode::InternalError,
            "Camera3D lens command did not apply the reviewed values while preserving component identity"});
    pendingCamera3DLens_.reset();
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::confirmAndSetActiveCamera3D(
    core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity)
{
    if (!pendingCamera3DActiveSelection_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "There is no pending active Camera3D preview to confirm"});
    const auto& pending = *pendingCamera3DActiveSelection_;
    if (pending.projectIdentity != projectIdentity || pending.sceneIdentity != sceneIdentity
        || pending.preview.sceneId != activeScene.id())
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Project or scene changed after preview; create a new preview before applying"});

    auto current = adapter.previewSetActiveCamera3D(activeScene, pending.commandSchemaVersion, pending.payload);
    if (!current) return core::Result<void>::failure(current.error());
    const auto& expected = pending.preview;
    if (current.value().sceneId != expected.sceneId
        || current.value().previousEntity != expected.previousEntity
        || current.value().targetEntity != expected.targetEntity
        || current.value().previousComponentId != expected.previousComponentId
        || current.value().targetComponentId != expected.targetComponentId
        || !current.value().changesValue)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Active Camera3D selection changed after preview; review the current selection before applying"});

    auto applied = api.execute<scene::SetActiveCamera3DPayload, void>(
        "camera3d.set_active", pending.commandSchemaVersion, pending.payload);
    if (!applied) return applied;
    if (activeScene.activeCamera3D() != expected.targetEntity)
        return core::Result<void>::failure({core::ErrorCode::InternalError,
            "camera3d.set_active did not apply the reviewed camera selection"});
    pendingCamera3DActiveSelection_.reset();
    return core::Result<void>::success();
}

core::Result<void> CodexChangeReviewModel::confirmAndReparent(
    core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
    const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity)
{
    if (!pendingEntityParent_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "There is no pending hierarchy preview to confirm"});
    const auto& pending = *pendingEntityParent_;
    const auto& expected = pending.preview;
    if (pending.projectIdentity != projectIdentity || pending.sceneIdentity != sceneIdentity
        || expected.sceneId != activeScene.id())
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Project or scene changed after preview; create a new preview before applying"});

    auto current = adapter.previewSetEntityParent(activeScene, pending.commandSchemaVersion, pending.payload);
    if (!current) return core::Result<void>::failure(current.error());
    if (current.value().sceneId != expected.sceneId || current.value().entityId != expected.entityId
        || current.value().transformComponentId != expected.transformComponentId
        || current.value().beforeParent != expected.beforeParent || current.value().afterParent != expected.afterParent
        || current.value().localTransform != expected.localTransform
        || current.value().sourceHierarchy != expected.sourceHierarchy
        || current.value().targetParentHierarchy != expected.targetParentHierarchy
        || current.value().affectedSubtree != expected.affectedSubtree || !current.value().changesValue)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Entity, parent chain, or affected subtree changed after preview; review a fresh hierarchy preview"});

    auto applied = api.execute<scene::SetEntityParentPayload, void>(
        "entity.reparent", pending.commandSchemaVersion, pending.payload);
    if (!applied) return applied;
    const auto* entity = activeScene.find(expected.entityId);
    const auto* transform = activeScene.transform(expected.entityId);
    if (!entity || !transform || entity->parent != expected.afterParent
        || transform->id != expected.transformComponentId || transform->value != expected.localTransform)
        return core::Result<void>::failure({core::ErrorCode::InternalError,
            "Reparent command did not preserve the reviewed local Transform and target parent"});
    pendingEntityParent_.reset();
    return core::Result<void>::success();
}
}
