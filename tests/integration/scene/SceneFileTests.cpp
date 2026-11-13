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
    const auto unknownComponentPath = directory / "unknown component.scene";

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
        check(saveSceneFile(scenePath, source).hasValue(), "save a scene atomically");
        auto loaded = loadSceneFile(scenePath);
        check(loaded.hasValue(), "load a saved scene");
        if (loaded)
        {
            check(loaded.value().id() == source.id() && loaded.value().name() == source.name(),
                "preserve scene identity and Unicode name");
            check(loaded.value().entities().size() == 2, "round-trip entity count");
            check(loaded.value().find(root.value())->name == "Корень Ω"
                && loaded.value().find(child.value())->parent == root.value(), "preserve entity IDs and hierarchy");
            const auto* loadedTransform = loaded.value().transform(root.value());
            check(loadedTransform && loadedTransform->id == source.transform(root.value())->id
                && loadedTransform->value == moved, "preserve Transform values and component ID");
            auto next = loaded.value().createEntity("Next ID");
            check(next && next.value() == 4, "continue the persisted entity ID sequence");
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
        check(saveSceneFile(legacyPath, migrated.value()).hasValue(), "rewrite migrated v1 file as v2");
        auto migratedAgain = loadSceneFile(legacyPath);
        check(migratedAgain && migratedAgain.value().transform(12)->id == oldComponentId,
            "preserve migrated component ID after writing v2");
    }
    writeText(unknownComponentPath, "ASTRO_SCENE 2\nSCENE 1 \"X\"\nENTITY 1 0 \"Node\"\nCOMPONENT 1 1 \"astro.Unknown\" 1 0 0 0 0 0 0 1 1 1\nEND\n");
    auto unknownComponent = loadSceneFile(unknownComponentPath);
    check(!unknownComponent && unknownComponent.error().message.find("Unknown component type") != std::string::npos,
        "diagnose unknown component type without silently dropping it");
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
        legacyPath, unknownComponentPath})
        std::filesystem::remove(file, fsError);
    check(std::filesystem::remove(directory, fsError), "remove only the test-owned empty directory");

    if (failures == 0)
    {
        std::cout << "All scene file integration tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
