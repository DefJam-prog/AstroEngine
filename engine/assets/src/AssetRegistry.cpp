#include "astro/assets/AssetRegistry.hpp"

#include <algorithm>
#include <array>
#include <atomic>
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
        const auto saved = registry.saveIndex({}, 1);
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
    if (!input || !nextLine() || line != "ASTRO_ASSETS 1")
        return assetError(core::ErrorCode::ParseError, "Invalid asset index header or version");
    if (!nextLine()) return assetError(core::ErrorCode::ParseError, "Asset index counter is missing");
    std::istringstream counterLine(line); counterLine.imbue(std::locale::classic());
    std::string marker;
    if (!(counterLine >> marker >> registry.nextId_) || marker != "NEXT" || registry.nextId_ == 0 || hasExtra(counterLine))
        return assetError(core::ErrorCode::ParseError, "Invalid asset ID counter");
    std::uint64_t maximumId = 0;
    std::set<std::string> registeredPaths;
    bool ended = false;
    while (nextLine())
    {
        if (line == "END") { ended = true; break; }
        std::istringstream record(line); record.imbue(std::locale::classic());
        std::string format, relativeText, hash;
        AssetReference reference;
        if (!(record >> marker >> reference.id.value >> std::quoted(relativeText) >> format
            >> reference.width >> reference.height >> reference.sourceSize >> hash) || marker != "ASSET" || hasExtra(record))
            return assetError(core::ErrorCode::ParseError, "Invalid asset index entry");
        if ((format != "PNG" && format != "JPEG") || !validHash(hash) || !validUtf8(relativeText)
            || relativeText.size() > MaxAssetPathBytes
            || !validDimensions(reference.width, reference.height) || reference.sourceSize == 0 || reference.sourceSize > MaxImageBytes)
            return assetError(core::ErrorCode::ValidationError, "Asset index contains invalid metadata");
        reference.format = format == "PNG" ? ImageFormat::Png : ImageFormat::Jpeg;
        reference.sourceRelativePath = relativeText;
        reference.sha256 = std::move(hash);
        const auto sourcePath = fromUtf8(reference.sourceRelativePath);
        if (reference.id.value == 0 || reference.sourceRelativePath.find('\0') != std::string::npos
            || !validRelative(sourcePath) || sourcePath != sourcePath.lexically_normal())
            return assetError(core::ErrorCode::ValidationError, "Asset index contains an invalid ID or source path");
        if (registry.entries_.size() >= MaxAssetEntries)
            return assetError(core::ErrorCode::ValidationError, "Asset index has a forbidden path or exceeds the 100000-entry limit");
        if (!registeredPaths.emplace(reference.sourceRelativePath).second)
            return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate source path");
        maximumId = std::max(maximumId, reference.id.value);
        if (!registry.entries_.emplace(reference.id, std::move(reference)).second)
            return assetError(core::ErrorCode::AlreadyExists, "Asset index contains a duplicate ID");
    }
    if (!ended || input.bad()) return assetError(core::ErrorCode::ParseError, "Asset index terminator is missing or unreadable");
    while (nextLine())
        if (line.find_first_not_of(" \t\r") != std::string::npos)
            return assetError(core::ErrorCode::ParseError, "Unexpected data after asset index terminator");
    if (maximumId >= registry.nextId_)
        return assetError(core::ErrorCode::ValidationError, "Asset ID counter must exceed every registered ID");
    return core::Result<AssetRegistry>::success(std::move(registry));
}

core::Result<void> AssetRegistry::saveIndex(const std::map<AssetId, AssetReference>& entries, const std::uint64_t nextId) const
{
    if (entries.size() > MaxAssetEntries)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Asset index exceeds 100000 entries"});
    std::ostringstream output; output.imbue(std::locale::classic());
    output << "ASTRO_ASSETS 1\nNEXT " << nextId << '\n';
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

std::vector<AssetId> AssetRegistry::missingAssets() const
{
    std::vector<AssetId> missing;
    for (const auto& [id, reference] : entries_)
    {
        auto source = resolveSource(assetsRoot_, fromUtf8(reference.sourceRelativePath));
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
    const auto indexSaved = saveIndex(updated, nextId_ + 1);
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
    const auto indexSaved = saveIndex(updated, nextId_);
    if (!indexSaved) return referenceError(indexSaved.error().code, indexSaved.error().message);
    entries_ = std::move(updated);
    return core::Result<AssetReference>::success(std::move(updatedReference));
}
}
