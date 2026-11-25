#include "astro/render3d/Camera3D.hpp"
#include "astro/render3d/MeshData.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL: " #expression "\n"; ++failures; } } while (false)
}

int main()
{
    using namespace astro::render3d;
    Camera3D camera;
    const auto view = camera.viewMatrix();
    const auto projection = camera.projectionMatrix(16.0f / 9.0f);
    CHECK(view && projection);
    if (view)
    {
        CHECK(std::abs(view.value().values[12]) < 1.0e-6f);
        CHECK(std::abs(view.value().values[13]) < 1.0e-6f);
        CHECK(std::abs(view.value().values[14] + 5.0f) < 1.0e-6f);
    }
    if (projection)
    {
        CHECK(projection.value().values[0] > 0.0f);
        CHECK(projection.value().values[5] > 0.0f);
        CHECK(projection.value().values[11] == -1.0f);
        CHECK(projection.value().values[10] < 0.0f);
    }
    CHECK(camera.projectionMatrix(1.0f, ClipDepthRange::NegativeOneToOne));
    Camera3D bad = camera;
    bad.target = bad.position;
    CHECK(!bad.viewMatrix());
    bad = camera;
    bad.up = {0.0f, 0.0f, 1.0f};
    bad.target = {0.0f, 0.0f, 1.0f};
    CHECK(!bad.viewMatrix());
    CHECK(!camera.projectionMatrix(0.0f));
    bad = camera;
    bad.nearPlane = 1000.0f;
    CHECK(!bad.projectionMatrix(1.0f));
    CHECK(!camera.projectionMatrix(std::numeric_limits<float>::denorm_min()));
    CHECK(!camera.projectionMatrix(1.0f, static_cast<ClipDepthRange>(99)));
    bad = camera;
    bad.position.x = std::numeric_limits<float>::max();
    CHECK(!bad.viewMatrix());

    const MeshData cube = MeshData::makeColoredCube();
    CHECK(cube.vertices().size() == 8);
    CHECK(cube.triangleIndices().size() == 36);
    const auto recreatedCube = MeshData::create(cube.vertices(), cube.triangleIndices());
    CHECK(recreatedCube);
    CHECK(recreatedCube && recreatedCube.value().contentHash() == cube.contentHash());
    auto changedCubeVertices = cube.vertices();
    changedCubeVertices[0].position.x += 0.25f;
    const auto changedCube = MeshData::create(std::move(changedCubeVertices), cube.triangleIndices());
    CHECK(changedCube && changedCube.value().contentHash() != cube.contentHash());
    CHECK(!cube.hasNormals() && !cube.hasTexCoords());
    for (std::uint8_t primitive = 1; primitive <= 4; ++primitive)
    {
        const auto generated = MeshData::makePrimitive(primitive);
        CHECK(generated.hasValue());
        if (!generated) continue;
        const auto& mesh = generated.value();
        CHECK(!mesh.vertices().empty() && !mesh.triangleIndices().empty());
        CHECK(mesh.hasNormals() && mesh.hasTexCoords());
        CHECK(mesh.triangleIndices().size() % 3 == 0);
        for (const auto index : mesh.triangleIndices()) CHECK(index < mesh.vertices().size());
        for (const auto& vertex : mesh.vertices())
        {
            CHECK(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y)
                && std::isfinite(vertex.position.z));
            CHECK(std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y)
                && std::isfinite(vertex.normal.z));
            CHECK(std::isfinite(vertex.texCoord0.x) && std::isfinite(vertex.texCoord0.y));
        }
    }
    CHECK(!MeshData::makePrimitive(0));
    CHECK(!MeshData::makePrimitive(5));
    MeshVertex attributed{{0, 0, 0}, {}, {0, 0, 1}, {0.25f, 0.75f}};
    const auto attributedMesh = MeshData::create({attributed, attributed, attributed}, {0, 1, 2}, true, true);
    CHECK(attributedMesh && attributedMesh.value().hasNormals() && attributedMesh.value().hasTexCoords());
    CHECK(attributedMesh && attributedMesh.value().vertices()[0].normal == (Vec3{0, 0, 1}));
    CHECK(attributedMesh && attributedMesh.value().vertices()[0].texCoord0 == (Vec2{0.25f, 0.75f}));
    attributed.normal.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!MeshData::create({attributed, attributed, attributed}, {0, 1, 2}, true, false));
    attributed.normal = {0, 0, 1};
    attributed.texCoord0.y = std::numeric_limits<float>::infinity();
    CHECK(!MeshData::create({attributed, attributed, attributed}, {0, 1, 2}, false, true));
    CHECK(!MeshData::create({}, {0, 1, 2}));
    CHECK(!MeshData::create({{{0, 0, 0}, {}}}, {0, 0}));
    CHECK(!MeshData::create({{{std::numeric_limits<float>::infinity(), 0, 0}, {}}}, {0, 0, 0}));
    CHECK(!MeshData::create({{{0, 0, 0}, {}}}, {0, 0, 1}));
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
