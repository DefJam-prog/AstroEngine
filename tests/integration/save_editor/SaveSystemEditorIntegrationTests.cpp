#include "astro/save_editor/SaveSystemDocument.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL: " #expression "\n"; ++failures; } } while (false)
}

int main()
{
    using namespace astro;
    namespace fs = std::filesystem;
    scene::Scene authoring(610, "Checkpoint save integration");
    const auto player = authoring.createEntity("Player");
    const auto checkpointEntity = authoring.createEntity("Checkpoint");
    CHECK(player && checkpointEntity);
    auto playerTransform = authoring.transform(player.value())->value;
    playerTransform.position = {-10.0, 0.0, 0.0};
    CHECK(authoring.setTransform(player.value(), playerTransform));
    save_editor::SaveSystemDocument document;
    CHECK(document.createGroup("actors"));
    CHECK(document.setGroupMember("actors", player.value(), true));
    CHECK(document.createProfile("selected-progress"));
    CHECK(document.setGroupField("selected-progress", "actors", std::string(runtime::SaveProperty::entityName), true));
    runtime::CheckpointDefinition definition;
    definition.entity = checkpointEntity.value();
    definition.area = {2.0, 2.0};
    definition.respawnPosition = {4.0, 5.0, 0.0};
    definition.mode = runtime::CheckpointMode::SaveSelectedProfile;
    definition.saveProfileId = "selected-progress";
    CHECK(document.setCheckpoint(definition));

    const auto profiles = document.compileProfiles(authoring);
    CHECK(profiles);
    if (!profiles) return EXIT_FAILURE;
    const auto checkpoints = document.validateCheckpoints(authoring, profiles.value());
    CHECK(checkpoints && checkpoints.value().size() == 1);
    if (!checkpoints) return EXIT_FAILURE;

    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = fs::temp_directory_path() / ("astro-save-editor-" + unique);
    std::error_code ec;
    CHECK(fs::create_directory(root, ec) && !ec);
    if (ec || !fs::exists(root)) return EXIT_FAILURE;
    {
        auto storeResult = runtime::SaveStore::open(root, 2);
        CHECK(storeResult);
        if (storeResult)
        {
            auto store = std::move(storeResult).value();
            runtime::CheckpointSystem system;
            CHECK(system.registerCheckpoint(authoring, checkpoints.value().front()));
            auto runtimeScene = authoring;
            core::EventBus events;
            runtime::CheckpointSaveContext context{&store, &profiles.value(), "1.0"};
            auto outside = runtimeScene.transform(player.value())->value;
            outside.position = {-10.0, 0.0, 0.0};
            CHECK(runtimeScene.setTransform(player.value(), outside));
            auto update = system.update(runtimeScene, player.value(), events, &context);
            CHECK(update && update.value().activated == 0);
            outside.position = {0.0, 0.0, 0.0};
            CHECK(runtimeScene.setTransform(player.value(), outside));
            update = system.update(runtimeScene, player.value(), events, &context);
            CHECK(update && update.value().activated == 1);
            runtimeScene.find(player.value())->name = "Changed after save";
            const auto respawn = system.respawn(runtimeScene, player.value(),
                {runtime::RespawnRestoreMode::SelectedProfile, {}}, events, &context);
            CHECK(respawn && respawn.value().restoredSelectedProfile);
            CHECK(runtimeScene.find(player.value())->name == "Player");
            CHECK((runtimeScene.transform(player.value())->value.position == scene::Vec3{4.0, 5.0, 0.0}));
            CHECK(authoring.find(player.value())->name == "Player");
        }
    }
    fs::remove_all(root, ec);
    CHECK(!ec);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
