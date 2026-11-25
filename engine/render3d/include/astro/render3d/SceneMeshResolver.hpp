#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/render3d/MeshData.hpp"
#include "astro/render3d/GltfImporter.hpp"
#include "astro/scene/Scene.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace astro::render3d
{
/// Supported source-material data for one selected glTF primitive. Image bytes remain in
/// the project source tree and are resolved only for a local external image.
struct ResolvedSceneSourceMaterial final
{
    assets::MaterialAssetData values;
    bool hasBaseColorTexture = false;
    std::uint32_t baseColorTexCoord = 0;
    std::optional<ImportedGltfSampler> baseColorSampler;
    std::optional<BaseColorUvTransform> baseColorTransform;
    bool baseColorImageAvailable = true;
    std::filesystem::path baseColorImagePath;
    std::string baseColorImageHash;
    std::string diagnostic;
};

/// Backend-neutral geometry selected by one Scene MeshRenderer component.
struct ResolvedSceneMesh final
{
    assets::AssetId modelAssetId{};
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
    std::optional<std::uint32_t> sourceMaterialIndex;
    assets::AssetId materialOverride{};
    std::optional<ResolvedSceneSourceMaterial> sourceMaterial;
    std::string meshName;
    MeshData geometry;
    bool visible = true;
};

/// Resolves a Scene component against the project registry and current imported model data.
/// Rejects stale importer digests and out-of-range mesh/primitive selectors.
[[nodiscard]] core::Result<ResolvedSceneMesh> resolveMeshRenderer(
    const assets::AssetRegistry& registry,
    const scene::MeshRendererComponent& component);
}
