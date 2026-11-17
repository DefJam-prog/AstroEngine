#pragma once

#include "astro/core/Result.hpp"

#include <array>

namespace astro::render3d
{
struct Vec3 final
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    friend bool operator==(const Vec3&, const Vec3&) = default;
};

// Matrices are column-major, right-handed, and suitable for bgfx::setViewTransform.
struct Mat4 final
{
    std::array<float, 16> values{};
    friend bool operator==(const Mat4&, const Mat4&) = default;
};

enum class ClipDepthRange { ZeroToOne, NegativeOneToOne };

struct Camera3D final
{
    Vec3 position{0.0f, 0.0f, 5.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float verticalFovRadians = 1.0471975512f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    [[nodiscard]] core::Result<Mat4> viewMatrix() const;
    [[nodiscard]] core::Result<Mat4> projectionMatrix(float aspectRatio,
        ClipDepthRange depthRange = ClipDepthRange::ZeroToOne) const;
};
}
