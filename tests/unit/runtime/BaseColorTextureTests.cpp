#include "astro/render3d/BaseColorTexture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using astro::render3d::prepareLinearBaseColorBgra8;
    using astro::render3d::transformBaseColorUv;
    const std::array<std::uint8_t, 8> source{0, 128, 255, 128, 64, 0, 0, 255};
    const std::array<float, 4> factor{0.65f, 0.60f, 0.80f, 0.50f};
    const auto prepared = prepareLinearBaseColorBgra8(source, factor);
    check(static_cast<bool>(prepared), "valid sRGB BGRA8 and linear factor are accepted");
    check(source[1] == 128 && source[2] == 255, "source bytes are preserved");
    if (prepared)
    {
        const std::array<std::uint8_t, 8> expected{0, 33, 166, 64, 10, 0, 0, 128};
        check(std::equal(prepared.value().begin(), prepared.value().end(), expected.begin()),
            "sRGB RGB channels decode before linear factor multiplication; alpha stays linear");
    }

    const std::array<std::uint8_t, 4> midGray{128, 128, 128, 255};
    const std::array<float, 4> identity{1, 1, 1, 1};
    const auto gray = prepareLinearBaseColorBgra8(midGray, identity);
    check(gray && gray.value()[0] == 55 && gray.value()[1] == 55 && gray.value()[2] == 55,
        "sRGB mid-gray is converted to the expected linear UNORM8 value");
    const std::array<float, 4> halfLinear{0.5f, 0.5f, 0.5f, 1.0f};
    const auto halfGray = prepareLinearBaseColorBgra8(midGray, halfLinear);
    check(halfGray && halfGray.value() == std::vector<std::uint8_t>({28, 28, 28, 255}),
        "sRGB 128 mid-gray multiplied by a 0.5 linear factor uploads as linear UNORM8 28");
    const std::array<float, 4> transparent{0, 0, 0, 0};
    const auto zero = prepareLinearBaseColorBgra8(midGray, transparent);
    check(zero && zero.value() == std::vector<std::uint8_t>({0, 0, 0, 0}),
        "zero factor yields zero linear BGRA including alpha");

    check(!prepareLinearBaseColorBgra8({}, identity), "empty payload is rejected");
    const std::array<std::uint8_t, 3> partialPixel{1, 2, 3};
    check(!prepareLinearBaseColorBgra8(partialPixel, identity), "partial BGRA pixel is rejected");
    const std::array<float, 4> outside{1, 1.01f, 1, 1};
    check(!prepareLinearBaseColorBgra8(midGray, outside), "out-of-range factor is rejected");
    const std::array<float, 4> notFinite{1, std::numeric_limits<float>::quiet_NaN(), 1, 1};
    check(!prepareLinearBaseColorBgra8(midGray, notFinite), "non-finite factor is rejected");

    const std::array<std::uint8_t, 16> rgba2x2{
        0, 0, 0, 255, 0, 0, 0, 255,
        0, 0, 0, 255, 255, 255, 255, 255};
    const auto mips2x2 = astro::render3d::prepareLinearBaseColorMipChainBgra8(
        rgba2x2, 2, 2, identity, true);
    check(mips2x2 && mips2x2.value().levels.size() == 2
        && mips2x2.value().levels[0].width == 2 && mips2x2.value().levels[0].height == 2
        && mips2x2.value().levels[1].width == 1 && mips2x2.value().levels[1].height == 1
        && mips2x2.value().pixels[mips2x2.value().levels[1].offset] == 64,
        "2x2 mip averages decoded linear pixels into a 1x1 level");
    check(rgba2x2[12] == 255 && rgba2x2[15] == 255, "mip generation preserves source image bytes");

    const std::array<std::uint8_t, 8> transparentColorEdge{
        0, 0, 255, 0, 255, 0, 0, 255}; // transparent red, opaque blue in BGRA
    const auto blendEdge = astro::render3d::prepareLinearBaseColorMipChainBgra8(
        transparentColorEdge, 2, 1, identity, true, astro::render3d::BaseColorAlphaMipMode::Blend);
    check(blendEdge && blendEdge.value().pixels[blendEdge.value().levels[1].offset] == 255
        && blendEdge.value().pixels[blendEdge.value().levels[1].offset + 2] == 0
        && blendEdge.value().pixels[blendEdge.value().levels[1].offset + 3] == 128,
        "BLEND mip filters premultiplied color and unpremultiplies without transparent-color fringe");

    std::array<std::uint8_t, 4 * 4 * 4> maskCoveragePixels{};
    for (std::size_t pixel = 0; pixel < 16; ++pixel)
    {
        maskCoveragePixels[pixel * 4] = 255;
        const auto x = pixel % 4, y = pixel / 4;
        maskCoveragePixels[pixel * 4 + 3] = (x < 2 && y < 2) ? 230 : 26;
    }
    const auto maskMips = astro::render3d::prepareLinearBaseColorMipChainBgra8(
        maskCoveragePixels, 4, 4, identity, true, astro::render3d::BaseColorAlphaMipMode::Mask, 0.5f);
    check(maskMips && maskMips.value().levels.size() == 3,
        "MASK material generates its mip chain using the authored cutoff");
    if (maskMips && maskMips.value().levels.size() > 1)
    {
        const auto& mip = maskMips.value().levels[1];
        std::size_t covered = 0;
        for (std::size_t i = 0; i < std::size_t(mip.width) * mip.height; ++i)
            covered += astro::render3d::passesBaseColorAlphaMask(
                maskMips.value().pixels[mip.offset + i * 4 + 3] / 255.0f, 0.5f) ? 1u : 0u;
        check(covered == 1, "MASK mip rescales alpha to retain the base-level 25 percent coverage");
    }
    constexpr float maskBoundary = 128.0f / 255.0f;
    check(!astro::render3d::passesBaseColorAlphaMask(maskBoundary - 1.0f / 255.0f, maskBoundary)
        && astro::render3d::passesBaseColorAlphaMask(maskBoundary, maskBoundary)
        && astro::render3d::passesBaseColorAlphaMask(maskBoundary + 1.0f / 255.0f, maskBoundary),
        "MASK rejects alpha below cutoff and accepts exact equality and values above");
    check(!astro::render3d::passesBaseColorAlphaMask(std::numeric_limits<float>::quiet_NaN(), 0.5f)
        && !astro::render3d::passesBaseColorAlphaMask(0.5f, std::numeric_limits<float>::infinity()),
        "MASK boundary helper rejects non-finite alpha and cutoff values");

    const auto coverageAt = [](const astro::render3d::BaseColorMipChain& chain,
        const std::size_t levelIndex, const float cutoff)
    {
        const auto& level = chain.levels[levelIndex];
        std::uint64_t covered = 0;
        const auto count = std::uint64_t(level.width) * level.height;
        for (std::uint64_t pixel = 0; pixel < count; ++pixel)
            covered += astro::render3d::passesBaseColorAlphaMask(
                chain.pixels[level.offset + static_cast<std::size_t>(pixel) * 4 + 3] / 255.0f, cutoff) ? 1u : 0u;
        return static_cast<double>(covered) / static_cast<double>(count);
    };
    const auto nearestScaleCoverageError = [&](const astro::render3d::BaseColorMipChain& rawChain,
        const std::size_t levelIndex, const float cutoff, const double target)
    {
        const auto& level = rawChain.levels[levelIndex];
        const auto count = std::uint64_t(level.width) * level.height;
        const auto scaledCoverage = [&](const double scale)
        {
            std::uint64_t covered = 0;
            for (std::uint64_t pixel = 0; pixel < count; ++pixel)
            {
                const auto alpha = rawChain.pixels[level.offset + static_cast<std::size_t>(pixel) * 4 + 3];
                const auto scaledByte = static_cast<std::uint8_t>(std::lround(
                    std::clamp(static_cast<double>(alpha) * scale, 0.0, 255.0)));
                covered += astro::render3d::passesBaseColorAlphaMask(scaledByte / 255.0f, cutoff) ? 1u : 0u;
            }
            return static_cast<double>(covered) / static_cast<double>(count);
        };
        double bestError = std::abs(scaledCoverage(0.0) - target);
        bestError = std::min(bestError, std::abs(scaledCoverage(1.0) - target));
        auto firstPassingByte = 0;
        while (firstPassingByte < 256
            && !astro::render3d::passesBaseColorAlphaMask(firstPassingByte / 255.0f, cutoff))
            ++firstPassingByte;
        for (int sourceAlpha = 1; sourceAlpha <= 255; ++sourceAlpha)
        {
            const auto thresholdScale = (firstPassingByte - 0.5) / sourceAlpha;
            bestError = std::min(bestError,
                std::abs(scaledCoverage(thresholdScale + 1.0e-5) - target));
        }
        return bestError;
    };
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 2> maskSourceSizes{{
        {64, 64}, {31, 17}}};
    constexpr std::array<const char*, 3> maskPatternNames{{"split", "checker", "center-hole"}};
    bool allMaskMipCoverageNearest = true;
    double worstMaskMipCoverageError = 0.0;
    for (const auto [width, height] : maskSourceSizes)
    {
        for (std::size_t pattern = 0; pattern < maskPatternNames.size(); ++pattern)
        {
            std::vector<std::uint8_t> patternPixels(static_cast<std::size_t>(width) * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
            {
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    bool opaque = false;
                    if (pattern == 0) opaque = x < width / 2;
                    else if (pattern == 1)
                    {
                        const auto tileX = std::max(1u, width / 4);
                        const auto tileY = std::max(1u, height / 4);
                        opaque = ((x / tileX + y / tileY) % 2) == 0;
                    }
                    else opaque = !(x >= width / 4 && x < (3 * width) / 4
                        && y >= height / 4 && y < (3 * height) / 4);
                    const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
                    patternPixels[offset + 0] = 255;
                    patternPixels[offset + 1] = 255;
                    patternPixels[offset + 2] = 255;
                    patternPixels[offset + 3] = opaque ? 255 : 0;
                }
            }
            const auto originalPatternPixels = patternPixels;
            const auto rawMips = astro::render3d::prepareLinearBaseColorMipChainBgra8(
                patternPixels, width, height, identity, true, astro::render3d::BaseColorAlphaMipMode::Blend, 0.5f);
            const auto maskPatternMips = astro::render3d::prepareLinearBaseColorMipChainBgra8(
                patternPixels, width, height, identity, true, astro::render3d::BaseColorAlphaMipMode::Mask, 0.5f);
            check(rawMips && maskPatternMips && rawMips.value().levels.size() == maskPatternMips.value().levels.size(),
                "MASK coverage fixtures generate matching raw and compensated mip chains");
            if (!rawMips || !maskPatternMips
                || rawMips.value().levels.size() != maskPatternMips.value().levels.size()) continue;
            check(patternPixels == originalPatternPixels, "MASK coverage evaluation preserves every source texel");
            const auto targetCoverage = coverageAt(maskPatternMips.value(), 0, 0.5f);
            for (std::size_t level = 1; level < maskPatternMips.value().levels.size(); ++level)
            {
                const auto actualCoverage = coverageAt(maskPatternMips.value(), level, 0.5f);
                const auto bestPossibleError = nearestScaleCoverageError(rawMips.value(), level, 0.5f, targetCoverage);
                const auto error = std::abs(actualCoverage - targetCoverage);
                allMaskMipCoverageNearest = allMaskMipCoverageNearest && error <= bestPossibleError + 1.0e-6;
                worstMaskMipCoverageError = std::max(worstMaskMipCoverageError, error);
                const auto& mip = maskPatternMips.value().levels[level];
                std::cout << "MASK coverage pattern=" << maskPatternNames[pattern] << " source=" << width << 'x' << height
                          << " mip=" << mip.width << 'x' << mip.height << " target=" << targetCoverage
                          << " actual=" << actualCoverage << " nearestError=" << bestPossibleError << '\n';
            }
        }
    }
    constexpr std::array<const char*, 2> fractionalMaskPatternNames{{"alpha-steps", "alpha-ramp"}};
    constexpr std::array<float, 3> maskCutoffs{{0.25f, 0.5f, 0.75f}};
    constexpr std::array<std::uint8_t, 8> alphaSteps{{0, 32, 64, 96, 128, 160, 192, 255}};
    for (const auto [width, height] : maskSourceSizes)
    {
        for (std::size_t pattern = 0; pattern < fractionalMaskPatternNames.size(); ++pattern)
        {
            std::vector<std::uint8_t> patternPixels(static_cast<std::size_t>(width) * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
            {
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const auto alpha = pattern == 0
                        ? alphaSteps[(x * alphaSteps.size()) / width]
                        : static_cast<std::uint8_t>((std::uint64_t(x) * 255) / (width - 1));
                    const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
                    patternPixels[offset + 0] = 255;
                    patternPixels[offset + 1] = 255;
                    patternPixels[offset + 2] = 255;
                    patternPixels[offset + 3] = alpha;
                }
            }
            const auto originalPatternPixels = patternPixels;
            for (const auto cutoff : maskCutoffs)
            {
                const auto rawMips = astro::render3d::prepareLinearBaseColorMipChainBgra8(
                    patternPixels, width, height, identity, true, astro::render3d::BaseColorAlphaMipMode::Blend, cutoff);
                const auto maskPatternMips = astro::render3d::prepareLinearBaseColorMipChainBgra8(
                    patternPixels, width, height, identity, true, astro::render3d::BaseColorAlphaMipMode::Mask, cutoff);
                check(rawMips && maskPatternMips
                    && rawMips.value().levels.size() == maskPatternMips.value().levels.size(),
                    "fractional-alpha MASK fixtures generate matching raw and compensated mip chains");
                if (!rawMips || !maskPatternMips
                    || rawMips.value().levels.size() != maskPatternMips.value().levels.size()) continue;
                check(patternPixels == originalPatternPixels,
                    "fractional-alpha MASK evaluation preserves every source texel");
                const auto targetCoverage = coverageAt(maskPatternMips.value(), 0, cutoff);
                for (std::size_t level = 1; level < maskPatternMips.value().levels.size(); ++level)
                {
                    const auto actualCoverage = coverageAt(maskPatternMips.value(), level, cutoff);
                    const auto bestPossibleError = nearestScaleCoverageError(rawMips.value(), level, cutoff,
                        targetCoverage);
                    const auto error = std::abs(actualCoverage - targetCoverage);
                    allMaskMipCoverageNearest = allMaskMipCoverageNearest
                        && error <= bestPossibleError + 1.0e-6;
                    worstMaskMipCoverageError = std::max(worstMaskMipCoverageError, error);
                    const auto& mip = maskPatternMips.value().levels[level];
                    std::cout << "MASK coverage pattern=" << fractionalMaskPatternNames[pattern]
                              << " source=" << width << 'x' << height << " cutoff=" << cutoff
                              << " mip=" << mip.width << 'x' << mip.height << " target=" << targetCoverage
                              << " actual=" << actualCoverage << " nearestError=" << bestPossibleError << '\n';
                }
            }
            check(patternPixels == originalPatternPixels,
                "fractional-alpha MASK cutoff sweep leaves its source fixture unchanged");
        }
    }
    check(allMaskMipCoverageNearest,
        "MASK coverage adjustment selects the nearest achievable coverage for binary and fractional-alpha patterns, all tested cutoffs and mip levels");
    std::cout << "MASK coverage worst absolute delta across fixtures: " << worstMaskMipCoverageError << '\n';

    const std::array<std::uint8_t, 12> odd3x1{0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    const auto oddMips = astro::render3d::prepareLinearBaseColorMipChainBgra8(
        odd3x1, 3, 1, identity, true);
    check(oddMips && oddMips.value().levels.size() == 2
        && oddMips.value().levels[1].width == 1 && oddMips.value().levels[1].height == 1
        && oddMips.value().pixels[oddMips.value().levels[1].offset] == 85,
        "odd-sized mip footprints include every source texel");

    const auto noMipChain = astro::render3d::prepareLinearBaseColorMipChainBgra8(
        rgba2x2, 2, 2, identity, false);
    check(noMipChain && noMipChain.value().levels.size() == 1
        && noMipChain.value().pixels.size() == rgba2x2.size(),
        "non-mip filtering uploads exactly one texture level");
    check(!astro::render3d::prepareLinearBaseColorMipChainBgra8(rgba2x2, 3, 2, identity, true),
        "reject byte counts inconsistent with declared mip dimensions");
    check(!astro::render3d::prepareLinearBaseColorMipChainBgra8({}, 65536, 1, identity, true),
        "reject dimensions outside the bounded BGFX range before allocation");

    using MinFilter = astro::render3d::BaseColorMinFilter;
    constexpr std::array filterCases{
        std::pair{MinFilter::Nearest, astro::render3d::BaseColorMinFilterSelection{true, false, false}},
        std::pair{MinFilter::Linear, astro::render3d::BaseColorMinFilterSelection{false, false, false}},
        std::pair{MinFilter::NearestMipmapNearest, astro::render3d::BaseColorMinFilterSelection{true, true, true}},
        std::pair{MinFilter::LinearMipmapNearest, astro::render3d::BaseColorMinFilterSelection{false, true, true}},
        std::pair{MinFilter::NearestMipmapLinear, astro::render3d::BaseColorMinFilterSelection{true, false, true}},
        std::pair{MinFilter::LinearMipmapLinear, astro::render3d::BaseColorMinFilterSelection{false, false, true}}
    };
    bool allFiltersValid = true;
    for (const auto& [filter, expectedSelection] : filterCases)
        allFiltersValid = allFiltersValid
            && astro::render3d::selectBaseColorMinFilter(filter) == expectedSelection;
    check(allFiltersValid, "all six glTF minification modes resolve to their exact texel/mip policy");

    const astro::render3d::BaseColorUvTransform identityUv;
    const auto identitySample = transformBaseColorUv({0.25f, 0.75f}, identityUv);
    check(identitySample && std::abs(identitySample.value().x - 0.25f) < 1e-6f
        && std::abs(identitySample.value().y - 0.75f) < 1e-6f,
        "default UV transform preserves coordinates");
    constexpr float halfPi = 1.57079632679489661923f;
    const astro::render3d::BaseColorUvTransform atlasUv{0.2f, -0.1f, halfPi, 2.0f, 3.0f};
    const auto atlasSample = transformBaseColorUv({0.25f, 0.5f}, atlasUv);
    check(atlasSample && std::abs(atlasSample.value().x + 1.3f) < 1e-5f
        && std::abs(atlasSample.value().y - 0.4f) < 1e-5f,
        "UV transform applies scale, counter-clockwise rotation, then offset");
    const astro::render3d::BaseColorUvTransform invalidUv{
        std::numeric_limits<float>::infinity(), 0, 0, 1, 1};
    check(!transformBaseColorUv({0, 0}, invalidUv), "non-finite UV transforms are rejected");

    std::cout << "Base-color transfer unit: " << failures << " failure(s)\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
