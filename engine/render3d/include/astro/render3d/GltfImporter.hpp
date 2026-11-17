#pragma once

#include "astro/core/Result.hpp"
#include "astro/assets/AssetRegistry.hpp"
#include "astro/render3d/MeshData.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace astro::render3d
{
struct ImportedGltfMesh final
{
    std::string name;
    std::vector<MeshData> primitives;
};

struct GltfImportResult final
{
    std::vector<ImportedGltfMesh> meshes;
    std::string contentHash;
    std::filesystem::path cachePath;
    bool cacheHit = false;
    assets::AssetId modelAssetId{};
};

/// Bounded static triangle geometry importer. Source paths are relative to the project root
/// and must begin with Assets/. Only POSITION and triangle indices are retained.
class GltfImporter final
{
public:
    [[nodiscard]] static core::Result<GltfImportResult> importFile(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& sourceRelativePath);

    /// Imports the source and registers/updates its stable project Model AssetId.
    [[nodiscard]] static core::Result<GltfImportResult> importAndRegister(
        assets::AssetRegistry& registry,
        const std::filesystem::path& sourceRelativePath);
};
}
