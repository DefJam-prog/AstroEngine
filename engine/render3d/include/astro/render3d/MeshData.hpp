#pragma once

#include "astro/core/Result.hpp"
#include "astro/render3d/Camera3D.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace astro::render3d
{
struct Color8 final
{
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    friend bool operator==(const Color8&, const Color8&) = default;
};

struct MeshVertex final
{
    Vec3 position{};
    Color8 color{};
};

class MeshData final
{
public:
    static core::Result<MeshData> create(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> triangleIndices);
    [[nodiscard]] const std::vector<MeshVertex>& vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<std::uint32_t>& triangleIndices() const noexcept { return triangleIndices_; }
    [[nodiscard]] static MeshData makeColoredCube();

private:
    MeshData(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices)
        : vertices_(std::move(vertices)), triangleIndices_(std::move(indices)) {}
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> triangleIndices_;
};
}
