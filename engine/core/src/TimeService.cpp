#include "astro/core/TimeService.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace astro::core
{
Result<void> TimeService::configure(TimeConfiguration configuration)
{
    if (!std::isfinite(configuration.fixedDeltaSeconds) || configuration.fixedDeltaSeconds <= 0.0
        || !std::isfinite(configuration.maxFrameDeltaSeconds)
        || configuration.maxFrameDeltaSeconds < configuration.fixedDeltaSeconds
        || configuration.maxCatchUpSteps == 0)
        return Result<void>::failure({ErrorCode::InvalidArgument, "Invalid fixed timestep configuration"});

    configuration_ = configuration;
    accumulatorSeconds_ = 0.0;
    return Result<void>::success();
}

Result<FrameTiming> TimeService::advance(double realDeltaSeconds)
{
    if (!std::isfinite(realDeltaSeconds) || realDeltaSeconds < 0.0)
        return Result<FrameTiming>::failure({ErrorCode::InvalidArgument, "Frame delta must be finite and non-negative"});

    FrameTiming timing;
    timing.frameDeltaSeconds = std::min(realDeltaSeconds, configuration_.maxFrameDeltaSeconds);
    timing.fixedDeltaSeconds = configuration_.fixedDeltaSeconds;
    timing.simulationTimeSeconds = simulationTimeSeconds_;
    timing.droppedTimeSeconds = realDeltaSeconds - timing.frameDeltaSeconds;

    if (!paused_)
    {
        accumulatorSeconds_ += timing.frameDeltaSeconds;
        const auto availableSteps = accumulatorSeconds_ / configuration_.fixedDeltaSeconds;
        const auto stepTolerance = std::numeric_limits<double>::epsilon() * std::max(1.0, availableSteps) * 4.0;
        if (availableSteps >= static_cast<double>(configuration_.maxCatchUpSteps))
            timing.fixedStepCount = configuration_.maxCatchUpSteps;
        else
            timing.fixedStepCount = static_cast<std::uint32_t>(std::floor(availableSteps + stepTolerance));
        if (timing.fixedStepCount == 0
            && accumulatorSeconds_ + std::numeric_limits<double>::epsilon() * 4.0 >= configuration_.fixedDeltaSeconds)
            timing.fixedStepCount = 1;
        const auto simulatedDelta = static_cast<double>(timing.fixedStepCount) * configuration_.fixedDeltaSeconds;
        accumulatorSeconds_ = std::max(0.0, accumulatorSeconds_ - simulatedDelta);
        simulationTimeSeconds_ += simulatedDelta;

        if (accumulatorSeconds_ >= configuration_.fixedDeltaSeconds)
        {
            const auto remainder = std::fmod(accumulatorSeconds_, configuration_.fixedDeltaSeconds);
            const auto droppedWholeSteps = accumulatorSeconds_ - remainder;
            accumulatorSeconds_ = remainder;
            timing.droppedTimeSeconds += droppedWholeSteps;
        }
        timing.interpolationAlpha = accumulatorSeconds_ / configuration_.fixedDeltaSeconds;
        timing.simulationTimeSeconds = simulationTimeSeconds_;
    }
    totalDroppedTimeSeconds_ += timing.droppedTimeSeconds;
    return Result<FrameTiming>::success(timing);
}

void TimeService::setPaused(bool paused) noexcept
{
    if (paused && !paused_) accumulatorSeconds_ = 0.0;
    paused_ = paused;
}
}
