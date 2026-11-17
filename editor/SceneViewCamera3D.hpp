#pragma once

#include "astro/render3d/Camera3D.hpp"

namespace astro::editor
{
struct SceneViewBounds3D final
{
    render3d::Vec3 minimum{};
    render3d::Vec3 maximum{};
};

/// Stateful editor-only orbit camera. Mouse input is supplied only while the
/// Scene View image region is hovered; it has no SDL, ImGui or bgfx dependency.
class SceneViewCamera3D final
{
public:
    [[nodiscard]] bool orbitPixels(float deltaX, float deltaY) noexcept;
    [[nodiscard]] bool panPixels(float deltaX, float deltaY, float viewportHeight) noexcept;
    [[nodiscard]] bool zoomWheel(float wheelDelta) noexcept;
    [[nodiscard]] bool frame(const SceneViewBounds3D& bounds, float aspectRatio) noexcept;

    [[nodiscard]] render3d::Camera3D camera() const noexcept;
    [[nodiscard]] render3d::Vec3 focus() const noexcept { return focus_; }
    [[nodiscard]] float distance() const noexcept { return distance_; }
    [[nodiscard]] float yawRadians() const noexcept { return yawRadians_; }
    [[nodiscard]] float pitchRadians() const noexcept { return pitchRadians_; }

private:
    render3d::Vec3 focus_{0.0f, 0.0f, 0.0f};
    float distance_ = 5.0f;
    float yawRadians_ = 0.0f;
    float pitchRadians_ = 0.0f;
    float frameRadius_ = 0.0f;
};
}
