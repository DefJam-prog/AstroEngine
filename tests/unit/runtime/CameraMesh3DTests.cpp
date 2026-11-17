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
    CHECK(MeshData::create(cube.vertices(), cube.triangleIndices()));
    CHECK(!MeshData::create({}, {0, 1, 2}));
    CHECK(!MeshData::create({{{0, 0, 0}, {}}}, {0, 0}));
    CHECK(!MeshData::create({{{std::numeric_limits<float>::infinity(), 0, 0}, {}}}, {0, 0, 0}));
    CHECK(!MeshData::create({{{0, 0, 0}, {}}}, {0, 0, 1}));
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
