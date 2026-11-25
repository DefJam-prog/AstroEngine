#include "EditorWorkspaceLayoutPolicy.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace astro::editor
{
EditorWorkspaceLayoutPlan EditorWorkspaceLayoutPolicy::calculate(const int workAreaWidth,
    const int workAreaHeight, const int requestedWidth, const int requestedHeight) noexcept
{
    const int availableWidth = std::max(1, workAreaWidth);
    const int availableHeight = std::max(1, workAreaHeight);
    EditorWorkspaceLayoutPlan plan;
    plan.windowWidth = std::clamp(requestedWidth, 1, availableWidth);
    plan.windowHeight = std::clamp(requestedHeight, 1, availableHeight);
    plan.compact = plan.windowWidth < compactWidthThreshold;
    if (!plan.compact)
    {
        plan.leftPaneFraction = 0.23f;
        plan.rightPaneFraction = 0.25f;
        plan.bottomPaneFraction = 0.32f;
    }
    else
    {
        // Keep the center viewport near the existing width while moving some
        // room from Hierarchy to the Inspector/Project tab group.
        plan.rightPaneFraction = 0.2777778f;
    }
    plan.gameViewWidth = static_cast<float>(std::min(520, std::max(1, plan.windowWidth - 32)));
    plan.gameViewHeight = static_cast<float>(std::min(560, std::max(1, plan.windowHeight - 96)));
    return plan;
}

EditorWindowGeometry EditorWorkspaceLayoutPolicy::constrainWindow(const int areaX, const int areaY,
    const int areaWidth, const int areaHeight, const int x, const int y, const int width, const int height) noexcept
{
    const auto boundedAreaWidth = std::max(1, areaWidth);
    const auto boundedAreaHeight = std::max(1, areaHeight);
    const auto boundedWidth = std::clamp(width, 1, boundedAreaWidth);
    const auto boundedHeight = std::clamp(height, 1, boundedAreaHeight);
    const auto clampPosition = [](const int value, const int origin, const int extent, const int windowExtent)
    {
        const auto minimum = static_cast<std::int64_t>(origin);
        const auto maximum = std::max(minimum, minimum + static_cast<std::int64_t>(extent) - windowExtent);
        const auto bounded = std::clamp(static_cast<std::int64_t>(value), minimum, maximum);
        return static_cast<int>(std::clamp(bounded,
            static_cast<std::int64_t>(std::numeric_limits<int>::min()),
            static_cast<std::int64_t>(std::numeric_limits<int>::max())));
    };
    return {clampPosition(x, areaX, boundedAreaWidth, boundedWidth),
        clampPosition(y, areaY, boundedAreaHeight, boundedHeight), boundedWidth, boundedHeight};
}
}
