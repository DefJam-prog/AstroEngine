#pragma once

#include "astro/core/Result.hpp"
#include "astro/render3d/Camera3D.hpp"

#include <cstdint>
#include <compare>
#include <utility>
#include <vector>

namespace astro::render3d
{
struct Color8 final
{
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    friend bool operator==(const Color8&, const Color8&) = default;
};

struct Vec2 final
{
    float x = 0.0f;
    float y = 0.0f;
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

struct MeshVertex final
{
    Vec3 position{};
    Color8 color{};
    Vec3 normal{};
    Vec2 texCoord0{};
};

class MeshData final
{
public:
    static core::Result<MeshData> create(std::vector<MeshVertex> vertices,
        std::vector<std::uint32_t> triangleIndices, bool hasNormals = false, bool hasTexCoords = false);
    [[nodiscard]] const std::vector<MeshVertex>& vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<std::uint32_t>& triangleIndices() const noexcept { return triangleIndices_; }
    [[nodiscard]] bool hasNormals() const noexcept { return hasNormals_; }
    [[nodiscard]] bool hasTexCoords() const noexcept { return hasTexCoords_; }
    /// Stable content key for renderer resource caches; not a security digest.
    [[nodiscard]] std::uint64_t contentHash() const noexcept { return contentHash_; }
    [[nodiscard]] static MeshData makeColoredCube();
    [[nodiscard]] static core::Result<MeshData> makePrimitive(std::uint8_t primitive);

private:
    MeshData(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices,
        const bool hasNormals, const bool hasTexCoords, const std::uint64_t contentHash)
        : vertices_(std::move(vertices)), triangleIndices_(std::move(indices)),
          hasNormals_(hasNormals), hasTexCoords_(hasTexCoords), contentHash_(contentHash) {}
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> triangleIndices_;
    bool hasNormals_ = false;
    bool hasTexCoords_ = false;
    std::uint64_t contentHash_ = 0;
};
}
