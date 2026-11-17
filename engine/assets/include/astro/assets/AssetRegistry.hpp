#pragma once

#include "astro/core/Result.hpp"

#include <cstdint>
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
    [[nodiscard]] const AssetReference* find(AssetId id) const noexcept;
    [[nodiscard]] const ScriptAssetReference* findScript(AssetId id) const noexcept;
    [[nodiscard]] const ModelAssetReference* findModel(AssetId id) const noexcept;
    [[nodiscard]] core::Result<ResolvedScriptAsset> resolveScript(AssetId id) const;
    [[nodiscard]] core::Result<ResolvedModelAsset> resolveModel(AssetId id) const;
    [[nodiscard]] ScriptBuildReport inspectScriptBuild() const;
    core::Result<std::vector<ScriptAssetReference>> registerScriptsFromBuild();
    [[nodiscard]] std::vector<AssetId> missingAssets() const;
    [[nodiscard]] std::filesystem::path cachedPath(AssetId id) const;
    [[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }
    [[nodiscard]] const std::map<AssetId, AssetReference>& entries() const noexcept { return entries_; }
    [[nodiscard]] const std::map<AssetId, ScriptAssetReference>& scriptEntries() const noexcept { return scriptEntries_; }
    [[nodiscard]] const std::map<AssetId, ModelAssetReference>& modelEntries() const noexcept { return modelEntries_; }

private:
    AssetRegistry(std::filesystem::path projectRoot, std::filesystem::path assetsRoot,
        std::filesystem::path cacheRoot, std::filesystem::path indexPath);
    core::Result<void> saveIndex(const std::map<AssetId, AssetReference>& entries,
        const std::map<AssetId, ScriptAssetReference>& scripts,
        const std::map<AssetId, ModelAssetReference>& models, std::uint64_t nextId) const;

    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
    std::filesystem::path cacheRoot_;
    std::filesystem::path indexPath_;
    std::map<AssetId, AssetReference> entries_;
    std::map<AssetId, ScriptAssetReference> scriptEntries_;
    std::map<AssetId, ModelAssetReference> modelEntries_;
    std::uint64_t nextId_ = 1;
};

[[nodiscard]] std::string_view imageFormatName(ImageFormat format) noexcept;
[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> bytes);
}
