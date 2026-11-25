#include "EditorUiScalePolicy.hpp"

#include <algorithm>
#include <cmath>

namespace astro::editor
{
float EditorUiScalePolicy::normalizeScale(const float displayScale) noexcept
{
    return std::isfinite(displayScale) && displayScale > 0.0f
        ? std::clamp(displayScale, 1.0f, 4.0f) : 1.0f;
}

EditorUiScalePolicy::EditorUiScalePolicy(const ImGuiStyle& baseStyle) noexcept
    : baseStyle_(baseStyle)
{
}

bool EditorUiScalePolicy::apply(const float displayScale, ImGuiStyle& style) noexcept
{
    const auto validScale = normalizeScale(displayScale);
    if (std::abs(validScale - appliedScale_) < 0.001f) return false;

    style = baseStyle_;
    style.ScaleAllSizes(validScale);
    style.FontScaleMain = validScale;
    appliedScale_ = validScale;
    return true;
}
}
