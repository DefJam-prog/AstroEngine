#pragma once

#include "astro/core/Result.hpp"

#include <cstdint>

namespace astro::core
{
struct TimeConfiguration
{
    double fixedDeltaSeconds = 1.0 / 60.0;
    double maxFrameDeltaSeconds = 0.25;
    std::uint32_t maxCatchUpSteps = 8;
};

struct FrameTiming
{
    double frameDeltaSeconds = 0.0;
    double fixedDeltaSeconds = 0.0;
    std::uint32_t fixedStepCount = 0;
    double interpolationAlpha = 0.0;
    double simulationTimeSeconds = 0.0;
    double droppedTimeSeconds = 0.0;
};

class TimeService
{
public:
    TimeService() = default;

    Result<void> configure(TimeConfiguration configuration);
    Result<FrameTiming> advance(double realDeltaSeconds);
    void setPaused(bool paused) noexcept;

    [[nodiscard]] bool isPaused() const noexcept { return paused_; }
    [[nodiscard]] TimeConfiguration configuration() const noexcept { return configuration_; }
    [[nodiscard]] double simulationTimeSeconds() const noexcept { return simulationTimeSeconds_; }
    [[nodiscard]] double totalDroppedTimeSeconds() const noexcept { return totalDroppedTimeSeconds_; }

private:
    TimeConfiguration configuration_;
    double accumulatorSeconds_ = 0.0;
    double simulationTimeSeconds_ = 0.0;
    double totalDroppedTimeSeconds_ = 0.0;
    bool paused_ = false;
};
}
