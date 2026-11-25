#include "astro/scene/Scene.hpp"
#include "astro/core/AIEditorApi.hpp"
#include "astro/ai/CodexProjectAdapter.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
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
    core::EngineContext context;
    core::CommandRegistry commands(context);
    scene::Scene document(7, "Командная сцена");
    check(document.prefabInstances().empty(), "scene exposes an empty prefab instance set by default");
    auto entity = document.createEntity("Старое имя Ω");
    check(entity.hasValue(), "create test entity");
    if (entity)
    {
        const auto* transform = document.transform(entity.value());
        const scene::ScriptBehaviorComponent collision{transform ? transform->id : 0, 1, {700}, "Game.Collision", false};
        check(!document.restoreScriptBehavior(entity.value(), collision, 0),
            "restore rejects a component ID already owned by Transform");
        const scene::Camera3DComponent cameraCollision{transform ? transform->id : 0, 2, 60.0, 0.1, 1000.0, false};
        check(!document.restoreCamera3D(entity.value(), cameraCollision),
            "Camera3D restore rejects a component ID already owned by the same entity Transform");
    }
    check(scene::entityPropertySchema().size() == 2, "schema exposes editable Name and read-only ID");
    const auto& nameSchema = scene::entityPropertySchema().front();
    check(nameSchema.id == "name" && nameSchema.editable && nameSchema.required
        && nameSchema.maxLengthBytes == 240 && nameSchema.defaultValue.has_value(),
        "Name schema declares default, requiredness and byte bound");
    auto registration = scene::registerSceneCommands(commands, document);
    check(registration.hasValue(), "register entity.set_property");
    if (!entity || !registration) return EXIT_FAILURE;

    core::AIEditorApi localApi(commands);
    ai::CodexProjectAdapter codexAdapter(localApi);
    const auto apiVersion = localApi.version();
    check(apiVersion.major == 1 && apiVersion.minor == 0, "local AI Editor API publishes its version");
    const auto catalog = localApi.commandCatalog();
    check(catalog.size() == 25, "local API exposes the registered Scene commands");
    check(catalog.size() == 25 && catalog[0].schema.id == "camera3d.add"
        && catalog[1].schema.id == "camera3d.remove"
        && catalog[2].schema.id == "camera3d.set_active"
        && catalog[3].schema.id == "camera3d.set_lens"
        && catalog[4].schema.id == "entity.create_primitive"
        && catalog[5].schema.id == "entity.delete_subtree"
        && catalog[6].schema.id == "entity.reparent"
        && catalog[7].schema.id == "entity.reparent_many"
        && catalog[8].schema.id == "entity.set_mesh_material_override"
        && catalog[9].schema.id == "entity.set_mesh_renderer"
        && catalog[10].schema.id == "entity.set_primitive_material_override"
        && catalog[11].schema.id == "entity.set_primitive_renderer"
        && catalog[12].schema.id == "entity.set_property"
        && catalog[13].schema.id == "entity.set_sprite_renderer"
        && catalog[14].schema.id == "entity.set_transform"
        && catalog[15].schema.id == "entity.set_transforms"
        && catalog[16].schema.id == "light3d.add_directional"
        && catalog[17].schema.id == "light3d.add_point"
        && catalog[18].schema.id == "light3d.remove_directional"
        && catalog[19].schema.id == "light3d.remove_point"
        && catalog[20].schema.id == "light3d.set_directional"
        && catalog[21].schema.id == "light3d.set_point"
        && catalog[22].schema.id == "script_behavior.add"
        && catalog[23].schema.id == "script_behavior.remove"
        && catalog[24].schema.id == "script_behavior.set_enabled", "command catalog order is stable");
    check(catalog.size() == 25 && catalog[12].schema.version == 1
        && catalog[12].transactional && catalog[12].schema.payloadFields.size() == 3
        && catalog[12].schema.payloadFields[2].id == "value"
        && catalog[12].schema.payloadFields[2].type == core::CommandFieldType::StringOrUnsignedInteger,
        "command catalog describes real payload fields and transactional behavior");
    check(catalog.size() == 25 && catalog[3].schema.payloadFields.size() == 4
        && catalog[3].schema.payloadFields[1].type == core::CommandFieldType::Number,
        "Camera3D lens command exposes explicitly typed numeric parameters");
    check(catalog.size() == 25 && catalog[14].schema.id == "entity.set_transform"
        && catalog[14].schema.version == 1 && catalog[14].transactional
        && catalog[14].schema.payloadFields.size() == 10
        && catalog[14].schema.payloadFields[1].id == "position_x"
        && catalog[14].schema.payloadFields[1].type == core::CommandFieldType::Number
        && catalog[14].schema.payloadFields[9].id == "scale_z"
        && catalog[14].schema.payloadFields[9].type == core::CommandFieldType::Number,
        "local Transform command catalog describes all typed numeric fields and transaction support");
    check(catalog.size() == 25 && catalog[15].schema.version == 1 && catalog[15].transactional
        && catalog[15].schema.id == "entity.set_transforms"
        && catalog[15].schema.payloadFields.size() == 1
        && catalog[15].schema.payloadFields[0].id == "assignments"
        && catalog[15].schema.payloadFields[0].type == core::CommandFieldType::TransformAssignmentArray,
        "group Transform command publishes a structured assignment array and one transaction");
    check(catalog.size() == 25 && catalog[6].schema.version == 1 && catalog[6].transactional
        && catalog[6].schema.payloadFields.size() == 2
        && catalog[6].schema.payloadFields[0].id == "entity"
        && catalog[6].schema.payloadFields[1].id == "parent"
        && catalog[6].schema.payloadFields[1].type == core::CommandFieldType::UnsignedInteger,
        "entity.reparent publishes a typed parent field and transaction support");
    check(catalog.size() == 25 && catalog[7].schema.version == 1 && catalog[7].transactional
        && catalog[7].schema.payloadFields.size() == 2
        && catalog[7].schema.payloadFields[0].id == "entities"
        && catalog[7].schema.payloadFields[0].type == core::CommandFieldType::UnsignedIntegerArray
        && catalog[7].schema.payloadFields[1].id == "parent"
        && catalog[7].schema.payloadFields[1].type == core::CommandFieldType::UnsignedInteger,
        "entity.reparent_many publishes an exact unsigned EntityId array and one transaction");

    const auto contextSnapshot = codexAdapter.readContext("Astro Ω", document);
    check(contextSnapshot && contextSnapshot.value().schemaVersion == 1
        && contextSnapshot.value().projectLabel == "Astro Ω"
        && contextSnapshot.value().sceneId == document.id()
        && contextSnapshot.value().sceneName == document.name()
        && contextSnapshot.value().entities.size() == 1
        && contextSnapshot.value().entities.front().id == entity.value()
        && contextSnapshot.value().entities.front().name == "Старое имя Ω"
        && contextSnapshot.value().entities.front().transform.has_value()
        && contextSnapshot.value().entities.front().components.size() == 1
        && contextSnapshot.value().commands.size() == catalog.size(),
        "Codex adapter returns a deterministic read-only snapshot of the loaded scene and available commands");
    check(!codexAdapter.readContext("", document), "Codex adapter rejects an empty project label");
    check(!codexAdapter.readContext(std::string{"\xC0\xAF", 2}, document),
        "Codex adapter rejects malformed UTF-8 project labels");
    ai::CodexChangePlan codexPlan{1, "Rename the selected entity", {
        {"rename-1", "entity.set_property", 1, "Set the entity name to the requested value"}}};
    check(codexAdapter.validatePlan(codexPlan).hasValue(),
        "Codex adapter accepts a plan referencing an available command schema");
    auto invalidUtf8CodexPlan = codexPlan;
    invalidUtf8CodexPlan.summary = std::string{"\xC0\xAF", 2};
    check(!codexAdapter.validatePlan(invalidUtf8CodexPlan), "Codex adapter rejects malformed UTF-8 plan text");
    check(document.find(entity.value())->name == "Старое имя Ω",
        "reading context and validating a plan do not modify the scene");
    auto unknownCodexPlan = codexPlan;
    unknownCodexPlan.steps.front().commandId = "entity.execute_arbitrary_code";
    check(!codexAdapter.validatePlan(unknownCodexPlan), "Codex adapter rejects an unavailable command");
    auto staleCodexPlan = codexPlan;
    staleCodexPlan.steps.front().commandSchemaVersion = 999;
    check(!codexAdapter.validatePlan(staleCodexPlan), "Codex adapter rejects a stale command schema version");
    auto duplicateStepPlan = codexPlan;
    duplicateStepPlan.steps.push_back(duplicateStepPlan.steps.front());
    check(!codexAdapter.validatePlan(duplicateStepPlan), "Codex adapter rejects duplicate plan step IDs");

    const scene::SetScenePropertyPayload previewRename{entity.value(), "name", std::string{"Предпросмотр Ω"}};
    const auto propertyPreview = codexAdapter.previewSetProperty(document, 1, previewRename);
    check(propertyPreview && propertyPreview.value().sceneId == document.id()
        && propertyPreview.value().entityId == entity.value()
        && propertyPreview.value().propertyId == "name"
        && std::get_if<std::string>(&propertyPreview.value().before)
        && *std::get_if<std::string>(&propertyPreview.value().before) == "Старое имя Ω"
        && std::get_if<std::string>(&propertyPreview.value().after)
        && *std::get_if<std::string>(&propertyPreview.value().after) == "Предпросмотр Ω"
        && propertyPreview.value().changesValue,
        "Codex property preview returns typed before/after values for a valid edit");
    check(document.find(entity.value())->name == "Старое имя Ω",
        "Codex property preview does not change the Scene");
    const scene::SetScenePropertyPayload noOpPreview{entity.value(), "name", std::string{"Старое имя Ω"}};
    const auto noOp = codexAdapter.previewSetProperty(document, 1, noOpPreview);
    check(noOp && !noOp.value().changesValue, "Codex property preview identifies an unchanged value");
    check(!codexAdapter.previewSetProperty(document, 999, previewRename),
        "Codex property preview rejects a stale command schema version");
    check(!codexAdapter.previewSetProperty(document, 1,
        {entity.value(), "unknown", std::string{"value"}}), "Codex property preview rejects an unknown property");
    check(!codexAdapter.previewSetProperty(document, 1,
        {999999, "name", std::string{"value"}}), "Codex property preview rejects a missing entity");

    const scene::CreatePrimitiveEntityPayload createSphere{"Sphere Ω", std::nullopt,
        scene::Primitive3D::Sphere, true};
    check(localApi.validate("entity.create_primitive", 1, createSphere).hasValue()
        && document.entities().size() == 1, "primitive dry-run does not mutate the scene");
    const auto createdSphere = commands.execute<scene::CreatePrimitiveEntityPayload, scene::EntityId>(
        "entity.create_primitive", 1, createSphere);
    check(createdSphere && document.primitiveRenderer(createdSphere.value())
        && document.find(createdSphere.value())->name == "Sphere Ω", "command creates a complete primitive entity");
    const auto sphereEntityId = createdSphere ? createdSphere.value() : 0;
    const auto sphereComponentId = createdSphere ? document.primitiveRenderer(sphereEntityId)->id : 0;
    check(createdSphere && commands.undo().hasValue() && !document.find(sphereEntityId),
        "Undo removes the created primitive entity");
    check(createdSphere && commands.redo().hasValue() && document.primitiveRenderer(sphereEntityId)
        && document.primitiveRenderer(sphereEntityId)->id == sphereComponentId,
        "Redo restores primitive entity and stable entity/component IDs");
    auto sphereChild = document.createEntity("Sphere child", sphereEntityId);
    check(sphereChild.hasValue(), "create a child under the primitive for subtree deletion coverage");
    const auto deletedSphereComponent = document.primitiveRenderer(sphereEntityId)->id;
    check(commands.execute<scene::DeleteEntitySubtreePayload, void>("entity.delete_subtree", 1,
        {sphereEntityId}).hasValue() && !document.find(sphereEntityId) && !document.find(sphereChild.value()),
        "delete command removes a primitive and its descendants");
    check(commands.undo().hasValue() && document.primitiveRenderer(sphereEntityId)
        && document.primitiveRenderer(sphereEntityId)->id == deletedSphereComponent
        && document.find(sphereChild.value())->parent == sphereEntityId,
        "Undo restores the primitive subtree with stable IDs and parent links");
    check(commands.redo().hasValue() && !document.find(sphereEntityId) && !document.find(sphereChild.value()),
        "Redo deletes the restored primitive subtree");

    const scene::SetScenePropertyPayload rename{entity.value(), "name", std::string{"Новое имя Ω"}};
    check(localApi.validate("entity.set_property", 1, rename).hasValue(), "local API dry-run validates a valid property");
    check(document.find(entity.value())->name == "Старое имя Ω", "dry-run does not mutate the scene");
    check(localApi.execute<scene::SetScenePropertyPayload, void>("entity.set_property", 1, rename).hasValue(),
        "local API executes through the command registry");
    check(document.find(entity.value())->name == "Новое имя Ω", "command changes the entity through the schema");
    check(commands.canUndo(), "property command records an undo transaction");
    check(commands.undo().hasValue() && document.find(entity.value())->name == "Старое имя Ω",
        "undo restores the prior schema value");
    check(commands.redo().hasValue() && document.find(entity.value())->name == "Новое имя Ω",
        "redo reapplies the schema value");

    const auto originalTransform = document.transform(entity.value())->value;
    const auto originalTransformComponentId = document.transform(entity.value())->id;
    scene::SetTransformPayload transformEdit{entity.value(),
        {{3.5, -2.25, 8.0}, {15.0, 35.0, -10.0}, {1.5, 0.75, 2.0}}};
    check(localApi.validate("entity.set_transform", 1, transformEdit).hasValue()
        && document.transform(entity.value())->value == originalTransform,
        "local Transform dry-run validates without mutating the Scene");
    const scene::SetTransformPayload missingTransformEntity{999999, transformEdit.value};
    check(!localApi.validate("entity.set_transform", 1, missingTransformEntity)
        && document.transform(entity.value())->value == originalTransform,
        "local Transform dry-run rejects a missing entity without mutation");
    auto nonFiniteTransform = transformEdit;
    nonFiniteTransform.value.rotationDegrees.y = std::numeric_limits<double>::quiet_NaN();
    check(!localApi.validate("entity.set_transform", 1, nonFiniteTransform)
        && document.transform(entity.value())->value == originalTransform,
        "local Transform dry-run rejects non-finite values without mutation");
    check(localApi.execute<scene::SetTransformPayload, void>(
            "entity.set_transform", 1, transformEdit).hasValue()
        && document.transform(entity.value())->value == transformEdit.value
        && document.transform(entity.value())->id == originalTransformComponentId,
        "typed local Transform command updates every field and preserves component identity");
    check(commands.undo().hasValue() && document.transform(entity.value())->value == originalTransform
        && document.transform(entity.value())->id == originalTransformComponentId,
        "local Transform command Undo restores values without changing component identity");
    check(commands.redo().hasValue() && document.transform(entity.value())->value == transformEdit.value
        && document.transform(entity.value())->id == originalTransformComponentId,
        "local Transform command Redo reapplies values without changing component identity");

    const auto originalParent = document.createEntity("Original parent");
    const auto nextParent = document.createEntity("Next parent");
    const auto movedEntity = document.createEntity("Moved entity", originalParent.value());
    const auto movedChild = document.createEntity("Moved descendant", movedEntity.value());
    scene::Transform movedLocal;
    movedLocal.position = {1.0, 2.0, 3.0};
    movedLocal.rotationDegrees = {5.0, 15.0, 25.0};
    movedLocal.scale = {2.0, 3.0, 4.0};
    check(originalParent && nextParent && movedEntity && movedChild
        && document.setTransform(movedEntity.value(), movedLocal).hasValue(),
        "create hierarchy command fixture with a non-default local Transform");
    const auto movedComponentId = document.transform(movedEntity.value())->id;
    const scene::SetEntityParentPayload reparent{movedEntity.value(), nextParent.value()};
    check(localApi.validate("entity.reparent", 1, reparent).hasValue()
        && document.find(movedEntity.value())->parent == originalParent.value()
        && document.transform(movedEntity.value())->value == movedLocal,
        "reparent dry-run validates without changing parent or local Transform");
    check(!localApi.validate("entity.reparent", 1,
            scene::SetEntityParentPayload{movedEntity.value(), 999999})
        && !localApi.validate("entity.reparent", 1,
            scene::SetEntityParentPayload{movedEntity.value(), movedChild.value()}),
        "reparent dry-run rejects a missing parent and hierarchy cycle");
    check(localApi.execute<scene::SetEntityParentPayload, void>("entity.reparent", 1, reparent).hasValue()
        && document.find(movedEntity.value())->parent == nextParent.value()
        && document.find(movedChild.value())->parent == movedEntity.value()
        && document.transform(movedEntity.value())->value == movedLocal
        && document.transform(movedEntity.value())->id == movedComponentId,
        "reparent changes the hierarchy while retaining local Transform, descendants and component identity");
    check(commands.undo().hasValue() && document.find(movedEntity.value())->parent == originalParent.value()
        && document.transform(movedEntity.value())->value == movedLocal
        && commands.redo().hasValue() && document.find(movedEntity.value())->parent == nextParent.value()
        && document.transform(movedEntity.value())->value == movedLocal,
        "reparent participates in Undo/Redo and preserves local Transform");
    check(commands.undo().hasValue() && document.find(movedEntity.value())->parent == originalParent.value(),
        "restore hierarchy fixture after transactional reparent coverage");

    const auto originalParentB = document.createEntity("Second original parent");
    const auto movedEntityB = document.createEntity("Second moved entity", originalParentB.value());
    const auto movedChildB = document.createEntity("Second moved descendant", movedEntityB.value());
    scene::Transform movedLocalB;
    movedLocalB.position = {-4.0, 6.0, 8.0};
    check(originalParentB && movedEntityB && movedChildB
        && document.setTransform(movedEntityB.value(), movedLocalB).hasValue(),
        "create a second independent reparent root with a descendant");
    const auto movedComponentIdB = document.transform(movedEntityB.value())->id;
    const scene::SetEntityParentsPayload reparentMany{
        {movedChild.value(), movedEntity.value(), movedEntityB.value(), movedEntityB.value()}, nextParent.value()};
    check(localApi.validate("entity.reparent_many", 1, reparentMany).hasValue()
        && document.find(movedEntity.value())->parent == originalParent.value()
        && document.find(movedEntityB.value())->parent == originalParentB.value(),
        "group reparent dry-run normalizes selected ancestors/descendants and duplicates without mutation");
    check(!localApi.validate("entity.reparent_many", 1,
            scene::SetEntityParentsPayload{{movedEntity.value(), movedEntityB.value()}, movedChild.value()})
        && !localApi.validate("entity.reparent_many", 1,
            scene::SetEntityParentsPayload{{movedEntity.value(), 999999}, nextParent.value()})
        && !localApi.validate("entity.reparent_many", 1,
            scene::SetEntityParentsPayload{{}, nextParent.value()}),
        "group reparent rejects cycles, missing entities and an empty selection before mutation");
    const auto undoCountBeforeGroup = commands.undoCount();
    check(localApi.execute<scene::SetEntityParentsPayload, void>("entity.reparent_many", 1, reparentMany).hasValue()
        && commands.undoCount() == undoCountBeforeGroup + 1
        && document.find(movedEntity.value())->parent == nextParent.value()
        && document.find(movedEntityB.value())->parent == nextParent.value()
        && document.find(movedChild.value())->parent == movedEntity.value()
        && document.find(movedChildB.value())->parent == movedEntityB.value()
        && document.transform(movedEntity.value())->value == movedLocal
        && document.transform(movedEntityB.value())->value == movedLocalB
        && document.transform(movedEntityB.value())->id == movedComponentIdB,
        "group reparent moves normalized roots in one undo item and preserves local data and descendants");
    check(commands.undo().hasValue()
        && document.find(movedEntity.value())->parent == originalParent.value()
        && document.find(movedEntityB.value())->parent == originalParentB.value()
        && document.transform(movedEntity.value())->id == movedComponentId
        && document.transform(movedEntityB.value())->id == movedComponentIdB
        && commands.redo().hasValue()
        && document.find(movedEntity.value())->parent == nextParent.value()
        && document.find(movedEntityB.value())->parent == nextParent.value(),
        "one Undo restores every original parent and one Redo reapplies the group");
    const auto undoCountBeforeNoop = commands.undoCount();
    check(localApi.execute<scene::SetEntityParentsPayload, void>("entity.reparent_many", 1,
            scene::SetEntityParentsPayload{{movedEntity.value(), movedEntityB.value()}, nextParent.value()}).hasValue()
        && commands.undoCount() == undoCountBeforeNoop,
        "group reparent to the current parent is a no-op without an Undo entry");
    check(commands.execute<scene::SetEntityParentsPayload, void>("entity.reparent_many", 1,
            scene::SetEntityParentsPayload{{movedEntity.value(), movedEntityB.value()}, std::nullopt}).hasValue()
        && !document.find(movedEntity.value())->parent && !document.find(movedEntityB.value())->parent
        && commands.undo().hasValue()
        && document.find(movedEntity.value())->parent == nextParent.value()
        && document.find(movedEntityB.value())->parent == nextParent.value(),
        "group reparent supports Scene-root drop and atomic Undo");
    check(commands.undo().hasValue()
        && document.find(movedEntity.value())->parent == originalParent.value()
        && document.find(movedEntityB.value())->parent == originalParentB.value(),
        "restore the multi-reparent fixture after root-group coverage");

    auto editedLocal = movedLocal;
    editedLocal.position.x = 12.5;
    auto editedLocalB = movedLocalB;
    editedLocalB.position.x = 12.5;
    const auto transformAssignments = scene::SetEntityTransformsPayload{{
        {movedEntity.value(), editedLocal}, {movedEntityB.value(), editedLocalB}}};
    check(localApi.validate("entity.set_transforms", 1, transformAssignments).hasValue()
        && document.transform(movedEntity.value())->value == movedLocal
        && document.transform(movedEntityB.value())->value == movedLocalB,
        "group Transform dry-run validates the complete payload without changing either entity");
    auto invalidGroupTransform = editedLocalB;
    invalidGroupTransform.position.y = std::numeric_limits<double>::quiet_NaN();
    check(!localApi.validate("entity.set_transforms", 1, scene::SetEntityTransformsPayload{{
            {movedEntity.value(), editedLocal}, {movedEntityB.value(), invalidGroupTransform}}})
        && !localApi.validate("entity.set_transforms", 1, scene::SetEntityTransformsPayload{{
            {movedEntity.value(), editedLocal}, {movedEntity.value(), editedLocalB}}})
        && !localApi.validate("entity.set_transforms", 1,
            scene::SetEntityTransformsPayload{{{999999, editedLocal}}})
        && !localApi.validate("entity.set_transforms", 1, scene::SetEntityTransformsPayload{}),
        "group Transform rejects invalid values, duplicate/missing IDs, and an empty payload");
    check(document.transform(movedEntity.value())->value == movedLocal
        && document.transform(movedEntityB.value())->value == movedLocalB,
        "a rejected group Transform leaves every target unchanged");
    const auto transformIdA = document.transform(movedEntity.value())->id;
    const auto transformIdB = document.transform(movedEntityB.value())->id;
    const auto undoCountBeforeTransforms = commands.undoCount();
    check(localApi.execute<scene::SetEntityTransformsPayload, void>(
            "entity.set_transforms", 1, transformAssignments).hasValue()
        && commands.undoCount() == undoCountBeforeTransforms + 1
        && document.transform(movedEntity.value())->value == editedLocal
        && document.transform(movedEntityB.value())->value == editedLocalB
        && document.transform(movedEntity.value())->id == transformIdA
        && document.transform(movedEntityB.value())->id == transformIdB,
        "group Transform applies all targets in one Undo entry and retains component identity");
    check(commands.undo().hasValue()
        && document.transform(movedEntity.value())->value == movedLocal
        && document.transform(movedEntityB.value())->value == movedLocalB
        && commands.redo().hasValue()
        && document.transform(movedEntity.value())->value == editedLocal
        && document.transform(movedEntityB.value())->value == editedLocalB,
        "one Undo/Redo restores and reapplies the full group Transform");
    const auto undoCountBeforeTransformNoop = commands.undoCount();
    check(commands.execute<scene::SetEntityTransformsPayload, void>(
            "entity.set_transforms", 1, transformAssignments).hasValue()
        && commands.undoCount() == undoCountBeforeTransformNoop,
        "group Transform with unchanged values is a no-op without an Undo entry");
    check(commands.undo().hasValue()
        && document.transform(movedEntity.value())->value == movedLocal
        && document.transform(movedEntityB.value())->value == movedLocalB,
        "restore group Transform fixture after transaction coverage");

    const scene::SetSpriteRendererPayload sprite{entity.value(), {501}, true};
    check(commands.validate("entity.set_sprite_renderer", 1, sprite).hasValue()
        && !document.spriteRenderer(entity.value()), "sprite command dry-run leaves scene unchanged");
    check(commands.execute<scene::SetSpriteRendererPayload, void>("entity.set_sprite_renderer", 1, sprite).hasValue()
        && document.spriteRenderer(entity.value())->texture.value == 501, "sprite command assigns stable asset reference");
    check(commands.undo().hasValue() && !document.spriteRenderer(entity.value()), "undo removes newly added SpriteRenderer");
    check(commands.redo().hasValue() && document.spriteRenderer(entity.value())->texture.value == 501,
        "redo restores SpriteRenderer");

    const auto primitiveEntity = document.createEntity("Primitive target");
    const scene::SetPrimitiveRendererPayload cube{primitiveEntity.value(), scene::Primitive3D::Cube, true};
    check(primitiveEntity && commands.validate("entity.set_primitive_renderer", 1, cube).hasValue()
        && !document.primitiveRenderer(primitiveEntity.value()),
        "primitive command dry-run does not mutate the target");
    check(primitiveEntity && commands.execute<scene::SetPrimitiveRendererPayload, void>(
        "entity.set_primitive_renderer", 1, cube).hasValue(), "command adds a built-in primitive renderer");
    const auto primitiveComponentId = primitiveEntity
        ? document.primitiveRenderer(primitiveEntity.value())->id : 0;
    check(primitiveEntity && commands.undo().hasValue() && !document.primitiveRenderer(primitiveEntity.value()),
        "undo removes the added PrimitiveRenderer component");
    check(primitiveEntity && commands.redo().hasValue() && document.primitiveRenderer(primitiveEntity.value())
        && document.primitiveRenderer(primitiveEntity.value())->id == primitiveComponentId,
        "redo restores the stable PrimitiveRenderer component ID");
    const scene::SetPrimitiveRendererPayload sphereHidden{
        primitiveEntity.value(), scene::Primitive3D::Sphere, false};
    check(commands.execute<scene::SetPrimitiveRendererPayload, void>(
        "entity.set_primitive_renderer", 1, sphereHidden).hasValue()
        && document.primitiveRenderer(primitiveEntity.value())->primitive == scene::Primitive3D::Sphere
        && !document.primitiveRenderer(primitiveEntity.value())->visible,
        "typed command edits primitive shape and visibility together");
    check(commands.undo().hasValue() && document.primitiveRenderer(primitiveEntity.value())->id == primitiveComponentId
        && document.primitiveRenderer(primitiveEntity.value())->primitive == scene::Primitive3D::Cube
        && document.primitiveRenderer(primitiveEntity.value())->visible,
        "Undo restores the prior PrimitiveRenderer shape and visibility");
    check(commands.redo().hasValue() && document.primitiveRenderer(primitiveEntity.value())->id == primitiveComponentId
        && document.primitiveRenderer(primitiveEntity.value())->primitive == scene::Primitive3D::Sphere
        && !document.primitiveRenderer(primitiveEntity.value())->visible,
        "Redo reapplies primitive shape and visibility while preserving its component ID");
    const scene::SetPrimitiveMaterialOverridePayload primitiveMaterial{primitiveEntity.value(), {902}};
    check(commands.validate("entity.set_primitive_material_override", 1, primitiveMaterial).hasValue()
        && document.primitiveRenderer(primitiveEntity.value())->materialOverride.value == 0,
        "primitive material command validates without mutating the Scene");
    check(commands.execute<scene::SetPrimitiveMaterialOverridePayload, void>(
        "entity.set_primitive_material_override", 1, primitiveMaterial).hasValue()
        && document.primitiveRenderer(primitiveEntity.value())->materialOverride.value == 902,
        "typed command assigns a project material AssetId to a built-in primitive");
    check(commands.undo().hasValue()
        && document.primitiveRenderer(primitiveEntity.value())->materialOverride.value == 0
        && commands.redo().hasValue()
        && document.primitiveRenderer(primitiveEntity.value())->materialOverride.value == 902,
        "primitive material assignment supports undo and redo");
    const scene::SetPrimitiveMaterialOverridePayload invalidPrimitiveMaterial{
        primitiveEntity.value(), {std::numeric_limits<std::uint64_t>::max()}};
    check(!commands.validate("entity.set_primitive_material_override", 1, invalidPrimitiveMaterial),
        "reject the reserved maximum AssetId sentinel");

    const scene::SetMeshRendererPayload mesh{entity.value(), {801}, 2, 1, false};
    check(commands.validate("entity.set_mesh_renderer", 1, mesh).hasValue()
        && !document.meshRenderer(entity.value()), "mesh command dry-run does not mutate the scene");
    check(commands.execute<scene::SetMeshRendererPayload, void>("entity.set_mesh_renderer", 1, mesh).hasValue()
        && document.meshRenderer(entity.value())->model.value == 801
        && document.meshRenderer(entity.value())->meshIndex == 2
        && document.meshRenderer(entity.value())->primitiveIndex == 1
        && !document.meshRenderer(entity.value())->visible, "mesh command records model/primitive reference");
    const auto meshComponentId = document.meshRenderer(entity.value())->id;
    check(commands.undo().hasValue() && !document.meshRenderer(entity.value()),
        "undo removes a newly added MeshRenderer");
    check(commands.redo().hasValue() && document.meshRenderer(entity.value())
        && document.meshRenderer(entity.value())->id == meshComponentId,
        "redo restores the stable MeshRenderer component ID");

    const scene::SetMeshMaterialOverridePayload assignMaterial{entity.value(), {901}};
    check(commands.validate("entity.set_mesh_material_override", 1, assignMaterial).hasValue()
        && document.meshRenderer(entity.value())->materialOverride.value == 0,
        "material override dry-run validates without changing the Scene");
    check(commands.execute<scene::SetMeshMaterialOverridePayload, void>(
        "entity.set_mesh_material_override", 1, assignMaterial).hasValue()
        && document.meshRenderer(entity.value())->materialOverride.value == 901
        && document.meshRenderer(entity.value())->schemaVersion == 2,
        "typed command assigns a project material to the selected primitive slot");
    check(commands.undo().hasValue() && document.meshRenderer(entity.value())->materialOverride.value == 0,
        "undo clears a newly assigned material override");
    check(commands.redo().hasValue() && document.meshRenderer(entity.value())->materialOverride.value == 901,
        "redo reapplies the material override");
    const scene::SetMeshMaterialOverridePayload clearMaterial{entity.value(), {}};
    check(commands.execute<scene::SetMeshMaterialOverridePayload, void>(
        "entity.set_mesh_material_override", 1, clearMaterial).hasValue()
        && document.meshRenderer(entity.value())->materialOverride.value == 0,
        "zero AssetId clears the project override to reveal the source material");
    check(commands.undo().hasValue() && document.meshRenderer(entity.value())->materialOverride.value == 901,
        "undo of clear restores the assigned project material");

    const scene::AddCamera3DPayload addCamera{entity.value()};
    check(commands.validate("camera3d.add", 1, addCamera).hasValue() && !document.camera3D(entity.value()),
        "camera add dry-run validates without changing the Scene");
    check(commands.execute<scene::AddCamera3DPayload, void>("camera3d.add", 1, addCamera).hasValue()
        && document.camera3D(entity.value()) && !document.camera3D(entity.value())->active,
        "typed command adds an inactive perspective Camera3D");
    const auto cameraComponentId = document.camera3D(entity.value())->id;
    check(commands.undo().hasValue() && !document.camera3D(entity.value()), "undo removes a newly added Camera3D");
    check(commands.redo().hasValue() && document.camera3D(entity.value())
        && document.camera3D(entity.value())->id == cameraComponentId,
        "redo restores the stable Camera3D component ID");
    check(!commands.validate("camera3d.add", 1, addCamera), "camera add rejects duplicates");

    const scene::SetCamera3DLensPayload invalidCameraLens{entity.value(), 180.0, 0.1, 1000.0};
    check(!commands.validate("camera3d.set_lens", 1, invalidCameraLens)
        && document.camera3D(entity.value())->verticalFovDegrees == 60.0,
        "camera lens dry-run rejects invalid values without mutating the Scene");
    const scene::SetCamera3DLensPayload cameraLens{entity.value(), 82.5, 0.25, 750.0};
    check(commands.execute<scene::SetCamera3DLensPayload, void>("camera3d.set_lens", 1, cameraLens).hasValue()
        && document.camera3D(entity.value())->verticalFovDegrees == 82.5,
        "typed command applies a validated Camera3D lens");
    check(commands.undo().hasValue() && document.camera3D(entity.value())->verticalFovDegrees == 60.0
        && commands.redo().hasValue() && document.camera3D(entity.value())->id == cameraComponentId
        && document.camera3D(entity.value())->verticalFovDegrees == 82.5,
        "camera lens Undo/Redo preserves the component ID and values");

    auto secondCameraEntity = document.createEntity("Second camera");
    check(secondCameraEntity && commands.execute<scene::AddCamera3DPayload, void>("camera3d.add", 1,
        {secondCameraEntity.value()}).hasValue(), "add a second camera for active-selection tests");
    auto invalidActiveEntities = document.entities();
    for (auto& invalidEntity : invalidActiveEntities)
        if (invalidEntity.camera3d) invalidEntity.camera3d->active = true;
    check(!scene::restoreScene(document.id(), document.name(), std::move(invalidActiveEntities)),
        "Scene validation rejects multiple active Camera3D components");
    check(commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1,
        {entity.value()}).hasValue() && document.activeCamera3D() == entity.value(),
        "select the first camera as active through its typed command");
    check(commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1,
        {secondCameraEntity.value()}).hasValue() && document.activeCamera3D() == secondCameraEntity.value()
        && !document.camera3D(entity.value())->active,
        "selecting another camera atomically leaves only one active");
    check(commands.undo().hasValue() && document.activeCamera3D() == entity.value()
        && commands.redo().hasValue() && document.activeCamera3D() == secondCameraEntity.value(),
        "active-camera switch Undo/Redo restores the previous selection");
    check(commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1, {0}).hasValue()
        && !document.activeCamera3D() && commands.undo().hasValue()
        && document.activeCamera3D() == secondCameraEntity.value(),
        "clearing active camera is undoable");
    check(commands.execute<scene::SetActiveCamera3DPayload, void>("camera3d.set_active", 1,
        {secondCameraEntity.value()}).hasValue(), "reactivate second camera before remove test");
    const auto secondCameraComponentId = document.camera3D(secondCameraEntity.value())->id;
    check(commands.execute<scene::RemoveCamera3DPayload, void>("camera3d.remove", 1,
        {secondCameraEntity.value()}).hasValue() && !document.camera3D(secondCameraEntity.value()),
        "typed command removes a camera");
    check(commands.undo().hasValue() && document.camera3D(secondCameraEntity.value())
        && document.camera3D(secondCameraEntity.value())->id == secondCameraComponentId
        && document.activeCamera3D() == secondCameraEntity.value(),
        "remove Undo restores stable ID and previous active state");

    const scene::AddDirectionalLight3DPayload addLight{entity.value()};
    check(commands.validate("light3d.add_directional", 1, addLight).hasValue()
        && !document.directionalLight3D(entity.value()), "directional light dry-run has no side effects");
    check(commands.execute<scene::AddDirectionalLight3DPayload, void>("light3d.add_directional", 1, addLight).hasValue()
        && document.directionalLight3D(entity.value()), "typed command adds DirectionalLight3D");
    const auto lightId = document.directionalLight3D(entity.value())->id;
    const scene::SetDirectionalLight3DPayload invalidLight{entity.value(), {-1, 1, 1}, 1, true};
    check(!commands.validate("light3d.set_directional", 1, invalidLight)
        && document.directionalLight3D(entity.value())->intensity == 1.0f,
        "directional light command rejects invalid RGB without mutation");
    const scene::SetDirectionalLight3DPayload setLight{entity.value(), {0.1f, 0.4f, 0.8f}, 3.5f, false};
    check(commands.execute<scene::SetDirectionalLight3DPayload, void>("light3d.set_directional", 1, setLight).hasValue()
        && document.directionalLight3D(entity.value())->color[2] == 0.8f
        && !document.directionalLight3D(entity.value())->enabled,
        "typed command updates light color, intensity, and enabled state");
    check(commands.undo().hasValue() && document.directionalLight3D(entity.value())->intensity == 1.0f
        && commands.redo().hasValue() && document.directionalLight3D(entity.value())->id == lightId
        && document.directionalLight3D(entity.value())->intensity == 3.5f,
        "light update Undo/Redo retains stable ComponentId");
    check(commands.execute<scene::RemoveDirectionalLight3DPayload, void>("light3d.remove_directional", 1,
        {entity.value()}).hasValue() && !document.directionalLight3D(entity.value())
        && commands.undo().hasValue() && document.directionalLight3D(entity.value())->id == lightId,
        "light removal Undo restores exact component identity and values");

    const scene::AddPointLight3DPayload addPointLight{entity.value()};
    check(commands.validate("light3d.add_point", 1, addPointLight).hasValue()
        && !document.pointLight3D(entity.value()), "point-light dry-run has no side effects");
    check(commands.execute<scene::AddPointLight3DPayload, void>("light3d.add_point", 1, addPointLight).hasValue()
        && document.pointLight3D(entity.value()), "typed command adds PointLight3D");
    const auto pointLightId = document.pointLight3D(entity.value())->id;
    const scene::SetPointLight3DPayload invalidPoint{entity.value(), {1, 1, 1}, 1.0f, 0.0f, true};
    check(!commands.validate("light3d.set_point", 1, invalidPoint)
        && document.pointLight3D(entity.value())->range == 10.0f,
        "point-light command rejects invalid range without mutation");
    const scene::SetPointLight3DPayload setPoint{entity.value(), {0.2f, 0.6f, 1.0f}, 4.0f, 32.0f, false};
    check(commands.execute<scene::SetPointLight3DPayload, void>("light3d.set_point", 1, setPoint).hasValue()
        && document.pointLight3D(entity.value())->range == 32.0f
        && !document.pointLight3D(entity.value())->enabled,
        "typed command updates point-light fields");
    check(commands.undo().hasValue() && document.pointLight3D(entity.value())->range == 10.0f
        && commands.redo().hasValue() && document.pointLight3D(entity.value())->id == pointLightId
        && document.pointLight3D(entity.value())->intensity == 4.0f,
        "point-light update Undo/Redo retains component identity");
    check(commands.execute<scene::RemovePointLight3DPayload, void>("light3d.remove_point", 1,
        {entity.value()}).hasValue() && !document.pointLight3D(entity.value())
        && commands.undo().hasValue() && document.pointLight3D(entity.value())->id == pointLightId,
        "point-light removal Undo restores identity and values");

    const scene::AddScriptBehaviorPayload addScript{entity.value(), {701}, "Игры.ActorΩ"};
    check(commands.validate("script_behavior.add", 1, addScript).hasValue()
        && document.scriptBehaviors(entity.value()).empty(), "script add validation is read-only");
    const auto firstScript = commands.execute<scene::AddScriptBehaviorPayload, void>(
        "script_behavior.add", 1, addScript);
    const scene::AddScriptBehaviorPayload addSecondScript{entity.value(), {702}, "Game.CameraFollow"};
    const auto secondScript = commands.execute<scene::AddScriptBehaviorPayload, void>(
        "script_behavior.add", 1, addSecondScript);
    const scene::AddScriptBehaviorPayload addThirdScript{entity.value(), {703}, "Game.QuestMarker"};
    const auto thirdScript = commands.execute<scene::AddScriptBehaviorPayload, void>(
        "script_behavior.add", 1, addThirdScript);
    check(firstScript && secondScript && thirdScript, "typed command adds multiple C# bindings");
    if (firstScript && secondScript && thirdScript)
    {
        auto behaviors = document.scriptBehaviors(entity.value());
        const auto firstId = behaviors.size() > 0 ? behaviors[0].id : 0;
        const auto secondId = behaviors.size() > 1 ? behaviors[1].id : 0;
        const auto thirdId = behaviors.size() > 2 ? behaviors[2].id : 0;
        check(behaviors.size() == 3
            && !behaviors[0].enabled && behaviors[0].managedTypeName == "Игры.ActorΩ",
            "new script bindings start disabled and preserve insertion order and UTF-8 type names");
        check(commands.undo().hasValue() && document.scriptBehaviors(entity.value()).size() == 2,
            "undo removes the last added ScriptBehavior");
        check(commands.redo().hasValue(), "redo restores the last added ScriptBehavior");
        behaviors = document.scriptBehaviors(entity.value());
        check(behaviors.size() == 3 && behaviors[2].id == thirdId,
            "add redo preserves the original component ID and insertion position");

        const scene::SetScriptBehaviorEnabledPayload enableSecond{
            entity.value(), secondId, true};
        check(commands.execute<scene::SetScriptBehaviorEnabledPayload, void>(
            "script_behavior.set_enabled", 1, enableSecond)
            && document.scriptBehaviors(entity.value())[1].enabled, "script enabled state is changed through a typed command");
        check(commands.undo().hasValue() && !document.scriptBehaviors(entity.value())[1].enabled,
            "undo restores the previous enabled state");
        check(commands.redo().hasValue() && document.scriptBehaviors(entity.value())[1].enabled,
            "redo restores the new enabled state");

        const scene::RemoveScriptBehaviorPayload removeFirst{entity.value(), firstId};
        check(commands.execute<scene::RemoveScriptBehaviorPayload, void>(
            "script_behavior.remove", 1, removeFirst)
            && document.scriptBehaviors(entity.value()).size() == 2,
            "typed command removes a script binding");
        check(commands.undo().hasValue(), "undo restores a removed script binding");
        behaviors = document.scriptBehaviors(entity.value());
        check(behaviors.size() == 3 && behaviors[0].id == firstId
            && behaviors[1].id == secondId && behaviors[2].id == thirdId,
            "remove undo preserves stable component ID and exact execution order");
        check(commands.redo().hasValue() && commands.undo().hasValue(),
            "remove redo and undo remain repeatable");
    }
    check(!commands.validate("script_behavior.add", 1,
        scene::AddScriptBehaviorPayload{entity.value(), {}, "Game.Invalid"}),
        "script add rejects a zero AssetId");
    check(!commands.validate("script_behavior.add", 1,
        scene::AddScriptBehaviorPayload{entity.value(), {704}, "Invalid Type"}),
        "script add rejects malformed managed type names");
    check(!commands.validate("script_behavior.set_enabled", 1,
        scene::SetScriptBehaviorEnabledPayload{entity.value(), 999999, true}),
        "enabled command rejects a missing component");

    const scene::SetScenePropertyPayload blankName{entity.value(), "name", std::string{" \t"}};
    check(!commands.validate("entity.set_property", 1, blankName), "schema rejects blank property values");
    const scene::SetScenePropertyPayload invalidUtf8{entity.value(), "name", std::string{"\xC0\xAF", 2}};
    check(!commands.validate("entity.set_property", 1, invalidUtf8), "schema rejects invalid UTF-8");
    const scene::SetScenePropertyPayload wrongType{entity.value(), "name", scene::EntityId{22}};
    check(!commands.validate("entity.set_property", 1, wrongType), "schema rejects a value of the wrong type");
    const scene::SetScenePropertyPayload unknownProperty{entity.value(), "missing", std::string{"value"}};
    check(!commands.validate("entity.set_property", 1, unknownProperty), "schema rejects unknown properties");
    const scene::SetScenePropertyPayload readOnlyId{entity.value(), "id", scene::EntityId{22}};
    check(!commands.validate("entity.set_property", 1, readOnlyId), "schema rejects writes to read-only ID");
    const scene::SetScenePropertyPayload tooLong{entity.value(), "name", std::string(241, 'x')};
    check(!commands.execute<scene::SetScenePropertyPayload, void>("entity.set_property", 1, tooLong),
        "execute rejects values above the schema byte limit");
    check(document.find(entity.value())->name == "Новое имя Ω", "rejected edits leave the prior value intact");
    check(!commands.execute<scene::SetScenePropertyPayload, void>("entity.set_property", 2, rename),
        "unknown schema version is rejected");
    check(commands.auditTrail().size() >= 20, "validation, execution, undo and redo are audited");
    commands.clearHistory();
    check(!commands.canUndo() && !commands.canRedo(), "scene replacement can clear stale history");

    if (failures == 0)
    {
        std::cout << "All scene command integration tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
