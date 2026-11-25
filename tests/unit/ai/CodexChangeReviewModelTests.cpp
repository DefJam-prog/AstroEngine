#include "astro/ai/CodexChangeReviewModel.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    using namespace astro;
    core::EngineContext engineContext;
    core::CommandRegistry commands(engineContext);
    scene::Scene document(41, "Review Ω");
    const auto entity = document.createEntity("Исходное имя Ω");
    check(entity.hasValue(), "create review entity");
    check(scene::registerSceneCommands(commands, document).hasValue(), "register scene commands");
    if (!entity) return EXIT_FAILURE;

    core::AIEditorApi api(commands);
    ai::CodexProjectAdapter adapter(api);
    ai::CodexChangeReviewModel review;
    const std::filesystem::path projectPath = "C:/Projects/Astro";
    const std::filesystem::path scenePath = "C:/Projects/Astro/Scenes/Main.astro-scene";
    scene::SetScenePropertyPayload proposal{entity.value(), "name", std::string{"Предложенное имя Ω"}};

    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath),
        "cannot apply without a pending preview");
    check(review.preparePropertyChange(adapter, document, projectPath, scenePath, proposal).hasValue(),
        "prepare valid property preview");
    check(review.pending().has_value() && review.pending()->preview.changesValue
        && document.find(entity.value())->name == "Исходное имя Ω",
        "preparing a review displays a change without mutating the Scene");

    check(!review.confirmAndApply(api, adapter, document, projectPath,
        "C:/Projects/Astro/Scenes/Other.astro-scene"),
        "confirmation rejects a different active scene path");
    check(!review.confirmAndApply(api, adapter, document,
        "C:/Projects/Other", scenePath),
        "confirmation rejects a different active project path");
    check(document.find(entity.value())->name == "Исходное имя Ω",
        "scene identity rejection leaves data unchanged");

    check(document.setProperty(entity.value(), "name", std::string{"Внешнее изменение Ω"}).hasValue(),
        "simulate an intervening editor change");
    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath),
        "confirmation rejects a stale preview");
    check(document.find(entity.value())->name == "Внешнее изменение Ω",
        "stale preview does not overwrite intervening data");

    proposal.value = std::string{"Подтверждённое имя Ω"};
    check(review.preparePropertyChange(adapter, document, projectPath, scenePath, proposal).hasValue(),
        "refresh a stale preview");
    check(review.confirmAndApply(api, adapter, document, projectPath, scenePath).hasValue(),
        "confirm and apply the current preview");
    check(!review.pending() && document.find(entity.value())->name == "Подтверждённое имя Ω",
        "confirmed change is applied and the pending review is consumed");
    check(commands.canUndo() && commands.undo().hasValue()
        && document.find(entity.value())->name == "Внешнее изменение Ω",
        "confirmed property change is recorded in transactional Undo history");
    check(commands.redo().hasValue()
        && document.find(entity.value())->name == "Подтверждённое имя Ω",
        "confirmed property change can be redone");

    proposal.value = std::string{"Отменённое имя"};
    check(review.preparePropertyChange(adapter, document, projectPath, scenePath, proposal).hasValue(),
        "prepare preview for cancellation");
    review.cancel();
    check(!review.pending() && !review.confirmAndApply(api, adapter, document, projectPath, scenePath)
        && document.find(entity.value())->name == "Подтверждённое имя Ω",
        "cancel clears preview and prevents later application");

    check(review.preparePropertyChange(adapter, document, projectPath, scenePath, proposal).hasValue(),
        "prepare a valid preview before an invalid replacement request");
    auto invalidProposal = proposal;
    invalidProposal.property = "unknown_property";
    check(!review.preparePropertyChange(adapter, document, projectPath, scenePath, invalidProposal)
        && !review.pending(),
        "a rejected replacement preview cannot leave an earlier proposal confirmable");
    check(document.find(entity.value())->name == "Подтверждённое имя Ω",
        "invalid replacement preview leaves Scene unchanged");

    check(document.setSpriteRenderer(entity.value(), {501}, true).hasValue(),
        "create existing SpriteRenderer for visibility review");
    check(review.prepareSpriteRendererVisibilityChange(adapter, document, projectPath, scenePath,
        entity.value(), false).hasValue(), "prepare SpriteRenderer visibility preview");
    const auto spriteBefore = document.spriteRenderer(entity.value());
    check(spriteBefore && spriteBefore->visible && spriteBefore->texture.value == 501
        && review.pendingSpriteRenderer()
        && review.pendingSpriteRenderer()->preview.texture.value == 501
        && !review.pendingSpriteRenderer()->preview.afterVisible,
        "sprite visibility preview preserves the texture and does not mutate Scene");
    check(!review.confirmAndApply(api, adapter, document, "C:/Projects/Other", scenePath)
        && document.spriteRenderer(entity.value())->visible,
        "sprite visibility confirmation rejects changed project identity");

    check(document.setSpriteRenderer(entity.value(), {502}, true).hasValue(),
        "simulate an intervening SpriteRenderer texture change");
    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath)
        && document.spriteRenderer(entity.value())->texture.value == 502
        && document.spriteRenderer(entity.value())->visible,
        "stale SpriteRenderer preview cannot overwrite another texture or visibility edit");
    check(document.setSpriteRenderer(entity.value(), {501}, true).hasValue(),
        "restore SpriteRenderer before refreshed review");
    check(review.prepareSpriteRendererVisibilityChange(adapter, document, projectPath, scenePath,
        entity.value(), false).hasValue()
        && review.confirmAndApply(api, adapter, document, projectPath, scenePath).hasValue(),
        "confirm refreshed SpriteRenderer visibility preview");
    check(!document.spriteRenderer(entity.value())->visible
        && document.spriteRenderer(entity.value())->texture.value == 501,
        "confirmed visibility change preserves SpriteRenderer texture");
    check(commands.undo().hasValue() && document.spriteRenderer(entity.value())->visible
        && document.spriteRenderer(entity.value())->texture.value == 501,
        "SpriteRenderer visibility change participates in Undo");
    check(commands.redo().hasValue() && !document.spriteRenderer(entity.value())->visible
        && document.spriteRenderer(entity.value())->texture.value == 501,
        "SpriteRenderer visibility change participates in Redo");

    check(review.prepareSpriteRendererVisibilityChange(adapter, document, projectPath, scenePath,
        entity.value(), true).hasValue(), "prepare SpriteRenderer review for cancellation");
    review.cancel();
    check(!review.hasPending() && !review.confirmAndApply(api, adapter, document, projectPath, scenePath)
        && !document.spriteRenderer(entity.value())->visible,
        "cancel prevents SpriteRenderer visibility application");

    const scene::CreatePrimitiveEntityPayload primitiveProposal{
        "AI Sphere Ω", entity.value(), scene::Primitive3D::Sphere, true};
    const auto entityCountBeforePrimitiveReview = document.entities().size();
    check(!review.confirmAndCreatePrimitive(api, adapter, document, projectPath, scenePath),
        "cannot create a primitive without a pending review");
    check(review.preparePrimitiveCreation(adapter, document, projectPath, scenePath,
        primitiveProposal).hasValue(), "prepare a typed primitive creation preview");
    const auto firstExpectedPrimitiveId = document.nextEntityIdHint();
    check(review.pendingPrimitive()
        && review.pendingPrimitive()->preview.expectedEntityId == firstExpectedPrimitiveId
        && review.pendingPrimitive()->preview.name == primitiveProposal.name
        && review.pendingPrimitive()->preview.parent == primitiveProposal.parent
        && review.pendingPrimitive()->preview.primitive == primitiveProposal.primitive
        && document.entities().size() == entityCountBeforePrimitiveReview,
        "primitive preview exposes its typed result without mutating Scene");
    check(!review.confirmAndCreatePrimitive(api, adapter, document, "C:/Projects/Other", scenePath)
        && document.entities().size() == entityCountBeforePrimitiveReview,
        "primitive confirmation rejects changed project identity");

    const auto intervening = document.createEntity("Intervening entity Ω");
    check(intervening && document.nextEntityIdHint() != firstExpectedPrimitiveId,
        "simulate another operation consuming the reviewed next entity ID");
    check(!review.confirmAndCreatePrimitive(api, adapter, document, projectPath, scenePath)
        && document.entities().size() == entityCountBeforePrimitiveReview + 1,
        "stale predicted entity ID cannot create an unreviewed primitive");

    check(review.preparePrimitiveCreation(adapter, document, projectPath, scenePath,
        primitiveProposal).hasValue(), "refresh primitive preview after Scene changed");
    const auto expectedPrimitiveId = document.nextEntityIdHint();
    const auto createdPrimitive = review.confirmAndCreatePrimitive(api, adapter, document, projectPath, scenePath);
    check(createdPrimitive && createdPrimitive.value() == expectedPrimitiveId
        && !review.hasPending(), "confirmed primitive creation returns the reviewed stable EntityId");
    const auto* primitiveEntity = createdPrimitive ? document.find(createdPrimitive.value()) : nullptr;
    const auto* primitiveComponent = createdPrimitive ? document.primitiveRenderer(createdPrimitive.value()) : nullptr;
    check(primitiveEntity && primitiveEntity->name == "AI Sphere Ω" && primitiveEntity->parent == entity.value()
        && primitiveComponent && primitiveComponent->primitive == scene::Primitive3D::Sphere && primitiveComponent->visible,
        "confirmed primitive has the reviewed name, parent, kind, and visibility");
    check(commands.undo().hasValue() && !document.find(expectedPrimitiveId),
        "primitive creation confirmation is recorded in transactional Undo history");
    check(commands.redo().hasValue() && document.primitiveRenderer(expectedPrimitiveId)
        && document.primitiveRenderer(expectedPrimitiveId)->primitive == scene::Primitive3D::Sphere,
        "primitive creation confirmation can be redone with stable identity");

    check(review.preparePrimitiveCreation(adapter, document, projectPath, scenePath,
        primitiveProposal).hasValue(), "prepare a primitive preview for cancellation");
    review.cancel();
    const auto entitiesAfterPrimitiveCancel = document.entities().size();
    check(!review.hasPending() && !review.confirmAndCreatePrimitive(api, adapter, document, projectPath, scenePath)
        && document.entities().size() == entitiesAfterPrimitiveCancel,
        "cancelling primitive review prevents entity creation");

    const scene::AddCamera3DPayload cameraProposal{entity.value()};
    check(!review.confirmAndAddCamera3D(api, adapter, document, projectPath, scenePath),
        "cannot add Camera3D without a pending review");
    check(review.prepareCamera3DAddition(adapter, document, projectPath, scenePath, cameraProposal).hasValue()
        && review.pendingCamera3D()
        && review.pendingCamera3D()->preview.sceneId == document.id()
        && review.pendingCamera3D()->preview.entityId == entity.value()
        && !document.camera3D(entity.value()),
        "Camera3D preview captures the target without changing Scene");
    check(!review.confirmAndAddCamera3D(api, adapter, document, "C:/Projects/Other", scenePath)
        && !document.camera3D(entity.value()),
        "Camera3D confirmation rejects a changed project identity");

    check(api.execute<scene::AddCamera3DPayload, void>("camera3d.add", 1, cameraProposal).hasValue()
        && document.camera3D(entity.value()), "simulate an intervening Camera3D component addition");
    check(!review.confirmAndAddCamera3D(api, adapter, document, projectPath, scenePath)
        && document.camera3D(entity.value()),
        "stale Camera3D preview rejects a target that acquired the component");
    check(commands.undo().hasValue() && !document.camera3D(entity.value()),
        "remove intervening Camera3D operation before refreshed review");

    check(review.prepareCamera3DAddition(adapter, document, projectPath, scenePath, cameraProposal).hasValue()
        && review.confirmAndAddCamera3D(api, adapter, document, projectPath, scenePath).hasValue()
        && !review.hasPending(), "confirm current Camera3D addition preview");
    const auto cameraComponentId = document.camera3D(entity.value())
        ? document.camera3D(entity.value())->id : 0;
    check(cameraComponentId != 0 && !document.camera3D(entity.value())->active,
        "confirmed Camera3D is created inactive with a stable component ID");
    check(commands.undo().hasValue() && !document.camera3D(entity.value()),
        "confirmed Camera3D addition participates in Undo");
    check(commands.redo().hasValue() && document.camera3D(entity.value())
        && document.camera3D(entity.value())->id == cameraComponentId,
        "confirmed Camera3D addition participates in Redo with stable identity");
    check(commands.undo().hasValue() && !document.camera3D(entity.value()),
        "restore the test Scene before Camera3D cancellation coverage");

    check(review.prepareCamera3DAddition(adapter, document, projectPath, scenePath, cameraProposal).hasValue(),
        "prepare Camera3D preview for cancellation");
    review.cancel();
    check(!review.hasPending() && !review.confirmAndAddCamera3D(api, adapter, document, projectPath, scenePath)
        && !document.camera3D(entity.value()), "cancel prevents Camera3D addition");

    check(commands.execute<scene::AddCamera3DPayload, void>("camera3d.add", 1, cameraProposal).hasValue()
        && commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1,
            {entity.value()}).hasValue(), "prepare an active Camera3D for lens review");
    if (!document.camera3D(entity.value())) return EXIT_FAILURE;
    const auto lensComponentId = document.camera3D(entity.value())->id;
    const scene::SetCamera3DLensPayload lensProposal{entity.value(), 82.5, 0.25, 900.0};
    check(!review.confirmAndSetCamera3DLens(api, adapter, document, projectPath, scenePath),
        "cannot apply a Camera3D lens without a pending preview");
    check(review.prepareCamera3DLensChange(adapter, document, projectPath, scenePath, lensProposal).hasValue()
        && review.pendingCamera3DLens() && review.pendingCamera3DLens()->preview.changesValue
        && review.pendingCamera3DLens()->preview.componentId == lensComponentId
        && review.pendingCamera3DLens()->preview.active
        && document.camera3D(entity.value())->verticalFovDegrees == 60.0
        && document.camera3D(entity.value())->nearPlane == 0.1
        && document.camera3D(entity.value())->farPlane == 1000.0,
        "Camera3D lens preview captures typed before/after values without mutating the active camera");
    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath),
        "generic confirmation cannot apply a Camera3D lens operation");
    check(!review.confirmAndSetCamera3DLens(api, adapter, document, projectPath,
            "C:/Projects/Astro/Scenes/Other.astro-scene")
        && document.camera3D(entity.value())->verticalFovDegrees == 60.0,
        "Camera3D lens confirmation rejects a changed scene identity");
    check(!review.confirmAndSetCamera3DLens(api, adapter, document, "C:/Projects/Other", scenePath)
        && document.camera3D(entity.value())->verticalFovDegrees == 60.0,
        "Camera3D lens confirmation rejects a changed project identity");

    const scene::SetCamera3DLensPayload interveningLens{entity.value(), 70.0, 0.15, 600.0};
    check(commands.execute<scene::SetCamera3DLensPayload, void>("camera3d.set_lens", 1,
            interveningLens).hasValue()
        && document.camera3D(entity.value())->active,
        "simulate another valid lens edit while keeping the camera active");
    check(!review.confirmAndSetCamera3DLens(api, adapter, document, projectPath, scenePath)
        && document.camera3D(entity.value())->verticalFovDegrees == 70.0
        && document.camera3D(entity.value())->active,
        "stale Camera3D lens review refuses to overwrite newer values");
    check(commands.undo().hasValue() && document.camera3D(entity.value())
        && document.camera3D(entity.value())->verticalFovDegrees == 60.0
        && document.camera3D(entity.value())->nearPlane == 0.1
        && document.camera3D(entity.value())->farPlane == 1000.0,
        "undo the intervening lens edit before refreshing the review");

    check(review.prepareCamera3DLensChange(adapter, document, projectPath, scenePath, lensProposal).hasValue()
        && review.confirmAndSetCamera3DLens(api, adapter, document, projectPath, scenePath).hasValue()
        && !review.hasPending(), "confirm the current Camera3D lens preview");
    const auto* reviewedLens = document.camera3D(entity.value());
    check(reviewedLens && reviewedLens->id == lensComponentId && reviewedLens->active
        && reviewedLens->verticalFovDegrees == 82.5 && reviewedLens->nearPlane == 0.25
        && reviewedLens->farPlane == 900.0,
        "confirmed lens update preserves active state and stable Camera3D component identity");
    check(commands.undo().hasValue() && document.camera3D(entity.value())
        && document.camera3D(entity.value())->id == lensComponentId
        && document.camera3D(entity.value())->active
        && document.camera3D(entity.value())->verticalFovDegrees == 60.0
        && document.camera3D(entity.value())->nearPlane == 0.1
        && document.camera3D(entity.value())->farPlane == 1000.0,
        "confirmed Camera3D lens update participates in Undo");
    check(commands.redo().hasValue() && document.camera3D(entity.value())
        && document.camera3D(entity.value())->id == lensComponentId
        && document.camera3D(entity.value())->active
        && document.camera3D(entity.value())->verticalFovDegrees == 82.5
        && document.camera3D(entity.value())->nearPlane == 0.25
        && document.camera3D(entity.value())->farPlane == 900.0,
        "confirmed Camera3D lens update participates in Redo");
    check(commands.undo().hasValue(), "restore original camera lens before no-op review");
    const scene::SetCamera3DLensPayload unchangedLens{entity.value(), 60.0, 0.1, 1000.0};
    check(review.prepareCamera3DLensChange(adapter, document, projectPath, scenePath, unchangedLens).hasValue()
        && review.pendingCamera3DLens() && !review.pendingCamera3DLens()->preview.changesValue
        && !review.confirmAndSetCamera3DLens(api, adapter, document, projectPath, scenePath),
        "unchanged Camera3D lens preview cannot add a no-op history entry");
    review.cancel();
    check(!review.hasPending() && document.camera3D(entity.value())->active
        && document.camera3D(entity.value())->verticalFovDegrees == 60.0,
        "cancelling a no-op lens review preserves Scene state");

    const auto secondCamera = document.createEntity("Target camera Ω");
    check(secondCamera && commands.execute<scene::AddCamera3DPayload, void>("camera3d.add", 1,
        {secondCamera.value()}).hasValue(), "create second camera for active-selection review");
    if (!secondCamera || !document.camera3D(secondCamera.value())) return EXIT_FAILURE;
    const auto secondCameraComponentId = document.camera3D(secondCamera.value())->id;
    const scene::SetActiveCamera3DPayload activateSecondCamera{secondCamera.value()};
    check(!review.confirmAndSetActiveCamera3D(api, adapter, document, projectPath, scenePath),
        "cannot switch active camera without a pending review");
    check(review.prepareCamera3DActiveSelectionChange(adapter, document, projectPath, scenePath,
            activateSecondCamera).hasValue()
        && review.pendingCamera3DActiveSelection()
        && review.pendingCamera3DActiveSelection()->preview.previousEntity == entity.value()
        && review.pendingCamera3DActiveSelection()->preview.targetEntity == secondCamera.value()
        && review.pendingCamera3DActiveSelection()->preview.previousComponentId == lensComponentId
        && review.pendingCamera3DActiveSelection()->preview.targetComponentId == secondCameraComponentId
        && review.pendingCamera3DActiveSelection()->preview.changesValue
        && document.activeCamera3D() == entity.value(),
        "active-camera preview records old and new camera identities without mutating Scene");
    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath),
        "generic change confirmation cannot apply an active-camera selection review");
    check(!review.confirmAndSetActiveCamera3D(api, adapter, document, projectPath,
            "C:/Projects/Astro/Scenes/Other.astro-scene")
        && document.activeCamera3D() == entity.value(),
        "active-camera confirmation rejects a changed scene path");
    check(!review.confirmAndSetActiveCamera3D(api, adapter, document, "C:/Projects/Other", scenePath)
        && document.activeCamera3D() == entity.value(),
        "active-camera confirmation rejects a changed project path");

    check(commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1,
        activateSecondCamera).hasValue() && document.activeCamera3D() == secondCamera.value(),
        "simulate an active-camera selection change after preview");
    check(!review.confirmAndSetActiveCamera3D(api, adapter, document, projectPath, scenePath)
        && document.activeCamera3D() == secondCamera.value(),
        "stale camera-switch preview does not overwrite a newer selection");
    check(commands.undo().hasValue() && document.activeCamera3D() == entity.value(),
        "undo intervening camera switch before refreshing preview");

    check(review.prepareCamera3DActiveSelectionChange(adapter, document, projectPath, scenePath,
            activateSecondCamera).hasValue()
        && review.confirmAndSetActiveCamera3D(api, adapter, document, projectPath, scenePath).hasValue()
        && !review.hasPending() && document.activeCamera3D() == secondCamera.value()
        && document.camera3D(secondCamera.value())->active && !document.camera3D(entity.value())->active,
        "confirming camera switch selects only the reviewed Camera3D");
    check(commands.undo().hasValue() && document.activeCamera3D() == entity.value()
        && commands.redo().hasValue() && document.activeCamera3D() == secondCamera.value(),
        "confirmed camera switch participates in Undo/Redo");
    check(commands.undo().hasValue() && document.activeCamera3D() == entity.value(),
        "restore source camera before explicit clear review");

    const scene::SetActiveCamera3DPayload clearActiveCamera{0};
    check(review.prepareCamera3DActiveSelectionChange(adapter, document, projectPath, scenePath,
            clearActiveCamera).hasValue()
        && review.pendingCamera3DActiveSelection()
        && review.pendingCamera3DActiveSelection()->preview.previousEntity == entity.value()
        && !review.pendingCamera3DActiveSelection()->preview.targetEntity
        && review.pendingCamera3DActiveSelection()->preview.changesValue
        && document.activeCamera3D() == entity.value(),
        "clear preview distinguishes an explicit no-active-camera target without mutating Scene");
    check(!review.confirmAndSetActiveCamera3D(api, adapter, document, "C:/Projects/Other", scenePath)
        && document.activeCamera3D() == entity.value(), "clear review rejects a changed project");
    check(review.confirmAndSetActiveCamera3D(api, adapter, document, projectPath, scenePath).hasValue()
        && !document.activeCamera3D() && !document.camera3D(entity.value())->active
        && !document.camera3D(secondCamera.value())->active,
        "confirmed clear leaves no active camera");
    check(commands.undo().hasValue() && document.activeCamera3D() == entity.value()
        && document.camera3D(entity.value())->active,
        "undo of reviewed clear restores the prior active camera");

    check(commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1,
        activateSecondCamera).hasValue(), "select second camera before stale-clear coverage");
    check(review.prepareCamera3DActiveSelectionChange(adapter, document, projectPath, scenePath,
            clearActiveCamera).hasValue(), "prepare explicit clear preview for stale-state coverage");
    check(commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1,
            {entity.value()}).hasValue()
        && !review.confirmAndSetActiveCamera3D(api, adapter, document, projectPath, scenePath)
        && document.activeCamera3D() == entity.value(),
        "stale clear preview refuses to clear a different active camera");
    check(commands.undo().hasValue() && document.activeCamera3D() == secondCamera.value(),
        "restore second camera after stale-clear test");
    check(review.prepareCamera3DActiveSelectionChange(adapter, document, projectPath, scenePath,
            activateSecondCamera).hasValue()
        && !review.pendingCamera3DActiveSelection()->preview.changesValue
        && !review.confirmAndSetActiveCamera3D(api, adapter, document, projectPath, scenePath),
        "previewing the already active camera cannot create a no-op transaction");
    review.cancel();
    check(!review.hasPending() && document.activeCamera3D() == secondCamera.value(),
        "cancelling an active-camera no-op leaves selection unchanged");

    const auto transformParent = document.createEntity("Transform parent Ω");
    check(transformParent && document.reparent(entity.value(), transformParent.value()),
        "prepare a parented entity for local Transform review");
    if (!transformParent) return EXIT_FAILURE;
    const auto* originalTransformComponent = document.transform(entity.value());
    if (!originalTransformComponent) return EXIT_FAILURE;
    const auto transformComponentId = originalTransformComponent->id;
    scene::Transform desiredTransform;
    desiredTransform.position = {2.0, 3.0, 4.0};
    desiredTransform.rotationDegrees = {10.0, 20.0, 30.0};
    desiredTransform.scale = {1.25, 1.5, 0.75};
    const scene::SetTransformPayload transformProposal{entity.value(), desiredTransform};
    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath),
        "cannot confirm a Transform without a pending preview");
    check(review.prepareTransformChange(adapter, document, projectPath, scenePath, transformProposal).hasValue()
        && review.pendingTransform() && review.pendingTransform()->preview.changesValue
        && review.pendingTransform()->preview.componentId == transformComponentId
        && review.pendingTransform()->preview.before == originalTransformComponent->value
        && review.pendingTransform()->preview.after == desiredTransform
        && review.pendingTransform()->preview.hierarchy.size() == 2
        && document.transform(entity.value())->value == originalTransformComponent->value,
        "Transform preview records local values and hierarchy without mutating Scene");
    check(!review.confirmAndApply(api, adapter, document, "C:/Projects/Other", scenePath)
        && document.transform(entity.value())->value == originalTransformComponent->value,
        "Transform confirmation rejects a changed project identity");

    scene::Transform changedParent;
    changedParent.position.x = 8.0;
    check(api.execute<scene::SetTransformPayload, void>("entity.set_transform", 1,
        {transformParent.value(), changedParent}).hasValue(),
        "simulate an ancestor Transform edit after preview");
    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath)
        && document.transform(entity.value())->value == originalTransformComponent->value,
        "stale parent-chain preview cannot apply a reviewed local Transform");
    check(commands.undo().hasValue(), "restore ancestor after stale Transform review test");

    check(review.prepareTransformChange(adapter, document, projectPath, scenePath, transformProposal).hasValue()
        && review.confirmAndApply(api, adapter, document, projectPath, scenePath).hasValue()
        && !review.hasPending() && document.transform(entity.value())
        && document.transform(entity.value())->id == transformComponentId
        && document.transform(entity.value())->value == desiredTransform,
        "confirmation applies the reviewed local Transform and preserves component identity");
    check(commands.undo().hasValue() && document.transform(entity.value())
        && document.transform(entity.value())->id == transformComponentId
        && document.transform(entity.value())->value == scene::Transform{}
        && commands.redo().hasValue() && document.transform(entity.value())
        && document.transform(entity.value())->id == transformComponentId
        && document.transform(entity.value())->value == desiredTransform,
        "reviewed local Transform participates in Undo/Redo with stable identity");
    check(review.prepareTransformChange(adapter, document, projectPath, scenePath,
            {entity.value(), desiredTransform}).hasValue()
        && review.pendingTransform() && !review.pendingTransform()->preview.changesValue
        && !review.confirmAndApply(api, adapter, document, projectPath, scenePath),
        "unchanged Transform preview cannot create a no-op history entry");
    review.cancel();
    check(!review.hasPending() && document.transform(entity.value())->value == desiredTransform,
        "cancelling Transform review leaves Scene unchanged");

    const auto destinationRoot = document.createEntity("Destination root Ω");
    const auto destinationParent = destinationRoot
        ? document.createEntity("Destination parent Ω", destinationRoot.value())
        : core::Result<scene::EntityId>::failure({core::ErrorCode::InvalidState, "Missing destination root"});
    const auto reparentTarget = document.createEntity("Reparent target Ω", transformParent.value());
    const auto reparentChild = reparentTarget
        ? document.createEntity("Reparent descendant Ω", reparentTarget.value())
        : core::Result<scene::EntityId>::failure({core::ErrorCode::InvalidState, "Missing reparent target"});
    if (!destinationRoot || !destinationParent || !reparentTarget || !reparentChild) return EXIT_FAILURE;
    scene::Transform reparentLocal;
    reparentLocal.position = {1.0, 2.0, 3.0};
    reparentLocal.rotationDegrees = {5.0, 15.0, 25.0};
    reparentLocal.scale = {2.0, 3.0, 4.0};
    check(document.setTransform(reparentTarget.value(), reparentLocal).hasValue(),
        "assign a non-default local Transform for reparent review");
    const auto reparentComponentId = document.transform(reparentTarget.value())->id;
    const scene::SetEntityParentPayload parentProposal{reparentTarget.value(), destinationParent.value()};
    check(!review.confirmAndReparent(api, adapter, document, projectPath, scenePath),
        "cannot confirm parent change without a pending review");
    check(review.prepareEntityParentChange(adapter, document, projectPath, scenePath, parentProposal).hasValue()
        && review.pendingEntityParent() && review.pendingEntityParent()->preview.changesValue
        && review.pendingEntityParent()->preview.beforeParent == transformParent.value()
        && review.pendingEntityParent()->preview.afterParent == destinationParent.value()
        && review.pendingEntityParent()->preview.transformComponentId == reparentComponentId
        && review.pendingEntityParent()->preview.localTransform == reparentLocal
        && review.pendingEntityParent()->preview.sourceHierarchy.size() == 2
        && review.pendingEntityParent()->preview.targetParentHierarchy.size() == 2
        && review.pendingEntityParent()->preview.affectedSubtree.size() == 2
        && document.find(reparentTarget.value())->parent == transformParent.value(),
        "parent preview captures source, destination and affected subtree without mutating Scene");
    check(!review.confirmAndApply(api, adapter, document, projectPath, scenePath),
        "generic property confirmation cannot apply a parent review");

    scene::Transform changedDestinationRoot;
    changedDestinationRoot.position.x = 8.0;
    check(api.execute<scene::SetTransformPayload, void>("entity.set_transform", 1,
        {destinationRoot.value(), changedDestinationRoot}).hasValue()
        && !review.confirmAndReparent(api, adapter, document, projectPath, scenePath)
        && document.find(reparentTarget.value())->parent == transformParent.value(),
        "stale destination ancestor invalidates hierarchy preview before apply");
    check(commands.undo().hasValue(), "restore destination root after stale hierarchy review");

    scene::Transform changedDescendant;
    changedDescendant.position.y = 6.0;
    check(api.execute<scene::SetTransformPayload, void>("entity.set_transform", 1,
        {reparentChild.value(), changedDescendant}).hasValue()
        && !review.confirmAndReparent(api, adapter, document, projectPath, scenePath)
        && document.find(reparentTarget.value())->parent == transformParent.value(),
        "stale affected descendant invalidates hierarchy preview before apply");
    check(commands.undo().hasValue(), "restore descendant after stale hierarchy review");

    check(review.prepareEntityParentChange(adapter, document, projectPath, scenePath, parentProposal).hasValue()
        && review.confirmAndReparent(api, adapter, document, projectPath, scenePath).hasValue()
        && !review.hasPending() && document.find(reparentTarget.value())->parent == destinationParent.value()
        && document.find(reparentChild.value())->parent == reparentTarget.value()
        && document.transform(reparentTarget.value())->id == reparentComponentId
        && document.transform(reparentTarget.value())->value == reparentLocal,
        "confirmation reparents the full subtree while preserving local Transform and stable component ID");
    check(commands.undo().hasValue() && document.find(reparentTarget.value())->parent == transformParent.value()
        && document.transform(reparentTarget.value())->value == reparentLocal
        && commands.redo().hasValue() && document.find(reparentTarget.value())->parent == destinationParent.value()
        && document.transform(reparentTarget.value())->value == reparentLocal,
        "reviewed parent change participates in Undo/Redo without changing local Transform");
    check(review.prepareEntityParentChange(adapter, document, projectPath, scenePath,
            {reparentTarget.value(), destinationParent.value()}).hasValue()
        && review.pendingEntityParent() && !review.pendingEntityParent()->preview.changesValue
        && !review.confirmAndReparent(api, adapter, document, projectPath, scenePath),
        "unchanged hierarchy preview cannot add a no-op history entry");
    review.cancel();
    check(!review.hasPending() && document.find(reparentTarget.value())->parent == destinationParent.value(),
        "cancel clears parent preview without changing Scene");

    if (failures == 0)
    {
        std::cout << "All Codex change review model tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
