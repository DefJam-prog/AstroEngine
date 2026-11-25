#pragma once

#include "astro/core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace astro::runtime
{
struct WicDecodedImage final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Rows are tightly packed BGRA8, matching WIC's requested pixel format.
    std::vector<std::uint8_t> pixels;
};

// Decodes an already-resolved image path through Windows WIC with bounded
// dimensions and memory use. Asset callers should pass AssetRegistry::cachedPath.
[[nodiscard]] core::Result<WicDecodedImage> decodeWicImage(const std::filesystem::path& path);
}
