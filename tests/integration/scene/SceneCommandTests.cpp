#include "astro/scene/Scene.hpp"

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
    check(scene::entityPropertySchema().size() == 2, "schema exposes editable Name and read-only ID");
    const auto& nameSchema = scene::entityPropertySchema().front();
    check(nameSchema.id == "name" && nameSchema.editable && nameSchema.required
        && nameSchema.maxLengthBytes == 240 && nameSchema.defaultValue.has_value(),
        "Name schema declares default, requiredness and byte bound");
    auto registration = scene::registerSceneCommands(commands, document);
    check(registration.hasValue(), "register entity.set_property");
    if (!entity || !registration) return EXIT_FAILURE;

    const scene::SetScenePropertyPayload rename{entity.value(), "name", std::string{"Новое имя Ω"}};
    check(commands.validate("entity.set_property", 1, rename).hasValue(), "dry-run validates a valid property");
    check(document.find(entity.value())->name == "Старое имя Ω", "dry-run does not mutate the scene");
    check(commands.execute<scene::SetScenePropertyPayload, void>("entity.set_property", 1, rename).hasValue(),
        "execute applies a valid property");
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
    check(commands.auditTrail().size() >= 8, "validation and execution are audited");
    commands.clearHistory();
    check(!commands.canUndo() && !commands.canRedo(), "scene replacement can clear stale history");

    if (failures == 0)
    {
        std::cout << "All scene command integration tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
