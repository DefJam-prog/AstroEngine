#pragma once

#include "astro/core/Result.hpp"

#include <cstdint>
#include <array>
#include <compare>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace astro::assets
{
struct AssetId
{
    std::uint64_t value = 0;
    friend bool operator==(const AssetId&, const AssetId&) = default;
    friend auto operator<=>(const AssetId&, const AssetId&) = default;
};

enum class ImageFormat
{
    Png,
    Jpeg
};

struct AssetReference
{
    AssetId id;
    std::string sourceRelativePath;
    ImageFormat format = ImageFormat::Png;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t sourceSize = 0;
    std::string sha256;
};

struct ScriptAssetReference
{
    AssetId id;
    std::string sourceRelativePath;
    std::string managedTypeName;
    std::string assemblyRelativePath;
    std::string runtimeConfigRelativePath;
    std::string sourceSha256;
};

struct ModelAssetReference
{
    AssetId id;
    std::string sourceRelativePath;
    std::uint64_t sourceSize = 0;
    std::string sourceSha256;
    // Geometry-input digest reported by a successful bounded model import.
    std::string importContentHash;
};

enum class MaterialAlphaMode
{
    Opaque,
    Mask,
    Blend
};

enum class MaterialTextureWrapMode { Repeat, MirroredRepeat, ClampToEdge };
enum class MaterialTextureMinFilter
{
    Nearest, Linear, NearestMipmapNearest, LinearMipmapNearest,
    NearestMipmapLinear, LinearMipmapLinear
};
enum class MaterialTextureMagFilter { Nearest, Linear };

struct MaterialUvTransform
{
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    float rotationRadians = 0.0f;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
    friend bool operator==(const MaterialUvTransform&, const MaterialUvTransform&) = default;
};

struct MaterialTextureBinding
{
    AssetId imageAssetId;
    std::uint32_t texCoord = 0;
    friend bool operator==(const MaterialTextureBinding&, const MaterialTextureBinding&) = default;
};

// Backend-neutral authored data. Numeric color values are linear-light; texture
// sampling/color-space interpretation is defined by each semantic texture role.
struct MaterialAssetData
{
    std::uint32_t schemaVersion = 2;
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    std::optional<MaterialTextureBinding> baseColorTexture;
    std::optional<MaterialTextureBinding> metallicRoughnessTexture;
    std::optional<MaterialTextureBinding> normalTexture;
    std::optional<MaterialTextureBinding> occlusionTexture;
    std::optional<MaterialTextureBinding> emissiveTexture;
    // Schema-v1 data resolves to the historical Scene View policy.
    MaterialTextureWrapMode baseColorWrapU = MaterialTextureWrapMode::ClampToEdge;
    MaterialTextureWrapMode baseColorWrapV = MaterialTextureWrapMode::ClampToEdge;
    MaterialTextureMinFilter baseColorMinFilter = MaterialTextureMinFilter::Nearest;
    MaterialTextureMagFilter baseColorMagFilter = MaterialTextureMagFilter::Nearest;
    MaterialUvTransform baseColorUvTransform{};
    friend bool operator==(const MaterialAssetData&, const MaterialAssetData&) = default;
};

struct MaterialAssetReference
{
    AssetId id;
    std::string sourceRelativePath;
    std::uint32_t schemaVersion = 1;
    std::uint64_t sourceSize = 0;
    std::string sourceSha256;
};

struct ResolvedMaterialAsset
{
    AssetId id;
    std::filesystem::path sourcePath;
    MaterialAssetData material;
};

struct ResolvedModelAsset
{
    AssetId id;
    std::filesystem::path sourcePath;
    std::string importContentHash;
};

struct ResolvedScriptAsset
{
    AssetId id;
    std::filesystem::path sourcePath;
    std::filesystem::path assemblyPath;
    std::filesystem::path runtimeConfigPath;
    std::string managedTypeName;
};

enum class ScriptBuildFreshness
{
    Missing,
    Current,
    Stale,
    Invalid
};

struct ScriptBuildBindingStatus
{
    std::string sourceRelativePath;
    std::string sourceSha256;
    std::string managedTypeName;
    std::optional<AssetId> assetId;
    bool registered = false;
    std::string diagnostic;
};

struct ScriptBuildReport
{
    ScriptBuildFreshness freshness = ScriptBuildFreshness::Missing;
    std::string sdkVersion;
    std::string fingerprint;
    std::string assemblyRelativePath;
    std::string assemblySha256;
    std::string runtimeConfigRelativePath;
    std::string runtimeConfigSha256;
    std::vector<ScriptBuildBindingStatus> bindings;
    std::vector<std::string> diagnostics;
    bool requiresRegistration = false;
};

// Project-local early registry. Source files under Assets/ are never modified;
// validated encoded image bytes are mirrored into a content-addressed Library cache.
class AssetRegistry final
{
public:
    static core::Result<AssetRegistry> open(const std::filesystem::path& projectRoot);

    core::Result<AssetReference> importImage(const std::filesystem::path& sourceRelativePath);
    core::Result<AssetReference> reimport(AssetId id);
    core::Result<ScriptAssetReference> registerScript(const std::filesystem::path& sourceRelativePath,
        std::string managedTypeName, const std::filesystem::path& assemblyRelativePath,
        const std::filesystem::path& runtimeConfigRelativePath);
    core::Result<ModelAssetReference> registerModel(const std::filesystem::path& sourceRelativePath,
        std::string importContentHash);
    core::Result<MaterialAssetReference> registerMaterial(const std::filesystem::path& sourceRelativePath);
    core::Result<MaterialAssetReference> saveMaterial(AssetId id, const MaterialAssetData& material);
    [[nodiscard]] const AssetReference* find(AssetId id) const noexcept;
    [[nodiscard]] const ScriptAssetReference* findScript(AssetId id) const noexcept;
    [[nodiscard]] const ModelAssetReference* findModel(AssetId id) const noexcept;
    [[nodiscard]] const MaterialAssetReference* findMaterial(AssetId id) const noexcept;
    [[nodiscard]] core::Result<ResolvedScriptAsset> resolveScript(AssetId id) const;
    [[nodiscard]] core::Result<ResolvedModelAsset> resolveModel(AssetId id) const;
    [[nodiscard]] core::Result<ResolvedMaterialAsset> resolveMaterial(AssetId id) const;
    [[nodiscard]] ScriptBuildReport inspectScriptBuild() const;
    core::Result<std::vector<ScriptAssetReference>> registerScriptsFromBuild();
    [[nodiscard]] std::vector<AssetId> missingAssets() const;
    [[nodiscard]] std::filesystem::path cachedPath(AssetId id) const;
    [[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }
    [[nodiscard]] const std::map<AssetId, AssetReference>& entries() const noexcept { return entries_; }
    [[nodiscard]] const std::map<AssetId, ScriptAssetReference>& scriptEntries() const noexcept { return scriptEntries_; }
    [[nodiscard]] const std::map<AssetId, ModelAssetReference>& modelEntries() const noexcept { return modelEntries_; }
    [[nodiscard]] const std::map<AssetId, MaterialAssetReference>& materialEntries() const noexcept { return materialEntries_; }

private:
    AssetRegistry(std::filesystem::path projectRoot, std::filesystem::path assetsRoot,
        std::filesystem::path cacheRoot, std::filesystem::path indexPath);
    core::Result<void> saveIndex(const std::map<AssetId, AssetReference>& entries,
        const std::map<AssetId, ScriptAssetReference>& scripts,
        const std::map<AssetId, ModelAssetReference>& models,
        const std::map<AssetId, MaterialAssetReference>& materials, std::uint64_t nextId) const;

    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
    std::filesystem::path cacheRoot_;
    std::filesystem::path indexPath_;
    std::map<AssetId, AssetReference> entries_;
    std::map<AssetId, ScriptAssetReference> scriptEntries_;
    std::map<AssetId, ModelAssetReference> modelEntries_;
    std::map<AssetId, MaterialAssetReference> materialEntries_;
    std::uint64_t nextId_ = 1;
};

[[nodiscard]] std::string_view imageFormatName(ImageFormat format) noexcept;
[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> bytes);
}
