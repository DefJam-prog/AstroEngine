#include "astro/runtime/CameraRig.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
}

int main()
{
    using namespace astro;
    scene::Scene scene{1, "Camera test"};
    const auto created = scene.createEntity("Target");
    check(created.hasValue(), "create camera target");
    if (!created) return EXIT_FAILURE;
    const auto target = created.value();
    scene::Transform transform = scene.transform(target)->value;
    transform.position = {4.0, 5.0, 6.0};
    transform.rotationDegrees = {10.0, 25.0, 0.0};
    check(static_cast<bool>(scene.setTransform(target, transform)), "set target transform");

    runtime::CameraRig rig;
    runtime::CameraRig customRig;
    const runtime::CameraPose customPose{{3.0, 4.0, 5.0}, {0.0, 45.0, 0.0}, 2.0};
    check(static_cast<bool>(customRig.setPose(customPose)) && customRig.pose() == customPose
        && static_cast<bool>(customRig.update(scene, 0.1)) && customRig.pose() == customPose,
        "custom camera accepts a configured pose without a follow target");
    auto invalidPose = customPose;
    invalidPose.position.x = std::numeric_limits<double>::quiet_NaN();
    check(!customRig.setPose(invalidPose), "reject non-finite camera pose");

    check(static_cast<bool>(rig.setMode(runtime::CameraMode::TopDown)), "select top-down camera preset");
    auto settings = rig.settings();
    settings.target = target;
    settings.zoom = 1.5;
    settings.minZoom = 0.5;
    settings.maxZoom = 3.0;
    check(static_cast<bool>(rig.configure(settings)), "configure target, zoom and bounds");
    check(static_cast<bool>(rig.snapToTarget(scene)), "snap camera to target");
    check(rig.pose().position == scene::Vec3{4.0, 25.0, 6.0}
        && rig.pose().rotationDegrees == scene::Vec3{-90.0, 0.0, 0.0}
        && near(rig.pose().zoom, 1.5), "top-down preset produces documented camera pose");

    transform.position.x = 20.0;
    check(static_cast<bool>(scene.setTransform(target, transform)), "move followed target");
    settings = rig.settings();
    settings.followOffset = {0.0, 0.0, -2.0};
    settings.followSharpness = 2.0;
    check(static_cast<bool>(rig.configure(settings)), "set custom follow offset and smoothing");
    check(static_cast<bool>(rig.update(scene, 0.1)), "update camera with fixed frame delta");
    const auto expectedAlpha = 1.0 - std::exp(-0.2);
    check(near(rig.pose().position.x, 4.0 + (20.0 - 4.0) * expectedAlpha)
        && near(rig.pose().position.z, 6.0 + (4.0 - 6.0) * expectedAlpha),
        "exponential smoothing is frame-delta aware");
    const auto beforeZeroDelta = rig.pose();
    check(static_cast<bool>(rig.update(scene, 0.0)) && rig.pose() == beforeZeroDelta,
        "zero frame delta leaves camera pose unchanged");
    check(!rig.update(scene, -0.1) && !rig.update(scene, std::numeric_limits<double>::infinity()),
        "reject invalid camera frame deltas");

    settings = rig.settings();
    settings.bounds = runtime::CameraBounds{{0.0, 0.0, -20.0}, {10.0, 30.0, 20.0}};
    settings.followSharpness = 0.0;
    check(static_cast<bool>(rig.configure(settings)) && static_cast<bool>(rig.snapToTarget(scene)),
        "apply camera position bounds");
    check(rig.pose().position == scene::Vec3{10.0, 5.0, 4.0}, "clamp desired camera location to configured bounds");
    check(static_cast<bool>(rig.setZoom(100.0)) && near(rig.pose().zoom, 3.0), "clamp zoom to configured range");
    check(!rig.setZoom(std::numeric_limits<double>::quiet_NaN()), "reject non-finite zoom");

    settings = rig.settings();
    settings.bounds.reset();
    check(static_cast<bool>(rig.configure(settings))
        && static_cast<bool>(rig.setMode(runtime::CameraMode::FirstPerson))
        && static_cast<bool>(rig.snapToTarget(scene)), "switch camera mode and snap again");
    check(rig.pose().position == scene::Vec3{20.0, 6.6, 6.0}
        && rig.pose().rotationDegrees == transform.rotationDegrees,
        "first-person preset follows target position and rotation");
    check(static_cast<bool>(rig.setMode(runtime::CameraMode::Isometric))
        && rig.settings().followOffset == scene::Vec3{0.0, 12.0, -12.0},
        "switching mode installs its default offset while retaining target");

    const auto beforeMissingTarget = rig.pose();
    settings = rig.settings();
    settings.target = 999;
    check(static_cast<bool>(rig.configure(settings)), "configure missing target ID for diagnostic test");
    const auto missing = rig.snapToTarget(scene);
    check(!missing && missing.error().code == core::ErrorCode::NotFound && rig.pose() == beforeMissingTarget,
        "missing target is reported without changing camera pose");
    settings = rig.settings();
    settings.target = target;
    settings.bounds = runtime::CameraBounds{{3.0, 0.0, 0.0}, {2.0, 1.0, 1.0}};
    check(!rig.configure(settings), "reject inverted camera bounds");
    settings.bounds.reset();
    settings.minZoom = 4.0;
    settings.maxZoom = 2.0;
    check(!rig.configure(settings), "reject inverted zoom limits");

    runtime::CameraRig unconfigured;
    check(!unconfigured.snapToTarget(scene), "unconfigured following rig requires a target");
    if (failures == 0) std::cout << "All camera rig tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
