#include "astro/runtime/RuntimeSession.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

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

    if (failures == 0) std::cout << "All runtime session tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
