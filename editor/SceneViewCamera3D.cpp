#include "SceneViewCamera3D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace astro::editor
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
constexpr float MinDistance = 0.05f;
constexpr float MaxDistance = 1'000'000.0f;
constexpr float MaxCoordinate = 10'000'000.0f;

bool finite(const render3d::Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float clampCoordinate(const float value) noexcept
{
    return std::clamp(value, -MaxCoordinate, MaxCoordinate);
}
}

bool SceneViewCamera3D::orbitPixels(const float deltaX, const float deltaY) noexcept
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return false;
    constexpr float radiansPerPixel = 0.008f;
    const float boundedDeltaX = std::clamp(deltaX, -1'000'000.0f, 1'000'000.0f);
    const float boundedDeltaY = std::clamp(deltaY, -1'000'000.0f, 1'000'000.0f);
    yawRadians_ = std::remainder(yawRadians_ + boundedDeltaX * radiansPerPixel, 2.0f * Pi);
    pitchRadians_ = std::clamp(pitchRadians_ + boundedDeltaY * radiansPerPixel, -1.553343f, 1.553343f);
    return true;
}

bool SceneViewCamera3D::panPixels(const float deltaX, const float deltaY,
    const float viewportHeight) noexcept
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(viewportHeight)
        || viewportHeight < 1.0f) return false;
    const float boundedDeltaX = std::clamp(deltaX, -1'000'000.0f, 1'000'000.0f);
    const float boundedDeltaY = std::clamp(deltaY, -1'000'000.0f, 1'000'000.0f);
    const float unitsPerPixel = 2.0f * distance_ * std::tan(Pi / 6.0f) / viewportHeight;
    const float sineYaw = std::sin(yawRadians_), cosineYaw = std::cos(yawRadians_);
    const float sinePitch = std::sin(pitchRadians_), cosinePitch = std::cos(pitchRadians_);
    const render3d::Vec3 right{cosineYaw, 0.0f, -sineYaw};
    const render3d::Vec3 up{-sineYaw * sinePitch, cosinePitch, -cosineYaw * sinePitch};
    focus_.x = clampCoordinate(focus_.x + (-boundedDeltaX * right.x + boundedDeltaY * up.x) * unitsPerPixel);
    focus_.y = clampCoordinate(focus_.y + (-boundedDeltaX * right.y + boundedDeltaY * up.y) * unitsPerPixel);
    focus_.z = clampCoordinate(focus_.z + (-boundedDeltaX * right.z + boundedDeltaY * up.z) * unitsPerPixel);
    return true;
}

bool SceneViewCamera3D::zoomWheel(const float wheelDelta) noexcept
{
    if (!std::isfinite(wheelDelta)) return false;
    const float exponent = std::clamp(-wheelDelta * 0.12f, -20.0f, 20.0f);
    distance_ = std::clamp(distance_ * std::exp(exponent), MinDistance, MaxDistance);
    return true;
}

bool SceneViewCamera3D::frame(const SceneViewBounds3D& bounds, const float aspectRatio) noexcept
{
    if (!finite(bounds.minimum) || !finite(bounds.maximum)
        || std::abs(bounds.minimum.x) > MaxCoordinate || std::abs(bounds.minimum.y) > MaxCoordinate
        || std::abs(bounds.minimum.z) > MaxCoordinate || std::abs(bounds.maximum.x) > MaxCoordinate
        || std::abs(bounds.maximum.y) > MaxCoordinate || std::abs(bounds.maximum.z) > MaxCoordinate
        || !std::isfinite(aspectRatio) || aspectRatio < 0.001f || aspectRatio > 1000.0f
        || bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y
        || bounds.minimum.z > bounds.maximum.z)
        return false;
    const render3d::Vec3 extents{
        bounds.maximum.x - bounds.minimum.x,
        bounds.maximum.y - bounds.minimum.y,
        bounds.maximum.z - bounds.minimum.z};
    if (!finite(extents)) return false;
    const render3d::Vec3 center{
        bounds.minimum.x + extents.x * 0.5f,
        bounds.minimum.y + extents.y * 0.5f,
        bounds.minimum.z + extents.z * 0.5f};
    const float radius = std::max(0.25f, 0.5f * std::sqrt(
        extents.x * extents.x + extents.y * extents.y + extents.z * extents.z));
    if (!finite(center) || !std::isfinite(radius)) return false;

    constexpr float verticalFov = Pi / 3.0f;
    const float horizontalHalfFov = std::atan(std::tan(verticalFov * 0.5f) * aspectRatio);
    const float limitingHalfFov = std::min(verticalFov * 0.5f, horizontalHalfFov);
    const float fittedDistance = radius * 1.2f / std::sin(limitingHalfFov);
    if (!std::isfinite(fittedDistance) || fittedDistance > MaxDistance) return false;

    focus_ = center;
    frameRadius_ = radius;
    distance_ = std::clamp(fittedDistance, MinDistance, MaxDistance);
    return true;
}

render3d::Camera3D SceneViewCamera3D::camera() const noexcept
{
    const float horizontal = distance_ * std::cos(pitchRadians_);
    render3d::Camera3D result;
    result.target = focus_;
    result.position = {
        focus_.x + horizontal * std::sin(yawRadians_),
        focus_.y + distance_ * std::sin(pitchRadians_),
        focus_.z + horizontal * std::cos(yawRadians_)};
    result.verticalFovRadians = Pi / 3.0f;
    result.nearPlane = std::max(0.01f, distance_ * 0.001f);
    result.farPlane = std::clamp(distance_ + std::max(frameRadius_ * 2.0f, distance_), 1000.0f, 1.0e9f);
    return result;
}
}
