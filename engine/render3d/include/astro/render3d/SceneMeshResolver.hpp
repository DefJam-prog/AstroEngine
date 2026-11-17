#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/render3d/MeshData.hpp"
#include "astro/scene/Scene.hpp"

#include <cstdint>
#include <string>

namespace astro::render3d
{
/// Backend-neutral geometry selected by one Scene MeshRenderer component.
struct ResolvedSceneMesh final
{
    assets::AssetId modelAssetId{};
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
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
