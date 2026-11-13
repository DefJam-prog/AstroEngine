#include "astro/runtime/Physics2D.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro;
    scene::Scene authoringScene{501, "Physics integration"};
    const auto floorResult = authoringScene.createEntity("Floor");
    const auto actorResult = authoringScene.createEntity("Actor");
    const auto triggerResult = authoringScene.createEntity("Trigger");
    check(floorResult && actorResult && triggerResult, "create physics fixture entities");
    if (!floorResult || !actorResult || !triggerResult) return EXIT_FAILURE;
    const auto floor = floorResult.value();
    const auto actor = actorResult.value();
    const auto trigger = triggerResult.value();

    auto actorTransform = authoringScene.transform(actor)->value;
    actorTransform.position = {0.0, 1.1, 0.0};
    check(static_cast<bool>(authoringScene.setTransform(actor, actorTransform)), "set actor start position");
    auto triggerTransform = authoringScene.transform(trigger)->value;
    triggerTransform.position = {4.0, 0.0, 0.0};
    check(static_cast<bool>(authoringScene.setTransform(trigger, triggerTransform)), "set trigger position");

    scene::Scene runtimeScene = authoringScene;
    runtime::PhysicsWorld2D physics;
    core::EventBus events;
    std::vector<runtime::CollisionEvent> observed;
    const auto subscription = events.subscribe<runtime::CollisionEvent>([&](const auto& event) {
        observed.push_back(event);
    });
    check(static_cast<bool>(subscription), "subscribe to collision events");

    check(static_cast<bool>(physics.setGravity({0.0, -10.0})), "configure gravity");
    check(static_cast<bool>(physics.addBody(runtimeScene, floor, {}, {{2.0, 0.5}, false, true})),
        "register static floor collider");
    check(static_cast<bool>(physics.addBody(runtimeScene, actor,
        {runtime::BodyType2D::Dynamic, {}, 1.0}, {{0.5, 0.5}, false, true})), "register dynamic actor");
    check(static_cast<bool>(physics.addBody(runtimeScene, trigger,
        {runtime::BodyType2D::Static, {}, 0.0}, {{0.5, 0.5}, true, true})), "register trigger collider");
    check(physics.bodyCount() == 3, "count registered bodies");
    check(!physics.addBody(runtimeScene, actor), "reject a duplicate body");
    scene::Scene unrelatedScene{502, "Unrelated scene"};
    (void)unrelatedScene.createEntity("Other floor");
    const auto unrelatedActor = unrelatedScene.createEntity("Other actor");
    check(unrelatedActor && !physics.addBody(unrelatedScene, unrelatedActor.value()),
        "reject body registration from another SceneId");
    check(!physics.step(unrelatedScene, events, 0.1), "reject stepping a different SceneId");
    check(!physics.setVelocity(floor, {1.0, 0.0}), "reject velocity on static body");
    check(!physics.setGravity({0.0, std::numeric_limits<double>::infinity()}), "reject non-finite gravity");
    check(!physics.addBody(runtimeScene, trigger, {}, {{std::numeric_limits<double>::infinity(), 0.5}, false, true}),
        "reject non-finite collider extent");
    check(!physics.step(runtimeScene, events, 0.0), "reject zero fixed step");

    check(static_cast<bool>(physics.step(runtimeScene, events, 0.1)), "integrate one fixed physics step");
    check(static_cast<bool>(physics.step(runtimeScene, events, 0.1)), "resolve first floor contact");
    check(observed.size() == 1 && observed[0].kind == runtime::CollisionEventKind::Enter
        && observed[0].first == floor && observed[0].second == actor
        && observed[0].normal == runtime::Vec2{0.0, 1.0} && !observed[0].trigger,
        "publish deterministic solid contact enter event");
    check(static_cast<bool>(physics.step(runtimeScene, events, 0.1)), "keep actor resting on floor");
    check(observed.size() == 2 && observed[1].kind == runtime::CollisionEventKind::Stay
        && observed[1].sequence == 2 && runtimeScene.transform(actor)->value.position.y >= 1.0
        && runtimeScene.transform(actor)->value.position.y < 1.001,
        "resolve gravity against floor and publish stay event");
    const auto restingVelocity = physics.velocity(actor);
    check(restingVelocity && restingVelocity.value() == runtime::Vec2{},
        "stop dynamic velocity along the resolved floor normal");
    check(!physics.velocity(999), "report missing physics body velocity");

    auto movedActor = runtimeScene.transform(actor)->value;
    movedActor.position.x = 4.0;
    movedActor.position.y = 0.0;
    check(static_cast<bool>(runtimeScene.setTransform(actor, movedActor)), "move actor into trigger");
    check(static_cast<bool>(physics.setGravity({0.0, 0.0})), "disable gravity for trigger check");
    check(static_cast<bool>(physics.step(runtimeScene, events, 0.1)), "detect trigger overlap");
    check(observed.size() == 4 && observed[2].kind == runtime::CollisionEventKind::Exit
        && observed[2].first == floor && observed[2].second == actor
        && observed[3].kind == runtime::CollisionEventKind::Enter
        && observed[3].first == actor && observed[3].second == trigger && observed[3].trigger,
        "report trigger enter and previous floor contact exit without solid response");

    check(runtimeScene.transform(actor)->value.position.x == 4.0
        && authoringScene.transform(actor)->value.position.x == 0.0
        && authoringScene.transform(actor)->value.position.y == 1.1,
        "physics mutates only runtime Scene copy");
    check(static_cast<bool>(physics.removeBody(trigger)) && physics.bodyCount() == 2,
        "remove a registered physics body");
    check(!physics.removeBody(trigger), "reject duplicate body removal");
    if (subscription) check(static_cast<bool>(events.unsubscribe(subscription.value())), "unsubscribe collision observer");

    if (failures == 0) std::cout << "All 2D physics integration tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
