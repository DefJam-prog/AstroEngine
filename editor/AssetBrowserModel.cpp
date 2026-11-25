#include "AssetBrowserModel.hpp"

#include <algorithm>
#include <string>

namespace astro::editor
{
std::vector<AssetBrowserEntry> buildAssetBrowserCatalog(const assets::AssetRegistry& registry)
{
    std::vector<AssetBrowserEntry> entries;
    entries.reserve(registry.entries().size() + registry.modelEntries().size()
        + registry.materialEntries().size() + registry.scriptEntries().size());

    for (const auto& [id, reference] : registry.entries())
        entries.push_back({id, AssetBrowserKind::Image, reference.sourceRelativePath});
    for (const auto& [id, reference] : registry.modelEntries())
        entries.push_back({id, AssetBrowserKind::Model, reference.sourceRelativePath});
    for (const auto& [id, reference] : registry.materialEntries())
        entries.push_back({id, AssetBrowserKind::Material, reference.sourceRelativePath});
    for (const auto& [id, reference] : registry.scriptEntries())
        entries.push_back({id, AssetBrowserKind::Script, reference.sourceRelativePath});

    std::sort(entries.begin(), entries.end(), [](const AssetBrowserEntry& left, const AssetBrowserEntry& right) {
        if (left.id != right.id) return left.id < right.id;
        if (left.kind != right.kind) return left.kind < right.kind;
        return left.sourceRelativePath < right.sourceRelativePath;
    });
    return entries;
}

std::vector<std::size_t> filterAssetBrowserCatalog(const std::vector<AssetBrowserEntry>& entries,
    const std::string_view utf8Query, const AssetBrowserTypeFilter typeFilter)
{
    const auto acceptsKind = [typeFilter](const AssetBrowserKind kind) {
        switch (typeFilter)
        {
        case AssetBrowserTypeFilter::All: return true;
        case AssetBrowserTypeFilter::Images: return kind == AssetBrowserKind::Image;
        case AssetBrowserTypeFilter::Models: return kind == AssetBrowserKind::Model;
        case AssetBrowserTypeFilter::Materials: return kind == AssetBrowserKind::Material;
        case AssetBrowserTypeFilter::Scripts: return kind == AssetBrowserKind::Script;
        }
        return false;
    };

    std::vector<std::size_t> visible;
    visible.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        if (!acceptsKind(entry.kind)) continue;
        if (!utf8Query.empty())
        {
            const auto idText = std::to_string(entry.id.value);
            if (entry.sourceRelativePath.find(utf8Query) == std::string::npos
                && idText.find(utf8Query) == std::string::npos)
                continue;
        }
        visible.push_back(index);
    }
    return visible;
}
}
