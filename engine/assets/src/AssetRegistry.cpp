#include "astro/assets/AssetRegistry.hpp"
#include "astro/assets/MaterialAsset.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <set>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace astro::assets
{
namespace
{
constexpr std::uint64_t MaxImageBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t MaxIndexBytes = 16ull * 1024ull * 1024ull;
constexpr std::size_t MaxAssetEntries = 100000;
constexpr std::size_t MaxAssetPathBytes = 4096;
constexpr std::size_t MaxScriptSourceBytes = 16ull * 1024ull * 1024ull;
constexpr std::size_t MaxModelSourceBytes = 8ull * 1024ull * 1024ull;
constexpr std::size_t MaxMaterialSourceBytes = 64ull * 1024ull;
constexpr std::size_t MaxManagedTypeNameBytes = 512;
constexpr std::size_t MaxScriptBuildStateBytes = 4ull * 1024ull * 1024ull;
constexpr std::size_t MaxScriptBuildSources = 4096;
constexpr std::uint64_t MaxScriptBuildOutputBytes = 512ull * 1024ull * 1024ull;
constexpr std::uint64_t MaxPixels = 268435456ull;
constexpr std::uint32_t MaxDimension = 65535;

core::Result<AssetRegistry> assetError(const core::ErrorCode code, std::string message)
{
    return core::Result<AssetRegistry>::failure({code, std::move(message)});
}

core::Result<AssetReference> referenceError(const core::ErrorCode code, std::string message)
{
    return core::Result<AssetReference>::failure({code, std::move(message)});
}

core::Result<ScriptAssetReference> scriptReferenceError(const core::ErrorCode code, std::string message)
{
    return core::Result<ScriptAssetReference>::failure({code, std::move(message)});
}

core::Result<ModelAssetReference> modelReferenceError(const core::ErrorCode code, std::string message)
{
    return core::Result<ModelAssetReference>::failure({code, std::move(message)});
}

core::Result<MaterialAssetReference> materialReferenceError(const core::ErrorCode code, std::string message)
{
    return core::Result<MaterialAssetReference>::failure({code, std::move(message)});
}

core::Result<ResolvedScriptAsset> resolvedScriptError(const core::ErrorCode code, std::string message)
{
    return core::Result<ResolvedScriptAsset>::failure({code, std::move(message)});
}

core::Result<ResolvedModelAsset> resolvedModelError(const core::ErrorCode code, std::string message)
{
    return core::Result<ResolvedModelAsset>::failure({code, std::move(message)});
}

core::Result<ResolvedMaterialAsset> resolvedMaterialError(const core::ErrorCode code, std::string message)
{
    return core::Result<ResolvedMaterialAsset>::failure({code, std::move(message)});
}

template<typename MaterialType>
core::Result<void> validateMaterialImageReferences(const MaterialType& material,
    const std::map<AssetId, AssetReference>& images)
{
    const std::array<const std::optional<MaterialTextureBinding>*, 5> bindings{
        &material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture,
        &material.occlusionTexture, &material.emissiveTexture};
    for (const auto* binding : bindings)
    {
        if (!*binding) continue;
        if (!(*binding)->imageAssetId.value || !images.contains((*binding)->imageAssetId))
            return core::Result<void>::failure({core::ErrorCode::NotFound,
                "Material texture reference is not a registered image AssetId"});
    }
    return core::Result<void>::success();
}

bool hasExtra(std::istringstream& input)
{
    std::string extra;
    return static_cast<bool>(input >> extra);
}

std::string utf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path fromUtf8(const std::string& text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool validUtf8(const std::string_view value)
{
    const auto continuation = [&value](const std::size_t index) {
        return index < value.size() && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80;
    };
    for (std::size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i]);
        if (lead <= 0x7F) { ++i; continue; }
        if (lead >= 0xC2 && lead <= 0xDF && continuation(i + 1)) { i += 2; continue; }
        if (lead >= 0xE0 && lead <= 0xEF && continuation(i + 1) && continuation(i + 2))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xE0 || second >= 0xA0) && (lead != 0xED || second < 0xA0)) { i += 3; continue; }
        }
        if (lead >= 0xF0 && lead <= 0xF4 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xF0 || second >= 0x90) && (lead != 0xF4 || second <= 0x8F)) { i += 4; continue; }
        }
        return false;
    }
    return true;
}

bool validRelative(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path)
        if (part == ".." || part == ".") return false;
    return true;
}

bool isWithin(const std::filesystem::path& candidate, const std::filesystem::path& base);

bool validManagedTypeName(const std::string_view name)
{
    if (name.empty() || name.size() > MaxManagedTypeNameBytes || !validUtf8(name)) return false;
    for (const auto character : name)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte <= 0x20 || byte == 0x7F) return false;
    }
    return true;
}

bool hasPathPrefix(const std::filesystem::path& path, const std::filesystem::path& prefix)
{
    auto pathPart = path.begin();
    for (auto prefixPart = prefix.begin(); prefixPart != prefix.end(); ++prefixPart, ++pathPart)
        if (pathPart == path.end() || *pathPart != *prefixPart) return false;
    return true;
}

core::Result<std::filesystem::path> resolveProjectFile(const std::filesystem::path& projectRoot,
    const std::filesystem::path& relativePath, const std::filesystem::path& requiredRoot,
    const std::string_view extension, const std::string_view description)
{
    const auto filename = utf8(relativePath.filename());
    if (!validRelative(relativePath) || relativePath != relativePath.lexically_normal()
        || !hasPathPrefix(relativePath, requiredRoot) || !filename.ends_with(extension))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            std::string(description) + " path must stay inside " + utf8(requiredRoot)
                + " and use the required file extension"});

    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot / requiredRoot, error);
    if (error || !std::filesystem::is_directory(root, error) || error)
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::NotFound,
            std::string(description) + " directory is missing"});
    const auto unresolved = projectRoot / relativePath;
    const auto status = std::filesystem::symlink_status(unresolved, error);
    if (error)
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::NotFound,
            std::string(description) + " file is missing: " + error.message()});
    if (std::filesystem::is_symlink(status))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            std::string(description) + " file must not be a symbolic link"});
    const auto candidate = std::filesystem::canonical(unresolved, error);
    if (error || !isWithin(candidate, root))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            std::string(description) + " file resolves outside its project directory"});
    if (!std::filesystem::is_regular_file(candidate, error) || error)
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::NotFound,
            std::string(description) + " file is missing or is not a regular file"});
    return core::Result<std::filesystem::path>::success(candidate);
}

core::Result<std::vector<unsigned char>> readScriptSource(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
            "Cannot inspect C# source: " + error.message()});
    if (size == 0 || size > MaxScriptSourceBytes)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::ValidationError,
            "C# source must contain 1 byte through 16 MiB"});
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
            "Cannot open C# source"});
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof())
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
            "C# source changed or could not be read completely"});
    if (!validUtf8(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())))
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::ValidationError,
            "C# source is not valid UTF-8"});
    return core::Result<std::vector<unsigned char>>::success(std::move(bytes));
}

core::Result<std::vector<unsigned char>> readModelSource(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
            "Cannot inspect model source: " + error.message()});
    if (size == 0 || size > MaxModelSourceBytes)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::ValidationError,
            "Model source must contain 1 byte through 8 MiB"});
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
            "Cannot open model source"});
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof())
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
            "Model source changed or could not be read completely"});
    return core::Result<std::vector<unsigned char>>::success(std::move(bytes));
}

bool isWithin(const std::filesystem::path& candidate, const std::filesystem::path& base)
{
    const auto relative = candidate.lexically_relative(base);
    return validRelative(relative);
}

std::uint32_t be32(const std::vector<unsigned char>& bytes, const std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint16_t be16(const std::vector<unsigned char>& bytes, const std::size_t offset)
{
    return static_cast<std::uint16_t>((static_cast<unsigned>(bytes[offset]) << 8) | bytes[offset + 1]);
}

struct ImageInfo
{
    ImageFormat format;
    std::uint32_t width;
    std::uint32_t height;
};

bool validDimensions(const std::uint32_t width, const std::uint32_t height)
{
    return width > 0 && height > 0 && width <= MaxDimension && height <= MaxDimension
        && static_cast<std::uint64_t>(width) * height <= MaxPixels;
}

std::uint32_t crc32(const unsigned char* data, const std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

core::Result<ImageInfo> inspectPng(const std::vector<unsigned char>& bytes)
{
    static constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 45 || !std::equal(signature.begin(), signature.end(), bytes.begin()))
        return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "Invalid or truncated PNG"});
    std::size_t offset = signature.size();
    bool haveHeader = false, havePalette = false, haveImageData = false, haveEnd = false, imageDataEnded = false;
    std::uint32_t width = 0, height = 0;
    unsigned char colorType = 0;
    while (offset + 12 <= bytes.size())
    {
        const auto length = be32(bytes, offset);
        if (length > bytes.size() - offset - 12)
            return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG chunk exceeds file bounds"});
        const auto* type = bytes.data() + offset + 4;
        const auto crcOffset = offset + 8 + length;
        if (crc32(type, static_cast<std::size_t>(length) + 4) != be32(bytes, crcOffset))
            return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG chunk checksum is invalid"});
        if (!haveHeader)
        {
            if (std::memcmp(type, "IHDR", 4) != 0 || length != 13)
                return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG does not start with a valid IHDR chunk"});
            width = be32(bytes, offset + 8);
            height = be32(bytes, offset + 12);
            const auto bitDepth = bytes[offset + 16];
            colorType = bytes[offset + 17];
            const bool legalDepth = (colorType == 0 && (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16))
                || (colorType == 2 && (bitDepth == 8 || bitDepth == 16))
                || (colorType == 3 && (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8))
                || (colorType == 4 && (bitDepth == 8 || bitDepth == 16))
                || (colorType == 6 && (bitDepth == 8 || bitDepth == 16));
            if (!validDimensions(width, height) || !legalDepth || bytes[offset + 18] != 0
                || bytes[offset + 19] != 0 || bytes[offset + 20] > 1)
                return core::Result<ImageInfo>::failure({core::ErrorCode::ValidationError,
                    "PNG IHDR values are outside supported limits (" + std::to_string(width) + "x" + std::to_string(height)
                    + ", depth " + std::to_string(bitDepth) + ", color " + std::to_string(colorType) + ")"});
            haveHeader = true;
        }
        else if (std::memcmp(type, "IHDR", 4) == 0)
            return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG has duplicate IHDR chunks"});
        else if (std::memcmp(type, "PLTE", 4) == 0)
        {
            if (havePalette || haveImageData || length == 0 || length > 768 || length % 3 != 0
                || colorType == 0 || colorType == 4)
                return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG palette chunk is invalid or misplaced"});
            havePalette = true;
        }
        else if (std::memcmp(type, "IDAT", 4) == 0)
        {
            if (haveEnd) return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG image data follows IEND"});
            if (imageDataEnded) return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG IDAT chunks must be consecutive"});
            haveImageData = haveImageData || length > 0;
        }
        else if (std::memcmp(type, "IEND", 4) == 0)
        {
            if (length != 0 || !haveImageData)
                return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG IEND is invalid or image data is missing"});
            haveEnd = true;
            offset = crcOffset + 4;
            break;
        }
        else
        {
            if (haveImageData) imageDataEnded = true;
            if ((type[0] & 0x20) == 0)
                return core::Result<ImageInfo>::failure({core::ErrorCode::ValidationError, "PNG contains an unsupported critical chunk"});
        }
        offset = crcOffset + 4;
    }
    if (!haveHeader || !haveImageData || !haveEnd || offset != bytes.size() || (colorType == 3 && !havePalette))
        return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "PNG is incomplete or has trailing data"});
    return core::Result<ImageInfo>::success({ImageFormat::Png, width, height});
}

bool isJpegSof(const unsigned char marker)
{
    return marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
}

core::Result<ImageInfo> inspectJpeg(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8)
        return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "Invalid or truncated JPEG signature"});
    if (bytes[bytes.size() - 2] != 0xFF || bytes.back() != 0xD9)
        return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "JPEG end marker is missing"});
    std::size_t offset = 2;
    while (offset < bytes.size())
    {
        if (bytes[offset++] != 0xFF)
            return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "Invalid JPEG marker stream"});
        while (offset < bytes.size() && bytes[offset] == 0xFF) ++offset;
        if (offset >= bytes.size()) break;
        const auto marker = bytes[offset++];
        if (marker == 0xD9 || marker == 0xDA) break;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (offset + 2 > bytes.size()) break;
        const auto length = be16(bytes, offset);
        if (length < 2 || offset + length > bytes.size())
            return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "Invalid JPEG segment length"});
        if (isJpegSof(marker))
        {
            if (length < 8)
                return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "Truncated JPEG frame header"});
            const auto height = be16(bytes, offset + 3);
            const auto width = be16(bytes, offset + 5);
            if (!validDimensions(width, height))
                return core::Result<ImageInfo>::failure({core::ErrorCode::ValidationError, "JPEG dimensions are outside supported limits"});
            return core::Result<ImageInfo>::success({ImageFormat::Jpeg, width, height});
        }
        offset += length;
    }
    return core::Result<ImageInfo>::failure({core::ErrorCode::ParseError, "JPEG frame dimensions were not found"});
}

core::Result<std::vector<unsigned char>> readBytes(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError, "Cannot inspect image source: " + error.message()});
    if (size == 0 || size > MaxImageBytes)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::ValidationError, "Image size must be between 1 byte and 256 MiB"});
    std::ifstream file(path, std::ios::binary);
    if (!file) return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError, "Cannot open image source"});
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof())
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError, "Image source changed or could not be read completely"});
    return core::Result<std::vector<unsigned char>>::success(std::move(bytes));
}

class Sha256 final
{
public:
    void update(const unsigned char* data, const std::size_t size)
    {
        totalBytes_ += size;
        for (std::size_t i = 0; i < size; ++i)
        {
            block_[blockSize_++] = data[i];
            if (blockSize_ == block_.size()) { transform(); blockSize_ = 0; }
        }
    }

    std::string finish()
    {
        const auto bitLength = totalBytes_ * 8;
        block_[blockSize_++] = 0x80;
        if (blockSize_ > 56)
        {
            while (blockSize_ < 64) block_[blockSize_++] = 0;
            transform(); blockSize_ = 0;
        }
        while (blockSize_ < 56) block_[blockSize_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) block_[blockSize_++] = static_cast<unsigned char>(bitLength >> shift);
        transform();
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (const auto word : state_) output << std::setw(8) << word;
        return output.str();
    }

private:
    static constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    static std::uint32_t rotate(const std::uint32_t value, const unsigned amount) { return (value >> amount) | (value << (32 - amount)); }
    void transform()
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i)
            words[i] = (static_cast<std::uint32_t>(block_[i * 4]) << 24) | (static_cast<std::uint32_t>(block_[i * 4 + 1]) << 16)
                | (static_cast<std::uint32_t>(block_[i * 4 + 2]) << 8) | block_[i * 4 + 3];
        for (std::size_t i = 16; i < words.size(); ++i)
        {
            const auto s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const auto s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        auto [a,b,c,d,e,f,g,h] = state_;
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            const auto s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto temp1 = h + s1 + choice + constants[i] + words[i];
            const auto s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + majority;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }
    std::array<std::uint32_t, 8> state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<unsigned char, 64> block_{};
    std::size_t blockSize_ = 0;
    std::uint64_t totalBytes_ = 0;
};

std::string calculateSha256(const unsigned char* bytes, const std::size_t size)
{
    Sha256 hash;
    hash.update(bytes, size);
    return hash.finish();
}

core::Result<ImageInfo> inspect(const std::vector<unsigned char>& bytes, const std::filesystem::path& source)
{
    auto extension = source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".png") return inspectPng(bytes);
    if (extension == ".jpg" || extension == ".jpeg") return inspectJpeg(bytes);
    return core::Result<ImageInfo>::failure({core::ErrorCode::ValidationError, "Supported image extensions are .png, .jpg and .jpeg"});
}

std::filesystem::path cacheFile(const std::filesystem::path& cacheRoot, const AssetReference& asset)
{
    const auto extension = asset.format == ImageFormat::Png ? ".png" : ".jpg";
    return cacheRoot / (asset.sha256 + ".v1" + extension);
}

core::Result<void> writeAtomically(const std::filesystem::path& destination, const std::vector<unsigned char>& bytes)
{
    static std::atomic<std::uint64_t> counter{0};
#ifdef _WIN32
    const auto processId = GetCurrentProcessId();
#else
    const auto processId = static_cast<std::uint64_t>(::getpid());
#endif
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        temporary = destination;
        temporary += L".tmp." + std::to_wstring(processId) + L"."
            + std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) break;
    }
    if (handle == INVALID_HANDLE_VALUE)
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot reserve a unique temporary asset file"});
    std::size_t offset = 0;
    bool writeOk = true;
    while (offset < bytes.size())
    {
        const auto amount = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, amount, &written, nullptr) || written == 0)
        { writeOk = false; break; }
        offset += written;
    }
    if (writeOk) writeOk = FlushFileBuffers(handle) != 0;
    const bool closed = CloseHandle(handle) != 0;
    if (!writeOk || !closed)
    {
        DeleteFileW(temporary.c_str());
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot flush temporary asset file"});
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto code = GetLastError();
        DeleteFileW(temporary.c_str());
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot publish asset cache/index (Windows error " + std::to_string(code) + ")"});
    }
#else
    int descriptor = -1;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        temporary = destination;
        temporary += ".tmp." + std::to_string(processId) + "."
            + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
        descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0) break;
        if (errno != EEXIST) break;
    }
    if (descriptor < 0)
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot reserve a unique temporary asset file"});
    std::size_t offset = 0;
    bool writeOk = true;
    while (offset < bytes.size())
    {
        const auto written = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
        if (written <= 0) { writeOk = false; break; }
        offset += static_cast<std::size_t>(written);
    }
    if (writeOk) writeOk = ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    if (!writeOk || !closed)
    {
        std::error_code ignored; std::filesystem::remove(temporary, ignored);
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot flush temporary asset file"});
    }
    if (std::rename(temporary.string().c_str(), destination.string().c_str()) != 0)
    {
        std::error_code ignored; std::filesystem::remove(temporary, ignored);
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot publish asset cache/index"});
    }
#endif
    return core::Result<void>::success();
}

core::Result<void> ensureCached(const std::filesystem::path& cacheRoot, const AssetReference& reference,
    const std::vector<unsigned char>& bytes)
{
    const auto destination = cacheFile(cacheRoot, reference);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(destination, error);
    if (!error && std::filesystem::is_symlink(status))
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset cache entry must not be a symbolic link"});
    if (!error && std::filesystem::is_regular_file(status))
    {
        auto cached = readBytes(destination);
        if (cached && sha256Hex(cached.value()) == reference.sha256) return core::Result<void>::success();
    }
    return writeAtomically(destination, bytes);
}

core::Result<std::filesystem::path> resolveSource(const std::filesystem::path& assetsRoot,
    const std::filesystem::path& relative)
{
    if (!validRelative(relative))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError, "Asset path must be relative to Assets/ and contain no dot-dot segments"});
    std::error_code error;
    const auto candidate = std::filesystem::canonical(assetsRoot / relative, error);
    if (error) return core::Result<std::filesystem::path>::failure({core::ErrorCode::NotFound, "Asset source is missing: " + error.message()});
    const auto base = std::filesystem::canonical(assetsRoot, error);
    if (error) return core::Result<std::filesystem::path>::failure({core::ErrorCode::IoError, "Cannot resolve Assets directory"});
    const auto rel = candidate.lexically_relative(base);
    if (rel.empty() || !validRelative(rel))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError, "Asset source resolves outside the project Assets directory"});
    if (!std::filesystem::is_regular_file(candidate, error) || error)
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError, "Asset source must be a regular file"});
    return core::Result<std::filesystem::path>::success(candidate);
}

bool validHash(const std::string& hash)
{
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](const unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

struct ScriptBuildBindingRecord
{
    std::string sourceRelativePath;
    std::string managedTypeName;
};

struct ScriptBuildStateData
{
    std::string fingerprint;
    std::string manifestSha256;
    std::string sdkVersion;
    std::string targetFramework;
    std::string assemblyName;
    std::string assemblyRelativePath;
    std::string assemblySha256;
    std::string runtimeConfigRelativePath;
    std::string runtimeConfigSha256;
    std::map<std::string, std::string> sources;
    std::vector<ScriptBuildBindingRecord> bindings;
};

core::Result<ScriptBuildStateData> scriptBuildStateError(const core::ErrorCode code, std::string message)
{
    return core::Result<ScriptBuildStateData>::failure({code, std::move(message)});
}

bool parseTagLine(const std::string& line, const std::string_view expected, std::string& value)
{
    std::istringstream input(line);
    std::string tag;
    return static_cast<bool>(input >> tag >> std::quoted(value)) && tag == expected && !hasExtra(input);
}

bool parseHashLine(const std::string& line, const std::string_view expected, std::string& value)
{
    std::istringstream input(line);
    std::string tag;
    return static_cast<bool>(input >> tag >> value) && tag == expected && !hasExtra(input) && validHash(value);
}

bool parseCountLine(const std::string& line, const std::string_view expected, std::size_t& value)
{
    std::istringstream input(line);
    std::string tag;
    return static_cast<bool>(input >> tag >> value) && tag == expected && !hasExtra(input);
}

bool scriptSourcePath(const std::string& text, std::filesystem::path& path)
{
    if (text.empty() || text.size() > MaxAssetPathBytes || text.find('\\') != std::string::npos || !validUtf8(text))
        return false;
    auto extension = utf8(fromUtf8(text).extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension != ".cs") return false;
    path = fromUtf8(text);
    return validRelative(path) && path == path.lexically_normal()
        && hasPathPrefix(path, std::filesystem::path("Scripts")) && utf8(path) == text;
}

core::Result<ScriptBuildStateData> readScriptBuildState(const std::filesystem::path& projectRoot)
{
    const auto path = projectRoot / "Library" / "Scripts" / "asset-bindings.state";
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return scriptBuildStateError(core::ErrorCode::NotFound, "No C# build handoff state exists");
    if (error) return scriptBuildStateError(core::ErrorCode::IoError, "Cannot inspect C# build handoff state: " + error.message());
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return scriptBuildStateError(core::ErrorCode::ValidationError, "C# build handoff state must be a regular file");
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > MaxScriptBuildStateBytes)
        return scriptBuildStateError(core::ErrorCode::ValidationError, "C# build handoff state is empty or exceeds 4 MiB");
    std::ifstream file(path, std::ios::binary);
    if (!file) return scriptBuildStateError(core::ErrorCode::IoError, "Cannot open C# build handoff state");
    std::string contents(static_cast<std::size_t>(size), '\0');
    file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file || file.peek() != std::char_traits<char>::eof())
        return scriptBuildStateError(core::ErrorCode::IoError, "C# build handoff state changed while being read");
    if (!validUtf8(contents))
        return scriptBuildStateError(core::ErrorCode::ValidationError, "C# build handoff state is not valid UTF-8");

    std::istringstream input(contents);
    std::string line;
    auto nextLine = [&]() {
        if (!std::getline(input, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };
    int version = 0;
    {
        std::string magic, extra;
        std::istringstream header;
        if (!nextLine()) return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state header is missing");
        header.str(line);
        if (!(header >> magic >> version) || magic != "ASTRO_CSHARP_PROJECT_BUILD" || version != 1 || hasExtra(header))
            return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state header is invalid or unsupported");
    }
    ScriptBuildStateData state;
    const auto readHash = [&](const std::string_view tag, std::string& target) {
        return nextLine() && parseHashLine(line, tag, target);
    };
    const auto readTag = [&](const std::string_view tag, std::string& target) {
        return nextLine() && parseTagLine(line, tag, target);
    };
    if (!readHash("FINGERPRINT", state.fingerprint) || !readHash("MANIFEST_SHA256", state.manifestSha256)
        || !readTag("SDK_VERSION", state.sdkVersion) || !readTag("TARGET_FRAMEWORK", state.targetFramework)
        || !readTag("ASSEMBLY_NAME", state.assemblyName) || !readTag("ASSEMBLY_PATH", state.assemblyRelativePath)
        || !readHash("ASSEMBLY_SHA256", state.assemblySha256)
        || !readTag("RUNTIME_CONFIG_PATH", state.runtimeConfigRelativePath)
        || !readHash("RUNTIME_CONFIG_SHA256", state.runtimeConfigSha256))
        return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state metadata is malformed");
    if (!state.sdkVersion.starts_with("10.0.") || state.targetFramework != "net10.0"
        || state.assemblyName.empty() || state.assemblyName.size() > 100
        || !std::all_of(state.assemblyName.begin(), state.assemblyName.end(), [](const unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '_' || c == '.' || c == '-';
        }))
        return scriptBuildStateError(core::ErrorCode::ValidationError, "C# build state SDK/framework/assembly metadata is invalid");

    std::size_t sourceCount = 0;
    if (!nextLine() || !parseCountLine(line, "SOURCE_COUNT", sourceCount)
        || sourceCount == 0 || sourceCount > MaxScriptBuildSources)
        return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state source count is invalid");
    for (std::size_t i = 0; i < sourceCount; ++i)
    {
        if (!nextLine()) return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state source list is truncated");
        std::istringstream record(line);
        std::string tag, relative, hash, extra;
        std::filesystem::path parsedPath;
        if (!(record >> tag >> std::quoted(relative) >> hash) || tag != "SOURCE" || (record >> extra)
            || !scriptSourcePath(relative, parsedPath) || !validHash(hash)
            || !state.sources.emplace(relative, hash).second)
            return scriptBuildStateError(core::ErrorCode::ValidationError, "C# build state contains an invalid or duplicate source record");
    }

    std::size_t bindingCount = 0;
    if (!nextLine() || !parseCountLine(line, "BINDING_COUNT", bindingCount) || bindingCount > MaxScriptBuildSources)
        return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state binding count is invalid");
    std::set<std::string> boundSources;
    for (std::size_t i = 0; i < bindingCount; ++i)
    {
        if (!nextLine()) return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state binding list is truncated");
        std::istringstream record(line);
        std::string tag, source, type, extra;
        std::filesystem::path parsedPath;
        if (!(record >> tag >> std::quoted(source) >> std::quoted(type)) || tag != "BINDING" || (record >> extra)
            || !scriptSourcePath(source, parsedPath) || !validManagedTypeName(type)
            || !state.sources.contains(source) || !boundSources.insert(source).second)
            return scriptBuildStateError(core::ErrorCode::ValidationError, "C# build state contains an invalid, duplicate or unselected binding");
        state.bindings.push_back({std::move(source), std::move(type)});
    }
    if (!nextLine() || line != "END")
        return scriptBuildStateError(core::ErrorCode::ParseError, "C# build state terminator is missing");
    while (nextLine())
        if (line.find_first_not_of(" \t\r") != std::string::npos)
            return scriptBuildStateError(core::ErrorCode::ParseError, "Unexpected data after C# build state terminator");
    if (input.bad()) return scriptBuildStateError(core::ErrorCode::IoError, "Error while reading C# build handoff state");
    return core::Result<ScriptBuildStateData>::success(std::move(state));
}

core::Result<std::vector<unsigned char>> readBoundedFile(const std::filesystem::path& path,
    const std::uint64_t maximumBytes, const std::string_view description)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::ValidationError,
            std::string(description) + " must be a regular file without symbolic links"});
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximumBytes)
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::InvalidState,
            std::string(description) + " is missing, empty or exceeds its size limit"});
    std::ifstream file(path, std::ios::binary);
    if (!file) return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
        "Cannot open " + std::string(description)});
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof())
        return core::Result<std::vector<unsigned char>>::failure({core::ErrorCode::IoError,
            std::string(description) + " changed while being read"});
    return core::Result<std::vector<unsigned char>>::success(std::move(bytes));
}

bool readJsonStringField(const std::string& json, const std::string_view field, std::string& value)
{
    const auto marker = "\"" + std::string(field) + "\"";
    const auto start = json.find(marker);
    if (start == std::string::npos || json.find(marker, start + marker.size()) != std::string::npos) return false;
    auto position = start + marker.size();
    const auto skipSpace = [&]() {
        while (position < json.size() && (json[position] == ' ' || json[position] == '\t'
            || json[position] == '\r' || json[position] == '\n')) ++position;
    };
    skipSpace();
    if (position >= json.size() || json[position++] != ':') return false;
    skipSpace();
    if (position >= json.size() || json[position++] != '"') return false;
    value.clear();
    while (position < json.size())
    {
        const auto character = json[position++];
        if (character == '"') return true;
        if (character == '\\' || static_cast<unsigned char>(character) < 0x20) return false;
        value.push_back(character);
    }
    return false;
}

bool hasJsonIntegerField(const std::string& json, const std::string_view field, const int expected)
{
    const auto marker = "\"" + std::string(field) + "\"";
    const auto start = json.find(marker);
    if (start == std::string::npos || json.find(marker, start + marker.size()) != std::string::npos) return false;
    auto position = start + marker.size();
    const auto skipSpace = [&]() {
        while (position < json.size() && (json[position] == ' ' || json[position] == '\t'
            || json[position] == '\r' || json[position] == '\n')) ++position;
    };
    skipSpace();
    if (position >= json.size() || json[position++] != ':') return false;
    skipSpace();
    int value = -1;
    const auto parsed = std::from_chars(json.data() + position, json.data() + json.size(), value);
    if (parsed.ec != std::errc{} || value != expected) return false;
    position = static_cast<std::size_t>(parsed.ptr - json.data());
    skipSpace();
    return position < json.size() && (json[position] == ',' || json[position] == '}');
}

core::Result<std::map<std::string, std::string>> enumerateScriptSources(const std::filesystem::path& projectRoot)
{
    const auto scriptsRoot = projectRoot / "Scripts";
    std::error_code error;
    if (!std::filesystem::is_directory(scriptsRoot, error) || error)
        return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::NotFound,
            "Project Scripts directory is missing"});
    std::map<std::string, std::string> sources;
    std::uint64_t totalBytes = 0;
    std::filesystem::recursive_directory_iterator iterator(scriptsRoot, error), end;
    if (error) return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::IoError,
        "Cannot scan project Scripts directory: " + error.message()});
    for (; iterator != end; iterator.increment(error))
    {
        if (error) return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::IoError,
            "Cannot scan project Scripts directory: " + error.message()});
        const auto entryStatus = iterator->symlink_status(error);
        if (error || std::filesystem::is_symlink(entryStatus))
            return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::ValidationError,
                "Symbolic links are not allowed under project Scripts"});
        if (std::filesystem::is_directory(entryStatus)) continue;
        if (!std::filesystem::is_regular_file(entryStatus))
            return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::ValidationError,
                "Project Scripts contains a non-regular file"});
        auto extension = utf8(iterator->path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension != ".cs") continue;
        if (sources.size() >= MaxScriptBuildSources)
            return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::ValidationError,
                "Project Scripts contains more than 4096 C# source files"});
        const auto relative = utf8(iterator->path().lexically_relative(projectRoot));
        auto bytes = readScriptSource(iterator->path());
        if (!bytes) return core::Result<std::map<std::string, std::string>>::failure(bytes.error());
        totalBytes += bytes.value().size();
        if (totalBytes > 536870912ull)
            return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::ValidationError,
                "Project C# sources exceed 512 MiB total"});
        if (!sources.emplace(relative, sha256Hex(bytes.value())).second)
            return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::ValidationError,
                "Project contains duplicate C# source paths"});
    }
    if (sources.empty()) return core::Result<std::map<std::string, std::string>>::failure({core::ErrorCode::InvalidState,
        "Project contains no C# source files"});
    return core::Result<std::map<std::string, std::string>>::success(std::move(sources));
}

core::Result<void> validateScriptBuildState(const std::filesystem::path& projectRoot,
    const ScriptBuildStateData& state)
{
    for (const auto& directory : {projectRoot / ".astro", projectRoot / "Scripts",
        projectRoot / "Library", projectRoot / "Library" / "Scripts"})
    {
        std::error_code directoryError;
        const auto directoryStatus = std::filesystem::symlink_status(directory, directoryError);
        if (directoryError || std::filesystem::is_symlink(directoryStatus)
            || !std::filesystem::is_directory(directoryStatus))
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "C# project build directories must be regular directories without symbolic links"});
    }
    const auto manifestPath = projectRoot / ".astro" / "csharp-build.json";
    std::error_code manifestError;
    const auto manifestStatus = std::filesystem::symlink_status(manifestPath, manifestError);
    if (manifestError || std::filesystem::is_symlink(manifestStatus)
        || !std::filesystem::is_regular_file(manifestStatus))
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "C# build manifest must be a regular project file without symbolic links"});
    auto manifestBytes = readBoundedFile(manifestPath, 65536,
        "C# build manifest");
    if (!manifestBytes) return core::Result<void>::failure(manifestBytes.error());
    if (sha256Hex(manifestBytes.value()) != state.manifestSha256)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "C# build manifest changed after the last successful build"});
    const auto buildStatePath = projectRoot / "Library" / "Scripts" / "build-state.json";
    auto buildStateBytes = readBoundedFile(buildStatePath, 65536, "C# build-state record");
    if (!buildStateBytes)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "C# build-state record is missing or invalid"});
    const std::string buildStateText(buildStateBytes.value().begin(), buildStateBytes.value().end());
    std::string handoffPath, handoffHash;
    if (!validUtf8(buildStateText) || !hasJsonIntegerField(buildStateText, "schemaVersion", 2)
        || !readJsonStringField(buildStateText, "assetStatePath", handoffPath)
        || !readJsonStringField(buildStateText, "assetStateSha256", handoffHash)
        || handoffPath != "Library/Scripts/asset-bindings.state" || !validHash(handoffHash))
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "C# build-state record does not contain a valid versioned build handoff reference"});
    const auto handoffBytes = readBoundedFile(projectRoot / handoffPath, MaxScriptBuildStateBytes,
        "C# build handoff state");
    if (!handoffBytes || sha256Hex(handoffBytes.value()) != handoffHash)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "C# build handoff state is missing or its build-state hash does not match"});
    auto sources = enumerateScriptSources(projectRoot);
    if (!sources) return core::Result<void>::failure(sources.error());
    if (sources.value() != state.sources)
    {
        std::string detail = "C# sources were added, removed or changed after the last successful build";
        for (const auto& [path, hash] : sources.value())
            if (!state.sources.contains(path)) detail += "; unrecorded: " + path;
            else if (state.sources.at(path) != hash) detail += "; changed: " + path;
        for (const auto& [path, hash] : state.sources)
            if (!sources.value().contains(path)) detail += "; missing: " + path;
        return core::Result<void>::failure({core::ErrorCode::InvalidState, std::move(detail)});
    }

    const auto assemblyRelative = fromUtf8(state.assemblyRelativePath);
    const auto runtimeConfigRelative = fromUtf8(state.runtimeConfigRelativePath);
    auto assembly = resolveProjectFile(projectRoot, assemblyRelative,
        std::filesystem::path("Library") / "Scripts", ".dll", "Managed assembly");
    if (!assembly) return core::Result<void>::failure({core::ErrorCode::InvalidState, assembly.error().message});
    auto runtimeConfig = resolveProjectFile(projectRoot, runtimeConfigRelative,
        std::filesystem::path("Library") / "Scripts", ".runtimeconfig.json", "Managed runtimeconfig");
    if (!runtimeConfig) return core::Result<void>::failure({core::ErrorCode::InvalidState, runtimeConfig.error().message});
    if (utf8(assembly.value().filename()) != state.assemblyName + ".dll"
        || utf8(runtimeConfig.value().filename()) != state.assemblyName + ".runtimeconfig.json")
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "C# build outputs do not match the selected assembly name"});
    auto assemblyBytes = readBoundedFile(assembly.value(), MaxScriptBuildOutputBytes, "Managed assembly");
    if (!assemblyBytes || sha256Hex(assemblyBytes.value()) != state.assemblySha256)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Managed assembly is missing or its hash changed after build"});
    auto runtimeConfigBytes = readBoundedFile(runtimeConfig.value(), 1024 * 1024, "Managed runtimeconfig");
    if (!runtimeConfigBytes || sha256Hex(runtimeConfigBytes.value()) != state.runtimeConfigSha256)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "Managed runtimeconfig is missing or its hash changed after build"});
    for (const auto& binding : state.bindings)
    {
        if (!state.sources.contains(binding.sourceRelativePath) || !validManagedTypeName(binding.managedTypeName))
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "C# build contains a binding to an unknown source or invalid managed type name"});
    }
    return core::Result<void>::success();
}
}

std::string_view imageFormatName(const ImageFormat format) noexcept
{
    return format == ImageFormat::Png ? "PNG" : "JPEG";
}

std::string sha256Hex(const std::span<const std::uint8_t> bytes)
{
    return calculateSha256(bytes.data(), bytes.size());
}

AssetRegistry::AssetRegistry(std::filesystem::path projectRoot, std::filesystem::path assetsRoot,
    std::filesystem::path cacheRoot, std::filesystem::path indexPath)
    : projectRoot_(std::move(projectRoot)), assetsRoot_(std::move(assetsRoot)),
      cacheRoot_(std::move(cacheRoot)), indexPath_(std::move(indexPath)) {}

core::Result<AssetRegistry> AssetRegistry::open(const std::filesystem::path& projectRoot)
{
    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(root, error))
        return assetError(core::ErrorCode::NotFound, "Project root must be an existing directory");
    const auto assets = root / "Assets";
    const auto cache = root / "Library" / "Assets";
    const auto metadata = root / ".astro";
    std::filesystem::create_directories(assets, error);
    if (error) return assetError(core::ErrorCode::IoError, "Cannot create project Assets directory: " + error.message());
    std::filesystem::create_directories(cache, error);
    if (error) return assetError(core::ErrorCode::IoError, "Cannot create project asset cache: " + error.message());
    std::filesystem::create_directories(metadata, error);
    if (error) return assetError(core::ErrorCode::IoError, "Cannot create project metadata directory: " + error.message());

    const auto canonicalAssets = std::filesystem::canonical(assets, error);
    if (error || !isWithin(canonicalAssets, root))
        return assetError(core::ErrorCode::ValidationError, "Project Assets directory must resolve inside the project root");
    const auto canonicalCache = std::filesystem::canonical(cache, error);
    if (error || !isWithin(canonicalCache, root))
        return assetError(core::ErrorCode::ValidationError, "Project asset cache must resolve inside the project root");
    const auto canonicalMetadata = std::filesystem::canonical(metadata, error);
    if (error || !isWithin(canonicalMetadata, root))
        return assetError(core::ErrorCode::ValidationError, "Project metadata directory must resolve inside the project root");

    AssetRegistry registry(root, canonicalAssets, canonicalCache, canonicalMetadata / "assets.index");
    if (!std::filesystem::exists(registry.indexPath_, error))
    {
        const auto saved = registry.saveIndex({}, {}, {}, {}, 1);
        if (!saved) return assetError(saved.error().code, saved.error().message);
        return core::Result<AssetRegistry>::success(std::move(registry));
    }
    if (error) return assetError(core::ErrorCode::IoError, "Cannot inspect asset index: " + error.message());
    const auto indexStatus = std::filesystem::symlink_status(registry.indexPath_, error);
    if (error || std::filesystem::is_symlink(indexStatus) || !std::filesystem::is_regular_file(indexStatus))
        return assetError(core::ErrorCode::ValidationError, "Asset index must be a regular file inside the project");

    const auto indexSize = std::filesystem::file_size(registry.indexPath_, error);
    if (error || indexSize > MaxIndexBytes)
        return assetError(error ? core::ErrorCode::IoError : core::ErrorCode::ValidationError,
            error ? "Cannot inspect asset index: " + error.message() : "Asset index exceeds 16 MiB");
    std::ifstream input(registry.indexPath_, std::ios::binary);
    std::string line;
    const auto nextLine = [&input, &line] {
        if (!std::getline(input, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };
    std::istringstream header;
    int indexVersion = 0;
    if (!input || !nextLine())
        return assetError(core::ErrorCode::ParseError, "Invalid asset index header or version");
    header.str(line);
    header.imbue(std::locale::classic());
    std::string magic;
    if (!(header >> magic >> indexVersion) || hasExtra(header) || magic != "ASTRO_ASSETS"
        || indexVersion < 1 || indexVersion > 4)
        return assetError(core::ErrorCode::VersionMismatch, "Unsupported asset index format version");
    if (!nextLine()) return assetError(core::ErrorCode::ParseError, "Asset index counter is missing");
    std::istringstream counterLine(line); counterLine.imbue(std::locale::classic());
    std::string marker;
    if (!(counterLine >> marker >> registry.nextId_) || marker != "NEXT" || registry.nextId_ == 0 || hasExtra(counterLine))
        return assetError(core::ErrorCode::ParseError, "Invalid asset ID counter");
    std::uint64_t maximumId = 0;
    std::set<std::string> registeredPaths;
    std::set<std::string> registeredScriptPaths;
    std::set<std::string> registeredModelPaths;
    std::set<std::string> registeredScriptTypes;
    std::set<std::uint64_t> registeredIds;
    bool ended = false;
    while (nextLine())
    {
        if (line == "END") { ended = true; break; }
        std::istringstream record(line); record.imbue(std::locale::classic());
        if (!(record >> marker) || marker.empty())
            return assetError(core::ErrorCode::ParseError, "Invalid asset index entry");
        if (marker == "ASSET")
        {
            std::string format, relativeText, hash;
            AssetReference reference;
            if (!(record >> reference.id.value >> std::quoted(relativeText) >> format
                >> reference.width >> reference.height >> reference.sourceSize >> hash) || hasExtra(record))
                return assetError(core::ErrorCode::ParseError, "Invalid image asset index entry");
            if ((format != "PNG" && format != "JPEG") || !validHash(hash) || !validUtf8(relativeText)
                || relativeText.size() > MaxAssetPathBytes
                || !validDimensions(reference.width, reference.height) || reference.sourceSize == 0 || reference.sourceSize > MaxImageBytes)
                return assetError(core::ErrorCode::ValidationError, "Asset index contains invalid image metadata");
            reference.format = format == "PNG" ? ImageFormat::Png : ImageFormat::Jpeg;
            reference.sourceRelativePath = relativeText;
            reference.sha256 = std::move(hash);
            const auto sourcePath = fromUtf8(reference.sourceRelativePath);
            if (reference.id.value == 0 || reference.sourceRelativePath.find('\0') != std::string::npos
                || !validRelative(sourcePath) || sourcePath != sourcePath.lexically_normal())
                return assetError(core::ErrorCode::ValidationError, "Asset index contains an invalid ID or source path");
            if (!registeredPaths.emplace(reference.sourceRelativePath).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate source path");
            if (!registeredIds.emplace(reference.id.value).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate asset ID");
            maximumId = std::max(maximumId, reference.id.value);
            registry.entries_.emplace(reference.id, std::move(reference));
        }
        else if (marker == "SCRIPT" && indexVersion >= 2)
        {
            ScriptAssetReference reference;
            if (!(record >> reference.id.value >> std::quoted(reference.sourceRelativePath)
                >> std::quoted(reference.managedTypeName) >> std::quoted(reference.assemblyRelativePath)
                >> std::quoted(reference.runtimeConfigRelativePath) >> reference.sourceSha256) || hasExtra(record))
                return assetError(core::ErrorCode::ParseError, "Invalid script asset index entry");
            const auto sourcePath = fromUtf8(reference.sourceRelativePath);
            const auto assemblyPath = fromUtf8(reference.assemblyRelativePath);
            const auto runtimeConfigPath = fromUtf8(reference.runtimeConfigRelativePath);
            if (reference.id.value == 0 || reference.id.value == std::numeric_limits<std::uint64_t>::max()
                || !validUtf8(reference.sourceRelativePath)
                || !validUtf8(reference.managedTypeName) || !validUtf8(reference.assemblyRelativePath)
                || !validUtf8(reference.runtimeConfigRelativePath) || reference.sourceRelativePath.find('\0') != std::string::npos
                || reference.assemblyRelativePath.find('\0') != std::string::npos
                || reference.runtimeConfigRelativePath.find('\0') != std::string::npos
                || reference.sourceRelativePath.size() > MaxAssetPathBytes
                || reference.assemblyRelativePath.size() > MaxAssetPathBytes
                || reference.runtimeConfigRelativePath.size() > MaxAssetPathBytes
                || !validManagedTypeName(reference.managedTypeName) || !validHash(reference.sourceSha256)
                || !validRelative(sourcePath) || sourcePath != sourcePath.lexically_normal()
                || !hasPathPrefix(sourcePath, std::filesystem::path("Scripts")) || utf8(sourcePath.extension()) != ".cs"
                || !validRelative(assemblyPath) || assemblyPath != assemblyPath.lexically_normal()
                || !hasPathPrefix(assemblyPath, std::filesystem::path("Library") / "Scripts")
                || utf8(assemblyPath.extension()) != ".dll"
                || !validRelative(runtimeConfigPath) || runtimeConfigPath != runtimeConfigPath.lexically_normal()
                || !hasPathPrefix(runtimeConfigPath, std::filesystem::path("Library") / "Scripts")
                || !utf8(runtimeConfigPath.filename()).ends_with(".runtimeconfig.json"))
                return assetError(core::ErrorCode::ValidationError, "Asset index contains invalid script metadata");
            if (!registeredScriptPaths.emplace(reference.sourceRelativePath).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate script source path");
            const auto typeKey = reference.assemblyRelativePath + "\n" + reference.managedTypeName;
            if (!registeredScriptTypes.emplace(typeKey).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate managed type mapping");
            if (!registeredIds.emplace(reference.id.value).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate asset ID");
            maximumId = std::max(maximumId, reference.id.value);
            registry.scriptEntries_.emplace(reference.id, std::move(reference));
        }
        else if (marker == "MODEL" && indexVersion >= 3)
        {
            ModelAssetReference reference;
            if (!(record >> reference.id.value >> std::quoted(reference.sourceRelativePath)
                >> reference.sourceSize >> reference.sourceSha256 >> reference.importContentHash) || hasExtra(record))
                return assetError(core::ErrorCode::ParseError, "Invalid model asset index entry");
            const auto sourcePath = fromUtf8(reference.sourceRelativePath);
            const auto extension = utf8(sourcePath.extension());
            if (reference.id.value == 0 || !validUtf8(reference.sourceRelativePath)
                || reference.sourceRelativePath.find('\0') != std::string::npos
                || reference.sourceRelativePath.size() > MaxAssetPathBytes
                || !validHash(reference.sourceSha256) || !validHash(reference.importContentHash)
                || reference.sourceSize == 0 || reference.sourceSize > MaxModelSourceBytes
                || !validRelative(sourcePath) || sourcePath != sourcePath.lexically_normal()
                || (extension != ".gltf" && extension != ".glb"))
                return assetError(core::ErrorCode::ValidationError, "Asset index contains invalid model metadata");
            if (!registeredModelPaths.emplace(reference.sourceRelativePath).second
                || !registeredPaths.emplace(reference.sourceRelativePath).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate model source path");
            if (!registeredIds.emplace(reference.id.value).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate asset ID");
            maximumId = std::max(maximumId, reference.id.value);
            registry.modelEntries_.emplace(reference.id, std::move(reference));
        }
        else if (marker == "MATERIAL" && indexVersion >= 4)
        {
            MaterialAssetReference reference;
            if (!(record >> reference.id.value >> std::quoted(reference.sourceRelativePath)
                >> reference.schemaVersion >> reference.sourceSize >> reference.sourceSha256) || hasExtra(record))
                return assetError(core::ErrorCode::ParseError, "Invalid material asset index entry");
            const auto sourcePath = fromUtf8(reference.sourceRelativePath);
            const auto extension = utf8(sourcePath.extension());
            if (reference.id.value == 0 || reference.id.value == std::numeric_limits<std::uint64_t>::max()
                || (reference.schemaVersion != 1 && reference.schemaVersion != 2) || !validUtf8(reference.sourceRelativePath)
                || reference.sourceRelativePath.find('\0') != std::string::npos
                || reference.sourceRelativePath.size() > MaxAssetPathBytes
                || reference.sourceSize == 0 || reference.sourceSize > MaxMaterialSourceBytes
                || !validHash(reference.sourceSha256) || !validRelative(sourcePath)
                || sourcePath != sourcePath.lexically_normal()
                || !hasPathPrefix(sourcePath, std::filesystem::path("Materials"))
                || extension != ".astro-material")
                return assetError(core::ErrorCode::ValidationError, "Asset index contains invalid material metadata");
            if (!registeredPaths.emplace(reference.sourceRelativePath).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate material source path");
            if (!registeredIds.emplace(reference.id.value).second)
                return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate asset ID");
            maximumId = std::max(maximumId, reference.id.value);
            registry.materialEntries_.emplace(reference.id, std::move(reference));
        }
        else
            return assetError(core::ErrorCode::ValidationError, "Unknown or unsupported asset index record: " + marker);
        if (registry.entries_.size() + registry.scriptEntries_.size() + registry.modelEntries_.size()
            + registry.materialEntries_.size() > MaxAssetEntries)
            return assetError(core::ErrorCode::ValidationError, "Asset index exceeds 100000 total entries");
    }
    if (!ended || input.bad()) return assetError(core::ErrorCode::ParseError, "Asset index terminator is missing or unreadable");
    while (nextLine())
        if (line.find_first_not_of(" \t\r") != std::string::npos)
            return assetError(core::ErrorCode::ParseError, "Unexpected data after asset index terminator");
    if (maximumId >= registry.nextId_)
        return assetError(core::ErrorCode::ValidationError, "Asset ID counter must exceed every registered ID");
    return core::Result<AssetRegistry>::success(std::move(registry));
}

core::Result<void> AssetRegistry::saveIndex(const std::map<AssetId, AssetReference>& entries,
    const std::map<AssetId, ScriptAssetReference>& scripts,
    const std::map<AssetId, ModelAssetReference>& models,
    const std::map<AssetId, MaterialAssetReference>& materials, const std::uint64_t nextId) const
{
    if (entries.size() + scripts.size() + models.size() + materials.size() > MaxAssetEntries)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset index exceeds 100000 total entries"});
    std::ostringstream output; output.imbue(std::locale::classic());
    output << "ASTRO_ASSETS 4\nNEXT " << nextId << '\n';
    for (const auto& [id, reference] : entries)
    {
        if (reference.sourceRelativePath.size() > MaxAssetPathBytes)
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset path exceeds 4096 UTF-8 bytes"});
        output << "ASSET " << id.value << ' ' << std::quoted(reference.sourceRelativePath) << ' '
               << imageFormatName(reference.format) << ' ' << reference.width << ' ' << reference.height << ' '
               << reference.sourceSize << ' ' << reference.sha256 << '\n';
        if (output.tellp() > static_cast<std::streamoff>(MaxIndexBytes))
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset index exceeds 16 MiB"});
    }
    for (const auto& [id, script] : scripts)
    {
        if (script.sourceRelativePath.size() > MaxAssetPathBytes || script.managedTypeName.size() > MaxManagedTypeNameBytes
            || script.assemblyRelativePath.size() > MaxAssetPathBytes
            || script.runtimeConfigRelativePath.size() > MaxAssetPathBytes || !validHash(script.sourceSha256))
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Script asset metadata exceeds its bounds"});
        output << "SCRIPT " << id.value << ' ' << std::quoted(script.sourceRelativePath) << ' '
               << std::quoted(script.managedTypeName) << ' ' << std::quoted(script.assemblyRelativePath) << ' '
               << std::quoted(script.runtimeConfigRelativePath) << ' ' << script.sourceSha256 << '\n';
        if (output.tellp() > static_cast<std::streamoff>(MaxIndexBytes))
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset index exceeds 16 MiB"});
    }
    for (const auto& [id, model] : models)
    {
        const auto modelPath = fromUtf8(model.sourceRelativePath);
        const auto extension = utf8(modelPath.extension());
        if (id.value == 0 || id.value == std::numeric_limits<std::uint64_t>::max()
            || !validUtf8(model.sourceRelativePath) || model.sourceRelativePath.find('\0') != std::string::npos
            || !validRelative(modelPath) || modelPath != modelPath.lexically_normal()
            || (extension != ".gltf" && extension != ".glb")
            || model.sourceRelativePath.size() > MaxAssetPathBytes || model.sourceSize == 0
            || model.sourceSize > MaxModelSourceBytes || !validHash(model.sourceSha256)
            || !validHash(model.importContentHash))
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Model asset metadata is invalid or exceeds its bounds"});
        output << "MODEL " << id.value << ' ' << std::quoted(model.sourceRelativePath) << ' '
               << model.sourceSize << ' ' << model.sourceSha256 << ' ' << model.importContentHash << '\n';
        if (output.tellp() > static_cast<std::streamoff>(MaxIndexBytes))
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset index exceeds 16 MiB"});
    }
    for (const auto& [id, material] : materials)
    {
        const auto materialPath = fromUtf8(material.sourceRelativePath);
        if (id.value == 0 || id.value == std::numeric_limits<std::uint64_t>::max()
            || (material.schemaVersion != 1 && material.schemaVersion != 2) || !validUtf8(material.sourceRelativePath)
            || material.sourceRelativePath.find('\0') != std::string::npos
            || !validRelative(materialPath) || materialPath != materialPath.lexically_normal()
            || !hasPathPrefix(materialPath, std::filesystem::path("Materials"))
            || utf8(materialPath.extension()) != ".astro-material"
            || material.sourceRelativePath.size() > MaxAssetPathBytes || material.sourceSize == 0
            || material.sourceSize > MaxMaterialSourceBytes || !validHash(material.sourceSha256))
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Material asset metadata is invalid or exceeds its bounds"});
        output << "MATERIAL " << id.value << ' ' << std::quoted(material.sourceRelativePath) << ' '
               << material.schemaVersion << ' ' << material.sourceSize << ' ' << material.sourceSha256 << '\n';
        if (output.tellp() > static_cast<std::streamoff>(MaxIndexBytes))
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset index exceeds 16 MiB"});
    }
    output << "END\n";
    const auto text = output.str();
    if (text.size() > MaxIndexBytes)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset index exceeds 16 MiB"});
    return writeAtomically(indexPath_, std::vector<unsigned char>(text.begin(), text.end()));
}

const AssetReference* AssetRegistry::find(const AssetId id) const noexcept
{
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
}

const ScriptAssetReference* AssetRegistry::findScript(const AssetId id) const noexcept
{
    const auto found = scriptEntries_.find(id);
    return found == scriptEntries_.end() ? nullptr : &found->second;
}

const ModelAssetReference* AssetRegistry::findModel(const AssetId id) const noexcept
{
    const auto found = modelEntries_.find(id);
    return found == modelEntries_.end() ? nullptr : &found->second;
}

const MaterialAssetReference* AssetRegistry::findMaterial(const AssetId id) const noexcept
{
    const auto found = materialEntries_.find(id);
    return found == materialEntries_.end() ? nullptr : &found->second;
}

core::Result<ResolvedModelAsset> AssetRegistry::resolveModel(const AssetId id) const
{
    const auto* reference = findModel(id);
    if (!reference)
        return resolvedModelError(core::ErrorCode::NotFound, "Model AssetId is not registered");
    const auto relative = fromUtf8(reference->sourceRelativePath);
    const auto extension = utf8(relative.extension());
    if ((extension != ".gltf" && extension != ".glb") || !validRelative(relative)
        || relative != relative.lexically_normal())
        return resolvedModelError(core::ErrorCode::ValidationError, "Registered model path is invalid");
    auto source = resolveSource(assetsRoot_, relative);
    if (!source) return resolvedModelError(source.error().code, source.error().message);
    auto bytes = readModelSource(source.value());
    if (!bytes) return resolvedModelError(bytes.error().code, bytes.error().message);
    if (bytes.value().size() != reference->sourceSize || sha256Hex(bytes.value()) != reference->sourceSha256)
        return resolvedModelError(core::ErrorCode::InvalidState,
            "Model source changed after registration; import and register it again");
    return core::Result<ResolvedModelAsset>::success({id, source.value(), reference->importContentHash});
}

core::Result<ResolvedMaterialAsset> AssetRegistry::resolveMaterial(const AssetId id) const
{
    const auto* reference = findMaterial(id);
    if (!reference)
        return resolvedMaterialError(core::ErrorCode::NotFound, "Material AssetId is not registered");
    const auto relative = fromUtf8(reference->sourceRelativePath);
    if ((reference->schemaVersion != 1 && reference->schemaVersion != 2) || !validRelative(relative)
        || relative != relative.lexically_normal()
        || !hasPathPrefix(relative, std::filesystem::path("Materials"))
        || utf8(relative.extension()) != ".astro-material")
        return resolvedMaterialError(core::ErrorCode::ValidationError, "Registered material path is invalid");
    const auto projectRelative = std::filesystem::path("Assets") / relative;
    auto source = resolveProjectFile(projectRoot_, projectRelative,
        std::filesystem::path("Assets") / "Materials", ".astro-material", "Material source");
    if (!source) return resolvedMaterialError(source.error().code, source.error().message);
    std::error_code error;
    const auto sourceSize = std::filesystem::file_size(source.value(), error);
    if (error)
        return resolvedMaterialError(core::ErrorCode::IoError, "Cannot inspect material source: " + error.message());
    if (sourceSize == 0 || sourceSize > MaxMaterialSourceBytes)
        return resolvedMaterialError(core::ErrorCode::ValidationError, "Material source must contain 1 byte through 64 KiB");
    auto bytes = readBytes(source.value());
    if (!bytes) return resolvedMaterialError(bytes.error().code, bytes.error().message);
    if (bytes.value().size() != sourceSize || sourceSize != reference->sourceSize
        || sha256Hex(bytes.value()) != reference->sourceSha256)
        return resolvedMaterialError(core::ErrorCode::InvalidState,
            "Material source changed after registration; register it again");
    const auto parsed = parseMaterialAsset(std::string_view(
        reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()));
    if (!parsed) return resolvedMaterialError(parsed.error().code, parsed.error().message);
    const auto referencesValid = validateMaterialImageReferences(parsed.value(), entries_);
    if (!referencesValid) return resolvedMaterialError(referencesValid.error().code, referencesValid.error().message);
    return core::Result<ResolvedMaterialAsset>::success({id, source.value(), parsed.value()});
}

core::Result<ResolvedScriptAsset> AssetRegistry::resolveScript(const AssetId id) const
{
    const auto* reference = findScript(id);
    if (!reference)
        return resolvedScriptError(core::ErrorCode::NotFound, "Script AssetId is not registered");
    const auto sourceRelative = fromUtf8(reference->sourceRelativePath);
    const auto assemblyRelative = fromUtf8(reference->assemblyRelativePath);
    const auto runtimeConfigRelative = fromUtf8(reference->runtimeConfigRelativePath);
    auto source = resolveProjectFile(projectRoot_, sourceRelative, "Scripts", ".cs", "C# source");
    if (!source) return resolvedScriptError(source.error().code, source.error().message);
    auto assembly = resolveProjectFile(projectRoot_, assemblyRelative,
        std::filesystem::path("Library") / "Scripts", ".dll", "Managed assembly");
    if (!assembly) return resolvedScriptError(assembly.error().code, assembly.error().message);
    auto runtimeConfig = resolveProjectFile(projectRoot_, runtimeConfigRelative,
        std::filesystem::path("Library") / "Scripts", ".runtimeconfig.json", "Managed runtimeconfig");
    if (!runtimeConfig) return resolvedScriptError(runtimeConfig.error().code, runtimeConfig.error().message);
    auto sourceBytes = readScriptSource(source.value());
    if (!sourceBytes) return resolvedScriptError(sourceBytes.error().code, sourceBytes.error().message);
    if (sha256Hex(sourceBytes.value()) != reference->sourceSha256)
        return resolvedScriptError(core::ErrorCode::InvalidState,
            "C# source changed after the script asset was registered; rebuild and register it again");
    return core::Result<ResolvedScriptAsset>::success({id, source.value(), assembly.value(),
        runtimeConfig.value(), reference->managedTypeName});
}

core::Result<ScriptAssetReference> AssetRegistry::registerScript(
    const std::filesystem::path& sourceRelativePath, std::string managedTypeName,
    const std::filesystem::path& assemblyRelativePath,
    const std::filesystem::path& runtimeConfigRelativePath)
{
    auto sourceText = utf8(sourceRelativePath);
    auto assemblyText = utf8(assemblyRelativePath);
    auto runtimeConfigText = utf8(runtimeConfigRelativePath);
    if (!validUtf8(sourceText) || !validUtf8(assemblyText) || !validUtf8(runtimeConfigText)
        || sourceText.find('\0') != std::string::npos || assemblyText.find('\0') != std::string::npos
        || runtimeConfigText.find('\0') != std::string::npos
        || sourceText.size() > MaxAssetPathBytes || assemblyText.size() > MaxAssetPathBytes
        || runtimeConfigText.size() > MaxAssetPathBytes || !validManagedTypeName(managedTypeName))
        return scriptReferenceError(core::ErrorCode::ValidationError,
            "Script asset paths or managed type name are invalid or exceed their bounds");

    auto source = resolveProjectFile(projectRoot_, sourceRelativePath, "Scripts", ".cs", "C# source");
    if (!source) return scriptReferenceError(source.error().code, source.error().message);
    auto assembly = resolveProjectFile(projectRoot_, assemblyRelativePath,
        std::filesystem::path("Library") / "Scripts", ".dll", "Managed assembly");
    if (!assembly) return scriptReferenceError(assembly.error().code, assembly.error().message);
    auto runtimeConfig = resolveProjectFile(projectRoot_, runtimeConfigRelativePath,
        std::filesystem::path("Library") / "Scripts", ".runtimeconfig.json", "Managed runtimeconfig");
    if (!runtimeConfig) return scriptReferenceError(runtimeConfig.error().code, runtimeConfig.error().message);
    sourceText = utf8(source.value().lexically_relative(projectRoot_));
    assemblyText = utf8(assembly.value().lexically_relative(projectRoot_));
    runtimeConfigText = utf8(runtimeConfig.value().lexically_relative(projectRoot_));
    if (sourceText.size() > MaxAssetPathBytes || assemblyText.size() > MaxAssetPathBytes
        || runtimeConfigText.size() > MaxAssetPathBytes)
        return scriptReferenceError(core::ErrorCode::ValidationError,
            "Canonical script asset paths exceed the 4096 UTF-8 byte limit");
    auto sourceBytes = readScriptSource(source.value());
    if (!sourceBytes) return scriptReferenceError(sourceBytes.error().code, sourceBytes.error().message);

    const auto sourceHash = sha256Hex(sourceBytes.value());
    auto updatedScripts = scriptEntries_;
    auto existing = std::find_if(updatedScripts.begin(), updatedScripts.end(), [&](const auto& item) {
        return item.second.sourceRelativePath == sourceText;
    });
    for (const auto& [registeredId, registered] : updatedScripts)
        if (registered.assemblyRelativePath == assemblyText && registered.managedTypeName == managedTypeName
            && (existing == updatedScripts.end() || registeredId != existing->first))
            return scriptReferenceError(core::ErrorCode::AlreadyExists,
                "This managed type is already registered in the selected assembly");

    ScriptAssetReference reference;
    if (existing != updatedScripts.end())
    {
        reference = existing->second;
        reference.managedTypeName = std::move(managedTypeName);
        reference.assemblyRelativePath = assemblyText;
        reference.runtimeConfigRelativePath = runtimeConfigText;
        reference.sourceSha256 = sourceHash;
        existing->second = reference;
    }
    else
    {
        if (entries_.size() + scriptEntries_.size() + modelEntries_.size() + materialEntries_.size() >= MaxAssetEntries)
            return scriptReferenceError(core::ErrorCode::ValidationError, "Asset index exceeds 100000 total entries");
        if (nextId_ == 0 || nextId_ == std::numeric_limits<std::uint64_t>::max())
            return scriptReferenceError(core::ErrorCode::InvalidState, "Asset ID space is exhausted");
        reference = {{nextId_}, sourceText, std::move(managedTypeName), assemblyText, runtimeConfigText, sourceHash};
        updatedScripts.emplace(reference.id, reference);
    }

    const auto newNextId = existing == updatedScripts.end() ? nextId_ + 1 : nextId_;
    const auto saved = saveIndex(entries_, updatedScripts, modelEntries_, materialEntries_, newNextId);
    if (!saved) return scriptReferenceError(saved.error().code, saved.error().message);
    scriptEntries_ = std::move(updatedScripts);
    nextId_ = newNextId;
    return core::Result<ScriptAssetReference>::success(std::move(reference));
}

ScriptBuildReport AssetRegistry::inspectScriptBuild() const
{
    ScriptBuildReport report;
    auto state = readScriptBuildState(projectRoot_);
    if (!state)
    {
        if (state.error().code == core::ErrorCode::NotFound)
        {
            report.freshness = ScriptBuildFreshness::Missing;
            report.diagnostics.push_back(state.error().message);
        }
        else
        {
            report.freshness = ScriptBuildFreshness::Invalid;
            report.diagnostics.push_back(state.error().message);
        }
        return report;
    }
    report.fingerprint = state.value().fingerprint;
    report.sdkVersion = state.value().sdkVersion;
    report.assemblyRelativePath = state.value().assemblyRelativePath;
    report.assemblySha256 = state.value().assemblySha256;
    report.runtimeConfigRelativePath = state.value().runtimeConfigRelativePath;
    report.runtimeConfigSha256 = state.value().runtimeConfigSha256;
    const auto validated = validateScriptBuildState(projectRoot_, state.value());
    if (!validated)
    {
        report.freshness = validated.error().code == core::ErrorCode::InvalidState
            || validated.error().code == core::ErrorCode::IoError
            ? ScriptBuildFreshness::Stale : ScriptBuildFreshness::Invalid;
        report.diagnostics.push_back(validated.error().message);
        return report;
    }

    report.freshness = ScriptBuildFreshness::Current;
    std::set<std::string> boundSources;
    for (const auto& binding : state.value().bindings)
    {
        boundSources.insert(binding.sourceRelativePath);
        ScriptBuildBindingStatus item;
        item.sourceRelativePath = binding.sourceRelativePath;
        item.sourceSha256 = state.value().sources.at(binding.sourceRelativePath);
        item.managedTypeName = binding.managedTypeName;
        const auto source = state.value().sources.find(binding.sourceRelativePath);
        const auto existing = std::find_if(scriptEntries_.begin(), scriptEntries_.end(), [&](const auto& entry) {
            return entry.second.sourceRelativePath == binding.sourceRelativePath;
        });
        if (existing != scriptEntries_.end())
        {
            item.assetId = existing->first;
            if (existing->second.managedTypeName != binding.managedTypeName)
                item.diagnostic = "Registered managed type differs from the current build binding; register the current build";
            else if (existing->second.sourceSha256 != source->second)
                item.diagnostic = "Registered source hash differs from the current build; register the current build";
            else if (existing->second.assemblyRelativePath != state.value().assemblyRelativePath
                || existing->second.runtimeConfigRelativePath != state.value().runtimeConfigRelativePath)
                item.diagnostic = "Registered assembly output differs from the current build; register the current build";
            else
                item.registered = true;
        }
        if (!item.registered)
        {
            report.requiresRegistration = true;
            if (existing == scriptEntries_.end())
                item.diagnostic = "Managed type is selected but not registered in the project asset index";
        }
        report.bindings.push_back(std::move(item));
    }
    for (const auto& [source, hash] : state.value().sources)
        if (!boundSources.contains(source))
            report.diagnostics.push_back("No managed type is selected for C# source: " + source);
    for (const auto& [id, reference] : scriptEntries_)
        if (!boundSources.contains(reference.sourceRelativePath))
            report.diagnostics.push_back("Registered Script AssetId " + std::to_string(id.value)
                + " is not selected by the current C# build manifest: " + reference.sourceRelativePath);
    if (state.value().bindings.empty())
        report.diagnostics.push_back("No managed type bindings are configured in the C# build manifest");
    return report;
}

core::Result<std::vector<ScriptAssetReference>> AssetRegistry::registerScriptsFromBuild()
{
    const auto state = readScriptBuildState(projectRoot_);
    if (!state)
        return core::Result<std::vector<ScriptAssetReference>>::failure(state.error());
    const auto validated = validateScriptBuildState(projectRoot_, state.value());
    if (!validated)
        return core::Result<std::vector<ScriptAssetReference>>::failure(validated.error());
    if (state.value().bindings.empty())
        return core::Result<std::vector<ScriptAssetReference>>::success({});

    std::error_code error;
    const auto canonicalProject = std::filesystem::canonical(projectRoot_, error);
    if (error)
        return core::Result<std::vector<ScriptAssetReference>>::failure({core::ErrorCode::IoError,
            "Cannot resolve project root while registering C# build: " + error.message()});
    auto assembly = resolveProjectFile(canonicalProject, fromUtf8(state.value().assemblyRelativePath),
        std::filesystem::path("Library") / "Scripts", ".dll", "Managed assembly");
    if (!assembly) return core::Result<std::vector<ScriptAssetReference>>::failure(assembly.error());
    auto runtimeConfig = resolveProjectFile(canonicalProject, fromUtf8(state.value().runtimeConfigRelativePath),
        std::filesystem::path("Library") / "Scripts", ".runtimeconfig.json", "Managed runtimeconfig");
    if (!runtimeConfig) return core::Result<std::vector<ScriptAssetReference>>::failure(runtimeConfig.error());
    const auto assemblyText = utf8(assembly.value().lexically_relative(canonicalProject));
    const auto runtimeConfigText = utf8(runtimeConfig.value().lexically_relative(canonicalProject));
    if (assemblyText.size() > MaxAssetPathBytes || runtimeConfigText.size() > MaxAssetPathBytes)
        return core::Result<std::vector<ScriptAssetReference>>::failure({core::ErrorCode::ValidationError,
            "C# build output paths exceed the 4096 UTF-8 byte limit"});

    auto updatedScripts = scriptEntries_;
    auto nextId = nextId_;
    std::vector<ScriptAssetReference> registered;
    registered.reserve(state.value().bindings.size());
    for (const auto& binding : state.value().bindings)
    {
        const auto sourcePath = fromUtf8(binding.sourceRelativePath);
        auto source = resolveProjectFile(canonicalProject, sourcePath, "Scripts", ".cs", "C# source");
        if (!source) return core::Result<std::vector<ScriptAssetReference>>::failure(source.error());
        const auto sourceText = utf8(source.value().lexically_relative(canonicalProject));
        if (sourceText.size() > MaxAssetPathBytes || !validManagedTypeName(binding.managedTypeName))
            return core::Result<std::vector<ScriptAssetReference>>::failure({core::ErrorCode::ValidationError,
                "C# build binding source or managed type name is invalid"});
        const auto expectedSource = state.value().sources.find(binding.sourceRelativePath);
        if (expectedSource == state.value().sources.end())
            return core::Result<std::vector<ScriptAssetReference>>::failure({core::ErrorCode::ValidationError,
                "C# build binding references a source missing from its build snapshot"});
        const auto existing = std::find_if(updatedScripts.begin(), updatedScripts.end(), [&](const auto& entry) {
            return entry.second.sourceRelativePath == sourceText;
        });
        ScriptAssetReference reference;
        if (existing != updatedScripts.end())
        {
            reference = existing->second;
            reference.managedTypeName = binding.managedTypeName;
            reference.assemblyRelativePath = assemblyText;
            reference.runtimeConfigRelativePath = runtimeConfigText;
            reference.sourceSha256 = expectedSource->second;
            existing->second = reference;
        }
        else
        {
            if (entries_.size() + updatedScripts.size() + modelEntries_.size() + materialEntries_.size() >= MaxAssetEntries)
                return core::Result<std::vector<ScriptAssetReference>>::failure({core::ErrorCode::ValidationError,
                    "Asset index exceeds 100000 total entries"});
            if (nextId == 0 || nextId == std::numeric_limits<std::uint64_t>::max())
                return core::Result<std::vector<ScriptAssetReference>>::failure({core::ErrorCode::InvalidState,
                    "Asset ID space is exhausted"});
            reference = {{nextId++}, sourceText, binding.managedTypeName, assemblyText,
                runtimeConfigText, expectedSource->second};
            updatedScripts.emplace(reference.id, reference);
        }
        registered.push_back(std::move(reference));
    }
    std::set<std::pair<std::string, std::string>> registeredTypes;
    for (const auto& [id, reference] : updatedScripts)
    {
        static_cast<void>(id);
        if (!registeredTypes.emplace(reference.assemblyRelativePath, reference.managedTypeName).second)
            return core::Result<std::vector<ScriptAssetReference>>::failure({core::ErrorCode::AlreadyExists,
                "Multiple script sources select the same managed type in one assembly"});
    }
    const auto saved = saveIndex(entries_, updatedScripts, modelEntries_, materialEntries_, nextId);
    if (!saved) return core::Result<std::vector<ScriptAssetReference>>::failure(saved.error());
    scriptEntries_ = std::move(updatedScripts);
    nextId_ = nextId;
    return core::Result<std::vector<ScriptAssetReference>>::success(std::move(registered));
}

std::vector<AssetId> AssetRegistry::missingAssets() const
{
    std::vector<AssetId> missing;
    for (const auto& [id, reference] : entries_)
    {
        auto source = resolveSource(assetsRoot_, fromUtf8(reference.sourceRelativePath));
        if (!source) missing.push_back(id);
    }
    for (const auto& [id, reference] : modelEntries_)
    {
        auto source = resolveSource(assetsRoot_, fromUtf8(reference.sourceRelativePath));
        if (!source) missing.push_back(id);
    }
    for (const auto& [id, reference] : materialEntries_)
    {
        const auto projectRelative = std::filesystem::path("Assets") / fromUtf8(reference.sourceRelativePath);
        auto source = resolveProjectFile(projectRoot_, projectRelative,
            std::filesystem::path("Assets") / "Materials", ".astro-material", "Material source");
        if (!source) missing.push_back(id);
    }
    return missing;
}

std::filesystem::path AssetRegistry::cachedPath(const AssetId id) const
{
    const auto* reference = find(id);
    if (!reference) return {};
    const auto path = cacheFile(cacheRoot_, *reference);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) return {};
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !isWithin(canonical, cacheRoot_)) return {};
    const auto bytes = readBytes(canonical);
    if (!bytes || bytes.value().size() != reference->sourceSize || sha256Hex(bytes.value()) != reference->sha256) return {};
    return canonical;
}

core::Result<ModelAssetReference> AssetRegistry::registerModel(
    const std::filesystem::path& sourceRelativePath, std::string importContentHash)
{
    if (!validHash(importContentHash))
        return modelReferenceError(core::ErrorCode::ValidationError,
            "Imported model content hash must be a lowercase SHA-256 value");
    const auto extension = utf8(sourceRelativePath.extension());
    if (extension != ".gltf" && extension != ".glb")
        return modelReferenceError(core::ErrorCode::ValidationError,
            "Model source extension must be .gltf or .glb");
    auto resolved = resolveSource(assetsRoot_, sourceRelativePath);
    if (!resolved) return modelReferenceError(resolved.error().code, resolved.error().message);
    auto relative = resolved.value().lexically_relative(assetsRoot_).lexically_normal();
    const auto relativeText = utf8(relative);
    if (!validUtf8(relativeText) || relativeText.find('\0') != std::string::npos
        || relativeText.size() > MaxAssetPathBytes || !validRelative(relative))
        return modelReferenceError(core::ErrorCode::ValidationError,
            "Model source path is invalid or exceeds 4096 UTF-8 bytes");
    if (std::any_of(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry.second.sourceRelativePath == relativeText;
    }))
        return modelReferenceError(core::ErrorCode::AlreadyExists,
            "This source path is already registered as an image asset");
    auto bytes = readModelSource(resolved.value());
    if (!bytes) return modelReferenceError(bytes.error().code, bytes.error().message);

    auto updated = modelEntries_;
    auto existing = std::find_if(updated.begin(), updated.end(), [&](const auto& item) {
        return item.second.sourceRelativePath == relativeText;
    });
    ModelAssetReference reference;
    if (existing != updated.end())
    {
        reference = existing->second;
        reference.sourceSize = static_cast<std::uint64_t>(bytes.value().size());
        reference.sourceSha256 = sha256Hex(bytes.value());
        reference.importContentHash = std::move(importContentHash);
        existing->second = reference;
    }
    else
    {
        if (entries_.size() + scriptEntries_.size() + modelEntries_.size() + materialEntries_.size() >= MaxAssetEntries)
            return modelReferenceError(core::ErrorCode::ValidationError,
                "Asset index exceeds 100000 total entries");
        if (nextId_ == 0 || nextId_ == std::numeric_limits<std::uint64_t>::max())
            return modelReferenceError(core::ErrorCode::InvalidState, "Asset ID space is exhausted");
        reference = {{nextId_}, relativeText, static_cast<std::uint64_t>(bytes.value().size()),
            sha256Hex(bytes.value()), std::move(importContentHash)};
        updated.emplace(reference.id, reference);
    }
    const auto newNextId = existing == updated.end() ? nextId_ + 1 : nextId_;
    const auto saved = saveIndex(entries_, scriptEntries_, updated, materialEntries_, newNextId);
    if (!saved) return modelReferenceError(saved.error().code, saved.error().message);
    modelEntries_ = std::move(updated);
    nextId_ = newNextId;
    return core::Result<ModelAssetReference>::success(std::move(reference));
}

core::Result<MaterialAssetReference> AssetRegistry::registerMaterial(
    const std::filesystem::path& sourceRelativePath)
{
    const auto extension = utf8(sourceRelativePath.extension());
    if (extension != ".astro-material")
        return materialReferenceError(core::ErrorCode::ValidationError,
            "Material source extension must be .astro-material");
    const auto projectRelative = std::filesystem::path("Assets") / sourceRelativePath;
    auto resolved = resolveProjectFile(projectRoot_, projectRelative,
        std::filesystem::path("Assets") / "Materials", ".astro-material", "Material source");
    if (!resolved) return materialReferenceError(resolved.error().code, resolved.error().message);
    const auto relative = resolved.value().lexically_relative(assetsRoot_).lexically_normal();
    const auto relativeText = utf8(relative);
    if (!validUtf8(relativeText) || relativeText.find('\0') != std::string::npos
        || relativeText.size() > MaxAssetPathBytes || !validRelative(relative)
        || !hasPathPrefix(relative, std::filesystem::path("Materials")))
        return materialReferenceError(core::ErrorCode::ValidationError,
            "Material source path must be a normalized UTF-8 path under Assets/Materials");
    std::error_code error;
    const auto sourceSize = std::filesystem::file_size(resolved.value(), error);
    if (error)
        return materialReferenceError(core::ErrorCode::IoError, "Cannot inspect material source: " + error.message());
    if (sourceSize == 0 || sourceSize > MaxMaterialSourceBytes)
        return materialReferenceError(core::ErrorCode::ValidationError,
            "Material source must contain 1 byte through 64 KiB");
    auto bytes = readBytes(resolved.value());
    if (!bytes) return materialReferenceError(bytes.error().code, bytes.error().message);
    if (bytes.value().size() != sourceSize)
        return materialReferenceError(core::ErrorCode::IoError, "Material source changed while it was being read");
    const auto parsed = parseMaterialAsset(std::string_view(
        reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()));
    if (!parsed) return materialReferenceError(parsed.error().code, parsed.error().message);
    const auto referencesValid = validateMaterialImageReferences(parsed.value(), entries_);
    if (!referencesValid)
        return materialReferenceError(referencesValid.error().code, referencesValid.error().message);
    if (std::any_of(entries_.begin(), entries_.end(), [&](const auto& item) {
            return item.second.sourceRelativePath == relativeText;
        }) || std::any_of(modelEntries_.begin(), modelEntries_.end(), [&](const auto& item) {
            return item.second.sourceRelativePath == relativeText;
        }))
        return materialReferenceError(core::ErrorCode::AlreadyExists,
            "This source path is already registered as another asset type");

    auto updated = materialEntries_;
    auto existing = std::find_if(updated.begin(), updated.end(), [&](const auto& item) {
        return item.second.sourceRelativePath == relativeText;
    });
    MaterialAssetReference reference;
    const auto sourceHash = sha256Hex(bytes.value());
    if (existing != updated.end())
    {
        reference = existing->second;
        reference.schemaVersion = parsed.value().schemaVersion;
        reference.sourceSize = sourceSize;
        reference.sourceSha256 = sourceHash;
        existing->second = reference;
    }
    else
    {
        if (entries_.size() + scriptEntries_.size() + modelEntries_.size() + materialEntries_.size() >= MaxAssetEntries)
            return materialReferenceError(core::ErrorCode::ValidationError, "Asset index exceeds 100000 total entries");
        if (nextId_ == 0 || nextId_ == std::numeric_limits<std::uint64_t>::max())
            return materialReferenceError(core::ErrorCode::InvalidState, "Asset ID space is exhausted");
        reference = {{nextId_}, relativeText, parsed.value().schemaVersion, sourceSize, sourceHash};
        updated.emplace(reference.id, reference);
    }
    const auto newNextId = existing == updated.end() ? nextId_ + 1 : nextId_;
    const auto saved = saveIndex(entries_, scriptEntries_, modelEntries_, updated, newNextId);
    if (!saved) return materialReferenceError(saved.error().code, saved.error().message);
    materialEntries_ = std::move(updated);
    nextId_ = newNextId;
    return core::Result<MaterialAssetReference>::success(std::move(reference));
}

core::Result<MaterialAssetReference> AssetRegistry::saveMaterial(const AssetId id,
    const MaterialAssetData& material)
{
    const auto* reference = findMaterial(id);
    if (!reference)
        return materialReferenceError(core::ErrorCode::NotFound, "Material AssetId is not registered");
    if (material.schemaVersion != 2)
        return materialReferenceError(core::ErrorCode::VersionMismatch,
            "Explicit material saves must use schema version 2");
    auto current = resolveMaterial(id);
    if (!current) return materialReferenceError(current.error().code, current.error().message);
    const auto encoded = serializeMaterialAsset(material);
    if (!encoded) return materialReferenceError(encoded.error().code, encoded.error().message);
    const auto referencesValid = validateMaterialImageReferences(material, entries_);
    if (!referencesValid)
        return materialReferenceError(referencesValid.error().code, referencesValid.error().message);

    const auto previousBytes = readBytes(current.value().sourcePath);
    if (!previousBytes) return materialReferenceError(previousBytes.error().code, previousBytes.error().message);
    const auto relative = fromUtf8(reference->sourceRelativePath);
    std::vector<unsigned char> newBytes(encoded.value().begin(), encoded.value().end());
    const auto published = writeAtomically(current.value().sourcePath, newBytes);
    if (!published) return materialReferenceError(published.error().code, published.error().message);

    auto updated = registerMaterial(relative);
    if (!updated)
    {
        const auto restored = writeAtomically(current.value().sourcePath, previousBytes.value());
        if (!restored)
            return materialReferenceError(core::ErrorCode::IoError,
                "Material save failed and the previous source bytes could not be restored: "
                    + updated.error().message + "; " + restored.error().message);
        return materialReferenceError(updated.error().code, updated.error().message);
    }
    return updated;
}

core::Result<AssetReference> AssetRegistry::importImage(const std::filesystem::path& sourceRelativePath)
{
    auto resolved = resolveSource(assetsRoot_, sourceRelativePath);
    if (!resolved) return referenceError(resolved.error().code, resolved.error().message);
    auto relative = resolved.value().lexically_relative(assetsRoot_);
    relative = relative.lexically_normal();
    const auto relativeText = utf8(relative);
    if (!validUtf8(relativeText) || relativeText.find('\0') != std::string::npos
        || relativeText.size() > MaxAssetPathBytes)
        return referenceError(core::ErrorCode::ValidationError, "Asset source path is not valid UTF-8");
    auto bytes = readBytes(resolved.value());
    if (!bytes) return referenceError(bytes.error().code, bytes.error().message);
    auto image = inspect(bytes.value(), resolved.value());
    if (!image) return referenceError(image.error().code, image.error().message);
    const auto hash = sha256Hex(bytes.value());

    for (const auto& [id, existing] : entries_)
    {
        if (existing.sourceRelativePath != relativeText) continue;
        if (existing.sha256 != hash)
            return referenceError(core::ErrorCode::InvalidState, "Source content changed; call reimport with the existing AssetId");
        auto cached = ensureCached(cacheRoot_, existing, bytes.value());
        if (!cached) return referenceError(cached.error().code, cached.error().message);
        return core::Result<AssetReference>::success(existing);
    }
    if (nextId_ == 0 || nextId_ == std::numeric_limits<std::uint64_t>::max())
        return referenceError(core::ErrorCode::InvalidState, "Asset ID space is exhausted");
    AssetReference reference{{nextId_}, relativeText, image.value().format, image.value().width, image.value().height,
        static_cast<std::uint64_t>(bytes.value().size()), hash};
    auto cached = ensureCached(cacheRoot_, reference, bytes.value());
    if (!cached) return referenceError(cached.error().code, cached.error().message);
    auto updated = entries_;
    updated.emplace(reference.id, reference);
    const auto indexSaved = saveIndex(updated, scriptEntries_, modelEntries_, materialEntries_, nextId_ + 1);
    if (!indexSaved) return referenceError(indexSaved.error().code, indexSaved.error().message);
    entries_ = std::move(updated);
    ++nextId_;
    return core::Result<AssetReference>::success(std::move(reference));
}

core::Result<AssetReference> AssetRegistry::reimport(const AssetId id)
{
    const auto* existing = find(id);
    if (!existing) return referenceError(core::ErrorCode::NotFound, "Asset ID is not registered");
    const auto sourcePath = fromUtf8(existing->sourceRelativePath);
    auto resolved = resolveSource(assetsRoot_, sourcePath);
    if (!resolved) return referenceError(resolved.error().code, resolved.error().message);
    auto bytes = readBytes(resolved.value());
    if (!bytes) return referenceError(bytes.error().code, bytes.error().message);
    auto image = inspect(bytes.value(), resolved.value());
    if (!image) return referenceError(image.error().code, image.error().message);
    AssetReference updatedReference = *existing;
    updatedReference.format = image.value().format;
    updatedReference.width = image.value().width;
    updatedReference.height = image.value().height;
    updatedReference.sourceSize = static_cast<std::uint64_t>(bytes.value().size());
    updatedReference.sha256 = sha256Hex(bytes.value());
    auto cached = ensureCached(cacheRoot_, updatedReference, bytes.value());
    if (!cached) return referenceError(cached.error().code, cached.error().message);
    auto updated = entries_;
    updated[id] = updatedReference;
    const auto indexSaved = saveIndex(updated, scriptEntries_, modelEntries_, materialEntries_, nextId_);
    if (!indexSaved) return referenceError(indexSaved.error().code, indexSaved.error().message);
    entries_ = std::move(updated);
    return core::Result<AssetReference>::success(std::move(updatedReference));
}
}
