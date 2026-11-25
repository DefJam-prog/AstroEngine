#include "astro/render3d/SceneRenderPacket.hpp"
#include "astro/render3d/NormalMatrix.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool near(const float a, const float b) { return std::abs(a - b) < 1.0e-4f; }
}

int main()
{
    using namespace astro;
    scene::Scene document(1, "Scene render packet unit");
    const auto parent = document.createEntity("Parent");
    const auto camera = parent ? document.createEntity("Camera", parent.value())
        : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "parent creation failed"});
    const auto first = parent ? document.createEntity("First cube", parent.value())
        : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "parent creation failed"});
    const auto second = parent ? document.createEntity("Second cube", parent.value())
        : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "parent creation failed"});
    check(parent && camera && first && second, "create camera and primitive hierarchy");
    if (!parent || !camera || !first || !second) return 1;

    scene::Transform parentTransform;
    parentTransform.position = {10.0, 2.0, 3.0};
    parentTransform.rotationDegrees = {0.0, 90.0, 0.0};
    parentTransform.scale = {2.0, 1.0, 3.0};
    scene::Transform cameraTransform;
    cameraTransform.position = {1.0, 0.0, 2.0};
    scene::Transform firstTransform;
    firstTransform.position = {0.0, 1.0, 0.0};
    scene::Transform secondTransform;
    secondTransform.position = {2.0, 1.0, 0.0};
    check(document.setTransform(parent.value(), parentTransform)
        && document.setTransform(camera.value(), cameraTransform)
        && document.setTransform(first.value(), firstTransform)
        && document.setTransform(second.value(), secondTransform), "set hierarchy transforms");
    std::vector<scene::EntityId> lightEntities;
    for (int index = 0; index < 5; ++index)
    {
        const auto light = document.createEntity("Key Light " + std::to_string(index), parent.value());
        check(light && document.setDirectionalLight3D(light.value(), {1.0f, 0.8f, 0.6f}, 0.75f),
            "add a directional light to the scene");
        check(light && document.setPointLight3D(light.value(), {0.2f, 0.4f, 0.8f}, 1.5f, 15.0f).hasValue(),
            "add a point light to the scene");
        if (light) lightEntities.push_back(light.value());
    }
    check(document.setCamera3D(camera.value(), 70.0, 0.2, 500.0)
        && document.setCamera3DActive(camera.value(), true), "activate perspective camera");
    check(document.setPrimitiveRenderer(first.value(), scene::Primitive3D::Cube)
        && document.setPrimitiveRenderer(second.value(), scene::Primitive3D::Cube), "add two built-in cubes");

    auto packet = render3d::buildSceneRenderPacket(document);
    check(packet && packet.value().cameraEntity == camera.value() && packet.value().items.size() == 2
        && packet.value().directionalLights.size() == 5 && packet.value().pointLights.size() == 4,
        "build packet from active Camera3D, visible primitives and bounded light lists");
    if (packet && packet.value().items.size() == 2)
    {
        const auto& a = packet.value().items[0];
        const auto& b = packet.value().items[1];
        check(a.kind == render3d::SceneRenderItemKind::Primitive
            && a.component == document.primitiveRenderer(first.value())->id
            && a.geometry && a.geometry->vertices().size() == 24,
            "primitive item retains component identity and generated cube geometry");
        check(a.geometry == b.geometry, "same primitive kind shares immutable geometry within a packet");
        check(packet.value().directionalLights.size() == lightEntities.size()
            && std::is_sorted(packet.value().directionalLights.begin(), packet.value().directionalLights.end(),
                [](const auto& lhs, const auto& rhs) { return lhs.entity < rhs.entity; }),
            "directional light order is stable by entity ID for the bounded renderer input");
        check(packet.value().pointLights.size() == 4
            && std::is_sorted(packet.value().pointLights.begin(), packet.value().pointLights.end(),
                [](const auto& lhs, const auto& rhs) { return lhs.entity < rhs.entity; })
            && packet.value().pointLights.front().entity == lightEntities.front()
            && packet.value().pointLights.back().entity == lightEntities[3]
            && std::any_of(packet.value().diagnostics.begin(), packet.value().diagnostics.end(),
                [](const auto& diagnostic) { return diagnostic.message.find("PointLight3D shader limit is 4") != std::string::npos; }),
            "point lights keep stable EntityId order, cap at four, and diagnose overflow");
        const auto normalMatrix = render3d::inverseTransposeNormalMatrix(a.worldTransform);
        check(normalMatrix.hasValue(), "hierarchical rotated non-uniform scale yields a valid normal matrix");
        if (normalMatrix)
        {
            const auto& n = normalMatrix.value().values;
            // The parent applies Y rotation and scale (2,1,3); inverse-transpose maps
            // local +X to world +Z with magnitude 1/2.
            if (!(near(n[2], -0.5f) && near(n[0], 0.0f) && near(n[3], 1.0f / 3.0f)))
                std::cerr << "normal matrix: " << n[0] << ',' << n[1] << ',' << n[2] << ','
                    << n[3] << ',' << n[4] << ',' << n[5] << ',' << n[6] << ',' << n[7] << ',' << n[8] << '\n';
            check(near(n[2], 0.5f) && near(n[0], 0.0f) && near(n[6], -1.0f / 3.0f),
                "inverse-transpose preserves rotated normals under non-uniform hierarchy scale");
        }
        check(near(a.worldTransform.values[12], 10.0f)
            && near(a.worldTransform.values[13], 3.0f)
            && near(a.worldTransform.values[14], 3.0f),
            "world Transform composes parent translation and child position");
        check(near(packet.value().camera.position.x, 4.0f)
            && near(packet.value().camera.position.y, 2.0f)
            && near(packet.value().camera.position.z, 5.0f),
            "active camera pose includes ancestor rotation and non-uniform scale");
        check(packet.value().camera.viewMatrix() && packet.value().camera.projectionMatrix(1.5f),
            "resolved camera produces valid view/projection matrices");
    }

    render3d::Mat4 singularNormalTransform{};
    check(!render3d::inverseTransposeNormalMatrix(singularNormalTransform),
        "singular transform rejects invalid normal matrices");
    auto nonFiniteNormalTransform = singularNormalTransform;
    nonFiniteNormalTransform.values[0] = std::numeric_limits<float>::infinity();
    check(!render3d::inverseTransposeNormalMatrix(nonFiniteNormalTransform),
        "non-finite transform rejects invalid normal matrices");

    check(document.setCamera3DActive(camera.value(), false).hasValue(), "deactivate camera");
    check(!render3d::buildSceneRenderPacket(document), "scene without active camera fails packet build");
    check(document.setCamera3DActive(camera.value(), true).hasValue(), "reactivate camera");
    auto singular = parentTransform;
    singular.scale.x = 0.0;
    check(document.setTransform(parent.value(), singular).hasValue(), "set singular parent scale");
    const auto invalidCameraPacket = render3d::buildSceneRenderPacket(document);
    check(!invalidCameraPacket, "camera with singular ancestor is rejected");

    scene::Scene meshScene(2, "Mesh resolver diagnostics");
    const auto meshCamera = meshScene.createEntity("Camera");
    const auto meshEntity = meshScene.createEntity("Mesh");
    check(meshCamera && meshEntity && meshScene.setCamera3D(meshCamera.value())
        && meshScene.setCamera3DActive(meshCamera.value(), true)
        && meshScene.setMeshRenderer(meshEntity.value(), assets::AssetId{55}), "create MeshRenderer fixture");
    const auto noResolver = render3d::buildSceneRenderPacket(meshScene);
    check(noResolver && noResolver.value().items.empty() && noResolver.value().diagnostics.size() == 1
        && noResolver.value().diagnostics.front().entity == meshEntity.value()
        && noResolver.value().diagnostics.front().asset == assets::AssetId{55},
        "missing optional importer is reported against the entity and model asset");

    const render3d::SceneMeshResolver failingResolver = [](const scene::MeshRendererComponent&) {
        return core::Result<render3d::SceneMeshSource>::failure(
            {core::ErrorCode::NotFound, "fixture mesh missing"});
    };
    const auto failedResolve = render3d::buildSceneRenderPacket(meshScene, nullptr, failingResolver);
    check(failedResolve && failedResolve.value().items.empty()
        && failedResolve.value().diagnostics.size() == 1
        && failedResolve.value().diagnostics.front().code == core::ErrorCode::NotFound,
        "mesh resolver failures become diagnostics without aborting the scene packet");

    const render3d::SceneMeshResolver emptyResolver = [](const scene::MeshRendererComponent&) {
        return core::Result<render3d::SceneMeshSource>::success({});
    };
    const auto emptyResolve = render3d::buildSceneRenderPacket(meshScene, nullptr, emptyResolver);
    check(emptyResolve && emptyResolve.value().items.empty()
        && emptyResolve.value().diagnostics.size() == 1
        && emptyResolve.value().diagnostics.front().code == core::ErrorCode::InvalidState,
        "resolver cannot silently return an empty mesh");

    if (failures == 0) std::cout << "Scene render packet unit checks passed\n";
    return failures == 0 ? 0 : 1;
}
