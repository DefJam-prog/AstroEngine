#include "astro/runtime/RuntimeSession.hpp"

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
    scene::Scene authoring{1, "Runtime Test"};
    const auto created = authoring.createEntity("Player");
    check(created.hasValue(), "create authoring entity");
    if (!created) return EXIT_FAILURE;
    const auto entity = created.value();

    runtime::RuntimeSession session;
    check(session.state() == runtime::RuntimeState::Stopped, "session starts stopped");
    check(!session.pause() && !session.stepOnce(), "reject pause and step while stopped");
    check(!session.play(authoring, {}), "reject Play without update callback");

    std::uint32_t callbackCount = 0;
    auto incrementPosition = [&callbackCount, entity](scene::Scene& runtimeScene, double delta) -> core::Result<void> {
        ++callbackCount;
        auto transform = runtimeScene.transform(entity)->value;
        transform.position.x += delta;
        return runtimeScene.setTransform(entity, transform);
    };
    check(static_cast<bool>(session.play(authoring, incrementPosition, {0.1, 0.5, 8})), "start a copied runtime scene");
    check(session.state() == runtime::RuntimeState::Playing && session.runtimeScene()
        && session.runtimeScene() != &authoring, "Play owns a distinct Scene instance");
    check(!session.play(authoring, incrementPosition), "reject a second Play before Stop");

    auto frame = session.tick(0.25);
    check(frame && frame.value().fixedStepCount == 2 && callbackCount == 2,
        "advance callback only on fixed simulation steps");
    check(session.runtimeScene()->transform(entity)->value.position.x == 0.2
        && authoring.transform(entity)->value.position.x == 0.0, "runtime changes stay isolated from authoring scene");

    check(session.pause() && session.state() == runtime::RuntimeState::Paused, "pause active runtime");
    check(!session.pause(), "reject repeated pause");
    frame = session.tick(0.2);
    check(frame && frame.value().fixedStepCount == 0 && callbackCount == 2, "paused frames do not invoke simulation callback");
    const auto stepped = session.stepOnce();
    check(stepped && stepped.value().fixedStepCount == 1 && callbackCount == 3
        && session.state() == runtime::RuntimeState::Paused, "single step advances once and stays paused");
    check(session.simulationTimeSeconds() > 0.29 && session.simulationTimeSeconds() < 0.31,
        "fixed simulation clock includes manual step");
    check(session.resume() && session.state() == runtime::RuntimeState::Playing, "resume paused runtime");
    check(!session.resume(), "reject resume while already playing");

    session.stop();
    check(session.state() == runtime::RuntimeState::Stopped && session.runtimeScene() == nullptr
        && !session.tick(0.1), "Stop releases runtime copy and disables ticking");
    check(authoring.transform(entity)->value.position.x == 0.0, "Stop leaves authoring scene unchanged");

    auto failing = session.play(authoring, [](scene::Scene&, double) -> core::Result<void> {
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "deliberate tick failure"});
    }, {0.1, 0.5, 8});
    check(static_cast<bool>(failing), "restart runtime after Stop");
    auto failedFrame = session.tick(0.1);
    check(!failedFrame && session.state() == runtime::RuntimeState::Faulted
        && session.runtimeScene() != nullptr, "report tick failure and retain runtime state for diagnosis");
    session.stop();

    check(static_cast<bool>(session.play(authoring, [](scene::Scene&, double) -> core::Result<void> {
        throw std::runtime_error("runtime callback exception");
    }, {0.1, 0.5, 8})), "start callback exception case");
    const auto exceptionFrame = session.tick(0.1);
    check(!exceptionFrame && exceptionFrame.error().code == core::ErrorCode::InternalError
        && session.state() == runtime::RuntimeState::Faulted, "convert callback exception to fault diagnostic");
    session.stop();

    std::vector<std::string> lifecycleOrder;
    auto lifecycleStart = session.playWithLifecycle(authoring,
        [&](scene::Scene& runtimeScene) -> core::Result<void> {
            lifecycleOrder.push_back("create");
            auto transform = runtimeScene.transform(entity)->value;
            transform.position.y = 2.0;
            return runtimeScene.setTransform(entity, transform);
        },
        [&](scene::Scene& runtimeScene, double delta) -> core::Result<void> {
            lifecycleOrder.push_back("fixed");
            auto transform = runtimeScene.transform(entity)->value;
            transform.position.x += delta;
            return runtimeScene.setTransform(entity, transform);
        },
        [&](scene::Scene& runtimeScene) -> core::Result<void> {
            lifecycleOrder.push_back("destroy");
            auto transform = runtimeScene.transform(entity)->value;
            transform.position.z = 3.0;
            return runtimeScene.setTransform(entity, transform);
        }, {0.1, 0.5, 8});
    check(lifecycleStart && lifecycleOrder == std::vector<std::string>{"create"}
        && session.runtimeScene()->transform(entity)->value.position.y == 2.0,
        "OnCreate runs once on the isolated Scene before Play becomes active");
    const auto lifecycleTick = session.tick(0.1);
    check(lifecycleTick && lifecycleOrder == std::vector<std::string>({"create", "fixed"}),
        "fixed updates follow successful OnCreate");
    const auto lifecycleStop = session.stopChecked();
    check(lifecycleStop && lifecycleOrder == std::vector<std::string>({"create", "fixed", "destroy"})
        && session.state() == runtime::RuntimeState::Stopped && session.runtimeScene() == nullptr
        && authoring.transform(entity)->value.position == scene::Vec3{},
        "checked Stop runs OnDestroy before discarding runtime state and preserves the authoring Scene");

    int startupRollback = 0;
    const auto failedStartup = session.playWithLifecycle(authoring,
        [](scene::Scene&) -> core::Result<void> {
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "OnCreate fixture failure"});
        }, incrementPosition,
        [&](scene::Scene&) -> core::Result<void> { ++startupRollback; return core::Result<void>::success(); },
        {0.1, 0.5, 8});
    check(!failedStartup && startupRollback == 1 && session.state() == runtime::RuntimeState::Stopped
        && session.runtimeScene() == nullptr,
        "failed startup invokes cleanup and never publishes a partial runtime session");

    auto stopFailure = session.playWithLifecycle(authoring, {}, incrementPosition,
        [](scene::Scene&) -> core::Result<void> {
            return core::Result<void>::failure({core::ErrorCode::InternalError, "OnDestroy fixture failure"});
        }, {0.1, 0.5, 8});
    check(stopFailure && !session.stopChecked() && session.state() == runtime::RuntimeState::Stopped,
        "checked Stop reports lifecycle failures while still releasing the runtime Scene");

    if (failures == 0) std::cout << "All runtime session tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
