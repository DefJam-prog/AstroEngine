#include "SceneViewCamera3D.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using astro::editor::SceneViewBounds3D;
    using astro::editor::SceneViewCamera3D;

    SceneViewCamera3D camera;
    const auto initial = camera.camera();
    check(initial.position == astro::render3d::Vec3{0.0f, 0.0f, 5.0f}
        && initial.target == astro::render3d::Vec3{0.0f, 0.0f, 0.0f},
        "default camera retains the previous Scene View pose");

    check(camera.orbitPixels(100.0f, -20.0f), "finite orbit input is accepted");
    check(std::abs(camera.yawRadians()) > 0.1f && camera.pitchRadians() < 0.0f,
        "orbit input changes yaw and pitch");
    check(camera.orbitPixels(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest())
        && std::isfinite(camera.yawRadians()) && std::isfinite(camera.pitchRadians()),
        "extreme finite orbit deltas are bounded without producing NaN");
    check(!camera.orbitPixels(INFINITY, 1.0f), "non-finite orbit input is rejected");

    const auto focusBeforePan = camera.focus();
    check(camera.panPixels(12.0f, -8.0f, 360.0f), "finite pan input is accepted");
    check(camera.focus() != focusBeforePan, "pan changes the focus point");
    check(!camera.panPixels(1.0f, 1.0f, 0.0f), "zero viewport height is rejected");
    check(camera.panPixels(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), 1.0f)
        && std::isfinite(camera.focus().x) && std::isfinite(camera.focus().y) && std::isfinite(camera.focus().z),
        "extreme finite pan deltas keep the camera focus finite");

    check(camera.zoomWheel(1.0f), "finite wheel zoom is accepted");
    check(camera.distance() < 5.0f && camera.distance() >= 0.05f, "wheel-up zoom moves closer within limits");
    check(camera.zoomWheel(-100000.0f) && camera.distance() == 1'000'000.0f,
        "large zoom-out input clamps to the maximum camera distance");
    check(!camera.zoomWheel(NAN), "non-finite zoom input is rejected");

    const SceneViewBounds3D bounds{{-2.0f, -1.0f, -3.0f}, {2.0f, 1.0f, 3.0f}};
    check(camera.frame(bounds, 16.0f / 9.0f), "valid 3D bounds can be framed");
    check(camera.focus() == astro::render3d::Vec3{0.0f, 0.0f, 0.0f}
        && camera.distance() > 6.0f && camera.distance() < 20.0f,
        "frame centers the camera and fits the complete bounds with margin");
    const auto fitted = camera.camera();
    check(fitted.viewMatrix().hasValue() && fitted.projectionMatrix(16.0f / 9.0f).hasValue(),
        "fitted camera produces valid view and projection matrices");
    check(!camera.frame({{2.0f, 0.0f, 0.0f}, {-2.0f, 1.0f, 1.0f}}, 1.0f),
        "inverted bounds are rejected");
    check(!camera.frame(bounds, 0.0f), "invalid aspect ratio is rejected");
    check(!camera.frame({{-1.0e20f, 0.0f, 0.0f}, {1.0e20f, 1.0f, 1.0f}}, 1.0f),
        "unrepresentable bounds are rejected without corrupting the camera");
    check(!camera.frame({{-10'000'001.0f, 0.0f, 0.0f}, {10'000'001.0f, 1.0f, 1.0f}}, 1.0f),
        "bounds outside the configured Scene View coordinate range are rejected");
    check(camera.focus() == astro::render3d::Vec3{0.0f, 0.0f, 0.0f},
        "rejected frame request preserves the previous camera state");

    if (failures == 0) std::cout << "Scene View 3D camera checks passed\n";
    return failures == 0 ? 0 : 1;
}
