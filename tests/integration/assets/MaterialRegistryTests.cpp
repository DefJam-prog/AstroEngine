#include "astro/assets/AssetRegistry.hpp"
#include "astro/assets/MaterialAsset.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

void appendBe32(std::vector<unsigned char>& output, const std::uint32_t value)
{
    output.push_back(static_cast<unsigned char>(value >> 24));
    output.push_back(static_cast<unsigned char>(value >> 16));
    output.push_back(static_cast<unsigned char>(value >> 8));
    output.push_back(static_cast<unsigned char>(value));
}

std::uint32_t crc32(const unsigned char* data, const std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void addChunk(std::vector<unsigned char>& png, const std::array<char, 4> type,
    const std::vector<unsigned char>& data)
{
    appendBe32(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    for (const auto value : type) png.push_back(static_cast<unsigned char>(value));
    png.insert(png.end(), data.begin(), data.end());
    appendBe32(png, crc32(png.data() + start, png.size() - start));
}

std::vector<unsigned char> makePng()
{
    std::vector<unsigned char> png{137,80,78,71,13,10,26,10};
    std::vector<unsigned char> ihdr;
    appendBe32(ihdr, 1); appendBe32(ihdr, 1);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    addChunk(png, {'I','H','D','R'}, ihdr);
    const std::vector<unsigned char> raw{0, 255, 255, 255, 255};
    std::vector<unsigned char> zlib{0x78, 0x01, 0x01, 5, 0, 250, 255};
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (const auto value : raw) { a = (a + value) % 65521; b = (b + a) % 65521; }
    appendBe32(zlib, (b << 16) | a);
    addChunk(png, {'I','D','A','T'}, zlib);
    addChunk(png, {'I','E','N','D'}, {});
    return png;
}

void writeBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("failed to write material registry fixture");
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    writeBytes(path, std::vector<unsigned char>(text.begin(), text.end()));
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main()
{
    using namespace astro::assets;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("Astro Material Registry " + std::to_string(unique));
    const auto legacyRoot = std::filesystem::temp_directory_path() / ("Astro Material Legacy " + std::to_string(unique));
    std::error_code error;
    try
    {
        std::filesystem::create_directories(root / "Assets" / "Materials");
        auto opened = AssetRegistry::open(root);
        check(static_cast<bool>(opened), "open a temporary project asset registry");
        if (!opened) throw std::runtime_error("cannot open material registry fixture");
        auto registry = std::move(opened).value();

        const auto texturePath = root / "Assets" / "Textures" / "base.png";
        writeBytes(texturePath, makePng());
        const auto texture = registry.importImage(std::filesystem::path("Textures") / "base.png");
        check(static_cast<bool>(texture), "register a project image for a material texture slot");
        if (!texture) throw std::runtime_error("cannot import material texture fixture");

        MaterialAssetData material;
        material.baseColorFactor = {0.2f, 0.35f, 0.7f, 0.9f};
        material.metallicFactor = 0.4f;
        material.roughnessFactor = 0.6f;
        material.emissiveFactor = {0.0f, 0.1f, 0.2f};
        material.alphaMode = MaterialAlphaMode::Blend;
        material.baseColorTexture = MaterialTextureBinding{texture.value().id, 0};
        const auto serialized = serializeMaterialAsset(material);
        check(static_cast<bool>(serialized), "serialize the material fixture");
        if (!serialized) throw std::runtime_error("cannot serialize material fixture");
        const auto materialRelative = std::filesystem::path("Materials") / "surface.astro-material";
        const auto materialPath = root / "Assets" / materialRelative;
        writeText(materialPath, serialized.value());
        const auto originalMaterialBytes = readText(materialPath);
        const auto registered = registry.registerMaterial(materialRelative);
        check(registered && registered.value().id.value > texture.value().id.value,
            "register material with a globally unique AssetId after image assets");
        if (!registered) throw std::runtime_error("valid material registration failed");
        const auto materialId = registered.value().id;
        check(registry.findMaterial(materialId) && registry.materialEntries().size() == 1,
            "find the typed material registry entry");
        const auto resolved = registry.resolveMaterial(materialId);
        check(resolved && resolved.value().material == material
            && resolved.value().sourcePath == std::filesystem::canonical(materialPath),
            "resolve and parse registered material data and image AssetId");
        check(readText(materialPath) == originalMaterialBytes,
            "registration never modifies the authored material source");

        auto editedSampling = material;
        editedSampling.baseColorWrapU = MaterialTextureWrapMode::Repeat;
        editedSampling.baseColorWrapV = MaterialTextureWrapMode::MirroredRepeat;
        editedSampling.baseColorMinFilter = MaterialTextureMinFilter::Linear;
        editedSampling.baseColorMagFilter = MaterialTextureMagFilter::Linear;
        editedSampling.baseColorUvTransform = {0.1f, 0.2f, 0.3f, 1.5f, 0.75f};
        const auto savedSampling = registry.saveMaterial(materialId, editedSampling);
        check(savedSampling && savedSampling.value().schemaVersion == 2
            && readText(materialPath).starts_with("ASTRO_MATERIAL 2\n"),
            "explicit save atomically upgrades material source and registry metadata to schema v2");
        const auto resolvedSampling = registry.resolveMaterial(materialId);
        check(resolvedSampling && resolvedSampling.value().material == editedSampling,
            "resolve saved project sampler and UV transform values without changing Material AssetId");

        constexpr std::string_view legacyMaterialText =
            "ASTRO_MATERIAL 1\nBASE_COLOR 1 1 1 1\nMETALLIC 1\nROUGHNESS 1\nEMISSIVE 0 0 0\n"
            "ALPHA_MODE OPAQUE\nALPHA_CUTOFF 0.5\nDOUBLE_SIDED 0\nNORMAL_SCALE 1\n"
            "OCCLUSION_STRENGTH 1\nTEXTURE_COUNT 0\nEND\n";
        const auto legacyMaterialRelative = std::filesystem::path("Materials") / "legacy.astro-material";
        writeText(root / "Assets" / legacyMaterialRelative, std::string(legacyMaterialText));
        const auto registeredLegacy = registry.registerMaterial(legacyMaterialRelative);
        const auto resolvedLegacy = registeredLegacy
            ? registry.resolveMaterial(registeredLegacy.value().id)
            : astro::core::Result<ResolvedMaterialAsset>::failure({
                astro::core::ErrorCode::InternalError, "legacy material registration failed"});
        check(registeredLegacy && registeredLegacy.value().schemaVersion == 1 && resolvedLegacy
            && resolvedLegacy.value().material.baseColorWrapU == MaterialTextureWrapMode::ClampToEdge
            && resolvedLegacy.value().material.baseColorMinFilter == MaterialTextureMinFilter::Nearest,
            "registry reads legacy material v1 while preserving its prior preview defaults");
        if (resolvedLegacy && registeredLegacy)
        {
            auto upgradedLegacy = resolvedLegacy.value().material;
            upgradedLegacy.schemaVersion = 2;
            upgradedLegacy.baseColorWrapU = MaterialTextureWrapMode::Repeat;
            upgradedLegacy.baseColorUvTransform.offsetU = 0.25f;
            const auto savedLegacy = registry.saveMaterial(registeredLegacy.value().id, upgradedLegacy);
            const auto reopenedLegacy = registry.resolveMaterial(registeredLegacy.value().id);
            check(savedLegacy && savedLegacy.value().id == registeredLegacy.value().id
                && savedLegacy.value().schemaVersion == 2
                && reopenedLegacy && reopenedLegacy.value().material == upgradedLegacy,
                "explicit authoring save upgrades schema-v1 material and retains its stable AssetId");
        }

        const auto indexPath = root / ".astro" / "assets.index";
        auto indexBeforeInvalidAttempts = readText(indexPath);
        check(indexBeforeInvalidAttempts.find("ASTRO_ASSETS 4\n") == 0
            && indexBeforeInvalidAttempts.find("MATERIAL ") != std::string::npos,
            "write material metadata in Asset index v4");

        auto missingTextureMaterial = material;
        missingTextureMaterial.baseColorTexture = MaterialTextureBinding{{999999}, 0};
        const auto missingText = serializeMaterialAsset(missingTextureMaterial);
        check(static_cast<bool>(missingText), "serialize a syntactically valid but unresolved texture reference");
        if (missingText)
        {
            writeText(root / "Assets" / "Materials" / "missing-texture.astro-material", missingText.value());
            const auto missing = registry.registerMaterial(std::filesystem::path("Materials") / "missing-texture.astro-material");
            check(!missing && missing.error().code == astro::core::ErrorCode::NotFound,
                "reject material references that do not name a registered image AssetId");
            check(readText(indexPath) == indexBeforeInvalidAttempts,
                "failed missing-texture registration leaves the Asset index unchanged");
        }

        const auto badVersionPath = root / "Assets" / "Materials" / "bad-version.astro-material";
        auto badVersion = originalMaterialBytes;
        const auto versionOffset = badVersion.find("ASTRO_MATERIAL 2");
        badVersion.replace(versionOffset, std::string("ASTRO_MATERIAL 2").size(), "ASTRO_MATERIAL 9");
        writeText(badVersionPath, badVersion);
        const auto rejectedVersion = registry.registerMaterial(std::filesystem::path("Materials") / "bad-version.astro-material");
        check(!rejectedVersion && rejectedVersion.error().code == astro::core::ErrorCode::VersionMismatch,
            "reject unsupported material file versions");
        check(readText(indexPath) == indexBeforeInvalidAttempts,
            "failed schema-version registration leaves the Asset index unchanged");

        material.roughnessFactor = 0.25f;
        const auto editedText = serializeMaterialAsset(material);
        check(static_cast<bool>(editedText), "serialize an explicitly edited material source");
        if (editedText)
        {
            writeText(materialPath, editedText.value());
            const auto stale = registry.resolveMaterial(materialId);
            check(!stale && stale.error().code == astro::core::ErrorCode::InvalidState,
                "diagnose changed source bytes as stale before re-registration");
            const auto staleSave = registry.saveMaterial(materialId, editedSampling);
            check(!staleSave && readText(materialPath) == editedText.value(),
                "refuse to overwrite externally changed material bytes before re-registration");
            const auto refreshed = registry.registerMaterial(materialRelative);
            check(refreshed && refreshed.value().id == materialId,
                "explicit re-registration refreshes source metadata without changing AssetId");
            check(readText(materialPath) == editedText.value(),
                "re-registration leaves user-edited material bytes untouched");
            const auto resolvedEdited = registry.resolveMaterial(materialId);
            check(resolvedEdited && resolvedEdited.value().material == material,
                "resolve the explicitly re-registered material revision");
        }

        auto reopened = AssetRegistry::open(root);
        check(reopened && reopened.value().findMaterial(materialId)
            && reopened.value().entries().contains(texture.value().id),
            "reopen Asset index v4 while preserving material and image IDs");

        std::filesystem::create_directories(legacyRoot / ".astro");
        std::filesystem::create_directories(legacyRoot / "Assets" / "Materials");
        constexpr std::string_view legacyHash = "0000000000000000000000000000000000000000000000000000000000000000";
        const std::string legacyIndex = "ASTRO_ASSETS 3\nNEXT 2\nASSET 1 \"Textures/old.png\" PNG 1 1 4 "
            + std::string(legacyHash) + "\nEND\n";
        const auto legacyIndexPath = legacyRoot / ".astro" / "assets.index";
        writeText(legacyIndexPath, legacyIndex);
        auto legacyOpened = AssetRegistry::open(legacyRoot);
        check(legacyOpened && legacyOpened.value().entries().contains(AssetId{1}),
            "read legacy Asset index v3");
        check(readText(legacyIndexPath) == legacyIndex,
            "opening legacy index does not rewrite it before an explicit registration");
        const auto legacyMaterialV2 = serializeMaterialAsset(MaterialAssetData{});
        if (legacyMaterialV2)
        {
            writeText(legacyRoot / "Assets" / "Materials" / "default.astro-material", legacyMaterialV2.value());
            auto migrated = legacyOpened.value().registerMaterial(std::filesystem::path("Materials") / "default.astro-material");
            check(migrated && migrated.value().id == AssetId{2},
                "legacy ID counter allocates next stable ID while registering a material");
            const auto migratedIndex = readText(legacyIndexPath);
            check(migratedIndex.starts_with("ASTRO_ASSETS 4\n")
                && migratedIndex.find("ASSET 1 \"Textures/old.png\"") != std::string::npos
                && migratedIndex.find("MATERIAL 2 ") != std::string::npos,
                "first explicit material registration migrates v3 index and retains old asset records");
        }
    }
    catch (const std::exception& exception)
    {
        std::cerr << "UNEXPECTED: " << exception.what() << '\n';
        ++failures;
    }
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::remove_all(legacyRoot, error);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
