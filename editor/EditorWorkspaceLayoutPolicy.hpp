#pragma once

namespace astro::editor
{
struct EditorWorkspaceLayoutPlan
{
    int windowWidth = 1;
    int windowHeight = 1;
    bool compact = true;
    float leftPaneFraction = 0.20f;
    float rightPaneFraction = 0.30f;
    float bottomPaneFraction = 0.28f;
    float gameViewWidth = 1.0f;
    float gameViewHeight = 1.0f;
};

struct EditorWindowGeometry
{
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
};

class EditorWorkspaceLayoutPolicy
{
public:
    static constexpr int compactWidthThreshold = 1000;

    // All sizes are SDL logical pixels; work-area dimensions exclude taskbars/docks.
    static EditorWorkspaceLayoutPlan calculate(int workAreaWidth, int workAreaHeight,
        int requestedWidth = 1100, int requestedHeight = 720) noexcept;

    // Bounds a window rectangle to a display's usable logical-pixel area.
    // A window larger than that area is reduced before its position is clamped.
    static EditorWindowGeometry constrainWindow(int areaX, int areaY, int areaWidth, int areaHeight,
        int x, int y, int width, int height) noexcept;
};
}
