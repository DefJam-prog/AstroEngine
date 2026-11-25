#include "astro/render3d/MeshData.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <numbers>

namespace astro::render3d
{
namespace
{
std::uint64_t meshContentHash(const std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices, const bool hasNormals, const bool hasTexCoords)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto addByte = [&hash](const std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    const auto addU32 = [&addByte](const std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            addByte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    };
    addByte(static_cast<std::uint8_t>(hasNormals));
    addByte(static_cast<std::uint8_t>(hasTexCoords));
    for (const auto& vertex : vertices)
    {
        addU32(std::bit_cast<std::uint32_t>(vertex.position.x));
        addU32(std::bit_cast<std::uint32_t>(vertex.position.y));
        addU32(std::bit_cast<std::uint32_t>(vertex.position.z));
        addByte(vertex.color.r); addByte(vertex.color.g); addByte(vertex.color.b); addByte(vertex.color.a);
        addU32(std::bit_cast<std::uint32_t>(vertex.normal.x));
        addU32(std::bit_cast<std::uint32_t>(vertex.normal.y));
        addU32(std::bit_cast<std::uint32_t>(vertex.normal.z));
        addU32(std::bit_cast<std::uint32_t>(vertex.texCoord0.x));
        addU32(std::bit_cast<std::uint32_t>(vertex.texCoord0.y));
    }
    for (const auto index : indices) addU32(index);
    return hash;
}
}

core::Result<MeshData> MeshData::create(std::vector<MeshVertex> vertices,
    std::vector<std::uint32_t> triangleIndices, const bool hasNormals, const bool hasTexCoords)
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
        if (hasNormals && (!std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y)
                || !std::isfinite(vertex.normal.z)))
            return core::Result<MeshData>::failure({core::ErrorCode::ValidationError,
                "Mesh normals must be finite when the normal channel is present"});
        if (hasTexCoords && (!std::isfinite(vertex.texCoord0.x) || !std::isfinite(vertex.texCoord0.y)))
            return core::Result<MeshData>::failure({core::ErrorCode::ValidationError,
                "Mesh texture coordinates must be finite when the texCoord0 channel is present"});
    }
    for (const auto index : triangleIndices)
        if (index >= vertices.size())
            return core::Result<MeshData>::failure({core::ErrorCode::ValidationError,
                "Mesh triangle index is outside the vertex array"});
    const auto hash = meshContentHash(vertices, triangleIndices, hasNormals, hasTexCoords);
    return core::Result<MeshData>::success(MeshData(std::move(vertices), std::move(triangleIndices),
        hasNormals, hasTexCoords, hash));
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

core::Result<MeshData> MeshData::makePrimitive(const std::uint8_t primitive)
{
    constexpr Color8 white{};
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    const auto add = [&](const float x, const float y, const float z,
                         const float nx, const float ny, const float nz,
                         const float u, const float v) {
        vertices.push_back({{x, y, z}, white, {nx, ny, nz}, {u, v}});
    };
    if (primitive == 1) // Cube, unit side length.
    {
        constexpr float p = 0.5f;
        struct Face { float nx, ny, nz; std::array<std::array<float, 3>, 4> p; };
        constexpr Face faces[]{
            {0,0,1, {{{-p,-p,p},{p,-p,p},{p,p,p},{-p,p,p}}}},
            {0,0,-1, {{{p,-p,-p},{-p,-p,-p},{-p,p,-p},{p,p,-p}}}},
            {1,0,0, {{{p,-p,p},{p,-p,-p},{p,p,-p},{p,p,p}}}},
            {-1,0,0, {{{-p,-p,-p},{-p,-p,p},{-p,p,p},{-p,p,-p}}}},
            {0,1,0, {{{-p,p,p},{p,p,p},{p,p,-p},{-p,p,-p}}}},
            {0,-1,0, {{{-p,-p,-p},{p,-p,-p},{p,-p,p},{-p,-p,p}}}}
        };
        for (const auto& face : faces)
        {
            const auto base = static_cast<std::uint32_t>(vertices.size());
            for (std::size_t i = 0; i < 4; ++i)
                add(face.p[i][0], face.p[i][1], face.p[i][2], face.nx, face.ny, face.nz,
                    (i == 1 || i == 2) ? 1.0f : 0.0f, (i >= 2) ? 1.0f : 0.0f);
            indices.insert(indices.end(), {base, base+1, base+2, base, base+2, base+3});
        }
    }
    else if (primitive == 2) // Plane in XZ, facing +Y.
    {
        add(-0.5f,0,-0.5f, 0,1,0, 0,0); add(0.5f,0,-0.5f, 0,1,0, 1,0);
        add(0.5f,0,0.5f, 0,1,0, 1,1); add(-0.5f,0,0.5f, 0,1,0, 0,1);
        indices = {0,2,1, 0,3,2};
    }
    else if (primitive == 3) // UV sphere.
    {
        constexpr std::uint32_t rings = 16, segments = 24;
        for (std::uint32_t ring = 0; ring <= rings; ++ring)
        {
            const float v = float(ring) / float(rings);
            const float latitude = std::numbers::pi_v<float> * (v - 0.5f);
            const float y = std::sin(latitude), radius = std::cos(latitude);
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float u = float(segment) / float(segments);
                const float angle = u * 2.0f * std::numbers::pi_v<float>;
                const float x = radius * std::cos(angle), z = radius * std::sin(angle);
                add(x, y, z, x, y, z, u, v);
            }
        }
        for (std::uint32_t ring = 0; ring < rings; ++ring)
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const auto a = ring * (segments + 1) + segment, b = a + segments + 1;
                indices.insert(indices.end(), {a,b,a+1, a+1,b,b+1});
            }
    }
    else if (primitive == 4) // Cylinder, Y axis, radius and height 1.
    {
        constexpr std::uint32_t segments = 24;
        for (std::uint32_t i = 0; i <= segments; ++i)
        {
            const float u = float(i) / float(segments), angle = u * 2.0f * std::numbers::pi_v<float>;
            const float x = std::cos(angle), z = std::sin(angle);
            add(x*0.5f,-0.5f,z*0.5f,x,0,z,u,0); add(x*0.5f,0.5f,z*0.5f,x,0,z,u,1);
        }
        for (std::uint32_t i = 0; i < segments; ++i)
        {
            const auto a = i * 2;
            indices.insert(indices.end(), {a,a+1,a+2, a+2,a+1,a+3});
        }
        for (const float y : {-0.5f, 0.5f})
        {
            const auto center = static_cast<std::uint32_t>(vertices.size());
            add(0,y,0,0,y > 0 ? 1.0f : -1.0f,0,0.5f,0.5f);
            for (std::uint32_t i = 0; i <= segments; ++i)
            {
                const float angle = float(i) / float(segments) * 2.0f * std::numbers::pi_v<float>;
                add(std::cos(angle)*0.5f,y,std::sin(angle)*0.5f,0,y > 0 ? 1.0f : -1.0f,0,
                    0.5f + 0.5f*std::cos(angle), 0.5f + 0.5f*std::sin(angle));
            }
            for (std::uint32_t i = 0; i < segments; ++i)
            {
                const auto a = center + 1 + i, b = a + 1;
                if (y > 0) indices.insert(indices.end(), {center,a,b});
                else indices.insert(indices.end(), {center,b,a});
            }
        }
    }
    else
        return core::Result<MeshData>::failure({core::ErrorCode::ValidationError, "Unknown built-in 3D primitive"});

    return create(std::move(vertices), std::move(indices), true, true);
}
}
