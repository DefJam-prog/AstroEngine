#pragma once

#include "astro/core/Result.hpp"
#include "astro/render3d/MeshData.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <cstddef>
#include <optional>
#include <vector>

namespace astro::render3d
{
struct BaseColorUvTransform final
{
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    float rotationRadians = 0.0f;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
};

struct BaseColorMipLevel final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t offset = 0;
    std::size_t byteSize = 0;
};

struct BaseColorMipChain final
{
    // BGRA8 levels are tightly packed from largest to smallest for bgfx upload.
    std::vector<BaseColorMipLevel> levels;
    std::vector<std::uint8_t> pixels;
};

enum class BaseColorAlphaMipMode
{
    Opaque,
    Blend,
    Mask
};

enum class BaseColorMinFilter
{
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear
};

struct BaseColorMinFilterSelection final
{
    bool nearestTexel = false;
    bool nearestMip = false;
    bool usesMipmaps = false;
    friend bool operator==(const BaseColorMinFilterSelection&, const BaseColorMinFilterSelection&) = default;
};

[[nodiscard]] std::optional<BaseColorMinFilterSelection> selectBaseColorMinFilter(BaseColorMinFilter filter) noexcept;

/// Applies KHR_texture_transform order: scale, counter-clockwise rotation, then offset.
[[nodiscard]] core::Result<Vec2> transformBaseColorUv(Vec2 uv, const BaseColorUvTransform& transform);

// Converts decoded sRGB BGRA8 base-color pixels to linear BGRA8 and applies
// the authored linear RGBA factor. Alpha is linear and is multiplied directly.
// The source span is read-only; callers may upload/cache the returned copy.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> prepareLinearBaseColorBgra8(
    std::span<const std::uint8_t> srgbBgra8, const std::array<float, 4>& linearFactor);

/// Prepares mip 0 and, when requested, generates a full chain by area-filtering
/// linear BGRA8 data. Bounds the source to 16 Mi-pixels and upload data to 96 MiB.
[[nodiscard]] core::Result<BaseColorMipChain> prepareLinearBaseColorMipChainBgra8(
    std::span<const std::uint8_t> srgbBgra8, std::uint32_t width, std::uint32_t height,
    const std::array<float, 4>& linearFactor, bool includeMipmaps,
    BaseColorAlphaMipMode alphaMode = BaseColorAlphaMipMode::Opaque,
    float alphaCutoff = 0.5f);

/// MASK follows the renderer contract: fragments exactly at cutoff survive.
[[nodiscard]] bool passesBaseColorAlphaMask(float alpha, float cutoff) noexcept;
}
