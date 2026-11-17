#include "astro/runtime/RuntimeSession.hpp"

#include <exception>
#include <utility>

namespace astro::runtime
{
core::Result<void> RuntimeSession::play(const scene::Scene& authoringScene, FixedStep fixedStep,
    core::TimeConfiguration timing)
{
    return playWithLifecycle(authoringScene, {}, std::move(fixedStep), {}, timing);
}

core::Result<void> RuntimeSession::playWithLifecycle(const scene::Scene& authoringScene,
    RuntimeLifecycle onCreate, FixedStep fixedStep, RuntimeLifecycle onDestroy,
    core::TimeConfiguration timing)
{
    if (state_ != RuntimeState::Stopped)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "Runtime session must be stopped before Play"});
    if (!fixedStep)
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument, "Play requires a fixed-step callback"});
    core::TimeService freshTime;
    const auto configured = freshTime.configure(timing);
    if (!configured) return configured;

    auto candidate = std::make_unique<scene::Scene>(authoringScene);
    if (onCreate)
    {
        core::Result<void> created = core::Result<void>::failure(
            {core::ErrorCode::InternalError, "Runtime OnCreate did not complete"});
        try { created = onCreate(*candidate); }
        catch (const std::exception& exception)
        {
            created = core::Result<void>::failure({core::ErrorCode::InternalError,
                std::string("Runtime OnCreate threw an exception: ") + exception.what()});
        }
        catch (...)
        {
            created = core::Result<void>::failure({core::ErrorCode::InternalError,
                "Runtime OnCreate threw an unknown exception"});
        }
        if (!created)
        {
            if (onDestroy)
            {
                try
                {
                    const auto cleanup = onDestroy(*candidate);
                    if (!cleanup) created = core::Result<void>::failure({created.error().code,
                        created.error().message + "; startup cleanup failed: " + cleanup.error().message});
                }
                catch (...) { /* Preserve the original startup error; the candidate is discarded. */ }
            }
            return created;
        }
    }

    runtimeScene_ = std::move(candidate);
    fixedStep_ = std::move(fixedStep);
    onDestroy_ = std::move(onDestroy);
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
    try { (void)stopChecked(); }
    catch (...) { /* Preserve the noexcept legacy Stop API. */ }
}

core::Result<void> RuntimeSession::stopChecked()
{
    core::Result<void> stopped = core::Result<void>::success();
    if (runtimeScene_ && onDestroy_)
    {
        try { stopped = onDestroy_(*runtimeScene_); }
        catch (const std::exception& exception)
        {
            stopped = core::Result<void>::failure({core::ErrorCode::InternalError,
                std::string("Runtime OnDestroy threw an exception: ") + exception.what()});
        }
        catch (...)
        {
            stopped = core::Result<void>::failure({core::ErrorCode::InternalError,
                "Runtime OnDestroy threw an unknown exception"});
        }
    }
    fixedStep_ = {};
    onDestroy_ = {};
    runtimeScene_.reset();
    time_ = core::TimeService{};
    state_ = RuntimeState::Stopped;
    return stopped;
}
}
