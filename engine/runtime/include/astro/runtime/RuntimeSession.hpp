#pragma once

#include "astro/core/TimeService.hpp"
#include "astro/scene/Scene.hpp"

#include <functional>
#include <memory>

namespace astro::runtime
{
enum class RuntimeState { Stopped, Playing, Paused, Faulted };
using FixedStep = std::function<core::Result<void>(scene::Scene&, double)>;
using RuntimeLifecycle = std::function<core::Result<void>(scene::Scene&)>;

// Owns a disposable runtime copy. It never writes to the authoring Scene.
class RuntimeSession final
{
public:
    core::Result<void> play(const scene::Scene& authoringScene, FixedStep fixedStep,
        core::TimeConfiguration timing = {});
    core::Result<void> playWithLifecycle(const scene::Scene& authoringScene,
        RuntimeLifecycle onCreate, FixedStep fixedStep, RuntimeLifecycle onDestroy,
        core::TimeConfiguration timing = {});
    core::Result<core::FrameTiming> tick(double realDeltaSeconds);
    core::Result<void> pause();
    core::Result<void> resume();
    core::Result<core::FrameTiming> stepOnce();
    void stop() noexcept;
    [[nodiscard]] core::Result<void> stopChecked();

    [[nodiscard]] RuntimeState state() const noexcept { return state_; }
    [[nodiscard]] const scene::Scene* runtimeScene() const noexcept { return runtimeScene_.get(); }
    [[nodiscard]] double simulationTimeSeconds() const noexcept { return time_.simulationTimeSeconds(); }

private:
    core::Result<void> runFixedSteps(std::uint32_t count, double deltaSeconds);
    std::unique_ptr<scene::Scene> runtimeScene_;
    FixedStep fixedStep_;
    RuntimeLifecycle onDestroy_;
    core::TimeService time_;
    RuntimeState state_ = RuntimeState::Stopped;
};
}
