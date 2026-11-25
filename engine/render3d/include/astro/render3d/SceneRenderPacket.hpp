#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/render3d/Camera3D.hpp"
#include "astro/render3d/MeshData.hpp"
#include "astro/scene/Scene.hpp"

#include <functional>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace astro::render3d
{
struct SceneMeshSource final
{
    // Importer adapters can share immutable geometry owned by their asset cache.
    std::shared_ptr<const MeshData> geometry;
    // Optional material embedded in a source model. A Scene override takes precedence.
    std::optional<assets::MaterialAssetData> sourceMaterial;
    // Optional local glTF base-color source. It is propagated only when no project
    // material override replaces the imported material.
    std::optional<std::filesystem::path> sourceBaseColorImage;
    std::string sourceBaseColorImageHash;
    std::uint32_t sourceBaseColorTexCoord = 0;
    std::optional<assets::MaterialTextureWrapMode> sourceBaseColorWrapU;
    std::optional<assets::MaterialTextureWrapMode> sourceBaseColorWrapV;
    std::optional<assets::MaterialTextureMinFilter> sourceBaseColorMinFilter;
    std::optional<assets::MaterialTextureMagFilter> sourceBaseColorMagFilter;
    std::optional<assets::MaterialUvTransform> sourceBaseColorUvTransform;
};

using SceneMeshResolver = std::function<core::Result<SceneMeshSource>(
    const scene::MeshRendererComponent&)>;

enum class SceneRenderItemKind { Mesh, Primitive };

struct SceneRenderItem final
{
    scene::EntityId entity = 0;
    scene::ComponentId component = 0;
    SceneRenderItemKind kind = SceneRenderItemKind::Mesh;
    Mat4 worldTransform{};
    std::shared_ptr<const MeshData> geometry;
    assets::AssetId materialAsset{};
    std::optional<assets::MaterialAssetData> material;
    std::optional<std::filesystem::path> sourceBaseColorImage;
    std::string sourceBaseColorImageHash;
    std::uint32_t sourceBaseColorTexCoord = 0;
    assets::MaterialTextureWrapMode sourceBaseColorWrapU = assets::MaterialTextureWrapMode::ClampToEdge;
    assets::MaterialTextureWrapMode sourceBaseColorWrapV = assets::MaterialTextureWrapMode::ClampToEdge;
    assets::MaterialTextureMinFilter sourceBaseColorMinFilter = assets::MaterialTextureMinFilter::Nearest;
    assets::MaterialTextureMagFilter sourceBaseColorMagFilter = assets::MaterialTextureMagFilter::Nearest;
    assets::MaterialUvTransform sourceBaseColorUvTransform{};
};

struct SceneRenderDiagnostic final
{
    scene::EntityId entity = 0;
    assets::AssetId asset{};
    core::ErrorCode code = core::ErrorCode::ValidationError;
    std::string message;
};

struct SceneDirectionalLight final
{
    scene::EntityId entity = 0;
    scene::ComponentId component = 0;
    Vec3 direction{0.0f, 0.0f, -1.0f}; // Direction of propagation; local -Z is the source convention.
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct ScenePointLight final
{
    scene::EntityId entity = 0;
    scene::ComponentId component = 0;
    Vec3 position{};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
};

struct SceneRenderPacket final
{
    scene::EntityId cameraEntity = 0;
    Camera3D camera;
    std::vector<SceneDirectionalLight> directionalLights;
    std::vector<ScenePointLight> pointLights;
    std::vector<SceneRenderItem> items;
    std::vector<SceneRenderDiagnostic> diagnostics;
};

/// Builds a backend-neutral, owned snapshot of visible 3D Scene items.
/// MeshRenderer geometry/source materials are provided by an optional importer adapter.
/// Invalid renderable entities are omitted and reported; an invalid/missing active camera fails the build.
[[nodiscard]] core::Result<SceneRenderPacket> buildSceneRenderPacket(
    const scene::Scene& document, const assets::AssetRegistry* assets = nullptr,
    const SceneMeshResolver& resolveMesh = {});

[[nodiscard]] core::Result<Mat4> sceneEntityWorldTransform(
    const scene::Scene& document, scene::EntityId entity);
[[nodiscard]] core::Result<Camera3D> resolveSceneCameraPose(
    const scene::Scene& document, scene::EntityId cameraEntity);
[[nodiscard]] core::Result<Vec3> resolveSceneDirectionalLightDirection(
    const scene::Scene& document, scene::EntityId lightEntity);
[[nodiscard]] core::Result<Vec3> resolveScenePointLightPosition(
    const scene::Scene& document, scene::EntityId lightEntity);
}
