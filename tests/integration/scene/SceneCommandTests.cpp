#include "astro/scene/Scene.hpp"
#include "astro/core/AIEditorApi.hpp"

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
    const auto apiVersion = localApi.version();
    check(apiVersion.major == 1 && apiVersion.minor == 0, "local AI Editor API publishes its version");
    const auto catalog = localApi.commandCatalog();
    check(catalog.size() == 6, "local API exposes the registered Scene commands");
    check(catalog.size() == 6 && catalog[0].schema.id == "entity.set_mesh_renderer"
        && catalog[1].schema.id == "entity.set_property"
        && catalog[2].schema.id == "entity.set_sprite_renderer"
        && catalog[3].schema.id == "script_behavior.add"
        && catalog[4].schema.id == "script_behavior.remove"
        && catalog[5].schema.id == "script_behavior.set_enabled", "command catalog order is stable");
    check(catalog.size() == 6 && catalog[1].schema.version == 1
        && catalog[1].transactional && catalog[1].schema.payloadFields.size() == 3
        && catalog[1].schema.payloadFields[2].id == "value"
        && catalog[1].schema.payloadFields[2].type == core::CommandFieldType::StringOrUnsignedInteger,
        "command catalog describes real payload fields and transactional behavior");

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

    const scene::SetSpriteRendererPayload sprite{entity.value(), {501}, true};
    check(commands.validate("entity.set_sprite_renderer", 1, sprite).hasValue()
        && !document.spriteRenderer(entity.value()), "sprite command dry-run leaves scene unchanged");
    check(commands.execute<scene::SetSpriteRendererPayload, void>("entity.set_sprite_renderer", 1, sprite).hasValue()
        && document.spriteRenderer(entity.value())->texture.value == 501, "sprite command assigns stable asset reference");
    check(commands.undo().hasValue() && !document.spriteRenderer(entity.value()), "undo removes newly added SpriteRenderer");
    check(commands.redo().hasValue() && document.spriteRenderer(entity.value())->texture.value == 501,
        "redo restores SpriteRenderer");

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
