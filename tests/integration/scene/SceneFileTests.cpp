#include "astro/scene/SceneFile.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) throw std::runtime_error("failed to write test fixture");
}
}

int main()
{
    using namespace astro::scene;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("AstroEngine Scene Tests " + std::to_string(unique));
    std::error_code fsError;
    const bool createdDirectory = std::filesystem::create_directory(directory, fsError);
    check(createdDirectory, "create unique temporary test directory");
    if (!createdDirectory || fsError) return EXIT_FAILURE;

    const auto scenePath = directory / "round trip.scene";
    const auto versionPath = directory / "version.scene";
    const auto missingParentPath = directory / "missing parent.scene";
    const auto cyclePath = directory / "cycle.scene";
    const auto trailingPath = directory / "trailing.scene";
    const auto invalidUtf8Path = directory / "invalid utf8.scene";
    const auto legacyPath = directory / "legacy v1.scene";
    const auto legacyV2Path = directory / "legacy v2.scene";
    const auto legacyV3Path = directory / "legacy v3.scene";
    const auto legacyV4Path = directory / "legacy v4.scene";
    const auto unknownComponentPath = directory / "unknown component.scene";
    const auto invalidScriptPath = directory / "invalid script component.scene";
    const auto legacyV5Path = directory / "legacy v5.scene";
    const auto invalidMeshPath = directory / "invalid mesh component.scene";

    Scene source(42, "Уровень Ω");
    auto root = source.createEntity("Корень Ω");
    auto removed = source.createEntity("Deleted ID gap");
    auto child = root ? source.createEntity("Ребёнок", root.value())
        : astro::core::Result<EntityId>::failure({astro::core::ErrorCode::InternalError, "parent creation failed"});
    check(root && removed && child, "create Unicode scene hierarchy");
    if (root && removed && child)
    {
        check(source.erase(removed.value()).hasValue(), "create a persisted ID gap");
        auto moved = source.transform(root.value())->value;
        moved.position = {1.25, -2.5, 1000.0};
        moved.rotationDegrees = {0.0, 90.0, -45.5};
        moved.scale = {2.0, 0.5, 1.0};
        check(source.setTransform(root.value(), moved).hasValue(), "set a non-default Transform");
        check(source.setSpriteRenderer(root.value(), {77}).hasValue(), "attach SpriteRenderer using stable AssetId");
        check(source.setMeshRenderer(root.value(), {90}, 3, 2, false).hasValue(),
            "attach MeshRenderer reference to a stable model AssetId and primitive");
        const auto rootScript = source.addScriptBehavior(root.value(), {88}, "Игры.PlayerController");
        const auto childScript = source.addScriptBehavior(child.value(), {89}, "Игры.DisabledBehavior", false);
        check(rootScript && childScript, "attach enabled and disabled ScriptBehavior bindings");
        check(saveSceneFile(scenePath, source).hasValue(), "save a scene atomically");
        auto loaded = loadSceneFile(scenePath);
        check(loaded.hasValue(), "load a saved scene");
        if (loaded)
        {
            check(loaded.value().id() == source.id() && loaded.value().name() == source.name(),
                "preserve scene identity and Unicode name");
            check(loaded.value().entities().size() == 2, "round-trip entity count");
            check(loaded.value().prefabInstances().empty(), "ordinary scene round-trip has no prefab instances");
            check(loaded.value().find(root.value())->name == "Корень Ω"
                && loaded.value().find(child.value())->parent == root.value(), "preserve entity IDs and hierarchy");
            const auto* loadedTransform = loaded.value().transform(root.value());
            check(loadedTransform && loadedTransform->id == source.transform(root.value())->id
                && loadedTransform->value == moved, "preserve Transform values and component ID");
            const auto* sprite = loaded.value().spriteRenderer(root.value());
            check(sprite && sprite->texture.value == 77 && sprite->visible
                && sprite->id == source.spriteRenderer(root.value())->id, "round-trip SpriteRenderer reference and component ID");
            const auto* mesh = loaded.value().meshRenderer(root.value());
            check(mesh && mesh->id == source.meshRenderer(root.value())->id && mesh->schemaVersion == 1
                && mesh->model.value == 90 && mesh->meshIndex == 3 && mesh->primitiveIndex == 2 && !mesh->visible,
                "round-trip MeshRenderer model reference, submesh selector, visibility, schema and component ID");
            const auto rootBehaviors = loaded.value().scriptBehaviors(root.value());
            const auto childBehaviors = loaded.value().scriptBehaviors(child.value());
            check(rootBehaviors.size() == 1 && rootBehaviors[0].id == rootScript.value()
                && rootBehaviors[0].schemaVersion == 1 && rootBehaviors[0].scriptAsset.value == 88
                && rootBehaviors[0].managedTypeName == "Игры.PlayerController" && rootBehaviors[0].enabled,
                "round-trip ScriptBehavior AssetId, type name, schema version, enabled flag, and ComponentId");
            check(childBehaviors.size() == 1 && childBehaviors[0].id == childScript.value()
                && childBehaviors[0].scriptAsset.value == 89 && !childBehaviors[0].enabled,
                "round-trip disabled ScriptBehavior without changing its stable IDs");
            auto next = loaded.value().createEntity("Next ID");
            check(next && next.value() == 4, "continue the persisted entity ID sequence");
            check(next && loaded.value().transform(next.value())->id > childScript.value(),
                "continue the component ID sequence after ScriptBehavior records");
        }

        check(source.setProperty(root.value(), "name", PropertyValue{std::string{"Updated Ω"}}).hasValue(),
            "change the scene before atomic replacement");
        check(saveSceneFile(scenePath, source).hasValue(), "atomically replace the same scene file");
        auto updated = loadSceneFile(scenePath);
        check(updated && updated.value().find(root.value())->name == "Updated Ω", "load the replacement file");
    }

    writeText(versionPath, "ASTRO_SCENE 999\nSCENE 1 \"X\"\nEND\n");
    auto badVersion = loadSceneFile(versionPath);
    check(!badVersion && badVersion.error().code == astro::core::ErrorCode::VersionMismatch,
        "report unsupported file version");
    writeText(legacyPath, "ASTRO_SCENE 1\nSCENE 7 \"Legacy Ω\"\nENTITY 12 0 \"Old root\"\nENTITY 21 12 \"Old child\"\nEND\n");
    auto migrated = loadSceneFile(legacyPath);
    check(migrated && migrated.value().transform(12) && migrated.value().transform(21)
        && migrated.value().transform(12)->value.scale == Vec3{1.0, 1.0, 1.0},
        "migrate v1 entities to default Transform components");
    if (migrated)
    {
        const auto oldComponentId = migrated.value().transform(12)->id;
        check(saveSceneFile(legacyPath, migrated.value()).hasValue(), "rewrite migrated v1 file as v6");
        auto migratedAgain = loadSceneFile(legacyPath);
        check(migratedAgain && migratedAgain.value().transform(12)->id == oldComponentId,
            "preserve migrated component ID after writing v3");
    }
    writeText(legacyV2Path, "ASTRO_SCENE 2\nSCENE 8 \"Legacy v2\"\nENTITY 4 0 \"Old root\"\nCOMPONENT 9 4 \"astro.Transform\" 1 1 2 3 4 5 6 7 8 9\nEND\n");
    auto migratedV2 = loadSceneFile(legacyV2Path);
    check(migratedV2 && migratedV2.value().transform(4)->id == 9
        && migratedV2.value().transform(4)->value.position == Vec3{1, 2, 3}
        && !migratedV2.value().spriteRenderer(4), "migrate v2 Transform to the v3 model without changing IDs");
    if (migratedV2)
    {
        check(saveSceneFile(legacyV2Path, migratedV2.value()).hasValue(), "write migrated v2 scene as v6");
        auto roundTripV2 = loadSceneFile(legacyV2Path);
        check(roundTripV2 && roundTripV2.value().transform(4)->id == 9,
            "preserve v2 component ID through v3 save/load");
    }
    writeText(legacyV3Path,
        "ASTRO_SCENE 3\nSCENE 9 \"Legacy v3\"\nENTITY 5 0 \"Sprite node\"\n"
        "COMPONENT 11 5 \"astro.Transform\" 1 0 0 0 0 0 0 1 1 1\n"
        "COMPONENT 12 5 \"astro.SpriteRenderer\" 1 33 1\nEND\n");
    auto migratedV3 = loadSceneFile(legacyV3Path);
    check(migratedV3 && migratedV3.value().spriteRenderer(5)
        && migratedV3.value().spriteRenderer(5)->id == 12
        && migratedV3.value().spriteRenderer(5)->texture.value == 33,
        "migrate v3 sprite component without changing component or asset IDs");
    if (migratedV3)
    {
        check(saveSceneFile(legacyV3Path, migratedV3.value()).hasValue(), "write migrated v3 scene as v6");
        auto roundTripV3 = loadSceneFile(legacyV3Path);
        check(roundTripV3 && roundTripV3.value().spriteRenderer(5)->id == 12,
            "preserve migrated v3 sprite through v4 save/load");
    }
    writeText(legacyV4Path,
        "ASTRO_SCENE 4\nSCENE 10 \"Legacy v4\"\nENTITY 4 0 \"Prefab root\"\n"
        "COMPONENT 21 4 \"astro.Transform\" 1 0 0 0 0 0 0 1 1 1\n"
        "COMPONENT 22 4 \"astro.SpriteRenderer\" 1 33 1\n"
        "PREFAB_INSTANCE 4 99 1 0\nPREFAB_LINK 4 \"root\" 4\nEND\n");
    auto migratedV4 = loadSceneFile(legacyV4Path);
    check(migratedV4 && migratedV4.value().transform(4)->id == 21
        && migratedV4.value().spriteRenderer(4)->id == 22
        && migratedV4.value().prefabInstances().size() == 1
        && migratedV4.value().scriptBehaviors(4).empty(),
        "migrate v4 scene and preserve prefab data without adding a script binding");
    if (migratedV4)
    {
        check(saveSceneFile(legacyV4Path, migratedV4.value()).hasValue(), "write migrated v4 scene as v6");
        auto roundTripV4 = loadSceneFile(legacyV4Path);
        check(roundTripV4 && roundTripV4.value().transform(4)->id == 21
            && roundTripV4.value().prefabInstances().size() == 1,
            "preserve migrated v4 scene fields and IDs after v5 round-trip");
    }
    writeText(legacyV5Path,
        "ASTRO_SCENE 5\nSCENE 11 \"Legacy v5\"\nENTITY 7 0 \"Old mesh node\"\n"
        "COMPONENT 21 7 \"astro.Transform\" 1 0 0 0 0 0 0 1 1 1\n"
        "COMPONENT 22 7 \"astro.SpriteRenderer\" 1 33 1\n"
        "COMPONENT 23 7 \"astro.ScriptBehavior\" 1 88 1 \"Game.Actor\"\nEND\n");
    auto migratedV5 = loadSceneFile(legacyV5Path);
    check(migratedV5 && migratedV5.value().spriteRenderer(7) && migratedV5.value().scriptBehaviors(7).size() == 1
        && !migratedV5.value().meshRenderer(7), "migrate v5 without inventing a MeshRenderer reference");
    if (migratedV5)
    {
        check(saveSceneFile(legacyV5Path, migratedV5.value()).hasValue(), "rewrite migrated v5 file as v6");
        auto roundTripV5 = loadSceneFile(legacyV5Path);
        check(roundTripV5 && roundTripV5.value().spriteRenderer(7)->id == 22
            && roundTripV5.value().scriptBehaviors(7)[0].id == 23,
            "preserve v5 component IDs while migrating to v6");
    }
    writeText(unknownComponentPath, "ASTRO_SCENE 2\nSCENE 1 \"X\"\nENTITY 1 0 \"Node\"\nCOMPONENT 1 1 \"astro.Unknown\" 1 0 0 0 0 0 0 1 1 1\nEND\n");
    auto unknownComponent = loadSceneFile(unknownComponentPath);
    check(!unknownComponent && unknownComponent.error().message.find("Unknown component type") != std::string::npos,
        "diagnose unknown component type without silently dropping it");
    writeText(invalidScriptPath,
        "ASTRO_SCENE 5\nSCENE 1 \"Script schema\"\nENTITY 1 0 \"Node\"\n"
        "COMPONENT 1 1 \"astro.Transform\" 1 0 0 0 0 0 0 1 1 1\n"
        "COMPONENT 2 1 \"astro.ScriptBehavior\" 2 88 1 \"Game.PlayerController\"\nEND\n");
    auto unsupportedScriptSchema = loadSceneFile(invalidScriptPath);
    check(!unsupportedScriptSchema && unsupportedScriptSchema.error().code == astro::core::ErrorCode::ValidationError,
        "reject unsupported ScriptBehavior schema version with a diagnostic");
    writeText(invalidScriptPath,
        "ASTRO_SCENE 5\nSCENE 1 \"Script reference\"\nENTITY 1 0 \"Node\"\n"
        "COMPONENT 1 1 \"astro.Transform\" 1 0 0 0 0 0 0 1 1 1\n"
        "COMPONENT 2 1 \"astro.ScriptBehavior\" 1 0 1 \"Game.PlayerController\"\nEND\n");
    auto missingScriptAssetId = loadSceneFile(invalidScriptPath);
    check(!missingScriptAssetId && missingScriptAssetId.error().code == astro::core::ErrorCode::ValidationError,
        "reject ScriptBehavior without a stable script AssetId");
    writeText(invalidMeshPath,
        "ASTRO_SCENE 6\nSCENE 1 \"Mesh schema\"\nENTITY 1 0 \"Node\"\n"
        "COMPONENT 1 1 \"astro.Transform\" 1 0 0 0 0 0 0 1 1 1\n"
        "COMPONENT 2 1 \"astro.MeshRenderer\" 2 88 0 0 1\nEND\n");
    const auto invalidMeshSchema = loadSceneFile(invalidMeshPath);
    check(!invalidMeshSchema && invalidMeshSchema.error().code == astro::core::ErrorCode::ValidationError,
        "reject unsupported MeshRenderer schema versions");
    writeText(invalidMeshPath,
        "ASTRO_SCENE 6\nSCENE 1 \"Mesh AssetId\"\nENTITY 1 0 \"Node\"\n"
        "COMPONENT 1 1 \"astro.Transform\" 1 0 0 0 0 0 0 1 1 1\n"
        "COMPONENT 2 1 \"astro.MeshRenderer\" 1 0 0 0 1\nEND\n");
    check(!loadSceneFile(invalidMeshPath), "reject MeshRenderer without a stable model AssetId");
    writeText(missingParentPath, "ASTRO_SCENE 1\nSCENE 1 \"X\"\nENTITY 1 99 \"Node\"\nEND\n");
    check(!loadSceneFile(missingParentPath), "reject missing parent reference");
    writeText(cyclePath, "ASTRO_SCENE 1\nSCENE 1 \"X\"\nENTITY 1 2 \"A\"\nENTITY 2 1 \"B\"\nEND\n");
    check(!loadSceneFile(cyclePath), "reject cyclic hierarchy");
    writeText(trailingPath, "ASTRO_SCENE 1\nSCENE 1 \"X\"\nEND\nTRAILING\n");
    check(!loadSceneFile(trailingPath), "reject trailing data");
    writeText(invalidUtf8Path, std::string("ASTRO_SCENE 1\nSCENE 1 \"X\"\nENTITY 1 0 \"")
        + std::string("\xC0\xAF", 2) + "\"\nEND\n");
    check(!loadSceneFile(invalidUtf8Path), "reject invalid UTF-8 in file");
    check(!loadSceneFile(directory / "absent.scene"), "report missing file");

    for (const auto& file : {scenePath, versionPath, missingParentPath, cyclePath, trailingPath, invalidUtf8Path,
        legacyPath, legacyV2Path, legacyV3Path, legacyV4Path, legacyV5Path,
        unknownComponentPath, invalidScriptPath, invalidMeshPath})
        std::filesystem::remove(file, fsError);
    check(std::filesystem::remove(directory, fsError), "remove only the test-owned empty directory");

    if (failures == 0)
    {
        std::cout << "All scene file integration tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
