#include "astro/assets/MaterialAsset.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace astro::assets
{
namespace
{
constexpr std::size_t MaxMaterialBytes = 64 * 1024;

core::Result<void> validationError(std::string message)
{
    return core::Result<void>::failure({core::ErrorCode::ValidationError, std::move(message)});
}

core::Result<MaterialAssetData> parseError(const core::ErrorCode code, std::string message)
{
    return core::Result<MaterialAssetData>::failure({code, std::move(message)});
}

std::string_view alphaModeName(const MaterialAlphaMode mode)
{
    switch (mode)
    {
    case MaterialAlphaMode::Opaque: return "OPAQUE";
    case MaterialAlphaMode::Mask: return "MASK";
    case MaterialAlphaMode::Blend: return "BLEND";
    }
    return {};
}

bool parseAlphaMode(const std::string_view name, MaterialAlphaMode& mode)
{
    if (name == "OPAQUE") mode = MaterialAlphaMode::Opaque;
    else if (name == "MASK") mode = MaterialAlphaMode::Mask;
    else if (name == "BLEND") mode = MaterialAlphaMode::Blend;
    else return false;
    return true;
}

struct TextureSlot
{
    std::string_view name;
    std::optional<MaterialTextureBinding> MaterialAssetData::*member;
};

constexpr std::array<TextureSlot, 5> TextureSlots{{
    {"BASE_COLOR", &MaterialAssetData::baseColorTexture},
    {"METALLIC_ROUGHNESS", &MaterialAssetData::metallicRoughnessTexture},
    {"NORMAL", &MaterialAssetData::normalTexture},
    {"OCCLUSION", &MaterialAssetData::occlusionTexture},
    {"EMISSIVE", &MaterialAssetData::emissiveTexture}
}};

std::string_view wrapName(const MaterialTextureWrapMode mode)
{
    switch (mode)
    {
    case MaterialTextureWrapMode::Repeat: return "REPEAT";
    case MaterialTextureWrapMode::MirroredRepeat: return "MIRRORED_REPEAT";
    case MaterialTextureWrapMode::ClampToEdge: return "CLAMP_TO_EDGE";
    }
    return {};
}

bool parseWrap(const std::string_view name, MaterialTextureWrapMode& mode)
{
    if (name == "REPEAT") mode = MaterialTextureWrapMode::Repeat;
    else if (name == "MIRRORED_REPEAT") mode = MaterialTextureWrapMode::MirroredRepeat;
    else if (name == "CLAMP_TO_EDGE") mode = MaterialTextureWrapMode::ClampToEdge;
    else return false;
    return true;
}

std::string_view minFilterName(const MaterialTextureMinFilter mode)
{
    switch (mode)
    {
    case MaterialTextureMinFilter::Nearest: return "NEAREST";
    case MaterialTextureMinFilter::Linear: return "LINEAR";
    case MaterialTextureMinFilter::NearestMipmapNearest: return "NEAREST_MIPMAP_NEAREST";
    case MaterialTextureMinFilter::LinearMipmapNearest: return "LINEAR_MIPMAP_NEAREST";
    case MaterialTextureMinFilter::NearestMipmapLinear: return "NEAREST_MIPMAP_LINEAR";
    case MaterialTextureMinFilter::LinearMipmapLinear: return "LINEAR_MIPMAP_LINEAR";
    }
    return {};
}

bool parseMinFilter(const std::string_view name, MaterialTextureMinFilter& mode)
{
    constexpr std::array modes{MaterialTextureMinFilter::Nearest, MaterialTextureMinFilter::Linear,
        MaterialTextureMinFilter::NearestMipmapNearest, MaterialTextureMinFilter::LinearMipmapNearest,
        MaterialTextureMinFilter::NearestMipmapLinear, MaterialTextureMinFilter::LinearMipmapLinear};
    for (const auto candidate : modes)
        if (minFilterName(candidate) == name) { mode = candidate; return true; }
    return false;
}

std::string_view magFilterName(const MaterialTextureMagFilter mode)
{
    switch (mode)
    {
    case MaterialTextureMagFilter::Nearest: return "NEAREST";
    case MaterialTextureMagFilter::Linear: return "LINEAR";
    }
    return {};
}

bool parseMagFilter(const std::string_view name, MaterialTextureMagFilter& mode)
{
    if (name == "NEAREST") mode = MaterialTextureMagFilter::Nearest;
    else if (name == "LINEAR") mode = MaterialTextureMagFilter::Linear;
    else return false;
    return true;
}

bool finiteInRange(const float value, const float minimum, const float maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool readFloatRow(const std::string& line, const std::string_view expected,
    const std::span<float> values)
{
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    std::string marker;
    if (!(input >> marker) || marker != expected) return false;
    for (auto& value : values)
        if (!(input >> value)) return false;
    std::string extra;
    return !(input >> extra);
}

bool readScalarRow(const std::string& line, const std::string_view expected, float& value)
{
    return readFloatRow(line, expected, std::span<float>(&value, 1));
}

bool readCountRow(const std::string& line, const std::string_view expected, std::size_t& value)
{
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    std::string marker;
    std::string extra;
    return (input >> marker >> value) && marker == expected && !(input >> extra);
}

bool readBoolRow(const std::string& line, const std::string_view expected, bool& value)
{
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    std::string marker;
    int raw = -1;
    std::string extra;
    if (!(input >> marker >> raw) || marker != expected || (input >> extra) || (raw != 0 && raw != 1))
        return false;
    value = raw == 1;
    return true;
}

bool readTextureRow(const std::string& line, std::string& role, MaterialTextureBinding& binding)
{
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    std::string marker;
    std::string assetIdText;
    std::string texCoordText;
    std::string extra;
    if (!(input >> marker >> role >> assetIdText >> texCoordText) || marker != "TEXTURE" || (input >> extra))
        return false;
    const auto assetId = std::from_chars(assetIdText.data(), assetIdText.data() + assetIdText.size(),
        binding.imageAssetId.value);
    const auto texCoord = std::from_chars(texCoordText.data(), texCoordText.data() + texCoordText.size(),
        binding.texCoord);
    return assetId.ec == std::errc{} && assetId.ptr == assetIdText.data() + assetIdText.size()
        && texCoord.ec == std::errc{} && texCoord.ptr == texCoordText.data() + texCoordText.size();
}

void writeTextureRow(std::ostringstream& output, const std::string_view role,
    const std::optional<MaterialTextureBinding>& binding)
{
    if (binding)
        output << "TEXTURE " << role << ' ' << binding->imageAssetId.value << ' ' << binding->texCoord << '\n';
}
}

core::Result<void> validateMaterialAsset(const MaterialAssetData& material)
{
    if (material.schemaVersion != 1 && material.schemaVersion != 2)
        return validationError("Unsupported material asset schema version");
    for (const auto value : material.baseColorFactor)
        if (!finiteInRange(value, 0.0f, 1.0f))
            return validationError("Material base color factors must be finite values in [0, 1]");
    if (!finiteInRange(material.metallicFactor, 0.0f, 1.0f))
        return validationError("Material metallic factor must be a finite value in [0, 1]");
    if (!finiteInRange(material.roughnessFactor, 0.0f, 1.0f))
        return validationError("Material roughness factor must be a finite value in [0, 1]");
    for (const auto value : material.emissiveFactor)
        if (!finiteInRange(value, 0.0f, 1.0f))
            return validationError("Material emissive factors must be finite values in [0, 1] for schema version 1");
    if (alphaModeName(material.alphaMode).empty())
        return validationError("Material alpha mode is invalid");
    if (!finiteInRange(material.alphaCutoff, 0.0f, 1.0f))
        return validationError("Material alpha cutoff must be a finite value in [0, 1]");
    if (!std::isfinite(material.normalScale) || material.normalScale < 0.0f)
        return validationError("Material normal scale must be finite and non-negative");
    if (!finiteInRange(material.occlusionStrength, 0.0f, 1.0f))
        return validationError("Material occlusion strength must be a finite value in [0, 1]");
    if (material.schemaVersion >= 2)
    {
        if (wrapName(material.baseColorWrapU).empty() || wrapName(material.baseColorWrapV).empty()
            || minFilterName(material.baseColorMinFilter).empty() || magFilterName(material.baseColorMagFilter).empty())
            return validationError("Material base-color sampler contains an invalid enum value");
        const auto& transform = material.baseColorUvTransform;
        constexpr float TransformLimit = 1'000'000.0f;
        for (const float value : {transform.offsetU, transform.offsetV, transform.rotationRadians,
                 transform.scaleU, transform.scaleV})
            if (!finiteInRange(value, -TransformLimit, TransformLimit))
                return validationError("Material base-color UV transform values must be finite and within +/-1000000");
    }
    for (const auto& slot : TextureSlots)
    {
        const auto& binding = material.*(slot.member);
        if (binding && binding->imageAssetId.value == 0)
            return validationError("Material texture bindings must use nonzero image AssetIds");
    }
    return core::Result<void>::success();
}

core::Result<MaterialAssetData> parseMaterialAsset(const std::string_view text)
{
    if (text.empty() || text.size() > MaxMaterialBytes)
        return parseError(core::ErrorCode::ValidationError, "Material asset must contain 1 byte through 64 KiB");

    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    std::string line;
    const auto nextLine = [&]() {
        if (!std::getline(input, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };
    if (!nextLine()) return parseError(core::ErrorCode::ParseError, "Material asset header is missing");
    std::istringstream header(line);
    header.imbue(std::locale::classic());
    std::string magic, extra;
    int version = 0;
    if (!(header >> magic >> version) || (header >> extra) || magic != "ASTRO_MATERIAL")
        return parseError(core::ErrorCode::ParseError, "Material asset header is invalid");
    if (version != 1 && version != 2)
        return parseError(core::ErrorCode::VersionMismatch, "Unsupported material asset format version");

    MaterialAssetData material;
    material.schemaVersion = static_cast<std::uint32_t>(version);
    const auto readRequired = [&]() {
        return nextLine() && !line.empty();
    };
    if (!readRequired() || !readFloatRow(line, "BASE_COLOR", material.baseColorFactor))
        return parseError(core::ErrorCode::ParseError, "Material BASE_COLOR row is missing or invalid");
    if (!readRequired() || !readScalarRow(line, "METALLIC", material.metallicFactor))
        return parseError(core::ErrorCode::ParseError, "Material METALLIC row is missing or invalid");
    if (!readRequired() || !readScalarRow(line, "ROUGHNESS", material.roughnessFactor))
        return parseError(core::ErrorCode::ParseError, "Material ROUGHNESS row is missing or invalid");
    if (!readRequired() || !readFloatRow(line, "EMISSIVE", material.emissiveFactor))
        return parseError(core::ErrorCode::ParseError, "Material EMISSIVE row is missing or invalid");
    if (!nextLine()) return parseError(core::ErrorCode::ParseError, "Material ALPHA_MODE row is missing");
    {
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string marker, mode;
        if (!(row >> marker >> mode) || marker != "ALPHA_MODE" || (row >> extra)
            || !parseAlphaMode(mode, material.alphaMode))
            return parseError(core::ErrorCode::ParseError, "Material ALPHA_MODE row is invalid");
    }
    if (!readRequired() || !readScalarRow(line, "ALPHA_CUTOFF", material.alphaCutoff))
        return parseError(core::ErrorCode::ParseError, "Material ALPHA_CUTOFF row is missing or invalid");
    if (!readRequired() || !readBoolRow(line, "DOUBLE_SIDED", material.doubleSided))
        return parseError(core::ErrorCode::ParseError, "Material DOUBLE_SIDED row is missing or invalid");
    if (!readRequired() || !readScalarRow(line, "NORMAL_SCALE", material.normalScale))
        return parseError(core::ErrorCode::ParseError, "Material NORMAL_SCALE row is missing or invalid");
    if (!readRequired() || !readScalarRow(line, "OCCLUSION_STRENGTH", material.occlusionStrength))
        return parseError(core::ErrorCode::ParseError, "Material OCCLUSION_STRENGTH row is missing or invalid");

    std::size_t textureCount = 0;
    if (!nextLine() || !readCountRow(line, "TEXTURE_COUNT", textureCount) || textureCount > TextureSlots.size())
        return parseError(core::ErrorCode::ParseError, "Material texture count is invalid");
    std::set<std::string> seenRoles;
    for (std::size_t index = 0; index < textureCount; ++index)
    {
        MaterialTextureBinding binding;
        std::string role;
        if (!nextLine() || !readTextureRow(line, role, binding))
            return parseError(core::ErrorCode::ParseError, "Material texture binding row is invalid");
        const auto slot = std::find_if(TextureSlots.begin(), TextureSlots.end(), [&](const TextureSlot& item) {
            return item.name == role;
        });
        if (slot == TextureSlots.end() || !seenRoles.emplace(role).second)
            return parseError(core::ErrorCode::ValidationError, "Material contains an unknown or duplicate texture role");
        material.*(slot->member) = binding;
    }

    if (version == 2)
    {
        std::string marker, wrapU, wrapV, minFilter, magFilter, extra;
        if (!nextLine()) return parseError(core::ErrorCode::ParseError, "Material base-color sampler row is missing");
        std::istringstream sampler(line);
        sampler.imbue(std::locale::classic());
        if (!(sampler >> marker >> wrapU >> wrapV >> minFilter >> magFilter) || marker != "BASE_COLOR_SAMPLER"
            || (sampler >> extra) || !parseWrap(wrapU, material.baseColorWrapU)
            || !parseWrap(wrapV, material.baseColorWrapV)
            || !parseMinFilter(minFilter, material.baseColorMinFilter)
            || !parseMagFilter(magFilter, material.baseColorMagFilter))
            return parseError(core::ErrorCode::ParseError, "Material base-color sampler row is invalid");

        if (!nextLine()) return parseError(core::ErrorCode::ParseError, "Material UV transform row is missing");
        std::istringstream transform(line);
        transform.imbue(std::locale::classic());
        if (!(transform >> marker >> material.baseColorUvTransform.offsetU >> material.baseColorUvTransform.offsetV
                >> material.baseColorUvTransform.rotationRadians >> material.baseColorUvTransform.scaleU
                >> material.baseColorUvTransform.scaleV) || marker != "BASE_COLOR_UV_TRANSFORM" || (transform >> extra))
            return parseError(core::ErrorCode::ParseError, "Material base-color UV transform row is invalid");
    }

    if (!nextLine() || line != "END")
        return parseError(core::ErrorCode::ParseError, "Material asset terminator is missing or invalid");
    while (nextLine())
        if (line.find_first_not_of(" \t\r") != std::string::npos)
            return parseError(core::ErrorCode::ParseError, "Unexpected data after material asset terminator");
    if (input.bad()) return parseError(core::ErrorCode::IoError, "Cannot read material asset text");

    const auto valid = validateMaterialAsset(material);
    if (!valid) return parseError(valid.error().code, valid.error().message);
    return core::Result<MaterialAssetData>::success(std::move(material));
}

core::Result<std::string> serializeMaterialAsset(const MaterialAssetData& material)
{
    const auto valid = validateMaterialAsset(material);
    if (!valid)
        return core::Result<std::string>::failure(valid.error());
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "ASTRO_MATERIAL " << material.schemaVersion << '\n'
           << "BASE_COLOR " << material.baseColorFactor[0] << ' ' << material.baseColorFactor[1] << ' '
           << material.baseColorFactor[2] << ' ' << material.baseColorFactor[3] << '\n'
           << "METALLIC " << material.metallicFactor << '\n'
           << "ROUGHNESS " << material.roughnessFactor << '\n'
           << "EMISSIVE " << material.emissiveFactor[0] << ' ' << material.emissiveFactor[1] << ' '
           << material.emissiveFactor[2] << '\n'
           << "ALPHA_MODE " << alphaModeName(material.alphaMode) << '\n'
           << "ALPHA_CUTOFF " << material.alphaCutoff << '\n'
           << "DOUBLE_SIDED " << (material.doubleSided ? 1 : 0) << '\n'
           << "NORMAL_SCALE " << material.normalScale << '\n'
           << "OCCLUSION_STRENGTH " << material.occlusionStrength << '\n';
    std::size_t textureCount = 0;
    for (const auto& slot : TextureSlots)
        if (material.*(slot.member)) ++textureCount;
    output << "TEXTURE_COUNT " << textureCount << '\n';
    for (const auto& slot : TextureSlots)
        writeTextureRow(output, slot.name, material.*(slot.member));
    if (material.schemaVersion >= 2)
        output << "BASE_COLOR_SAMPLER " << wrapName(material.baseColorWrapU) << ' '
               << wrapName(material.baseColorWrapV) << ' ' << minFilterName(material.baseColorMinFilter) << ' '
               << magFilterName(material.baseColorMagFilter) << '\n'
               << "BASE_COLOR_UV_TRANSFORM " << material.baseColorUvTransform.offsetU << ' '
               << material.baseColorUvTransform.offsetV << ' ' << material.baseColorUvTransform.rotationRadians << ' '
               << material.baseColorUvTransform.scaleU << ' ' << material.baseColorUvTransform.scaleV << '\n';
    output << "END\n";
    auto text = output.str();
    if (text.size() > MaxMaterialBytes)
        return core::Result<std::string>::failure({core::ErrorCode::ValidationError,
            "Serialized material asset exceeds 64 KiB"});
    return core::Result<std::string>::success(std::move(text));
}
}
