#include "astro/runtime/RuntimeSession.hpp"

#include <exception>
#include <utility>

namespace astro::runtime
{
core::Result<void> RuntimeSession::play(const scene::Scene& authoringScene, FixedStep fixedStep,
    core::TimeConfiguration timing)
{
    if (state_ != RuntimeState::Stopped)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "Runtime session must be stopped before Play"});
    if (!fixedStep)
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument, "Play requires a fixed-step callback"});
    core::TimeService freshTime;
    const auto configured = freshTime.configure(timing);
    if (!configured) return configured;

    runtimeScene_ = std::make_unique<scene::Scene>(authoringScene);
    fixedStep_ = std::move(fixedStep);
    time_ = std::move(freshTime);
    state_ = RuntimeState::Playing;
    return core::Result<void>::success();
}

core::Result<void> RuntimeSession::runFixedSteps(const std::uint32_t count, const double deltaSeconds)
{
    for (std::uint32_t i = 0; i < count; ++i)
    {
        core::Result<void> result = core::Result<void>::failure(
            {core::ErrorCode::InternalError, "Fixed-step callback did not complete"});
        try { result = fixedStep_(*runtimeScene_, deltaSeconds); }
        catch (const std::exception& exception)
        {
            state_ = RuntimeState::Faulted;
            return core::Result<void>::failure({core::ErrorCode::InternalError,
                std::string("Fixed-step callback threw an exception: ") + exception.what()});
        }
        catch (...)
        {
            state_ = RuntimeState::Faulted;
            return core::Result<void>::failure({core::ErrorCode::InternalError, "Fixed-step callback threw an unknown exception"});
        }
        if (!result)
        {
            state_ = RuntimeState::Faulted;
            return core::Result<void>::failure(result.error());
        }
    }
    return core::Result<void>::success();
}

core::Result<core::FrameTiming> RuntimeSession::tick(const double realDeltaSeconds)
{
    if (state_ != RuntimeState::Playing && state_ != RuntimeState::Paused)
        return core::Result<core::FrameTiming>::failure({core::ErrorCode::InvalidState, "Tick requires an active runtime session"});
    auto timing = time_.advance(realDeltaSeconds);
    if (!timing) return timing;
    const auto updated = runFixedSteps(timing.value().fixedStepCount, timing.value().fixedDeltaSeconds);
    if (!updated) return core::Result<core::FrameTiming>::failure(updated.error());
    return timing;
}

core::Result<void> RuntimeSession::pause()
{
    if (state_ != RuntimeState::Playing)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "Pause requires a playing runtime session"});
    time_.setPaused(true);
    state_ = RuntimeState::Paused;
    return core::Result<void>::success();
}

core::Result<void> RuntimeSession::resume()
{
    if (state_ != RuntimeState::Paused)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "Resume requires a paused runtime session"});
    time_.setPaused(false);
    state_ = RuntimeState::Playing;
    return core::Result<void>::success();
}

core::Result<core::FrameTiming> RuntimeSession::stepOnce()
{
    if (state_ != RuntimeState::Paused)
        return core::Result<core::FrameTiming>::failure({core::ErrorCode::InvalidState, "Step requires a paused runtime session"});
    auto timing = time_.stepOnce();
    if (!timing) return timing;
    const auto updated = runFixedSteps(1, timing.value().fixedDeltaSeconds);
    if (!updated) return core::Result<core::FrameTiming>::failure(updated.error());
    return timing;
}

void RuntimeSession::stop() noexcept
{
    fixedStep_ = {};
    runtimeScene_.reset();
    time_ = core::TimeService{};
    state_ = RuntimeState::Stopped;
}
}
