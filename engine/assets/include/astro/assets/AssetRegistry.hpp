#pragma once

#include "astro/core/Result.hpp"

#include <cstdint>
#include <compare>
#include <filesystem>
#include <map>
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

// Project-local early registry. Source files under Assets/ are never modified;
// validated encoded image bytes are mirrored into a content-addressed Library cache.
class AssetRegistry final
{
public:
    static core::Result<AssetRegistry> open(const std::filesystem::path& projectRoot);

    core::Result<AssetReference> importImage(const std::filesystem::path& sourceRelativePath);
    core::Result<AssetReference> reimport(AssetId id);
    [[nodiscard]] const AssetReference* find(AssetId id) const noexcept;
    [[nodiscard]] std::vector<AssetId> missingAssets() const;
    [[nodiscard]] std::filesystem::path cachedPath(AssetId id) const;
    [[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }
    [[nodiscard]] const std::map<AssetId, AssetReference>& entries() const noexcept { return entries_; }

private:
    AssetRegistry(std::filesystem::path projectRoot, std::filesystem::path assetsRoot,
        std::filesystem::path cacheRoot, std::filesystem::path indexPath);
    core::Result<void> saveIndex(const std::map<AssetId, AssetReference>& entries, std::uint64_t nextId) const;

    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
    std::filesystem::path cacheRoot_;
    std::filesystem::path indexPath_;
    std::map<AssetId, AssetReference> entries_;
    std::uint64_t nextId_ = 1;
};

[[nodiscard]] std::string_view imageFormatName(ImageFormat format) noexcept;
[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> bytes);
}
