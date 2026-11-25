#pragma once

#include "astro/assets/AssetRegistry.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace astro::editor
{
enum class AssetBrowserKind
{
    Image,
    Model,
    Material,
    Script
};

enum class AssetBrowserTypeFilter
{
    All,
    Images,
    Models,
    Materials,
    Scripts
};

struct AssetBrowserEntry
{
    assets::AssetId id;
    AssetBrowserKind kind = AssetBrowserKind::Image;
    std::string sourceRelativePath;
};

[[nodiscard]] std::vector<AssetBrowserEntry> buildAssetBrowserCatalog(
    const assets::AssetRegistry& registry);
[[nodiscard]] std::vector<std::size_t> filterAssetBrowserCatalog(
    const std::vector<AssetBrowserEntry>& entries, std::string_view utf8Query,
    AssetBrowserTypeFilter typeFilter);
}
