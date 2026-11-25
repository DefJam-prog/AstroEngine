#include "astro/render3d/NormalMatrix.hpp"

#include <cmath>

namespace astro::render3d
{
core::Result<NormalMatrix3> inverseTransposeNormalMatrix(const Mat4& worldTransform)
{
    // Convert the column-major 4x4 storage to a row-addressable 3x3 linear matrix.
    float a[3][3]{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            a[row][column] = worldTransform.values[column * 4 + row];

    const float c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    const float c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    const float c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    const float determinant = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8f)
        return core::Result<NormalMatrix3>::failure({core::ErrorCode::ValidationError,
            "Cannot transform mesh normals through a singular or non-finite world transform"});

    const float inverseDeterminant = 1.0f / determinant;
    const float inverse[3][3]{
        {c00, a[0][2] * a[2][1] - a[0][1] * a[2][2], a[0][1] * a[1][2] - a[0][2] * a[1][1]},
        {c01, a[0][0] * a[2][2] - a[0][2] * a[2][0], a[0][2] * a[1][0] - a[0][0] * a[1][2]},
        {c02, a[0][1] * a[2][0] - a[0][0] * a[2][1], a[0][0] * a[1][1] - a[0][1] * a[1][0]}};
    NormalMatrix3 result;
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
        {
            const float value = inverse[column][row] * inverseDeterminant;
            if (!std::isfinite(value))
                return core::Result<NormalMatrix3>::failure({core::ErrorCode::ValidationError,
                    "World transform produced a non-finite mesh normal matrix"});
            result.values[column * 3 + row] = value;
        }
    return core::Result<NormalMatrix3>::success(result);
}
}
