#include "astro/runtime/InteractionSystem.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro;
    scene::Scene authoringScene{1, "Interaction test"};
    const auto actorResult = authoringScene.createEntity("Player");
    const auto doorResult = authoringScene.createEntity("Door");
    const auto farResult = authoringScene.createEntity("Far target");
    const auto failingResult = authoringScene.createEntity("Failing target");
    const auto throwingResult = authoringScene.createEntity("Throwing target");
    check(actorResult && doorResult && farResult && failingResult && throwingResult, "create interaction fixture entities");
    if (!actorResult || !doorResult || !farResult || !failingResult || !throwingResult) return EXIT_FAILURE;
    const auto actor = actorResult.value();
    const auto door = doorResult.value();
    const auto farTarget = farResult.value();
    const auto failingTarget = failingResult.value();
    const auto throwingTarget = throwingResult.value();

    auto transform = authoringScene.transform(door)->value;
    transform.position = {1.0, 0.0, 0.0};
    check(static_cast<bool>(authoringScene.setTransform(door, transform)), "set door transform");
    transform = authoringScene.transform(farTarget)->value;
    transform.position = {10.0, 0.0, 0.0};
    check(static_cast<bool>(authoringScene.setTransform(farTarget, transform)), "set far target transform");
    transform = authoringScene.transform(failingTarget)->value;
    transform.position = {0.5, 0.0, 0.0};
    check(static_cast<bool>(authoringScene.setTransform(failingTarget, transform)), "set failing target transform");
    transform = authoringScene.transform(throwingTarget)->value;
    transform.position = {0.75, 0.0, 0.0};
    check(static_cast<bool>(authoringScene.setTransform(throwingTarget, transform)), "set throwing target transform");

    scene::Scene runtimeScene = authoringScene;
    runtime::InteractionSystem interactions;
    core::EventBus events;
    std::vector<runtime::InteractionEvent> observed;
    const auto subscription = events.subscribe<runtime::InteractionEvent>([&](const auto& event) {
        observed.push_back(event);
    });
    check(static_cast<bool>(subscription), "subscribe to typed runtime interaction events");

    std::uint32_t doorActivations = 0;
    auto doorBehavior = runtime::InteractableBehavior{2.0, true,
        [&](scene::Scene& scene, scene::EntityId source, scene::EntityId target) -> core::Result<void> {
            if (source != actor || target != door)
                return core::Result<void>::failure({core::ErrorCode::ValidationError, "Unexpected interaction IDs"});
            ++doorActivations;
            auto state = scene.transform(target)->value;
            state.position.y = 3.0;
            return scene.setTransform(target, state);
        }};
    check(static_cast<bool>(interactions.registerInteractable(runtimeScene, door, doorBehavior)),
        "register an interactable behavior for a scene entity");
    check(!interactions.registerInteractable(runtimeScene, door, doorBehavior), "reject duplicate interactable binding");
    check(!interactions.registerInteractable(runtimeScene, 999, doorBehavior), "reject missing interactable entity");
    check(!interactions.registerInteractable(runtimeScene, farTarget,
        {1.0, true, {}}), "reject empty interaction callback");
    check(static_cast<bool>(interactions.canInteract(runtimeScene, actor, door)), "report target as currently interactable");
    scene::Scene unrelatedScene{2, "Unrelated scene"};
    check(static_cast<bool>(unrelatedScene.createEntity("Aliased actor"))
        && static_cast<bool>(unrelatedScene.createEntity("Aliased target")), "create entities with IDs matching another scene");
    check(!interactions.canInteract(unrelatedScene, actor, door)
        && !interactions.requestInteraction(unrelatedScene, events, actor, door),
        "reject entity IDs from a different scene even when numeric IDs match");

    check(static_cast<bool>(interactions.requestInteraction(runtimeScene, events, actor, door)),
        "request and activate nearby interaction");
    check(doorActivations == 1 && runtimeScene.transform(door)->value.position.y == 3.0
        && authoringScene.transform(door)->value.position.y == 0.0,
        "behavior modifies only the runtime Scene copy");
    check(observed.size() == 2 && observed[0].kind == runtime::InteractionEventKind::Requested
        && observed[1].kind == runtime::InteractionEventKind::Activated
        && observed[0].sequence == 1 && observed[1].sequence == 2
        && observed[0].actor == actor && observed[0].target == door,
        "publish ordered, sequenced requested and activated events");

    check(static_cast<bool>(interactions.setEnabled(door, false))
        && !interactions.canInteract(runtimeScene, actor, door), "disable an interactable behavior");
    const auto disabled = interactions.requestInteraction(runtimeScene, events, actor, door);
    check(!disabled && disabled.error().code == core::ErrorCode::InvalidState
        && observed.size() == 4 && observed[2].kind == runtime::InteractionEventKind::Requested
        && observed[3].kind == runtime::InteractionEventKind::Rejected
        && !observed[3].diagnostic.empty() && doorActivations == 1,
        "emit a rejection event without invoking a disabled target");

    runtime::InteractableBehavior noop{1.0, true,
        [](scene::Scene&, scene::EntityId, scene::EntityId) { return core::Result<void>::success(); }};
    check(static_cast<bool>(interactions.registerInteractable(runtimeScene, farTarget, noop)),
        "register a second target");
    const auto outOfRange = interactions.requestInteraction(runtimeScene, events, actor, farTarget);
    check(!outOfRange && outOfRange.error().code == core::ErrorCode::ValidationError
        && observed.size() == 6 && observed[5].kind == runtime::InteractionEventKind::Rejected,
        "reject interaction outside activation range");

    runtime::InteractableBehavior failingBehavior{1.0, true,
        [](scene::Scene&, scene::EntityId, scene::EntityId) {
            return core::Result<void>::failure({core::ErrorCode::InternalError, "door motor failed"});
        }};
    check(static_cast<bool>(interactions.registerInteractable(runtimeScene, failingTarget, failingBehavior)),
        "register behavior that can fail");
    const auto failure = interactions.requestInteraction(runtimeScene, events, actor, failingTarget);
    check(!failure && observed.size() == 8 && observed[7].kind == runtime::InteractionEventKind::Rejected
        && observed[7].diagnostic == "door motor failed", "convert behavior failure into a runtime rejection event");

    runtime::InteractableBehavior throwingBehavior{1.0, true,
        [](scene::Scene&, scene::EntityId, scene::EntityId) -> core::Result<void> {
            throw std::runtime_error("interaction callback exception");
        }};
    check(static_cast<bool>(interactions.registerInteractable(runtimeScene, throwingTarget, throwingBehavior)),
        "register behavior that can throw");
    const auto thrown = interactions.requestInteraction(runtimeScene, events, actor, throwingTarget);
    check(!thrown && thrown.error().code == core::ErrorCode::InternalError
        && observed.size() == 10 && observed[9].kind == runtime::InteractionEventKind::Rejected
        && observed[9].diagnostic.find("interaction callback exception") != std::string::npos,
        "convert callback exception to internal error and rejection event");

    check(static_cast<bool>(interactions.unregisterInteractable(door))
        && !interactions.unregisterInteractable(door), "unregister behavior exactly once");
    if (subscription) check(static_cast<bool>(events.unsubscribe(subscription.value())), "unsubscribe runtime event observer");

    if (failures == 0) std::cout << "All interaction system integration tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
