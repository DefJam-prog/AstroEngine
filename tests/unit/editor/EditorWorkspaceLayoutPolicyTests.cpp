#include "EditorWorkspaceLayoutPolicy.hpp"

#include <cmath>
#include <cstdio>

namespace
{
bool near(const float lhs, const float rhs) noexcept
{
    return std::abs(lhs - rhs) < 0.001f;
}
}

int main()
{
    using astro::editor::EditorWorkspaceLayoutPolicy;
    const auto wide = EditorWorkspaceLayoutPolicy::calculate(1920, 1040);
    if (wide.windowWidth != 1100 || wide.windowHeight != 720 || wide.compact
        || !near(wide.leftPaneFraction, 0.23f) || !near(wide.rightPaneFraction, 0.25f)
        || !near(wide.bottomPaneFraction, 0.32f) || !near(wide.gameViewWidth, 520.0f)
        || !near(wide.gameViewHeight, 560.0f))
    {
        std::fputs("Wide workspace did not preserve the standard layout\n", stderr);
        return 1;
    }

    const auto compact = EditorWorkspaceLayoutPolicy::calculate(864, 1488);
    if (compact.windowWidth != 864 || compact.windowHeight != 720 || !compact.compact
        || !near(compact.leftPaneFraction, 0.20f) || !near(compact.rightPaneFraction, 0.2777778f)
        || !near(compact.bottomPaneFraction, 0.28f))
    {
        std::fputs("Compact workspace did not select compact dock proportions\n", stderr);
        return 1;
    }

    const auto bounded = EditorWorkspaceLayoutPolicy::calculate(420, 300, 1100, 720);
    if (bounded.windowWidth != 420 || bounded.windowHeight != 300
        || bounded.gameViewWidth > static_cast<float>(bounded.windowWidth - 32)
        || bounded.gameViewHeight > static_cast<float>(bounded.windowHeight - 96))
    {
        std::fputs("Window and Game View were not bounded to a small work area\n", stderr);
        return 1;
    }

    const auto invalid = EditorWorkspaceLayoutPolicy::calculate(0, -1, 0, 0);
    if (invalid.windowWidth != 1 || invalid.windowHeight != 1
        || invalid.gameViewWidth < 1.0f || invalid.gameViewHeight < 1.0f)
    {
        std::fputs("Non-positive work-area dimensions were not safely bounded\n", stderr);
        return 1;
    }

    const auto offscreen = EditorWorkspaceLayoutPolicy::constrainWindow(-1920, 0, 1920, 1040,
        -2100, -30, 1100, 720);
    if (offscreen.x != -1920 || offscreen.y != 0 || offscreen.width != 1100 || offscreen.height != 720)
    {
        std::fputs("A window left of a negative-origin display was not moved into its usable area\n", stderr);
        return 1;
    }

    const auto oversized = EditorWorkspaceLayoutPolicy::constrainWindow(100, -200, 800, 600,
        900, 500, 1000, 900);
    if (oversized.x != 100 || oversized.y != -200 || oversized.width != 800 || oversized.height != 600)
    {
        std::fputs("An oversized window was not reduced and positioned inside the usable area\n", stderr);
        return 1;
    }

    const auto rightBottom = EditorWorkspaceLayoutPolicy::constrainWindow(100, 200, 800, 600,
        900, 900, 400, 300);
    if (rightBottom.x != 500 || rightBottom.y != 500 || rightBottom.width != 400 || rightBottom.height != 300)
    {
        std::fputs("A window beyond the right/bottom edges was not moved inside the usable area\n", stderr);
        return 1;
    }

    std::puts("Editor workspace layout policy tests passed");
    return 0;
}
