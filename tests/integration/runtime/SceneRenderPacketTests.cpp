#include "astro/assets/MaterialAsset.hpp"
#include "astro/render3d/SceneRenderPacket.hpp"
#include "astro/scene/SceneFile.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto projectRoot = std::filesystem::temp_directory_path()
        / ("Astro Scene Render Packet Ω " + std::to_string(unique));
    std::error_code filesystemError;
    if (!std::filesystem::create_directories(projectRoot / "Assets" / "Materials", filesystemError)
        || filesystemError)
    {
        std::cerr << "FAIL: could not create unique temporary project\n";
        return 1;
    }
    std::filesystem::create_directories(projectRoot / "Scenes", filesystemError);
    if (filesystemError)
    {
        std::cerr << "FAIL: could not create temporary scene directory\n";
        std::filesystem::remove_all(projectRoot, filesystemError);
        return 1;
    }

    try
    {
        auto registryResult = assets::AssetRegistry::open(projectRoot);
        check(registryResult.hasValue(), "open isolated AssetRegistry");
        if (!registryResult) throw std::runtime_error(registryResult.error().message);
        auto registry = std::move(registryResult).value();

        assets::MaterialAssetData projectMaterial;
        projectMaterial.baseColorFactor = {0.15f, 0.75f, 0.2f, 1.0f};
        const auto materialText = assets::serializeMaterialAsset(projectMaterial);
        check(materialText.hasValue(), "serialize project material fixture");
        if (!materialText) throw std::runtime_error(materialText.error().message);
        const auto materialPath = projectRoot / "Assets" / "Materials" / "runtime-material.astro-material";
        {
            std::ofstream output(materialPath, std::ios::binary | std::ios::trunc);
            output.write(materialText.value().data(), static_cast<std::streamsize>(materialText.value().size()));
            if (!output) throw std::runtime_error("could not write temporary material fixture");
        }
        const auto registeredMaterial = registry.registerMaterial("Materials/runtime-material.astro-material");
        check(registeredMaterial.hasValue(), "register project material fixture");
        if (!registeredMaterial) throw std::runtime_error(registeredMaterial.error().message);

        scene::Scene sourceScene(81, "Runtime Scene Render Ω");
        const auto root = sourceScene.createEntity("World root");
        const auto camera = root ? sourceScene.createEntity("Camera", root.value())
            : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "root creation failed"});
        const auto primitive = root ? sourceScene.createEntity("Materialized primitive", root.value())
            : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "root creation failed"});
        const auto mesh = root ? sourceScene.createEntity("Imported mesh", root.value())
            : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "root creation failed"});
        const auto sourceTexturedMesh = root ? sourceScene.createEntity("Source textured mesh", root.value())
            : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "root creation failed"});
        const auto lightRoot = sourceScene.createEntity("Rotated light parent");
        const auto light = lightRoot ? sourceScene.createEntity("Directional light", lightRoot.value())
            : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "light parent creation failed"});
        check(root && camera && primitive && mesh && sourceTexturedMesh && lightRoot && light,
            "create serialized renderable scene");
        if (!root || !camera || !primitive || !mesh || !sourceTexturedMesh || !lightRoot || !light)
            throw std::runtime_error("scene fixture creation failed");
        scene::Transform rootTransform;
        rootTransform.position = {2.0, 0.0, -1.0};
        scene::Transform cameraTransform;
        cameraTransform.position = {0.0, 0.0, 8.0};
        scene::Transform meshTransform;
        meshTransform.position = {3.0, 0.0, 0.0};
        check(sourceScene.setTransform(root.value(), rootTransform)
            && sourceScene.setTransform(camera.value(), cameraTransform)
            && sourceScene.setTransform(mesh.value(), meshTransform), "author hierarchical transforms");
        scene::Transform lightParentTransform;
        lightParentTransform.position = {500.0, -20.0, 100.0};
        lightParentTransform.rotationDegrees = {0.0, 90.0, 0.0};
        lightParentTransform.scale = {8.0, 0.25, 3.0};
        scene::Transform pointLocalTransform;
        pointLocalTransform.position = {1.0, 2.0, 3.0};
        check(sourceScene.setTransform(lightRoot.value(), lightParentTransform)
            && sourceScene.setTransform(light.value(), pointLocalTransform)
            && sourceScene.setDirectionalLight3D(light.value(), {0.6f, 0.7f, 0.8f}, 2.0f),
            "author a directional light under translated, rotated, and scaled parent");
        check(sourceScene.setPointLight3D(light.value(), {0.9f, 0.4f, 0.2f}, 2.5f, 18.0f).hasValue(),
            "author a point light on a transformed child entity");
        check(sourceScene.setCamera3D(camera.value(), 68.0, 0.15, 900.0)
            && sourceScene.setCamera3DActive(camera.value(), true), "set serialized active camera");
        check(sourceScene.setPrimitiveRenderer(primitive.value(), scene::Primitive3D::Sphere)
            && sourceScene.setPrimitiveMaterialOverride(primitive.value(), registeredMaterial.value().id),
            "author materialized PrimitiveRenderer");
        check(sourceScene.setMeshRenderer(mesh.value(), assets::AssetId{777})
            && sourceScene.setMeshMaterialOverride(mesh.value(), registeredMaterial.value().id),
            "author MeshRenderer with project material override");
        check(sourceScene.setMeshRenderer(sourceTexturedMesh.value(), assets::AssetId{778}).hasValue(),
            "author MeshRenderer using imported source material");

        const auto scenePath = projectRoot / "Scenes" / "Runtime.astro-scene";
        check(scene::saveSceneFile(scenePath, sourceScene).hasValue(), "save renderable scene document");
        const auto loadedScene = scene::loadSceneFile(scenePath);
        check(loadedScene.hasValue(), "load renderable scene document");
        if (!loadedScene) throw std::runtime_error(loadedScene.error().message);
        check(loadedScene.value().find(primitive.value())->primitiveRenderer->id
            == sourceScene.find(primitive.value())->primitiveRenderer->id,
            "PrimitiveRenderer component ID survives scene round-trip");

        const render3d::SceneMeshResolver resolver = [](const scene::MeshRendererComponent&) {
            auto geometry = render3d::MeshData::makePrimitive(
                static_cast<std::uint8_t>(scene::Primitive3D::Cube));
            if (!geometry)
                return core::Result<render3d::SceneMeshSource>::failure(geometry.error());
            assets::MaterialAssetData embeddedMaterial;
            embeddedMaterial.baseColorFactor = {0.85f, 0.1f, 0.05f, 1.0f};
            auto sharedGeometry = std::make_shared<const render3d::MeshData>(std::move(geometry).value());
            render3d::SceneMeshSource source{std::move(sharedGeometry), embeddedMaterial};
            source.sourceBaseColorImage = std::filesystem::path("Assets/Textures/source.png");
            source.sourceBaseColorImageHash = "source-image-sha256";
            source.sourceBaseColorTexCoord = 0;
            source.sourceBaseColorWrapU = assets::MaterialTextureWrapMode::MirroredRepeat;
            source.sourceBaseColorWrapV = assets::MaterialTextureWrapMode::ClampToEdge;
            source.sourceBaseColorMinFilter = assets::MaterialTextureMinFilter::Linear;
            source.sourceBaseColorMagFilter = assets::MaterialTextureMagFilter::Nearest;
            return core::Result<render3d::SceneMeshSource>::success(std::move(source));
        };
        const auto packet = render3d::buildSceneRenderPacket(loadedScene.value(), &registry, resolver);
        check(packet.hasValue(), "build backend-neutral packet from round-tripped Scene");
        if (packet)
        {
            check(packet.value().items.size() == 3 && packet.value().diagnostics.empty(),
                "visible renderers resolve without diagnostics");
            check(packet.value().directionalLights.size() == 1
                && packet.value().directionalLights.front().entity == light.value()
                && packet.value().directionalLights.front().component == loadedScene.value().directionalLight3D(light.value())->id
                && std::abs(packet.value().directionalLights.front().direction.x - 1.0f) < 1.0e-4f
                && std::abs(packet.value().directionalLights.front().direction.y) < 1.0e-4f
                && std::abs(packet.value().directionalLights.front().direction.z) < 1.0e-4f
                && packet.value().directionalLights.front().intensity == 2.0f,
                "packet direction uses hierarchical rotation while ignoring translation and non-uniform scale");
            const auto resolvedPoint = render3d::resolveScenePointLightPosition(loadedScene.value(), light.value());
            check(packet.value().pointLights.size() == 1 && resolvedPoint
                && packet.value().pointLights.front().entity == light.value()
                && packet.value().pointLights.front().component == loadedScene.value().pointLight3D(light.value())->id
                && packet.value().pointLights.front().position.x == resolvedPoint.value().x
                && packet.value().pointLights.front().position.y == resolvedPoint.value().y
                && packet.value().pointLights.front().position.z == resolvedPoint.value().z
                && packet.value().pointLights.front().position.x != 0.0f
                && packet.value().pointLights.front().range == 18.0f,
                "packet point light carries stable identity, range, and hierarchical world position");
            check(packet.value().cameraEntity == camera.value()
                && packet.value().camera.verticalFovRadians > 1.18f
                && packet.value().camera.verticalFovRadians < 1.19f,
                "active serialized Camera3D resolves to runtime camera values");
            const auto primitiveItem = std::find_if(packet.value().items.begin(), packet.value().items.end(),
                [primitiveId = primitive.value()](const auto& item) { return item.entity == primitiveId; });
            const auto meshItem = std::find_if(packet.value().items.begin(), packet.value().items.end(),
                [meshId = mesh.value()](const auto& item) { return item.entity == meshId; });
            check(primitiveItem != packet.value().items.end()
                && primitiveItem->kind == render3d::SceneRenderItemKind::Primitive
                && primitiveItem->materialAsset == registeredMaterial.value().id
                && primitiveItem->material
                && primitiveItem->material->baseColorFactor == projectMaterial.baseColorFactor,
                "primitive material override resolves registered project data");
            check(meshItem != packet.value().items.end()
                && meshItem->kind == render3d::SceneRenderItemKind::Mesh
                && meshItem->materialAsset == registeredMaterial.value().id
                && meshItem->material
                && meshItem->material->baseColorFactor == projectMaterial.baseColorFactor,
                "project override takes precedence over imported source material");
            const auto sourceTextureItem = std::find_if(packet.value().items.begin(), packet.value().items.end(),
                [entity = sourceTexturedMesh.value()](const auto& item) { return item.entity == entity; });
            check(sourceTextureItem != packet.value().items.end()
                && sourceTextureItem->sourceBaseColorImage
                && *sourceTextureItem->sourceBaseColorImage == std::filesystem::path("Assets/Textures/source.png")
                && sourceTextureItem->sourceBaseColorImageHash == "source-image-sha256"
                && sourceTextureItem->sourceBaseColorWrapU == assets::MaterialTextureWrapMode::MirroredRepeat
                && sourceTextureItem->sourceBaseColorWrapV == assets::MaterialTextureWrapMode::ClampToEdge
                && sourceTextureItem->sourceBaseColorMinFilter == assets::MaterialTextureMinFilter::Linear
                && sourceTextureItem->sourceBaseColorMagFilter == assets::MaterialTextureMagFilter::Nearest,
                "unoverridden source glTF texture path, hash and sampler metadata reach the render packet");
            check(meshItem != packet.value().items.end()
                && std::abs(meshItem->worldTransform.values[12] - 5.0f) < 1.0e-4f,
                "mesh world transform includes serialized parent position");
        }

        scene::Scene missingMaterialScene(82, "Missing material diagnostic");
        const auto diagnosticCamera = missingMaterialScene.createEntity("Camera");
        const auto diagnosticPrimitive = missingMaterialScene.createEntity("Missing material");
        check(diagnosticCamera && diagnosticPrimitive
            && missingMaterialScene.setCamera3D(diagnosticCamera.value())
            && missingMaterialScene.setCamera3DActive(diagnosticCamera.value(), true)
            && missingMaterialScene.setPrimitiveRenderer(diagnosticPrimitive.value(), scene::Primitive3D::Cube)
            && missingMaterialScene.setPrimitiveMaterialOverride(diagnosticPrimitive.value(), assets::AssetId{9999}),
            "create missing material diagnostic fixture");
        const auto missingMaterialPacket = render3d::buildSceneRenderPacket(missingMaterialScene, &registry);
        check(missingMaterialPacket && missingMaterialPacket.value().items.empty()
            && missingMaterialPacket.value().diagnostics.size() == 1
            && missingMaterialPacket.value().diagnostics.front().entity == diagnosticPrimitive.value()
            && missingMaterialPacket.value().diagnostics.front().asset == assets::AssetId{9999},
            "unresolved material omits the entity and reports its IDs");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: integration exception: " << error.what() << '\n';
        ++failures;
    }
    std::filesystem::remove_all(projectRoot, filesystemError);
    if (failures == 0) std::cout << "Scene render packet integration checks passed\n";
    return failures == 0 ? 0 : 1;
}
