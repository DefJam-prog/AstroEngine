#include "astro/render3d/BaseColorTexture.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace astro::render3d
{
namespace
{
constexpr std::uint64_t MaxPixels = 16ull * 1024 * 1024;
constexpr std::uint64_t MaxMipChainBytes = 96ull * 1024 * 1024;

float srgbToLinear(const std::uint8_t encoded) noexcept
{
    const float value = static_cast<float>(encoded) / 255.0f;
    return value <= 0.04045f ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t toUnorm8(const float value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

std::uint8_t scaleAlphaByte(const std::uint8_t alpha, const double scale) noexcept
{
    const auto linear = static_cast<double>(alpha) / 255.0;
    return toUnorm8(static_cast<float>(std::min(1.0, linear * scale)));
}
}

core::Result<Vec2> transformBaseColorUv(const Vec2 uv, const BaseColorUvTransform& transform)
{
    const std::array values{uv.x, uv.y, transform.offsetU, transform.offsetV,
        transform.rotationRadians, transform.scaleU, transform.scaleV};
    if (!std::all_of(values.begin(), values.end(), [](const float value) { return std::isfinite(value); }))
        return core::Result<Vec2>::failure({core::ErrorCode::ValidationError,
            "Base-color UV transform values must be finite"});
    const auto scaledU = static_cast<double>(uv.x) * transform.scaleU;
    const auto scaledV = static_cast<double>(uv.y) * transform.scaleV;
    const auto cosine = std::cos(static_cast<double>(transform.rotationRadians));
    const auto sine = std::sin(static_cast<double>(transform.rotationRadians));
    const auto u = cosine * scaledU - sine * scaledV + transform.offsetU;
    const auto v = sine * scaledU + cosine * scaledV + transform.offsetV;
    constexpr auto maxFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(u) || !std::isfinite(v) || std::abs(u) > maxFloat || std::abs(v) > maxFloat)
        return core::Result<Vec2>::failure({core::ErrorCode::ValidationError,
            "Transformed base-color UV exceeds the supported finite range"});
    return core::Result<Vec2>::success({static_cast<float>(u), static_cast<float>(v)});
}

std::optional<BaseColorMinFilterSelection> selectBaseColorMinFilter(const BaseColorMinFilter filter) noexcept
{
    switch (filter)
    {
    case BaseColorMinFilter::Nearest: return BaseColorMinFilterSelection{true, false, false};
    case BaseColorMinFilter::Linear: return BaseColorMinFilterSelection{false, false, false};
    case BaseColorMinFilter::NearestMipmapNearest: return BaseColorMinFilterSelection{true, true, true};
    case BaseColorMinFilter::LinearMipmapNearest: return BaseColorMinFilterSelection{false, true, true};
    case BaseColorMinFilter::NearestMipmapLinear: return BaseColorMinFilterSelection{true, false, true};
    case BaseColorMinFilter::LinearMipmapLinear: return BaseColorMinFilterSelection{false, false, true};
    }
    return std::nullopt;
}

core::Result<std::vector<std::uint8_t>> prepareLinearBaseColorBgra8(
    const std::span<const std::uint8_t> srgbBgra8, const std::array<float, 4>& linearFactor)
{
    if (srgbBgra8.empty() || srgbBgra8.size() % 4 != 0)
        return core::Result<std::vector<std::uint8_t>>::failure({core::ErrorCode::ValidationError,
            "Base-color image must contain complete non-empty BGRA8 pixels"});
    const auto pixels = static_cast<std::uint64_t>(srgbBgra8.size() / 4);
    if (pixels > MaxPixels || srgbBgra8.size() > (std::numeric_limits<std::uint32_t>::max)())
        return core::Result<std::vector<std::uint8_t>>::failure({core::ErrorCode::ValidationError,
            "Base-color image exceeds the bounded pixel or upload size"});
    for (const float factor : linearFactor)
        if (!std::isfinite(factor) || factor < 0.0f || factor > 1.0f)
            return core::Result<std::vector<std::uint8_t>>::failure({core::ErrorCode::ValidationError,
                "Linear base-color factor must be finite and in [0,1]"});

    std::vector<std::uint8_t> linear(srgbBgra8.size());
    for (std::size_t offset = 0; offset < srgbBgra8.size(); offset += 4)
    {
        linear[offset + 0] = toUnorm8(srgbToLinear(srgbBgra8[offset + 0]) * linearFactor[2]);
        linear[offset + 1] = toUnorm8(srgbToLinear(srgbBgra8[offset + 1]) * linearFactor[1]);
        linear[offset + 2] = toUnorm8(srgbToLinear(srgbBgra8[offset + 2]) * linearFactor[0]);
        linear[offset + 3] = toUnorm8((static_cast<float>(srgbBgra8[offset + 3]) / 255.0f) * linearFactor[3]);
    }
    return core::Result<std::vector<std::uint8_t>>::success(std::move(linear));
}

core::Result<BaseColorMipChain> prepareLinearBaseColorMipChainBgra8(
    const std::span<const std::uint8_t> srgbBgra8, const std::uint32_t width, const std::uint32_t height,
    const std::array<float, 4>& linearFactor, const bool includeMipmaps,
    const BaseColorAlphaMipMode alphaMode, const float alphaCutoff)
{
    if (width == 0 || height == 0 || width > 65535 || height > 65535)
        return core::Result<BaseColorMipChain>::failure({core::ErrorCode::ValidationError,
            "Base-color mip source dimensions must be in [1, 65535]"});
    const auto sourcePixels = static_cast<std::uint64_t>(width) * height;
    if (sourcePixels > MaxPixels || sourcePixels > (std::numeric_limits<std::size_t>::max)() / 4
        || srgbBgra8.size() != static_cast<std::size_t>(sourcePixels * 4))
        return core::Result<BaseColorMipChain>::failure({core::ErrorCode::ValidationError,
            "Base-color mip source byte count does not match the bounded dimensions"});
    if (!std::isfinite(alphaCutoff) || alphaCutoff < 0.0f || alphaCutoff > 1.0f)
        return core::Result<BaseColorMipChain>::failure({core::ErrorCode::ValidationError,
            "Base-color alpha cutoff must be finite and in [0,1]"});

    auto base = prepareLinearBaseColorBgra8(srgbBgra8, linearFactor);
    if (!base) return core::Result<BaseColorMipChain>::failure(base.error());

    BaseColorMipChain chain;
    std::uint32_t levelWidth = width;
    std::uint32_t levelHeight = height;
    std::uint64_t totalBytes = 0;
    do
    {
        const auto levelBytes = static_cast<std::uint64_t>(levelWidth) * levelHeight * 4;
        if (levelBytes > MaxMipChainBytes - totalBytes)
            return core::Result<BaseColorMipChain>::failure({core::ErrorCode::ValidationError,
                "Base-color mip chain exceeds the 96 MiB upload limit"});
        chain.levels.push_back({levelWidth, levelHeight, static_cast<std::size_t>(totalBytes),
            static_cast<std::size_t>(levelBytes)});
        totalBytes += levelBytes;
        if (!includeMipmaps || (levelWidth == 1 && levelHeight == 1)) break;
        levelWidth = std::max(1u, levelWidth / 2);
        levelHeight = std::max(1u, levelHeight / 2);
    } while (true);

    chain.pixels.resize(static_cast<std::size_t>(totalBytes));
    std::copy(base.value().begin(), base.value().end(), chain.pixels.begin());
    for (std::size_t levelIndex = 1; levelIndex < chain.levels.size(); ++levelIndex)
    {
        const auto& source = chain.levels[levelIndex - 1];
        const auto& destination = chain.levels[levelIndex];
        const auto sourceEndX = static_cast<std::size_t>(source.width);
        const auto sourceEndY = static_cast<std::size_t>(source.height);
        for (std::uint32_t y = 0; y < destination.height; ++y)
        {
            const double top = static_cast<double>(y) * source.height / destination.height;
            const double bottom = static_cast<double>(y + 1) * source.height / destination.height;
            for (std::uint32_t x = 0; x < destination.width; ++x)
            {
                const double left = static_cast<double>(x) * source.width / destination.width;
                const double right = static_cast<double>(x + 1) * source.width / destination.width;
                std::array<double, 4> sums{};
                double weightTotal = 0.0;
                for (auto sy = static_cast<std::size_t>(std::floor(top)); sy < sourceEndY && sy < bottom; ++sy)
                {
                    const double weightY = std::max(0.0, std::min(bottom, static_cast<double>(sy + 1))
                        - std::max(top, static_cast<double>(sy)));
                    for (auto sx = static_cast<std::size_t>(std::floor(left)); sx < sourceEndX && sx < right; ++sx)
                    {
                        const double weightX = std::max(0.0, std::min(right, static_cast<double>(sx + 1))
                            - std::max(left, static_cast<double>(sx)));
                        const double weight = weightX * weightY;
                        const auto sourcePixel = source.offset + (sy * source.width + sx) * 4;
                        const double alpha = chain.pixels[sourcePixel + 3] / 255.0;
                        for (std::size_t channel = 0; channel < 3; ++channel)
                        {
                            const double color = chain.pixels[sourcePixel + channel];
                            sums[channel] += (alphaMode == BaseColorAlphaMipMode::Opaque
                                ? color : color * alpha) * weight;
                        }
                        sums[3] += chain.pixels[sourcePixel + 3] * weight;
                        weightTotal += weight;
                    }
                }
                if (weightTotal <= 0.0)
                    return core::Result<BaseColorMipChain>::failure({core::ErrorCode::InternalError,
                        "Base-color mip filter produced an empty sample footprint"});
                const auto destinationPixel = destination.offset
                    + (static_cast<std::size_t>(y) * destination.width + x) * 4;
                const double averageAlpha = sums[3] / weightTotal / 255.0;
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    const double filtered = alphaMode == BaseColorAlphaMipMode::Opaque
                        ? sums[channel] / weightTotal / 255.0
                        : averageAlpha > 0.0 ? (sums[channel] / weightTotal / 255.0) / averageAlpha : 0.0;
                    chain.pixels[destinationPixel + channel] = toUnorm8(static_cast<float>(filtered));
                }
                chain.pixels[destinationPixel + 3] = toUnorm8(static_cast<float>(averageAlpha));
            }
        }
        if (alphaMode == BaseColorAlphaMipMode::Mask)
        {
            const auto& baseLevel = chain.levels.front();
            std::uint64_t sourceCovered = 0;
            const auto sourceCount = std::uint64_t(baseLevel.width) * baseLevel.height;
            for (std::uint64_t i = 0; i < sourceCount; ++i)
                if (chain.pixels[baseLevel.offset + static_cast<std::size_t>(i) * 4 + 3] / 255.0f >= alphaCutoff)
                    ++sourceCovered;
            const double targetCoverage = sourceCount == 0 ? 0.0
                : static_cast<double>(sourceCovered) / static_cast<double>(sourceCount);
            for (std::size_t mip = 1; mip < chain.levels.size(); ++mip)
            {
                const auto& level = chain.levels[mip];
                const auto count = std::uint64_t(level.width) * level.height;
                std::array<std::uint64_t, 256> alphaHistogram{};
                for (std::uint64_t i = 0; i < count; ++i)
                    ++alphaHistogram[chain.pixels[level.offset + static_cast<std::size_t>(i) * 4 + 3]];
                const auto coverageAt = [&](const double scale)
                {
                    std::uint64_t covered = 0;
                    for (std::size_t alphaByte = 0; alphaByte < alphaHistogram.size(); ++alphaByte)
                    {
                        const auto scaled = scaleAlphaByte(static_cast<std::uint8_t>(alphaByte), scale);
                        if (scaled / 255.0f >= alphaCutoff) covered += alphaHistogram[alphaByte];
                    }
                    return static_cast<double>(covered) / static_cast<double>(count);
                };
                double selectedScale = 0.0;
                double bestError = std::abs(coverageAt(selectedScale) - targetCoverage);
                const auto considerScale = [&](const double candidateScale)
                {
                    const auto error = std::abs(coverageAt(candidateScale) - targetCoverage);
                    if (error < bestError)
                    {
                        bestError = error;
                        selectedScale = candidateScale;
                    }
                };
                considerScale(1.0);
                auto firstPassingByte = 0;
                while (firstPassingByte < 256
                    && static_cast<float>(firstPassingByte) / 255.0f < alphaCutoff)
                    ++firstPassingByte;
                for (int sourceAlpha = 1; sourceAlpha <= 255; ++sourceAlpha)
                {
                    // Every achievable MASK coverage changes when one UNORM alpha bin
                    // crosses the cutoff. Probe just beyond each rounded-byte threshold.
                    const auto thresholdScale = std::max(0.0,
                        (firstPassingByte - 0.5 + 1.0e-3) / sourceAlpha);
                    considerScale(thresholdScale);
                }
                for (std::uint64_t i = 0; i < count; ++i)
                {
                    const auto alphaOffset = level.offset + static_cast<std::size_t>(i) * 4 + 3;
                    chain.pixels[alphaOffset] = scaleAlphaByte(chain.pixels[alphaOffset], selectedScale);
                }
            }
        }
    }
    return core::Result<BaseColorMipChain>::success(std::move(chain));
}

bool passesBaseColorAlphaMask(const float alpha, const float cutoff) noexcept
{
    return std::isfinite(alpha) && std::isfinite(cutoff) && alpha >= cutoff;
}
}
