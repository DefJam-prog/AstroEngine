#include "astro/render3d/GltfImporter.hpp"

#include "astro/assets/AssetRegistry.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace astro::render3d
{
namespace
{
constexpr std::size_t MaxSourceBytes = 8u * 1024u * 1024u;
constexpr std::size_t MaxBufferBytes = 64u * 1024u * 1024u;
constexpr std::size_t MaxTotalBufferBytes = 128u * 1024u * 1024u;
constexpr std::size_t MaxMeshes = 128;
constexpr std::size_t MaxPrimitives = 256;
constexpr std::size_t MaxTotalVertices = 1'000'000;
constexpr std::size_t MaxTotalIndices = 3'000'000;
constexpr std::size_t MaxCacheBytes = 64u * 1024u * 1024u;
constexpr std::uint32_t CacheVersion = 1;
constexpr std::string_view CacheMagic = "ASTROMSH";

using Bytes = std::vector<std::uint8_t>;

core::Result<GltfImportResult> failure(const core::ErrorCode code, std::string message)
{
    return core::Result<GltfImportResult>::failure({code, std::move(message)});
}

std::string pathUtf8(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

bool isWithin(const std::filesystem::path& candidate, const std::filesystem::path& base)
{
    const auto relative = candidate.lexically_relative(base);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name()) return false;
    for (const auto& component : relative)
        if (component == ".." || component == ".") return false;
    return true;
}

bool isReparsePoint(const std::filesystem::path& path, const std::filesystem::file_status& status)
{
    if (std::filesystem::is_symlink(status)) return true;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    (void)path;
    return false;
#endif
}

core::Result<std::filesystem::path> canonicalProjectRoot(const std::filesystem::path& root)
{
    std::error_code error;
    auto canonical = std::filesystem::canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::NotFound,
            "glTF import project root must be an existing directory"});
    return core::Result<std::filesystem::path>::success(std::move(canonical));
}

core::Result<std::filesystem::path> resolveAssetFile(
    const std::filesystem::path& assetsRoot, const std::filesystem::path& candidate)
{
    std::error_code error;
    const auto relative = candidate.lexically_relative(assetsRoot);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF resource path is outside the project Assets directory"});
    for (const auto& component : relative)
        if (component == ".." || component == ".")
            return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
                "glTF resource path escapes the project Assets directory"});

    auto current = assetsRoot;
    for (const auto& component : relative)
    {
        current /= component;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error)
            return core::Result<std::filesystem::path>::failure({core::ErrorCode::NotFound,
                "glTF resource is missing: " + pathUtf8(current)});
        if (isReparsePoint(current, status))
            return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
                "glTF resource paths may not pass through symlinks or reparse points"});
    }

    const auto canonical = std::filesystem::canonical(candidate, error);
    if (error || !isWithin(canonical, assetsRoot))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF resource resolves outside the project Assets directory"});
    if (!std::filesystem::is_regular_file(canonical, error) || error)
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF resource must be a regular file"});
    return core::Result<std::filesystem::path>::success(canonical);
}

core::Result<Bytes> readBoundedFile(const std::filesystem::path& path, const std::size_t maximum,
    const std::string_view description)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return core::Result<Bytes>::failure({core::ErrorCode::IoError,
            "Cannot inspect " + std::string(description) + ": " + error.message()});
    if (size == 0 || size > maximum)
        return core::Result<Bytes>::failure({core::ErrorCode::ValidationError,
            std::string(description) + " must contain 1 byte through " + std::to_string(maximum) + " bytes"});
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return core::Result<Bytes>::failure({core::ErrorCode::IoError,
            "Cannot open " + std::string(description)});
    Bytes bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof())
        return core::Result<Bytes>::failure({core::ErrorCode::IoError,
            std::string(description) + " changed or could not be read completely"});
    return core::Result<Bytes>::success(std::move(bytes));
}

core::Result<std::filesystem::path> resolveSourcePath(const std::filesystem::path& root,
    const std::filesystem::path& sourceRelativePath)
{
    if (sourceRelativePath.empty() || sourceRelativePath.is_absolute()
        || sourceRelativePath.has_root_name() || sourceRelativePath.has_root_directory())
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF source path must be relative to the project root"});
    auto component = sourceRelativePath.begin();
    if (component == sourceRelativePath.end() || *component != "Assets")
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF source path must begin with Assets/"});
    for (const auto& part : sourceRelativePath)
        if (part == ".." || part == ".")
            return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
                "glTF source path may not contain dot segments"});
    const auto extension = sourceRelativePath.extension().string();
    if (extension != ".gltf" && extension != ".glb")
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF source extension must be .gltf or .glb"});

    std::error_code error;
    const auto assetsStatus = std::filesystem::symlink_status(root / "Assets", error);
    if (error || isReparsePoint(root / "Assets", assetsStatus))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "Project Assets directory may not be a symlink or reparse point"});
    const auto assetsRoot = std::filesystem::canonical(root / "Assets", error);
    if (error || !std::filesystem::is_directory(assetsRoot, error) || error || !isWithin(assetsRoot, root))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "Project Assets directory must exist inside the project root"});
    const auto resolved = resolveAssetFile(assetsRoot, root / sourceRelativePath);
    if (!resolved) return core::Result<std::filesystem::path>::failure(resolved.error());
    return resolved;
}

core::Result<std::filesystem::path> resolveExternalUri(const std::filesystem::path& assetsRoot,
    const std::filesystem::path& modelPath, const fastgltf::URI& uri)
{
    if (!uri.isLocalPath() || !uri.scheme().empty() || !uri.host().empty() || !uri.userinfo().empty()
        || !uri.port().empty() || !uri.query().empty() || !uri.fragment().empty())
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF external buffers must use a plain relative file URI"});
    const auto relative = uri.fspath();
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF external buffer URI must be a relative path"});
    const auto normalized = (modelPath.parent_path() / relative).lexically_normal();
    return resolveAssetFile(assetsRoot, normalized);
}

core::Result<std::filesystem::path> prepareCacheDirectory(const std::filesystem::path& root)
{
    auto current = root;
    std::error_code error;
    for (const auto& component : {std::filesystem::path("Library"), std::filesystem::path("Derived"), std::filesystem::path("gltf")})
    {
        current /= component;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            if (!std::filesystem::create_directory(current, error) && error)
                return core::Result<std::filesystem::path>::failure({core::ErrorCode::IoError,
                    "Cannot create glTF cache directory: " + error.message()});
        }
        else if (error)
            return core::Result<std::filesystem::path>::failure({core::ErrorCode::IoError,
                "Cannot inspect glTF cache directory: " + error.message()});
        else if (isReparsePoint(current, status) || !std::filesystem::is_directory(status))
            return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
                "glTF cache directories may not be links or non-directories"});
        error.clear();
    }
    const auto canonical = std::filesystem::canonical(current, error);
    if (error || !isWithin(canonical, root))
        return core::Result<std::filesystem::path>::failure({core::ErrorCode::ValidationError,
            "glTF cache must resolve inside the project root"});
    return core::Result<std::filesystem::path>::success(canonical);
}

void appendU32(Bytes& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendText(Bytes& bytes, const std::string_view value)
{
    appendU32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

bool takeU32(const Bytes& bytes, std::size_t& offset, const std::size_t limit, std::uint32_t& value)
{
    if (offset > limit || limit - offset < 4) return false;
    value = static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool takeText(const Bytes& bytes, std::size_t& offset, const std::size_t limit,
    const std::size_t maximum, std::string& value)
{
    std::uint32_t size = 0;
    if (!takeU32(bytes, offset, limit, size) || size > maximum || offset > limit || limit - offset < size)
        return false;
    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
    offset += size;
    return true;
}

Bytes encodeCache(const std::vector<ImportedGltfMesh>& meshes, const std::string_view contentHash)
{
    Bytes bytes;
    bytes.insert(bytes.end(), CacheMagic.begin(), CacheMagic.end());
    appendU32(bytes, CacheVersion);
    appendText(bytes, contentHash);
    appendU32(bytes, static_cast<std::uint32_t>(meshes.size()));
    for (const auto& mesh : meshes)
    {
        appendText(bytes, mesh.name);
        appendU32(bytes, static_cast<std::uint32_t>(mesh.primitives.size()));
        for (const auto& primitive : mesh.primitives)
        {
            appendU32(bytes, static_cast<std::uint32_t>(primitive.vertices().size()));
            appendU32(bytes, static_cast<std::uint32_t>(primitive.triangleIndices().size()));
            for (const auto& vertex : primitive.vertices())
            {
                appendU32(bytes, std::bit_cast<std::uint32_t>(vertex.position.x));
                appendU32(bytes, std::bit_cast<std::uint32_t>(vertex.position.y));
                appendU32(bytes, std::bit_cast<std::uint32_t>(vertex.position.z));
                bytes.push_back(vertex.color.r); bytes.push_back(vertex.color.g);
                bytes.push_back(vertex.color.b); bytes.push_back(vertex.color.a);
            }
            for (const auto index : primitive.triangleIndices()) appendU32(bytes, index);
        }
    }
    const auto checksum = assets::sha256Hex(bytes);
    bytes.insert(bytes.end(), checksum.begin(), checksum.end());
    return bytes;
}

core::Result<std::vector<ImportedGltfMesh>> decodeCache(const Bytes& bytes, const std::string_view contentHash)
{
    constexpr std::size_t HashBytes = 64;
    if (bytes.size() < CacheMagic.size() + 4 + 4 + HashBytes + 4 + HashBytes || bytes.size() > MaxCacheBytes)
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache size is invalid"});
    const auto payloadSize = bytes.size() - HashBytes;
    const std::string storedChecksum(reinterpret_cast<const char*>(bytes.data() + payloadSize), HashBytes);
    if (assets::sha256Hex(std::span<const std::uint8_t>(bytes.data(), payloadSize)) != storedChecksum)
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache checksum is invalid"});
    if (!std::equal(CacheMagic.begin(), CacheMagic.end(), bytes.begin()))
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache signature is invalid"});

    std::size_t offset = CacheMagic.size();
    std::uint32_t version = 0, meshCount = 0;
    std::string storedHash;
    if (!takeU32(bytes, offset, payloadSize, version) || version != CacheVersion
        || !takeText(bytes, offset, payloadSize, HashBytes, storedHash) || storedHash != contentHash
        || !takeU32(bytes, offset, payloadSize, meshCount) || meshCount == 0 || meshCount > MaxMeshes)
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache header is invalid"});

    std::size_t totalPrimitives = 0, totalVertices = 0, totalIndices = 0;
    std::vector<ImportedGltfMesh> meshes;
    meshes.reserve(meshCount);
    for (std::uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
    {
        ImportedGltfMesh mesh;
        std::uint32_t primitiveCount = 0;
        if (!takeText(bytes, offset, payloadSize, 4096, mesh.name)
            || !takeU32(bytes, offset, payloadSize, primitiveCount) || primitiveCount == 0
            || primitiveCount > MaxPrimitives - totalPrimitives)
            return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache entry is invalid"});
        totalPrimitives += primitiveCount;
        mesh.primitives.reserve(primitiveCount);
        for (std::uint32_t primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex)
        {
            std::uint32_t vertexCount = 0, indexCount = 0;
            if (!takeU32(bytes, offset, payloadSize, vertexCount) || !takeU32(bytes, offset, payloadSize, indexCount)
                || vertexCount == 0 || vertexCount > MaxTotalVertices - totalVertices
                || indexCount == 0 || indexCount % 3 != 0 || indexCount > MaxTotalIndices - totalIndices)
                return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache geometry bounds are invalid"});
            totalVertices += vertexCount;
            totalIndices += indexCount;
            if (vertexCount > (payloadSize - std::min(offset, payloadSize)) / 16)
                return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache vertices are truncated"});
            std::vector<MeshVertex> vertices;
            vertices.reserve(vertexCount);
            for (std::uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
            {
                std::uint32_t x = 0, y = 0, z = 0;
                if (!takeU32(bytes, offset, payloadSize, x) || !takeU32(bytes, offset, payloadSize, y)
                    || !takeU32(bytes, offset, payloadSize, z) || payloadSize - offset < 4)
                    return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache vertex is truncated"});
                Color8 color{bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]};
                offset += 4;
                vertices.push_back({{std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)}, color});
            }
            if (indexCount > (payloadSize - std::min(offset, payloadSize)) / 4)
                return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache indices are truncated"});
            std::vector<std::uint32_t> indices;
            indices.reserve(indexCount);
            for (std::uint32_t index = 0; index < indexCount; ++index)
            {
                std::uint32_t value = 0;
                if (!takeU32(bytes, offset, payloadSize, value))
                    return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache index is truncated"});
                indices.push_back(value);
            }
            auto meshData = MeshData::create(std::move(vertices), std::move(indices));
            if (!meshData)
                return core::Result<std::vector<ImportedGltfMesh>>::failure(meshData.error());
            mesh.primitives.push_back(std::move(meshData).value());
        }
        meshes.push_back(std::move(mesh));
    }
    if (offset != payloadSize)
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ParseError, "Mesh cache contains trailing payload"});
    return core::Result<std::vector<ImportedGltfMesh>>::success(std::move(meshes));
}

core::Result<void> writeAtomically(const std::filesystem::path& destination, const Bytes& bytes)
{
    static std::atomic<std::uint64_t> sequence{0};
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
        + "." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    auto temporary = destination;
    temporary += "." + suffix + ".tmp";
#ifdef _WIN32
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot create temporary mesh cache file"});
    bool success = true;
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written != chunk)
        { success = false; break; }
        offset += written;
    }
    if (success && !FlushFileBuffers(file)) success = false;
    if (!CloseHandle(file)) success = false;
    if (!success || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot atomically publish mesh cache"});
    }
#else
    const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file < 0)
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot create temporary mesh cache file"});
    bool success = true;
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
        if (written <= 0) { success = false; break; }
        offset += static_cast<std::size_t>(written);
    }
    if (success && ::fsync(file) != 0) success = false;
    if (::close(file) != 0) success = false;
    if (!success || ::rename(temporary.c_str(), destination.c_str()) != 0)
    {
        ::unlink(temporary.c_str());
        return core::Result<void>::failure({core::ErrorCode::IoError, "Cannot atomically publish mesh cache"});
    }
#endif
    return core::Result<void>::success();
}

core::Result<std::vector<ImportedGltfMesh>> extractMeshes(fastgltf::Asset& asset)
{
    if (!asset.extensionsRequired.empty())
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
            "glTF uses required extensions that this prototype does not support"});
    if (asset.meshes.empty() || asset.meshes.size() > MaxMeshes)
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
            "glTF must contain 1 through 128 meshes"});

    std::size_t totalPrimitives = 0, totalVertices = 0, totalIndices = 0;
    std::vector<ImportedGltfMesh> meshes;
    meshes.reserve(asset.meshes.size());
    for (const auto& sourceMesh : asset.meshes)
    {
        if (sourceMesh.primitives.empty() || sourceMesh.primitives.size() > MaxPrimitives - totalPrimitives)
            return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
                "glTF primitive count is outside the supported limit"});
        totalPrimitives += sourceMesh.primitives.size();
        ImportedGltfMesh mesh;
        mesh.name = sourceMesh.name;
        if (mesh.name.size() > 4096)
            return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
                "glTF mesh name exceeds 4096 bytes"});
        mesh.primitives.reserve(sourceMesh.primitives.size());
        for (const auto& primitive : sourceMesh.primitives)
        {
            if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty()
                || primitive.dracoCompression.has_value() || primitive.attributes.size() != 1
                || primitive.attributes.front().name != "POSITION")
                return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
                    "Only uncompressed static triangle primitives with POSITION and no other attributes are supported"});
            const auto& positionAccessor = asset.accessors[primitive.attributes.front().accessorIndex];
            if (positionAccessor.type != fastgltf::AccessorType::Vec3
                || positionAccessor.componentType != fastgltf::ComponentType::Float
                || positionAccessor.normalized || positionAccessor.sparse.has_value()
                || !positionAccessor.bufferViewIndex.has_value()
                || fastgltf::validateAccessorBounds(asset, positionAccessor) != fastgltf::Error::None
                || positionAccessor.count == 0 || positionAccessor.count > MaxTotalVertices - totalVertices)
                return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
                    "POSITION accessor must be a bounded, non-sparse FLOAT VEC3"});
            totalVertices += positionAccessor.count;

            std::vector<MeshVertex> vertices(positionAccessor.count);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, positionAccessor,
                [&vertices](const fastgltf::math::fvec3 position, const std::size_t index) {
                    vertices[index].position = {position.x(), position.y(), position.z()};
                });

            std::vector<std::uint32_t> indices;
            if (primitive.indicesAccessor.has_value())
            {
                const auto& accessor = asset.accessors[*primitive.indicesAccessor];
                if (accessor.type != fastgltf::AccessorType::Scalar || accessor.normalized
                    || accessor.sparse.has_value() || !accessor.bufferViewIndex.has_value()
                    || (accessor.componentType != fastgltf::ComponentType::UnsignedByte
                        && accessor.componentType != fastgltf::ComponentType::UnsignedShort
                        && accessor.componentType != fastgltf::ComponentType::UnsignedInt)
                    || fastgltf::validateAccessorBounds(asset, accessor) != fastgltf::Error::None
                    || accessor.count == 0 || accessor.count % 3 != 0
                    || accessor.count > MaxTotalIndices - totalIndices)
                    return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
                        "Triangle index accessor must be a bounded non-sparse unsigned scalar array"});
                indices.resize(accessor.count);
                fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset, accessor,
                    [&indices](const std::uint32_t index, const std::size_t position) { indices[position] = index; });
            }
            else
            {
                if (vertices.size() % 3 != 0 || vertices.size() > MaxTotalIndices - totalIndices)
                    return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
                        "Non-indexed triangle primitive must contain a bounded multiple of three vertices"});
                indices.resize(vertices.size());
                for (std::size_t index = 0; index < indices.size(); ++index)
                    indices[index] = static_cast<std::uint32_t>(index);
            }
            totalIndices += indices.size();
            auto meshData = MeshData::create(std::move(vertices), std::move(indices));
            if (!meshData)
                return core::Result<std::vector<ImportedGltfMesh>>::failure(meshData.error());
            mesh.primitives.push_back(std::move(meshData).value());
        }
        meshes.push_back(std::move(mesh));
    }
    return core::Result<std::vector<ImportedGltfMesh>>::success(std::move(meshes));
}

core::Result<std::string> calculateContentHash(const Bytes& source, const fastgltf::Asset& asset)
{
    std::string material = "ASTRO_GLTF_CACHE_1\n";
    material += assets::sha256Hex(source);
    material.push_back('\n');
    std::size_t totalBytes = 0;
    for (const auto& buffer : asset.buffers)
    {
        const auto* array = std::get_if<fastgltf::sources::Array>(&buffer.data);
        if (!array || array->bytes.size() < buffer.byteLength || array->bytes.size() > MaxBufferBytes
            || buffer.byteLength > MaxTotalBufferBytes - totalBytes)
            return core::Result<std::string>::failure({core::ErrorCode::ValidationError,
                "glTF buffer is missing, oversized, or not loaded into bounded memory"});
        totalBytes += buffer.byteLength;
        const auto* data = reinterpret_cast<const std::uint8_t*>(array->bytes.data());
        material += std::to_string(buffer.byteLength);
        material.push_back(':');
        material += assets::sha256Hex(std::span<const std::uint8_t>(data, array->bytes.size()));
        material.push_back('\n');
    }
    return core::Result<std::string>::success(assets::sha256Hex(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(material.data()), material.size())));
}

core::Result<void> loadExternalBuffers(fastgltf::Asset& asset,
    const std::filesystem::path& assetsRoot, const std::filesystem::path& modelPath)
{
    std::size_t totalBytes = 0;
    for (auto& buffer : asset.buffers)
    {
        if (buffer.byteLength == 0 || buffer.byteLength > MaxBufferBytes
            || buffer.byteLength > MaxTotalBufferBytes - totalBytes)
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "glTF declared buffer length exceeds configured limits"});
        totalBytes += buffer.byteLength;
        auto* uriSource = std::get_if<fastgltf::sources::URI>(&buffer.data);
        if (!uriSource) continue;
        if (uriSource->uri.valueless_after_move() || uriSource->uri->string().empty())
            return core::Result<void>::failure({core::ErrorCode::ParseError, "glTF buffer URI is empty"});
        const auto path = resolveExternalUri(assetsRoot, modelPath, *uriSource->uri);
        if (!path) return core::Result<void>::failure(path.error());
        auto bytes = readBoundedFile(path.value(), MaxBufferBytes, "glTF external buffer");
        if (!bytes) return core::Result<void>::failure(bytes.error());
        if (bytes.value().size() != buffer.byteLength)
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "External buffer byte length does not match its glTF declaration"});
        fastgltf::sources::Array array;
        array.bytes = fastgltf::static_vector<std::byte>(fastgltf::for_overwrite, bytes.value().size());
        std::memcpy(array.bytes.data(), bytes.value().data(), bytes.value().size());
        array.mimeType = fastgltf::MimeType::GltfBuffer;
        buffer.data = std::move(array);
    }
    return core::Result<void>::success();
}

core::Result<std::vector<ImportedGltfMesh>> loadCache(const std::filesystem::path& cachePath,
    const std::string_view key)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(cachePath, error);
    if (error == std::errc::no_such_file_or_directory)
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::NotFound, "Mesh cache miss"});
    if (error || isReparsePoint(cachePath, status) || !std::filesystem::is_regular_file(status))
        return core::Result<std::vector<ImportedGltfMesh>>::failure({core::ErrorCode::ValidationError,
            "Mesh cache entry is not a safe regular file"});
    auto bytes = readBoundedFile(cachePath, MaxCacheBytes, "mesh cache");
    if (!bytes) return core::Result<std::vector<ImportedGltfMesh>>::failure(bytes.error());
    return decodeCache(bytes.value(), key);
}
}

core::Result<GltfImportResult> GltfImporter::importFile(const std::filesystem::path& projectRoot,
    const std::filesystem::path& sourceRelativePath)
{
    try
    {
        auto root = canonicalProjectRoot(projectRoot);
        if (!root) return failure(root.error().code, root.error().message);
        auto sourcePath = resolveSourcePath(root.value(), sourceRelativePath);
        if (!sourcePath) return failure(sourcePath.error().code, sourcePath.error().message);
        auto sourceBytes = readBoundedFile(sourcePath.value(), MaxSourceBytes, "glTF source");
        if (!sourceBytes) return failure(sourceBytes.error().code, sourceBytes.error().message);

        auto data = fastgltf::GltfDataBuffer::FromBytes(
            reinterpret_cast<const std::byte*>(sourceBytes.value().data()), sourceBytes.value().size());
        if (!data) return failure(core::ErrorCode::ParseError,
            "Cannot read glTF/GLB source: " + std::string(fastgltf::getErrorMessage(data.error())));
        fastgltf::Parser parser;
        constexpr auto categories = fastgltf::Category::Asset | fastgltf::Category::Buffers
            | fastgltf::Category::BufferViews | fastgltf::Category::Accessors | fastgltf::Category::Meshes;
        auto parsed = parser.loadGltf(data.get(), sourcePath.value().parent_path(),
            fastgltf::Options::None, categories);
        if (!parsed) return failure(core::ErrorCode::ParseError,
            "Invalid glTF/GLB document: " + std::string(fastgltf::getErrorMessage(parsed.error())));
        auto& asset = parsed.get();
        std::error_code error;
        const auto assetsRoot = std::filesystem::canonical(root.value() / "Assets", error);
        if (error) return failure(core::ErrorCode::IoError, "Cannot resolve project Assets directory");
        const auto buffersLoaded = loadExternalBuffers(asset, assetsRoot, sourcePath.value());
        if (!buffersLoaded) return failure(buffersLoaded.error().code, buffersLoaded.error().message);
        auto key = calculateContentHash(sourceBytes.value(), asset);
        if (!key) return failure(key.error().code, key.error().message);

        auto cacheDirectory = prepareCacheDirectory(root.value());
        if (!cacheDirectory) return failure(cacheDirectory.error().code, cacheDirectory.error().message);
        const auto cachePath = cacheDirectory.value() / (key.value() + ".v1.astro-mesh-cache");
        auto cached = loadCache(cachePath, key.value());
        if (cached)
            return core::Result<GltfImportResult>::success({std::move(cached).value(), key.value(), cachePath, true, {}});
        if (cached.error().code != core::ErrorCode::NotFound && cached.error().code != core::ErrorCode::ParseError)
            return failure(cached.error().code, cached.error().message);

        auto meshes = extractMeshes(asset);
        if (!meshes) return failure(meshes.error().code, meshes.error().message);
        auto cacheBytes = encodeCache(meshes.value(), key.value());
        if (cacheBytes.size() > MaxCacheBytes)
            return failure(core::ErrorCode::ValidationError, "Generated mesh cache exceeds 64 MiB");
        const auto written = writeAtomically(cachePath, cacheBytes);
        if (!written) return failure(written.error().code, written.error().message);
        return core::Result<GltfImportResult>::success({std::move(meshes).value(), key.value(), cachePath, false, {}});
    }
    catch (const std::exception& exception)
    {
        return failure(core::ErrorCode::ValidationError, std::string("glTF import failed safely: ") + exception.what());
    }
}

core::Result<GltfImportResult> GltfImporter::importAndRegister(assets::AssetRegistry& registry,
    const std::filesystem::path& sourceRelativePath)
{
    auto imported = importFile(registry.projectRoot(), sourceRelativePath);
    if (!imported) return imported;
    const auto assetsPrefix = std::filesystem::path("Assets");
    const auto assetRelativePath = sourceRelativePath.lexically_relative(assetsPrefix);
    if (assetRelativePath.empty() || assetRelativePath.is_absolute() || assetRelativePath.has_root_name())
        return failure(core::ErrorCode::ValidationError,
            "Registered glTF source must be relative to the project Assets directory");
    auto registered = registry.registerModel(assetRelativePath, imported.value().contentHash);
    if (!registered) return failure(registered.error().code, registered.error().message);
    imported.value().modelAssetId = registered.value().id;
    return imported;
}
}
