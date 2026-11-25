#pragma once

#include "astro/core/Result.hpp"
#include "astro/assets/AssetRegistry.hpp"
#include "astro/render3d/BaseColorTexture.hpp"
#include "astro/render3d/MeshData.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace astro::render3d
{
enum class GltfImageSourceKind : std::uint32_t
{
    ExternalFile = 1,
    DataUri = 2,
    BufferView = 3,
    EmbeddedArray = 4,
    Unsupported = 5
};

struct ImportedGltfImageReference final
{
    std::string name;
    GltfImageSourceKind sourceKind = GltfImageSourceKind::Unsupported;
    std::string reference; // original URI or project-relative image path
    std::string mimeType;
    std::optional<std::uint32_t> bufferViewIndex;
    bool available = false;
    std::uint64_t sourceSize = 0;
    std::string sourceHash;
};

struct ImportedGltfTextureReference final
{
    std::string name;
    std::optional<std::uint32_t> imageIndex;
    std::optional<std::uint32_t> samplerIndex;
};

enum class GltfSamplerWrap : std::uint8_t
{
    Repeat = 0,
    MirroredRepeat = 1,
    ClampToEdge = 2
};

enum class GltfSamplerFilter : std::uint16_t
{
    Nearest = 9728,
    Linear = 9729,
    NearestMipMapNearest = 9984,
    LinearMipMapNearest = 9985,
    NearestMipMapLinear = 9986,
    LinearMipMapLinear = 9987
};

struct ImportedGltfSampler final
{
    GltfSamplerWrap wrapS = GltfSamplerWrap::Repeat;
    GltfSamplerWrap wrapT = GltfSamplerWrap::Repeat;
    std::optional<GltfSamplerFilter> magFilter;
    std::optional<GltfSamplerFilter> minFilter;
};

struct GltfMaterialTextureBinding final
{
    std::uint32_t textureIndex = 0;
    std::uint32_t texCoord = 0;
    std::optional<BaseColorUvTransform> transform;
};

struct ImportedGltfMaterial final
{
    std::string name;
    assets::MaterialAssetData values;
    std::optional<GltfMaterialTextureBinding> baseColorTexture;
    std::optional<GltfMaterialTextureBinding> metallicRoughnessTexture;
    std::optional<GltfMaterialTextureBinding> normalTexture;
    std::optional<GltfMaterialTextureBinding> occlusionTexture;
    std::optional<GltfMaterialTextureBinding> emissiveTexture;
};

struct ImportedGltfMesh final
{
    std::string name;
    std::vector<MeshData> primitives;
    // Kept parallel to primitives. Empty optional means the source primitive has no material.
    std::vector<std::optional<std::uint32_t>> primitiveMaterialIndices;
};

struct GltfImportResult final
{
    std::vector<ImportedGltfMesh> meshes;
    std::vector<ImportedGltfImageReference> images;
    std::vector<ImportedGltfTextureReference> textures;
    std::vector<ImportedGltfSampler> samplers;
    std::vector<ImportedGltfMaterial> materials;
    std::vector<std::string> diagnostics;
    std::string contentHash;
    std::filesystem::path cachePath;
    bool cacheHit = false;
    assets::AssetId modelAssetId{};
};

/// Bounded static triangle geometry importer. Source paths are relative to the project root
/// and must begin with Assets/. Geometry retains POSITION/triangle indices and optional NORMAL /
/// TEXCOORD_0 channels. Supported metallic-roughness material metadata, per-primitive material
/// indices and image references are retained separately. Unsupported attributes/features fail
/// explicitly or are reported as diagnostics, according to the source data category.
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
