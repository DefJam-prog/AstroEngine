#include "EditorUiScalePolicy.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
bool near(const float lhs, const float rhs) noexcept
{
    return std::abs(lhs - rhs) < 0.001f;
}
}

int main()
{
    ImGuiStyle base;
    base.WindowPadding = ImVec2(8.0f, 6.0f);
    base.FramePadding = ImVec2(4.0f, 3.0f);
    base.ItemSpacing = ImVec2(6.0f, 5.0f);
    base.FontScaleMain = 1.0f;
    ImGuiStyle style = base;
    astro::editor::EditorUiScalePolicy policy(base);

    if (!policy.apply(1.0f, style) || !near(policy.appliedScale(), 1.0f)
        || !near(style.FontScaleMain, 1.0f) || !near(style.WindowPadding.x, 8.0f))
    {
        std::fputs("100% scale was not applied from the base style\n", stderr);
        return 1;
    }
    if (!policy.apply(1.25f, style) || !near(policy.appliedScale(), 1.25f)
        || !near(style.FontScaleMain, 1.25f) || !near(style.WindowPadding.x, 10.0f)
        || !near(style.FramePadding.x, 5.0f))
    {
        std::fputs("125% scale did not scale font and style sizes\n", stderr);
        return 1;
    }
    if (policy.apply(1.25f, style) || !near(style.WindowPadding.x, 10.0f))
    {
        std::fputs("Repeated scale application changed style sizes\n", stderr);
        return 1;
    }
    if (!policy.apply(1.5f, style) || !near(style.WindowPadding.x, 12.0f)
        || !near(style.FontScaleMain, 1.5f))
    {
        std::fputs("Changing scale accumulated on top of the previous scale\n", stderr);
        return 1;
    }
    if (!policy.apply(1.0f, style) || !near(style.WindowPadding.x, 8.0f)
        || !near(style.FramePadding.x, 4.0f) || !near(style.FontScaleMain, 1.0f))
    {
        std::fputs("Returning to 100% did not restore the base style\n", stderr);
        return 1;
    }
    if (policy.apply(std::numeric_limits<float>::quiet_NaN(), style)
        || !near(policy.appliedScale(), 1.0f) || !near(style.WindowPadding.x, 8.0f))
    {
        std::fputs("Invalid scale did not safely retain the 100% fallback\n", stderr);
        return 1;
    }
    if (policy.apply(0.75f, style) || !near(policy.appliedScale(), 1.0f)
        || !near(style.WindowPadding.x, 8.0f))
    {
        std::fputs("Scale below 100% did not clamp to the supported minimum\n", stderr);
        return 1;
    }
    if (!policy.apply(8.0f, style) || !near(policy.appliedScale(), 4.0f)
        || !near(style.FontScaleMain, 4.0f))
    {
        std::fputs("Scale above the supported bound was not clamped\n", stderr);
        return 1;
    }
    std::puts("Editor UI scale policy tests passed");
    return 0;
}
