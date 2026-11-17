#include "astro/render3d/MeshData.hpp"

#include <cmath>

namespace astro::render3d
{
core::Result<MeshData> MeshData::create(std::vector<MeshVertex> vertices,
    std::vector<std::uint32_t> triangleIndices)
{
    if (vertices.empty() || vertices.size() > 1'000'000 || triangleIndices.empty()
        || triangleIndices.size() > 3'000'000 || triangleIndices.size() % 3 != 0)
        return core::Result<MeshData>::failure({core::ErrorCode::ValidationError,
            "Mesh requires bounded non-empty vertices and triangle indices"});
    for (const auto& vertex : vertices)
    {
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y)
            || !std::isfinite(vertex.position.z))
            return core::Result<MeshData>::failure({core::ErrorCode::ValidationError,
                "Mesh positions must be finite"});
    }
    for (const auto index : triangleIndices)
        if (index >= vertices.size())
            return core::Result<MeshData>::failure({core::ErrorCode::ValidationError,
                "Mesh triangle index is outside the vertex array"});
    return core::Result<MeshData>::success(MeshData(std::move(vertices), std::move(triangleIndices)));
}

MeshData MeshData::makeColoredCube()
{
    std::vector<MeshVertex> vertices{
        {{-1, 1, 1}, {0, 0, 0, 255}}, {{1, 1, 1}, {255, 0, 0, 255}},
        {{-1, -1, 1}, {0, 255, 0, 255}}, {{1, -1, 1}, {255, 255, 0, 255}},
        {{-1, 1, -1}, {0, 0, 255, 255}}, {{1, 1, -1}, {255, 0, 255, 255}},
        {{-1, -1, -1}, {0, 255, 255, 255}}, {{1, -1, -1}, {255, 255, 255, 255}}};
    std::vector<std::uint32_t> indices{
        0, 1, 2, 1, 3, 2, 4, 6, 5, 5, 6, 7,
        0, 2, 4, 4, 2, 6, 1, 5, 3, 5, 7, 3,
        0, 4, 1, 4, 5, 1, 2, 3, 6, 6, 3, 7};
    auto result = create(std::move(vertices), std::move(indices));
    return std::move(result).value();
}
}
