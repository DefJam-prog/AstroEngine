#pragma once

#include <imgui.h>

namespace astro::editor
{
class EditorUiScalePolicy
{
public:
    explicit EditorUiScalePolicy(const ImGuiStyle& baseStyle) noexcept;

    // Rebuilds size fields from the original style whenever the SDL display scale changes.
    // Returns true only when a new scale was applied.
    bool apply(float displayScale, ImGuiStyle& style) noexcept;
    static float normalizeScale(float displayScale) noexcept;

    float appliedScale() const noexcept { return appliedScale_; }
    const ImGuiStyle& baseStyle() const noexcept { return baseStyle_; }

private:
    ImGuiStyle baseStyle_;
    float appliedScale_ = 0.0f;
};
}
