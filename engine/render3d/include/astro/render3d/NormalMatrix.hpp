#pragma once

#include "astro/core/Result.hpp"
#include "astro/render3d/Camera3D.hpp"

#include <array>

namespace astro::render3d
{
// Column-major 3x3 inverse-transpose of the upper-left linear portion of a world matrix.
struct NormalMatrix3 final
{
    std::array<float, 9> values{};
    friend bool operator==(const NormalMatrix3&, const NormalMatrix3&) = default;
};

[[nodiscard]] core::Result<NormalMatrix3> inverseTransposeNormalMatrix(const Mat4& worldTransform);
}
