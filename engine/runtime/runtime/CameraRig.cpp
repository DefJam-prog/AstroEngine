#include "astro/runtime/CameraRig.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace astro::runtime
{
namespace
{
core::Error cameraError(core::ErrorCode code, const char* message) { return {code, message}; }
bool finite(const scene::Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool validMode(const CameraMode mode)
{
    switch (mode)
    {
    case CameraMode::Custom: case CameraMode::FirstPerson: case CameraMode::ThirdPerson:
    case CameraMode::Isometric: case CameraMode::TopDown: case CameraMode::Platformer:
    case CameraMode::Strategy: return true;
    }
    return false;
}
scene::Vec3 clamp(scene::Vec3 value, const CameraBounds& bounds)
{
    value.x = std::clamp(value.x, bounds.minimum.x, bounds.maximum.x);
    value.y = std::clamp(value.y, bounds.minimum.y, bounds.maximum.y);
    value.z = std::clamp(value.z, bounds.minimum.z, bounds.maximum.z);
    return value;
}
scene::Vec3 interpolate(const scene::Vec3& from, const scene::Vec3& to, const double alpha)
{
    return {from.x + (to.x - from.x) * alpha,
        from.y + (to.y - from.y) * alpha,
        from.z + (to.z - from.z) * alpha};
}
}

core::Result<void> CameraRig::configure(CameraSettings settings)
{
    if (!validMode(settings.mode) || (settings.target && (*settings.target == 0
        || *settings.target == std::numeric_limits<scene::EntityId>::max()))
        || !finite(settings.followOffset) || !finite(settings.rotationDegrees)
        || !std::isfinite(settings.followSharpness) || settings.followSharpness < 0.0
        || settings.followSharpness > 1000.0 || !std::isfinite(settings.zoom)
        || !std::isfinite(settings.minZoom) || !std::isfinite(settings.maxZoom)
        || settings.minZoom <= 0.0 || settings.maxZoom < settings.minZoom
        || settings.zoom < settings.minZoom || settings.zoom > settings.maxZoom)
        return core::Result<void>::failure(cameraError(core::ErrorCode::ValidationError, "Camera settings contain invalid values"));
    if (settings.bounds)
    {
        const auto& bounds = *settings.bounds;
        if (!finite(bounds.minimum) || !finite(bounds.maximum)
            || bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y
            || bounds.minimum.z > bounds.maximum.z)
            return core::Result<void>::failure(cameraError(core::ErrorCode::ValidationError, "Camera bounds are invalid"));
    }
    settings_ = std::move(settings);
    pose_.zoom = settings_.zoom;
    if (!settings_.inheritTargetRotation) pose_.rotationDegrees = settings_.rotationDegrees;
    return core::Result<void>::success();
}

core::Result<void> CameraRig::setMode(const CameraMode mode)
{
    if (!validMode(mode)) return core::Result<void>::failure(cameraError(core::ErrorCode::InvalidArgument, "Unknown camera mode"));
    auto settings = settings_;
    settings.mode = mode;
    settings.inheritTargetRotation = false;
    switch (mode)
    {
    case CameraMode::Custom: settings.followOffset = {}; settings.rotationDegrees = {}; break;
    case CameraMode::FirstPerson: settings.followOffset = {0.0, 1.6, 0.0}; settings.rotationDegrees = {}; settings.inheritTargetRotation = true; break;
    case CameraMode::ThirdPerson: settings.followOffset = {0.0, 2.0, -5.0}; settings.rotationDegrees = {15.0, 0.0, 0.0}; break;
    case CameraMode::Isometric: settings.followOffset = {0.0, 12.0, -12.0}; settings.rotationDegrees = {35.0, 0.0, 0.0}; break;
    case CameraMode::TopDown: settings.followOffset = {0.0, 20.0, 0.0}; settings.rotationDegrees = {-90.0, 0.0, 0.0}; break;
    case CameraMode::Platformer: settings.followOffset = {0.0, 1.0, -10.0}; settings.rotationDegrees = {}; break;
    case CameraMode::Strategy: settings.followOffset = {0.0, 30.0, 0.0}; settings.rotationDegrees = {-90.0, 0.0, 0.0}; break;
    }
    return configure(std::move(settings));
}

core::Result<void> CameraRig::setPose(CameraPose pose)
{
    if (!finite(pose.position) || !finite(pose.rotationDegrees) || !std::isfinite(pose.zoom))
        return core::Result<void>::failure(cameraError(core::ErrorCode::InvalidArgument, "Camera pose must contain finite values"));
    if (pose.zoom < settings_.minZoom || pose.zoom > settings_.maxZoom)
        return core::Result<void>::failure(cameraError(core::ErrorCode::ValidationError, "Camera pose zoom is outside the configured range"));
    if (settings_.bounds) pose.position = clamp(pose.position, *settings_.bounds);
    pose_ = pose;
    settings_.rotationDegrees = pose.rotationDegrees;
    settings_.zoom = pose.zoom;
    return core::Result<void>::success();
}

core::Result<void> CameraRig::setZoom(const double zoom)
{
    if (!std::isfinite(zoom)) return core::Result<void>::failure(cameraError(core::ErrorCode::InvalidArgument, "Camera zoom must be finite"));
    settings_.zoom = std::clamp(zoom, settings_.minZoom, settings_.maxZoom);
    pose_.zoom = settings_.zoom;
    return core::Result<void>::success();
}

core::Result<CameraPose> CameraRig::desiredPose(const scene::Scene& scene) const
{
    if (!settings_.target) return core::Result<CameraPose>::failure(cameraError(core::ErrorCode::InvalidState, "Following camera mode requires a target entity"));
    const auto* transform = scene.transform(*settings_.target);
    if (!transform) return core::Result<CameraPose>::failure(cameraError(core::ErrorCode::NotFound, "Camera target entity or Transform was not found"));
    CameraPose desired;
    desired.position = {transform->value.position.x + settings_.followOffset.x,
        transform->value.position.y + settings_.followOffset.y,
        transform->value.position.z + settings_.followOffset.z};
    desired.rotationDegrees = settings_.inheritTargetRotation ? transform->value.rotationDegrees : settings_.rotationDegrees;
    desired.zoom = settings_.zoom;
    if (settings_.bounds) desired.position = clamp(desired.position, *settings_.bounds);
    return core::Result<CameraPose>::success(desired);
}

core::Result<void> CameraRig::snapToTarget(const scene::Scene& scene)
{
    const auto desired = desiredPose(scene);
    if (!desired) return core::Result<void>::failure(desired.error());
    pose_ = desired.value();
    return core::Result<void>::success();
}

core::Result<void> CameraRig::update(const scene::Scene& scene, const double frameDeltaSeconds)
{
    if (!std::isfinite(frameDeltaSeconds) || frameDeltaSeconds < 0.0)
        return core::Result<void>::failure(cameraError(core::ErrorCode::InvalidArgument, "Camera frame delta must be finite and non-negative"));
    if (settings_.mode == CameraMode::Custom) return core::Result<void>::success();
    const auto desired = desiredPose(scene);
    if (!desired) return core::Result<void>::failure(desired.error());
    if (frameDeltaSeconds == 0.0) return core::Result<void>::success();
    const auto alpha = settings_.followSharpness == 0.0 ? 1.0
        : 1.0 - std::exp(-settings_.followSharpness * frameDeltaSeconds);
    pose_.position = interpolate(pose_.position, desired.value().position, alpha);
    if (settings_.bounds) pose_.position = clamp(pose_.position, *settings_.bounds);
    pose_.rotationDegrees = desired.value().rotationDegrees;
    pose_.zoom = settings_.zoom;
    return core::Result<void>::success();
}
}
