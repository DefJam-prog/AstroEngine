#include "astro/assets/MaterialAsset.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

void replaceOnce(std::string& text, const std::string_view from, const std::string_view to)
{
    const auto position = text.find(from);
    if (position == std::string::npos) throw std::runtime_error("material test fixture text was not found");
    text.replace(position, from.size(), to);
}
}

int main()
{
    using namespace astro::assets;

    MaterialAssetData defaults;
    auto encodedDefaults = serializeMaterialAsset(defaults);
    check(encodedDefaults.hasValue(), "serialize default material values");
    if (encodedDefaults)
    {
        auto parsedDefaults = parseMaterialAsset(encodedDefaults.value());
        check(parsedDefaults && parsedDefaults.value() == defaults,
            "default values survive a material file round-trip");
    }

    MaterialAssetData authored;
    authored.baseColorFactor = {0.125f, 0.25f, 0.5f, 0.75f};
    authored.metallicFactor = 0.33f;
    authored.roughnessFactor = 0.77f;
    authored.emissiveFactor = {0.0f, 0.2f, 1.0f};
    authored.alphaMode = MaterialAlphaMode::Mask;
    authored.alphaCutoff = 0.42f;
    authored.doubleSided = true;
    authored.normalScale = 0.8f;
    authored.occlusionStrength = 0.65f;
    authored.baseColorTexture = MaterialTextureBinding{{12}, 0};
    authored.metallicRoughnessTexture = MaterialTextureBinding{{13}, 1};
    authored.normalTexture = MaterialTextureBinding{{14}, 2};
    authored.occlusionTexture = MaterialTextureBinding{{15}, 0};
    authored.emissiveTexture = MaterialTextureBinding{{16}, 0};
    auto encoded = serializeMaterialAsset(authored);
    check(encoded.hasValue(), "serialize material factors, alpha policy, and all texture roles");
    if (encoded)
    {
        auto parsed = parseMaterialAsset(encoded.value());
        check(parsed && parsed.value() == authored, "all material fields and semantic image references round-trip");
        check(encoded.value().starts_with("ASTRO_MATERIAL 2\n"), "new material writes use schema version 2");
    }

    auto invalid = defaults;
    invalid.baseColorFactor[0] = std::numeric_limits<float>::quiet_NaN();
    check(!validateMaterialAsset(invalid) && !serializeMaterialAsset(invalid),
        "reject non-finite base-color factors before serialization");
    invalid = defaults; invalid.metallicFactor = -0.01f;
    check(!validateMaterialAsset(invalid), "reject metallic factors below zero");
    invalid = defaults; invalid.roughnessFactor = 1.01f;
    check(!validateMaterialAsset(invalid), "reject roughness factors above one");
    invalid = defaults; invalid.emissiveFactor[1] = 2.0f;
    check(!validateMaterialAsset(invalid), "reject out-of-range schema-v1 emissive factors");
    invalid = defaults; invalid.alphaCutoff = std::numeric_limits<float>::infinity();
    check(!validateMaterialAsset(invalid), "reject non-finite alpha cutoff");
    invalid = defaults; invalid.normalScale = -1.0f;
    check(!validateMaterialAsset(invalid), "reject negative normal scale");
    invalid = defaults; invalid.occlusionStrength = 1.1f;
    check(!validateMaterialAsset(invalid), "reject out-of-range occlusion strength");
    invalid = defaults; invalid.baseColorTexture = MaterialTextureBinding{{0}, 0};
    check(!validateMaterialAsset(invalid), "reject zero image AssetId");

    std::string malformed = encodedDefaults ? encodedDefaults.value() : std::string{};
    replaceOnce(malformed, "ASTRO_MATERIAL 2", "ASTRO_MATERIAL 9");
    const auto unsupportedVersion = parseMaterialAsset(malformed);
    check(!unsupportedVersion && unsupportedVersion.error().code == astro::core::ErrorCode::VersionMismatch,
        "report an explicit unsupported schema version");

    malformed = encodedDefaults ? encodedDefaults.value() : std::string{};
    replaceOnce(malformed, "METALLIC 1", "METALLIC nan");
    check(!parseMaterialAsset(malformed), "reject a non-finite scalar in source text");

    malformed = encodedDefaults ? encodedDefaults.value() : std::string{};
    replaceOnce(malformed, "TEXTURE_COUNT 0", "UNKNOWN_FIELD 7\nTEXTURE_COUNT 0");
    check(!parseMaterialAsset(malformed), "reject unknown schema fields without rewriting source data");

    malformed = encodedDefaults ? encodedDefaults.value() : std::string{};
    replaceOnce(malformed, "TEXTURE_COUNT 0", "TEXTURE_COUNT 2\nTEXTURE BASE_COLOR 9 0\nTEXTURE BASE_COLOR 10 0");
    check(!parseMaterialAsset(malformed), "reject duplicate texture roles");

    malformed = encodedDefaults ? encodedDefaults.value() : std::string{};
    replaceOnce(malformed, "TEXTURE_COUNT 0", "TEXTURE_COUNT 1\nTEXTURE UNKNOWN 9 0");
    check(!parseMaterialAsset(malformed), "reject unknown texture roles");

    constexpr std::string_view legacyV1 =
        "ASTRO_MATERIAL 1\nBASE_COLOR 1 1 1 1\nMETALLIC 1\nROUGHNESS 1\nEMISSIVE 0 0 0\n"
        "ALPHA_MODE OPAQUE\nALPHA_CUTOFF 0.5\nDOUBLE_SIDED 0\nNORMAL_SCALE 1\n"
        "OCCLUSION_STRENGTH 1\nTEXTURE_COUNT 0\nEND\n";
    const auto migratedV1 = parseMaterialAsset(legacyV1);
    check(migratedV1 && migratedV1.value().schemaVersion == 1
        && migratedV1.value().baseColorWrapU == MaterialTextureWrapMode::ClampToEdge
        && migratedV1.value().baseColorMinFilter == MaterialTextureMinFilter::Nearest
        && migratedV1.value().baseColorUvTransform == MaterialUvTransform{},
        "read schema v1 with the historical Clamp + Point and identity transform defaults");

    auto samplerMaterial = defaults;
    samplerMaterial.baseColorWrapU = MaterialTextureWrapMode::MirroredRepeat;
    samplerMaterial.baseColorWrapV = MaterialTextureWrapMode::Repeat;
    samplerMaterial.baseColorMinFilter = MaterialTextureMinFilter::LinearMipmapLinear;
    samplerMaterial.baseColorMagFilter = MaterialTextureMagFilter::Linear;
    samplerMaterial.baseColorUvTransform = {0.25f, -0.5f, 0.75f, 2.0f, 3.0f};
    const auto samplerText = serializeMaterialAsset(samplerMaterial);
    bool samplerRoundTripPassed = false;
    if (samplerText)
    {
        const auto samplerRoundTrip = parseMaterialAsset(samplerText.value());
        samplerRoundTripPassed = samplerRoundTrip && samplerRoundTrip.value() == samplerMaterial;
    }
    check(samplerRoundTripPassed,
        "round-trip project sampler and UV transform fields in schema v2");
    invalid = samplerMaterial; invalid.baseColorUvTransform.rotationRadians = std::numeric_limits<float>::infinity();
    check(!validateMaterialAsset(invalid), "reject non-finite schema-v2 UV transform values");
    malformed = samplerText ? samplerText.value() : std::string{};
    replaceOnce(malformed, "BASE_COLOR_SAMPLER MIRRORED_REPEAT", "BASE_COLOR_SAMPLER BOGUS");
    check(!parseMaterialAsset(malformed), "reject unknown v2 sampler enums");

    check(!parseMaterialAsset(std::string(64 * 1024 + 1, 'x')),
        "reject material source larger than the bounded format limit");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
