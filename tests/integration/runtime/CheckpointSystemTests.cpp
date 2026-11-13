#include "astro/runtime/CheckpointSystem.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
int failures = 0;
template <typename T>
void check(const T& condition, const char* message)
{
    if (!static_cast<bool>(condition)) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool setPosition(astro::scene::Scene& scene, const astro::scene::EntityId entity, const astro::scene::Vec3 position)
{
    const auto* component = scene.transform(entity);
    if (!component) return false;
    auto value = component->value;
    value.position = position;
    return scene.setTransform(entity, value).hasValue();
}
}

int main()
{
    using namespace astro;
    using namespace astro::runtime;

    scene::Scene authoring{810, "Checkpoint overlap"};
    auto actorResult = authoring.createEntity("Actor");
    auto beaconResult = authoring.createEntity("Beacon");
    auto conditionalResult = authoring.createEntity("Conditional beacon");
    check(actorResult && beaconResult && conditionalResult, "create actor and checkpoint entities");
    if (!actorResult || !beaconResult || !conditionalResult) return EXIT_FAILURE;
    const auto actor = actorResult.value();
    const auto beacon = beaconResult.value();
    const auto conditional = conditionalResult.value();
    check(setPosition(authoring, actor, {5.0, 0.0, 0.0}), "place actor outside trigger");
    check(setPosition(authoring, conditional, {10.0, 0.0, 0.0}), "place conditional trigger");
    auto runtimeScene = authoring;

    CheckpointSystem checkpoints;
    CheckpointDefinition oneShot;
    oneShot.entity = beacon;
    oneShot.area = {1.0, 1.5};
    oneShot.respawnPosition = {0.0, 2.0, 3.0};
    oneShot.repeatMode = CheckpointRepeatMode::OneShot;
    check(checkpoints.registerCheckpoint(runtimeScene, oneShot), "register a one-shot respawn-only checkpoint");
    bool conditionMet = false;
    CheckpointDefinition conditionalCheckpoint;
    conditionalCheckpoint.entity = conditional;
    conditionalCheckpoint.area = {0.5, 0.5};
    conditionalCheckpoint.condition = [&conditionMet](const scene::Scene&, const scene::EntityId) {
        return core::Result<bool>::success(conditionMet);
    };
    check(checkpoints.registerCheckpoint(runtimeScene, conditionalCheckpoint), "register condition-guarded checkpoint");
    check(!checkpoints.registerCheckpoint(runtimeScene, oneShot), "reject duplicate checkpoint entity");
    CheckpointDefinition invalidArea;
    invalidArea.entity = 9999;
    check(!checkpoints.registerCheckpoint(runtimeScene, invalidArea), "reject missing checkpoint entity");
    CheckpointDefinition invalidSave;
    invalidSave.entity = beacon;
    invalidSave.mode = CheckpointMode::SaveSelectedProfile;
    check(!checkpoints.registerCheckpoint(runtimeScene, invalidSave), "require a named profile for save checkpoint");

    core::EventBus events;
    std::vector<CheckpointEvent> observed;
    const auto subscription = events.subscribe<CheckpointEvent>([&](const auto& event) { observed.push_back(event); });
    check(subscription, "subscribe to checkpoint events");
    auto outside = checkpoints.update(runtimeScene, actor, events);
    check(outside && outside.value().activated == 0 && observed.empty(), "do not trigger outside the configured area");
    check(setPosition(runtimeScene, actor, {1.0, 1.5, 200.0}), "move actor to trigger boundary with unrelated Z");
    auto entered = checkpoints.update(runtimeScene, actor, events);
    check(entered && entered.value().activated == 1 && checkpoints.activeCheckpoint() == beacon,
        "activate checkpoint on XY area entry inclusively");
    check(observed.size() == 1 && observed.back().kind == CheckpointEventKind::Activated,
        "publish typed activation event");
    auto staysInside = checkpoints.update(runtimeScene, actor, events);
    check(staysInside && staysInside.value().activated == 0,
        "do not retrigger every frame while actor remains inside");
    check(setPosition(runtimeScene, actor, {5.0, 0.0, 0.0}), "leave one-shot trigger");
    check(checkpoints.update(runtimeScene, actor, events), "update actor after leaving trigger");
    check(setPosition(runtimeScene, actor, {0.0, 0.0, 0.0}), "re-enter consumed one-shot trigger");
    auto consumed = checkpoints.update(runtimeScene, actor, events);
    check(consumed && consumed.value().rejected == 1
        && observed.back().kind == CheckpointEventKind::Rejected, "one-shot checkpoint rejects subsequent activation");

    check(setPosition(runtimeScene, actor, {10.0, 0.0, 0.0}), "enter condition-guarded trigger");
    auto conditionDenied = checkpoints.update(runtimeScene, actor, events);
    check(conditionDenied && conditionDenied.value().rejected == 1,
        "condition can reject checkpoint activation");
    conditionMet = true;
    check(checkpoints.update(runtimeScene, actor, events), "condition rejection remains edge-triggered");
    check(setPosition(runtimeScene, actor, {5.0, 0.0, 0.0}), "leave conditional trigger");
    check(checkpoints.update(runtimeScene, actor, events), "clear conditional overlap state");
    check(setPosition(runtimeScene, actor, {10.0, 0.0, 0.0}), "re-enter conditional trigger after enabling condition");
    auto conditionAccepted = checkpoints.update(runtimeScene, actor, events);
    check(conditionAccepted && conditionAccepted.value().activated == 1
        && checkpoints.activeCheckpoint() == conditional, "activate after condition succeeds");
    check(setPosition(runtimeScene, actor, {5.0, 0.0, 0.0}), "leave repeatable conditional checkpoint");
    check(checkpoints.update(runtimeScene, actor, events), "clear repeatable checkpoint overlap");
    check(setPosition(runtimeScene, actor, {10.0, 0.0, 0.0}), "re-enter repeatable conditional checkpoint");
    auto repeated = checkpoints.update(runtimeScene, actor, events);
    check(repeated && repeated.value().activated == 1, "repeatable checkpoint activates again after leave and re-entry");
    check(checkpoints.unregisterCheckpoint(conditional), "unregister active checkpoint");
    check(!checkpoints.activeCheckpoint(), "unregister clears active checkpoint reference");
    auto fallback = checkpoints.respawn(runtimeScene, actor, {{RespawnRestoreMode::PositionOnly}, {4.0, 5.0, 6.0}}, events);
    check(fallback && fallback.value().position == scene::Vec3{4.0, 5.0, 6.0}
        && runtimeScene.transform(actor)->value.position == scene::Vec3{4.0, 5.0, 6.0},
        "use configured fallback spawn when no checkpoint is active");
    check(authoring.transform(actor)->value.position == scene::Vec3{5.0, 0.0, 0.0},
        "checkpoint update and respawn do not mutate authoring Scene copy");
    check(events.unsubscribe(subscription.value()), "unsubscribe checkpoint events");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto saveDirectory = std::filesystem::temp_directory_path() / ("AstroEngine checkpoint saves " + std::to_string(unique));
    std::error_code fsError;
    auto storeResult = SaveStore::open(saveDirectory, 2);
    check(storeResult, "open checkpoint SaveStore");
    if (!storeResult) return EXIT_FAILURE;
    auto store = std::move(storeResult).value();

    scene::Scene saveAuthoring{811, "Checkpoint save"};
    auto heroResult = saveAuthoring.createEntity("Player");
    auto progressResult = saveAuthoring.createEntity("Puzzle progress");
    auto checkpointEntityResult = saveAuthoring.createEntity("Finish beacon");
    check(heroResult && progressResult && checkpointEntityResult, "create save checkpoint entities");
    if (!heroResult || !progressResult || !checkpointEntityResult) return EXIT_FAILURE;
    const auto hero = heroResult.value();
    const auto progress = progressResult.value();
    const auto finishBeacon = checkpointEntityResult.value();
    check(setPosition(saveAuthoring, finishBeacon, {0.0, 0.0, 0.0}), "place save checkpoint at origin");
    check(setPosition(saveAuthoring, hero, {0.5, 0.5, 0.0}), "place hero inside save checkpoint area");
    auto profileResult = SaveProfile::create("checkpoint-progress", 1);
    check(profileResult && profileResult.value().addField(hero, std::string(SaveProperty::entityName))
        && profileResult.value().addField(progress, std::string(SaveProperty::entityName)),
        "select only player and puzzle progress names");
    if (!profileResult) return EXIT_FAILURE;
    SaveProfile checkpointProfile = std::move(profileResult).value();
    const auto checkpointProfileId = checkpointProfile.id();
    std::map<std::string, SaveProfile> profiles;
    profiles.emplace(checkpointProfileId, std::move(checkpointProfile));
    CheckpointSaveContext saveContext{&store, &profiles, "1.0.0"};

    auto saveRuntime = saveAuthoring;
    CheckpointSystem saveCheckpoints;
    CheckpointDefinition saveCheckpoint;
    saveCheckpoint.entity = finishBeacon;
    saveCheckpoint.area = {1.0, 1.0};
    saveCheckpoint.respawnPosition = {90.0, 12.0, 0.0};
    saveCheckpoint.mode = CheckpointMode::SaveSelectedProfile;
    saveCheckpoint.saveProfileId = "checkpoint-progress";
    saveCheckpoint.slot = 0;
    saveCheckpoint.repeatMode = CheckpointRepeatMode::Repeatable;
    check(saveCheckpoints.registerCheckpoint(saveRuntime, saveCheckpoint), "register checkpoint with selected SaveProfile");
    core::EventBus saveEvents;
    auto failedActivation = saveCheckpoints.update(saveRuntime, hero, saveEvents);
    check(failedActivation && failedActivation.value().rejected == 1 && !saveCheckpoints.activeCheckpoint(),
        "do not activate checkpoint when selected save context is missing");
    check(setPosition(saveRuntime, hero, {3.0, 0.5, 0.0}), "leave save trigger after rejected activation");
    check(saveCheckpoints.update(saveRuntime, hero, saveEvents), "clear save trigger overlap before retry");
    check(setPosition(saveRuntime, hero, {0.5, 0.5, 0.0}), "re-enter save trigger with valid context");
    auto activated = saveCheckpoints.update(saveRuntime, hero, saveEvents, &saveContext);
    check(activated && activated.value().activated == 1 && saveCheckpoints.activeCheckpoint() == finishBeacon,
        "save selected state before activating save checkpoint");
    auto info = store.inspect(0);
    check(info && info.value().profileId == "checkpoint-progress" && info.value().propertyCount == 2,
        "checkpoint writes only fields selected in its named profile");

    check(saveRuntime.setProperty(hero, "name", scene::PropertyValue{std::string("Dead player")}),
        "mutate player state before selected respawn");
    check(saveRuntime.setProperty(progress, "name", scene::PropertyValue{std::string("Lost progress")}),
        "mutate selected progress before selected respawn");
    check(setPosition(saveRuntime, hero, {40.0, 50.0, 0.0}), "move player before respawn");
    auto restored = saveCheckpoints.respawn(saveRuntime, hero,
        {RespawnRestoreMode::SelectedProfile, {0.0, 0.0, 0.0}}, saveEvents, &saveContext);
    check(restored && restored.value().restoredSelectedProfile && restored.value().restoredProperties == 2,
        "respawn can restore explicit SaveProfile before applying spawn point");
    check(saveRuntime.find(hero)->name == "Player" && saveRuntime.find(progress)->name == "Puzzle progress",
        "selected data is restored from checkpoint slot");
    check(saveRuntime.transform(hero)->value.position == scene::Vec3{90.0, 12.0, 0.0},
        "respawn point overrides saved actor position");
    check(saveAuthoring.find(hero)->name == "Player" && saveAuthoring.transform(hero)->value.position == scene::Vec3{0.5, 0.5, 0.0},
        "checkpoint save and respawn leave authoring Scene unchanged");

    check(saveRuntime.setProperty(progress, "name", scene::PropertyValue{std::string("Unsaved progress")}),
        "change selected state before position-only respawn");
    check(setPosition(saveRuntime, hero, {5.0, 5.0, 0.0}), "move actor before position-only respawn");
    auto positionOnly = saveCheckpoints.respawn(saveRuntime, hero,
        {RespawnRestoreMode::PositionOnly, {0.0, 0.0, 0.0}}, saveEvents, &saveContext);
    check(positionOnly && !positionOnly.value().restoredSelectedProfile
        && saveRuntime.find(progress)->name == "Unsaved progress",
        "position-only respawn leaves saved-state fields alone");
    check(saveRuntime.transform(hero)->value.position == scene::Vec3{90.0, 12.0, 0.0},
        "position-only respawn still uses active checkpoint spawn point");

    std::filesystem::remove_all(saveDirectory, fsError);
    check(!fsError, "remove isolated checkpoint test save files");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
