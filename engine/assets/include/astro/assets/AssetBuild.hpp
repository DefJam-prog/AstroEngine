#pragma once

#include "astro/assets/AssetRegistry.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace astro::assets
{
class AssetDependencyGraph final
{
public:
    core::Result<void> addDependency(AssetId asset, AssetId dependency);
    bool removeAsset(AssetId asset);
    [[nodiscard]] std::vector<AssetId> dependenciesOf(AssetId asset) const;
    core::Result<std::vector<AssetId>> closure(const std::vector<AssetId>& roots) const;
    core::Result<void> save(const std::filesystem::path& path) const;
    static core::Result<AssetDependencyGraph> load(const std::filesystem::path& path);

private:
    std::map<AssetId, std::set<AssetId>> edges_;
};

enum class AssetPlatform { Windows, MacOS, Android, IOS };
[[nodiscard]] std::string_view assetPlatformName(AssetPlatform platform) noexcept;

struct AssetPackageResult
{
    std::filesystem::path path;
    std::string sha256;
    std::string platform;
    std::size_t assetCount = 0;
    std::uint64_t packageBytes = 0;
    bool reused = false;
};

// Packages verified, byte-identical PNG/JPEG cache entries. Platform names are
// build-profile tags; this function does not convert or validate platform codecs.
core::Result<AssetPackageResult> buildAssetPackage(const AssetRegistry& registry,
    const AssetDependencyGraph& graph, const std::vector<AssetId>& roots, AssetPlatform platform);
}
