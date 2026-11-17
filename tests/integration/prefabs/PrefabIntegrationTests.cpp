#include "astro/prefabs/PrefabLibrary.hpp"
#include "astro/scene/SceneFile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void check(bool condition, const char* text)
{
    if (!condition) { std::cerr << "FAIL: " << text << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("Astro Prefab Tests " + std::to_string(unique));
    std::error_code ignored;
    if (!std::filesystem::create_directory(root)) return EXIT_FAILURE;
    auto opened = prefabs::PrefabLibrary::open(root);
    check(opened.hasValue(), "open project prefab library");
    if (!opened) { std::filesystem::remove_all(root, ignored); return EXIT_FAILURE; }
    auto library = opened.value();
    const auto blockedRoot = root / "blocked-project";
    std::filesystem::create_directory(blockedRoot);
    { std::ofstream blockedPath(blockedRoot / "Prefabs"); blockedPath << "preserve"; }
    check(!prefabs::PrefabLibrary::open(blockedRoot), "reject a non-directory Prefabs path");
    const auto linkRoot = root / "linked-project";
    const auto linkTarget = root / "outside-prefabs";
    std::filesystem::create_directory(linkRoot);
    std::filesystem::create_directory(linkTarget);
    std::error_code linkError;
    std::filesystem::create_directory_symlink(linkTarget, linkRoot / "Prefabs", linkError);
    if (!linkError)
        check(!prefabs::PrefabLibrary::open(linkRoot), "reject Prefabs symlink escaping the project root");

    prefabs::PrefabAsset nested{20, "nested", {
        {1, 0, "Nested Root", {}, std::nullopt, 0},
        {2, 1, "Nested Child", {{3, 0, 0}, {}, {1, 1, 1}}, std::nullopt, 0}}};
    prefabs::PrefabAsset parent{10, "parent", {
        {1, 0, "Base Root", {}, std::nullopt, 20},
        {2, 1, "Base Child", {}, std::nullopt, 0}}};
    check(library.save(nested).hasValue() && library.save(parent).hasValue(), "save nested prefab assets");
    auto roundTrip = library.load(10);
    check(roundTrip && roundTrip.value().nodes.size() == 2 && roundTrip.value().nodes[0].nestedPrefab == 20,
        "round-trip nested reference and local hierarchy");

    scene::Scene document(1, "Prefab Scene");
    scene::Scene scriptScene(2, "Script Prefab Boundary");
    auto scriptedEntity = scriptScene.createEntity("Scripted source");
    auto scriptBinding = scriptedEntity
        ? scriptScene.addScriptBehavior(scriptedEntity.value(), {501}, "Game.PlayerController")
        : core::Result<scene::ComponentId>::failure({core::ErrorCode::InternalError, "script fixture entity missing"});
    check(scriptBinding.hasValue(), "create ScriptBehavior prefab boundary fixture");
    check(scriptedEntity && !prefabs::capturePrefab(scriptScene, scriptedEntity.value(), 501, "Scripted Prefab"),
        "reject prefab capture instead of silently dropping ScriptBehavior");
    scene::Scene meshScene(3, "Mesh Prefab Boundary");
    auto meshEntity = meshScene.createEntity("Mesh source");
    check(meshEntity && meshScene.setMeshRenderer(meshEntity.value(), {502}),
        "create MeshRenderer prefab boundary fixture");
    check(meshEntity && !prefabs::capturePrefab(meshScene, meshEntity.value(), 502, "Mesh Prefab"),
        "reject prefab capture instead of silently dropping MeshRenderer");

    auto externalParent = document.createEntity("Container");
    auto instance = prefabs::instantiatePrefab(library, document, 10, externalParent.value());
    check(instance.hasValue(), "instantiate parent prefab with nested prefab");
    if (!instance) { std::filesystem::remove_all(root, ignored); return EXIT_FAILURE; }
    auto* metadata = document.prefabInstance(instance.value());
    check(metadata && metadata->sourceToEntity.size() == 4, "record flattened source-to-instance mapping");
    const auto originalRoot = instance.value();
    const auto baseChildKey = std::string("p10/n2");
    const auto baseChild = metadata ? metadata->sourceToEntity.at(baseChildKey) : 0;
    check(prefabs::setPrefabOverride(document, originalRoot, baseChildKey,
        scene::PrefabOverrideProperty::Name, std::string("Local Child Name Ω")).hasValue(),
        "apply explicit instance name override");
    check(!prefabs::setPrefabOverride(document, originalRoot, baseChildKey,
        scene::PrefabOverrideProperty::Name, scene::Transform{}),
        "reject a prefab override with the wrong variant type");

    nested.nodes[1].name = "Nested Child Updated";
    nested.nodes[1].transform.position.x = 42.0;
    parent.nodes[0].name = "Base Root Updated";
    parent.nodes[1].name = "Base Child Updated";
    parent.nodes[1].transform.position.y = 12.0;
    check(library.save(nested).hasValue() && library.save(parent).hasValue(), "save changed prefab sources");
    check(prefabs::updatePrefabInstance(library, document, originalRoot).hasValue(), "update live prefab instance");
    metadata = document.prefabInstance(originalRoot);
    check(metadata && metadata->sourceToEntity.at(baseChildKey) == baseChild,
        "retain mapped entity ID during update");
    check(document.find(originalRoot)->name == "Base Root Updated", "apply new non-overridden base value");
    check(document.find(baseChild)->name == "Local Child Name Ω", "preserve local override over prefab update");
    const auto nestedChildKey = std::string("p10/n1/nested/p20/n2");
    check(metadata && document.transform(metadata->sourceToEntity.at(nestedChildKey))->value.position.x == 42.0,
        "update nested prefab content in existing instance");

    const auto sceneFile = root / "instance.astro-scene";
    check(scene::saveSceneFile(sceneFile, document).hasValue(), "save Scene v5 instance metadata");
    auto loadedScene = scene::loadSceneFile(sceneFile);
    check(loadedScene && loadedScene.value().prefabInstance(originalRoot)
        && loadedScene.value().prefabInstance(originalRoot)->overrides.size() == 1,
        "round-trip prefab identity, mapping and overrides");
    if (loadedScene)
    {
        const auto beforeCount = loadedScene.value().entities().size();
        parent.nodes[1].name = "Changed again";
        check(library.save(parent).hasValue()
            && prefabs::updatePrefabInstance(library, loadedScene.value(), originalRoot).hasValue(),
            "update reloaded instance");
        check(loadedScene.value().entities().size() == beforeCount
            && loadedScene.value().find(baseChild)->name == "Local Child Name Ω",
            "reloaded overrides remain stable through another update");
    }

    const auto parentBeforeConflict = parent;
    std::erase_if(parent.nodes, [](const auto& node) { return node.localId == 2; });
    check(library.save(parent).hasValue(), "write prefab revision that removes overridden source node");
    const auto beforeConflictCount = document.entities().size();
    check(!prefabs::updatePrefabInstance(library, document, originalRoot),
        "reject an update that would orphan a local override");
    check(document.entities().size() == beforeConflictCount
        && document.find(baseChild)->name == "Local Child Name Ω",
        "orphaned override conflict leaves the scene unchanged");
    parent = parentBeforeConflict;
    check(library.save(parent).hasValue(), "restore source prefab after conflict test");

    scene::Scene commandScene(2, "Command Scene");
    core::EngineContext context;
    core::CommandRegistry commands(context);
    check(prefabs::registerPrefabCommands(commands, library, commandScene).hasValue(), "register prefab commands");
    const prefabs::InstantiatePrefabPayload createPayload{10, std::nullopt};
    auto commandInstance = commands.execute<prefabs::InstantiatePrefabPayload, scene::EntityId>(
        "prefab.instantiate", 1, createPayload);
    check(commandInstance && commandScene.prefabInstance(commandInstance.value()), "instantiate through typed command API");
    const auto commandRoot = commandInstance ? commandInstance.value() : 0;
    check(commands.undo().hasValue() && commandScene.entities().empty(), "undo prefab instantiation atomically");
    check(commands.redo().hasValue() && commandScene.prefabInstance(commandRoot), "redo prefab instantiation");
    const prefabs::SetPrefabOverridePayload commandOverride{
        commandRoot, baseChildKey, scene::PrefabOverrideProperty::Name, std::string("Command Override")};
    check(commands.execute<prefabs::SetPrefabOverridePayload, void>("prefab.set_override", 1, commandOverride).hasValue()
        && commandScene.find(commandScene.prefabInstance(commandRoot)->sourceToEntity.at(baseChildKey))->name == "Command Override",
        "set prefab override through typed command API");
    auto undoOverride = commands.undo();
    const auto baseChildAfterUndo = commandScene.find(commandScene.prefabInstance(commandRoot)->sourceToEntity.at(baseChildKey));
    check(undoOverride.hasValue() && baseChildAfterUndo && baseChildAfterUndo->name == "Changed again",
        "undo prefab override");
    check(commands.redo().hasValue(), "redo prefab override");
    parent.nodes[1].name = "Command Updated Base";
    check(library.save(parent).hasValue(), "write command update source");
    const prefabs::UpdatePrefabInstancePayload updatePayload{commandRoot};
    check(commands.execute<prefabs::UpdatePrefabInstancePayload, void>("prefab.update_instance", 1, updatePayload).hasValue()
        && commandScene.find(commandScene.prefabInstance(commandRoot)->sourceToEntity.at("p10/n2"))->name == "Command Override",
        "update through typed command while preserving override");
    check(commands.undo().hasValue()
        && commandScene.find(commandScene.prefabInstance(commandRoot)->sourceToEntity.at("p10/n2"))->name == "Command Override",
        "undo prefab update and restore complete prior scene snapshot");

    auto cycle = nested;
    cycle.nodes[0].nestedPrefab = 10;
    check(library.save(cycle).hasValue(), "write graph with cross-asset cycle for runtime validation");
    const auto unchangedCount = document.entities().size();
    check(!prefabs::instantiatePrefab(library, document, 10), "reject nested prefab reference cycle");
    check(document.entities().size() == unchangedCount, "cycle rejection leaves target scene unchanged");

    check(std::filesystem::remove_all(root, ignored) > 0, "remove only the integration test temporary project");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
