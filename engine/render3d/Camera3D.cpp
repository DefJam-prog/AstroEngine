#include "astro/render3d/Camera3D.hpp"

#include <cmath>
#include <string>

namespace astro::render3d
{
namespace
{
core::Error invalid(const char* message)
{
    return {core::ErrorCode::ValidationError, message};
}

bool finite(const Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const Mat4& matrix)
{
    for (const float value : matrix.values)
        if (!std::isfinite(value)) return false;
    return true;
}

Vec3 subtract(const Vec3 a, const Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(const Vec3 a, const Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3 a, const Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool normalize(Vec3& value)
{
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f) return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    value = {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
    return finite(value);
}
}

core::Result<Mat4> Camera3D::viewMatrix() const
{
    if (!finite(position) || !finite(target) || !finite(up))
        return core::Result<Mat4>::failure(invalid("Camera vectors must be finite"));
    Vec3 forward = subtract(target, position);
    if (!normalize(forward))
        return core::Result<Mat4>::failure(invalid("Camera position and target must differ"));
    Vec3 right = cross(forward, up);
    if (!normalize(right))
        return core::Result<Mat4>::failure(invalid("Camera up vector must not be parallel to its view direction"));
    Vec3 correctedUp = cross(right, forward);
    if (!normalize(correctedUp))
        return core::Result<Mat4>::failure(invalid("Camera basis is degenerate"));

    Mat4 matrix;
    matrix.values = {
        right.x, correctedUp.x, -forward.x, 0.0f,
        right.y, correctedUp.y, -forward.y, 0.0f,
        right.z, correctedUp.z, -forward.z, 0.0f,
        -dot(right, position), -dot(correctedUp, position), dot(forward, position), 1.0f};
    if (!finite(matrix))
        return core::Result<Mat4>::failure(invalid("Camera view matrix exceeds finite numeric range"));
    return core::Result<Mat4>::success(matrix);
}

core::Result<Mat4> Camera3D::projectionMatrix(const float aspectRatio, const ClipDepthRange depthRange) const
{
    constexpr float pi = 3.14159265358979323846f;
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0f || aspectRatio > 1000.0f
        || !std::isfinite(verticalFovRadians) || verticalFovRadians <= 0.01f
        || verticalFovRadians >= pi - 0.01f || !std::isfinite(nearPlane)
        || !std::isfinite(farPlane) || nearPlane <= 0.0f || farPlane <= nearPlane
        || farPlane > 1.0e9f)
        return core::Result<Mat4>::failure(invalid("Camera projection values are outside supported finite bounds"));
    if (depthRange != ClipDepthRange::ZeroToOne && depthRange != ClipDepthRange::NegativeOneToOne)
        return core::Result<Mat4>::failure(invalid("Camera clip-depth range is invalid"));

    const float yScale = 1.0f / std::tan(verticalFovRadians * 0.5f);
    const float xScale = yScale / aspectRatio;
    const float zScale = depthRange == ClipDepthRange::ZeroToOne
        ? farPlane / (nearPlane - farPlane)
        : (farPlane + nearPlane) / (nearPlane - farPlane);
    const float zOffset = depthRange == ClipDepthRange::ZeroToOne
        ? (farPlane * nearPlane) / (nearPlane - farPlane)
        : (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    Mat4 matrix;
    matrix.values = {xScale, 0.0f, 0.0f, 0.0f,
        0.0f, yScale, 0.0f, 0.0f,
        0.0f, 0.0f, zScale, -1.0f,
        0.0f, 0.0f, zOffset, 0.0f};
    if (!finite(matrix))
        return core::Result<Mat4>::failure(invalid("Camera projection matrix exceeds finite numeric range"));
    return core::Result<Mat4>::success(matrix);
}
}
