#include "astro/scene/Scene.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

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
    astro::scene::Scene scene(1, "Основная сцена");
    check(scene.prefabInstances().empty(), "new scene starts without prefab instance metadata");
    astro::scene::Scene defaults(2, "Defaults");
    const auto defaultEntity = defaults.createEntity();
    check(defaultEntity && defaults.find(defaultEntity.value())->name == "Entity", "apply schema default on creation");
    check(defaultEntity && defaults.transform(defaultEntity.value()) != nullptr
        && defaults.transform(defaultEntity.value())->value.scale == astro::scene::Vec3{1.0, 1.0, 1.0},
        "attach a default Transform component with unit scale");
    check(defaultEntity && defaults.transform(defaultEntity.value())->id != 0
        && defaults.findTransform(defaults.transform(defaultEntity.value())->id) != nullptr,
        "assign a stable addressable component ID");
    const auto anotherEntity = defaults.createEntity("Second");
    check(anotherEntity && defaults.transform(anotherEntity.value())->id > defaults.transform(defaultEntity.value())->id,
        "component IDs are scene-unique and monotonically allocated");
    if (defaultEntity)
    {
        check(defaults.setDirectionalLight3D(defaultEntity.value()).hasValue(),
            "add DirectionalLight3D with documented defaults");
        const auto* light = defaults.directionalLight3D(defaultEntity.value());
        check(light && light->id != 0 && light->schemaVersion == 1 && light->enabled
            && light->color == std::array<float, 3>{1, 1, 1} && light->intensity == 1.0f,
            "DirectionalLight3D defaults to enabled linear white at unit intensity");
        check(!defaults.setDirectionalLight3D(defaultEntity.value(), {-0.1f, 1, 1}, 1)
            && !defaults.setDirectionalLight3D(defaultEntity.value(), {1, 1, 1}, 64.1f)
            && defaults.directionalLight3D(defaultEntity.value())->intensity == 1.0f,
            "reject out-of-range light values without mutating existing component");
        check(defaults.setDirectionalLight3D(defaultEntity.value(), {0.2f, 0.3f, 0.4f}, 4.0f, false).hasValue()
            && defaults.directionalLight3D(defaultEntity.value())->color[1] == 0.3f
            && !defaults.directionalLight3D(defaultEntity.value())->enabled,
            "update light properties while preserving its component identity");
        check(defaults.removeDirectionalLight3D(defaultEntity.value()).hasValue()
            && !defaults.directionalLight3D(defaultEntity.value()), "remove DirectionalLight3D");
        check(defaults.setPointLight3D(defaultEntity.value()).hasValue(),
            "add PointLight3D with documented defaults");
        const auto* point = defaults.pointLight3D(defaultEntity.value());
        check(point && point->id != 0 && point->schemaVersion == 1 && point->enabled
            && point->color == std::array<float, 3>{1, 1, 1} && point->intensity == 1.0f
            && point->range == 10.0f, "PointLight3D defaults to enabled white, unit intensity and 10-unit range");
        check(!defaults.setPointLight3D(defaultEntity.value(), {1, 0, 0}, 1.0f, 0.0f)
            && !defaults.setPointLight3D(defaultEntity.value(), {1, 0, 0}, 1.0f,
                std::numeric_limits<float>::infinity())
            && defaults.pointLight3D(defaultEntity.value())->range == 10.0f,
            "reject invalid point-light range without mutating existing values");
        check(defaults.setPointLight3D(defaultEntity.value(), {0.2f, 0.3f, 0.4f}, 2.5f, 25.0f, false).hasValue()
            && defaults.pointLight3D(defaultEntity.value())->color[2] == 0.4f
            && !defaults.pointLight3D(defaultEntity.value())->enabled,
            "update point-light authoring fields while preserving component identity");
        check(defaults.removePointLight3D(defaultEntity.value()).hasValue()
            && !defaults.pointLight3D(defaultEntity.value()), "remove PointLight3D");
    }
    if (anotherEntity)
    {
        const auto removedComponentId = defaults.transform(anotherEntity.value())->id;
        check(defaults.erase(anotherEntity.value()).hasValue(), "remove entity with its component");
        const auto afterRemoval = defaults.createEntity("Third");
        check(afterRemoval && defaults.transform(afterRemoval.value())->id > removedComponentId,
            "do not reuse removed component IDs");
    }
    if (defaultEntity)
    {
        const auto script = defaults.addScriptBehavior(defaultEntity.value(), {5001}, "Game.PlayerController");
        check(script && defaults.findScriptBehavior(script.value()) != nullptr,
            "attach ScriptBehavior with stable AssetId and ComponentId");
        check(script && defaults.scriptBehaviors(defaultEntity.value()).size() == 1,
            "enumerate persistent ScriptBehavior components on an entity");
        if (script)
        {
            check(defaults.updateScriptBehavior(defaultEntity.value(), script.value(), {5002}, "Game.PlayerControllerV2", false).hasValue(),
                "update script reference, type, and enabled state");
            const auto* updated = defaults.findScriptBehavior(script.value());
            check(updated && updated->id == script.value() && updated->scriptAsset.value == 5002
                && updated->managedTypeName == "Game.PlayerControllerV2" && !updated->enabled,
                "preserve ScriptBehavior ComponentId when updating its binding");
            check(!defaults.updateScriptBehavior(defaultEntity.value(), script.value(), {}, "Game.Broken", true),
                "reject a zero script AssetId without changing the component");
            check(updated && updated->scriptAsset.value == 5002, "invalid update preserves the previous binding");
            check(defaults.removeScriptBehavior(defaultEntity.value(), script.value()).hasValue(),
                "remove ScriptBehavior from its owner entity");
            const auto afterScriptRemoval = defaults.createEntity("After script removal");
            check(afterScriptRemoval && defaults.transform(afterScriptRemoval.value())->id > script.value(),
                "do not reuse a removed ScriptBehavior ComponentId");
        }
        check(!defaults.addScriptBehavior(defaultEntity.value(), {}, "Game.PlayerController"),
            "reject a zero script AssetId");
        check(!defaults.addScriptBehavior(defaultEntity.value(), {5003}, ""),
            "reject an empty managed type name");
        check(!defaults.addScriptBehavior(defaultEntity.value(), {5003}, "Game.Player Controller"),
            "reject whitespace in managed type names");
        check(!defaults.addScriptBehavior(999999, {5003}, "Game.PlayerController"),
            "reject a missing ScriptBehavior owner");
        check(!defaults.removeScriptBehavior(defaultEntity.value(), 999999),
            "reject removal of a missing ScriptBehavior component");
    }
    if (defaultEntity)
    {
        const auto originalTransformComponentId = defaults.transform(defaultEntity.value())->id;
        auto changed = defaults.transform(defaultEntity.value())->value;
        changed.position = {12.5, -4.0, 3.25};
        check(defaults.setTransform(defaultEntity.value(), changed).hasValue()
            && defaults.transform(defaultEntity.value())->id == originalTransformComponentId,
            "set a finite Transform without replacing its stable component ID");
        const auto validBeforeInvalidEdit = defaults.transform(defaultEntity.value())->value;
        changed.position.x = std::numeric_limits<double>::infinity();
        check(!defaults.setTransform(defaultEntity.value(), changed)
            && defaults.transform(defaultEntity.value())->value == validBeforeInvalidEdit,
            "reject non-finite Transform values without mutation");
    }
    const auto root = scene.createEntity("Игрок Ω");
    check(root.hasValue(), "create UTF-8 root entity");
    const auto child = scene.createEntity("Камера", root ? std::optional{root.value()} : std::nullopt);
    check(child.hasValue(), "create child entity");
    check(scene.entities().size() == 2 && scene.find(root ? root.value() : 0) != nullptr,
        "entities are indexed and retain insertion order");

    const auto other = scene.createEntity("Группа");
    check(other.hasValue(), "create second root");
    if (root && child && other)
    {
        check(scene.reparent(root.value(), other.value()).hasValue(), "reparent root under another entity");
        check(scene.find(root.value())->parent == other.value(), "parent link updated");
        check(!scene.reparent(other.value(), child.value()), "reject hierarchy cycle");
        check(!scene.reparent(child.value(), 999999), "reject missing parent");
        check(!scene.erase(other.value()), "reject non-recursive deletion with children");
        check(scene.entities().size() == 3, "failed operation leaves model unchanged");
        check(scene.erase(other.value(), true).hasValue(), "delete hierarchy recursively");
        check(scene.entities().empty(), "recursive delete removes descendants");
    }

    check(!scene.createEntity(" \t\r\n"), "reject blank entity names");
    check(!scene.createEntity(std::string(241, 'x')), "reject names beyond schema byte limit");
    check(!scene.createEntity(std::string("\xC0\xAF", 2)), "reject invalid UTF-8 entity names");
    check(!scene.erase(123456), "reject deletion of a missing entity");
    astro::scene::Scene primitiveScene(3, "Primitive deletion");
    const auto primitiveEntity = primitiveScene.createEntity("Cube");
    check(primitiveEntity && primitiveScene.setPrimitiveRenderer(primitiveEntity.value(),
        astro::scene::Primitive3D::Cube).hasValue(), "create a primitive entity for deletion coverage");
    check(primitiveEntity && primitiveScene.erase(primitiveEntity.value()).hasValue()
        && primitiveScene.entities().empty(), "delete a selected primitive entity");
    if (failures == 0)
    {
        std::cout << "All scene tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
