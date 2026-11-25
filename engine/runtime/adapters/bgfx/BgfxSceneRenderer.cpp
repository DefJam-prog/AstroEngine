#include "BgfxSceneRenderer.hpp"

#include "astro/assets/MaterialAsset.hpp"
#include "astro/render3d/BaseColorTexture.hpp"
#include "astro/render3d/NormalMatrix.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <vector>

namespace astro::runtime
{
namespace
{
constexpr std::size_t MaxCachedMeshes = 4096;
constexpr std::uint32_t MaxVertices = 1'000'000;
constexpr std::uint32_t MaxIndices = 3'000'000;
constexpr std::uint64_t MaxCachedTextureBytes = 256ull * 1024 * 1024;

core::Error invalid(const char* message)
{
    return {core::ErrorCode::ValidationError, message};
}

bool sameGeometry(const render3d::MeshData& lhs, const render3d::MeshData& rhs)
{
    if (lhs.hasNormals() != rhs.hasNormals() || lhs.hasTexCoords() != rhs.hasTexCoords()
        || lhs.vertices().size() != rhs.vertices().size()
        || lhs.triangleIndices() != rhs.triangleIndices()) return false;
    for (std::size_t i = 0; i < lhs.vertices().size(); ++i)
    {
        const auto& a = lhs.vertices()[i];
        const auto& b = rhs.vertices()[i];
        if (a.position != b.position || a.color != b.color || a.normal != b.normal || a.texCoord0 != b.texCoord0)
            return false;
    }
    return true;
}

std::array<std::uint32_t, 4> materialFactor(const render3d::SceneRenderItem& item)
{
    std::array<std::uint32_t, 4> bits{};
    if (item.material)
        for (std::size_t i = 0; i < bits.size(); ++i)
            bits[i] = std::bit_cast<std::uint32_t>(item.material->baseColorFactor[i]);
    else
        bits.fill(std::bit_cast<std::uint32_t>(1.0f));
    return bits;
}
}

std::size_t BgfxSceneRenderer::CacheKeyHash::operator()(const CacheKey& key) const noexcept
{
    std::uint64_t hash = key.geometry;
    for (const auto factor : key.factor)
    {
        hash ^= factor + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    }
    return static_cast<std::size_t>(hash);
}

BgfxSceneRenderer::BgfxSceneRenderer(const bgfx::ProgramHandle program) : program_(program)
{
    layout_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
}

BgfxSceneRenderer::BgfxSceneRenderer(const bgfx::ProgramHandle program,
    const bgfx::ProgramHandle texturedProgram, const bgfx::UniformHandle baseColorSampler,
    const bgfx::UniformHandle alphaParams)
    : BgfxSceneRenderer(program)
{
    texturedProgram_ = texturedProgram;
    baseColorSampler_ = baseColorSampler;
    alphaParams_ = alphaParams;
}

void BgfxSceneRenderer::configureDirectionalLighting(const bgfx::UniformHandle normalMatrix,
    const bgfx::UniformHandle lightParams, const bgfx::UniformHandle lightDirections,
    const bgfx::UniformHandle lightColors, const bgfx::UniformHandle baseColorFactor) noexcept
{
    normalMatrix_ = normalMatrix;
    lightParams_ = lightParams;
    lightDirections_ = lightDirections;
    lightColors_ = lightColors;
    baseColorFactor_ = baseColorFactor;
    hasNormalAttribute_ = bgfx::isValid(normalMatrix_) && bgfx::isValid(lightParams_)
        && bgfx::isValid(lightDirections_) && bgfx::isValid(lightColors_) && bgfx::isValid(baseColorFactor_);
    if (hasNormalAttribute_)
        layout_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float).end();
}

void BgfxSceneRenderer::configurePointLighting(const bgfx::UniformHandle positions,
    const bgfx::UniformHandle colors, const bgfx::UniformHandle ranges) noexcept
{
    pointPositions_ = positions;
    pointColors_ = colors;
    pointRanges_ = ranges;
}

BgfxSceneRenderer::~BgfxSceneRenderer()
{
    clearCache();
}

core::Result<BgfxSceneRenderer::CachedMesh*> BgfxSceneRenderer::resolveGeometry(
    const render3d::SceneRenderItem& item, const bool bakeBaseColorFactor)
{
    if (!item.geometry)
        return core::Result<CachedMesh*>::failure(invalid("Scene render item has no geometry"));
    const auto& geometry = *item.geometry;
    if (geometry.vertices().empty() || geometry.vertices().size() > MaxVertices
        || geometry.triangleIndices().empty() || geometry.triangleIndices().size() > MaxIndices
        || geometry.triangleIndices().size() % 3 != 0)
        return core::Result<CachedMesh*>::failure(invalid("Scene geometry exceeds BGFX adapter limits"));
    auto factor = materialFactor(item);
    if (!bakeBaseColorFactor)
        factor.fill(std::bit_cast<std::uint32_t>(1.0f));
    CacheKey key{geometry.contentHash(), factor};
    if (const auto found = meshes_.find(key); found != meshes_.end())
    {
        if (!sameGeometry(*found->second.source, geometry))
            return core::Result<CachedMesh*>::failure(invalid("Mesh content hash collision in BGFX cache"));
        return core::Result<CachedMesh*>::success(&found->second);
    }
    if (meshes_.size() >= MaxCachedMeshes)
        return core::Result<CachedMesh*>::failure(invalid("BGFX scene mesh cache limit reached; clear it at scene unload"));

    const bool use32BitIndices = geometry.vertices().size() > 65'536;
    if (use32BitIndices && (!bgfx::getCaps() || !(bgfx::getCaps()->supported & BGFX_CAPS_INDEX32)))
        return core::Result<CachedMesh*>::failure(invalid("BGFX backend does not support required 32-bit mesh indices"));

    struct Vertex final { float x, y, z; std::uint8_t r, g, b, a; float u, v; float nx, ny, nz; };
    struct LegacyVertex final { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
    static_assert(sizeof(Vertex) == 36 && sizeof(LegacyVertex) == 24);
    std::vector<std::uint8_t> vertexData(geometry.vertices().size()
        * (hasNormalAttribute_ ? sizeof(Vertex) : sizeof(LegacyVertex)));
    for (const auto& vertex : geometry.vertices())
    {
        const auto tint = [&factor](const std::size_t channel) {
            const float value = std::bit_cast<float>(factor[channel]);
            return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const auto multiply = [](const std::uint8_t value, const std::uint8_t color) {
            return static_cast<std::uint8_t>((static_cast<unsigned>(value) * color + 127u) / 255u);
        };
        const auto r = multiply(vertex.color.r, tint(0));
        const auto g = multiply(vertex.color.g, tint(1));
        const auto b = multiply(vertex.color.b, tint(2));
        const auto alpha = multiply(vertex.color.a, tint(3));
        if (hasNormalAttribute_)
        {
            const Vertex packed{vertex.position.x, vertex.position.y, vertex.position.z,
                r, g, b, alpha, vertex.texCoord0.x, vertex.texCoord0.y,
                geometry.hasNormals() ? vertex.normal.x : 0.0f,
                geometry.hasNormals() ? vertex.normal.y : 0.0f,
                geometry.hasNormals() ? vertex.normal.z : 0.0f};
            std::memcpy(vertexData.data() + (&vertex - geometry.vertices().data()) * sizeof(Vertex),
                &packed, sizeof(Vertex));
        }
        else
        {
            const LegacyVertex packed{vertex.position.x, vertex.position.y, vertex.position.z,
                r, g, b, alpha, vertex.texCoord0.x, vertex.texCoord0.y};
            std::memcpy(vertexData.data() + (&vertex - geometry.vertices().data()) * sizeof(LegacyVertex),
                &packed, sizeof(LegacyVertex));
        }
    }
    const auto vertexBytes = vertexData.size();
    if (vertexBytes > std::numeric_limits<std::uint32_t>::max())
        return core::Result<CachedMesh*>::failure(invalid("BGFX vertex buffer byte size is out of range"));

    CachedMesh cached;
    cached.source = item.geometry;
    cached.vertices = bgfx::createVertexBuffer(bgfx::copy(vertexData.data(), static_cast<std::uint32_t>(vertexBytes)), layout_);
    if (use32BitIndices)
    {
        const auto& source = geometry.triangleIndices();
        const auto bytes = source.size() * sizeof(std::uint32_t);
        if (bytes > std::numeric_limits<std::uint32_t>::max())
        {
            if (bgfx::isValid(cached.vertices)) bgfx::destroy(cached.vertices);
            return core::Result<CachedMesh*>::failure(invalid("BGFX index buffer byte size is out of range"));
        }
        cached.indices = bgfx::createIndexBuffer(bgfx::copy(source.data(), static_cast<std::uint32_t>(bytes)),
            BGFX_BUFFER_INDEX32);
    }
    else
    {
        std::vector<std::uint16_t> indices;
        indices.reserve(geometry.triangleIndices().size());
        for (const auto index : geometry.triangleIndices())
        {
            if (index > std::numeric_limits<std::uint16_t>::max())
            {
                if (bgfx::isValid(cached.vertices)) bgfx::destroy(cached.vertices);
                return core::Result<CachedMesh*>::failure(invalid("BGFX 16-bit mesh index is out of range"));
            }
            indices.push_back(static_cast<std::uint16_t>(index));
        }
        const auto bytes = indices.size() * sizeof(std::uint16_t);
        cached.indices = bgfx::createIndexBuffer(bgfx::copy(indices.data(), static_cast<std::uint32_t>(bytes)));
    }
    if (!bgfx::isValid(cached.vertices) || !bgfx::isValid(cached.indices))
    {
        if (bgfx::isValid(cached.vertices)) bgfx::destroy(cached.vertices);
        if (bgfx::isValid(cached.indices)) bgfx::destroy(cached.indices);
        return core::Result<CachedMesh*>::failure({core::ErrorCode::InternalError,
            "BGFX could not allocate cached Scene geometry"});
    }
    auto [inserted, ok] = meshes_.emplace(key, std::move(cached));
    if (!ok)
    {
        bgfx::destroy(cached.vertices);
        bgfx::destroy(cached.indices);
        return core::Result<CachedMesh*>::failure({core::ErrorCode::InternalError,
            "BGFX geometry cache insertion failed"});
    }
    return core::Result<CachedMesh*>::success(&inserted->second);
}

core::Result<BgfxSceneRenderer::ResolvedTexture> BgfxSceneRenderer::resolveBaseColorTexture(
    const render3d::SceneRenderItem& item, const BaseColorImageResolver& resolver,
    const BaseColorImageHashResolver& hashResolver, const ExternalBaseColorImageResolver& externalResolver)
{
    const bool sourceTexture = item.sourceBaseColorImage.has_value();
    if (!item.material || (!item.material->baseColorTexture && !sourceTexture))
        return core::Result<ResolvedTexture>::success({});
    const auto& material = *item.material;
    if (material.metallicRoughnessTexture || material.normalTexture || material.occlusionTexture
        || material.emissiveTexture || material.alphaMode == assets::MaterialAlphaMode::Blend)
        return core::Result<ResolvedTexture>::failure({core::ErrorCode::InvalidState,
            "Runtime BGFX supports only opaque or masked base-color texture materials"});
    const auto texCoord = sourceTexture ? item.sourceBaseColorTexCoord : material.baseColorTexture->texCoord;
    if ((!sourceTexture && material.baseColorTexture->imageAssetId.value == 0)
        || texCoord != 0 || !item.geometry->hasTexCoords())
        return core::Result<ResolvedTexture>::failure({core::ErrorCode::InvalidState,
            "Runtime BGFX base-color texture requires a registered image and mesh UV0"});
    const auto uvTransform = sourceTexture ? item.sourceBaseColorUvTransform : material.baseColorUvTransform;
    if (uvTransform != assets::MaterialUvTransform{})
        return core::Result<ResolvedTexture>::failure({core::ErrorCode::InvalidState,
            "Runtime BGFX base-color texture currently requires the identity UV transform"});
    if (!bgfx::isValid(texturedProgram_) || !bgfx::isValid(baseColorSampler_)
        || !bgfx::isValid(alphaParams_) || (sourceTexture ? !externalResolver
            : (!resolver || !hashResolver)))
        return core::Result<ResolvedTexture>::failure({core::ErrorCode::InvalidState,
            "Runtime BGFX textured path is not configured with host shaders and an image resolver"});

    auto samplerFlags = [](const assets::MaterialTextureWrapMode mode, const bool vertical)
        -> std::optional<std::uint32_t> {
        switch (mode)
        {
        case assets::MaterialTextureWrapMode::Repeat: return 0u;
        case assets::MaterialTextureWrapMode::MirroredRepeat:
            return vertical ? BGFX_SAMPLER_V_MIRROR : BGFX_SAMPLER_U_MIRROR;
        case assets::MaterialTextureWrapMode::ClampToEdge:
            return vertical ? BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_U_CLAMP;
        }
        return std::nullopt;
    };
    const auto wrapU = samplerFlags(sourceTexture ? item.sourceBaseColorWrapU : material.baseColorWrapU, false);
    const auto wrapV = samplerFlags(sourceTexture ? item.sourceBaseColorWrapV : material.baseColorWrapV, true);
    if (!wrapU || !wrapV)
        return core::Result<ResolvedTexture>::failure(invalid("Runtime BGFX base-color sampler wrap mode is invalid"));
    std::uint32_t flags = *wrapU | *wrapV;
    const auto minFilter = sourceTexture ? item.sourceBaseColorMinFilter : material.baseColorMinFilter;
    const auto magFilter = sourceTexture ? item.sourceBaseColorMagFilter : material.baseColorMagFilter;
    using Min = assets::MaterialTextureMinFilter;
    render3d::BaseColorMinFilter runtimeMinFilter;
    switch (minFilter)
    {
    case Min::Nearest: runtimeMinFilter = render3d::BaseColorMinFilter::Nearest; break;
    case Min::Linear: runtimeMinFilter = render3d::BaseColorMinFilter::Linear; break;
    case Min::NearestMipmapNearest: runtimeMinFilter = render3d::BaseColorMinFilter::NearestMipmapNearest; break;
    case Min::LinearMipmapNearest: runtimeMinFilter = render3d::BaseColorMinFilter::LinearMipmapNearest; break;
    case Min::NearestMipmapLinear: runtimeMinFilter = render3d::BaseColorMinFilter::NearestMipmapLinear; break;
    case Min::LinearMipmapLinear: runtimeMinFilter = render3d::BaseColorMinFilter::LinearMipmapLinear; break;
    default:
        return core::Result<ResolvedTexture>::failure({core::ErrorCode::InvalidState,
            "Runtime BGFX base-color minification filter is invalid"});
    }
    const auto minSelection = render3d::selectBaseColorMinFilter(runtimeMinFilter);
    if (!minSelection)
        return core::Result<ResolvedTexture>::failure(invalid("Runtime BGFX base-color minification filter is invalid"));
    if (minSelection->nearestTexel) flags |= BGFX_SAMPLER_MIN_POINT;
    if (minSelection->nearestMip) flags |= BGFX_SAMPLER_MIP_POINT;
    if (magFilter == assets::MaterialTextureMagFilter::Nearest)
        flags |= BGFX_SAMPLER_MAG_POINT;
    else if (magFilter != assets::MaterialTextureMagFilter::Linear)
        return core::Result<ResolvedTexture>::failure(invalid("Runtime BGFX base-color magnification filter is invalid"));

    std::string key;
    if (sourceTexture)
    {
        if (item.sourceBaseColorImageHash.empty())
            return core::Result<ResolvedTexture>::failure({core::ErrorCode::NotFound,
                "Runtime BGFX source glTF base-color image has no verified content hash"});
        key = "source:";
        const auto pathUtf8 = item.sourceBaseColorImage->u8string();
        key.append(reinterpret_cast<const char*>(pathUtf8.data()), pathUtf8.size());
        key += ":" + item.sourceBaseColorImageHash + ":";
    }
    else
    {
        auto sourceHash = hashResolver(material.baseColorTexture->imageAssetId);
        if (!sourceHash) return core::Result<ResolvedTexture>::failure(sourceHash.error());
        key = "asset:" + std::to_string(material.baseColorTexture->imageAssetId.value)
            + ":" + sourceHash.value() + ":";
    }
    for (const auto factor : material.baseColorFactor)
        key += std::to_string(std::bit_cast<std::uint32_t>(factor)) + ":";
    key += material.alphaMode == assets::MaterialAlphaMode::Mask ? "mask:" : "opaque:";
    if (material.alphaMode == assets::MaterialAlphaMode::Mask)
        key += std::to_string(std::bit_cast<std::uint32_t>(material.alphaCutoff)) + ":";
    key += minSelection->usesMipmaps ? "mips:" : "single:";
    if (const auto found = textures_.find(key); found != textures_.end())
        return core::Result<ResolvedTexture>::success({found->second.texture, flags});

    auto image = sourceTexture ? externalResolver(*item.sourceBaseColorImage)
        : resolver(material.baseColorTexture->imageAssetId);
    if (!image) return core::Result<ResolvedTexture>::failure(image.error());
    const auto& decoded = image.value();
    const auto pixelCount = std::uint64_t(decoded.width) * decoded.height;
    if (decoded.width == 0 || decoded.height == 0 || decoded.width > 16384 || decoded.height > 16384
        || pixelCount > 16ull * 1024 * 1024 || decoded.srgbBgra8.size() != pixelCount * 4)
        return core::Result<ResolvedTexture>::failure(invalid("Decoded Runtime BGFX base-color image exceeds supported bounds"));
    auto prepared = render3d::prepareLinearBaseColorMipChainBgra8(decoded.srgbBgra8,
        decoded.width, decoded.height, material.baseColorFactor, minSelection->usesMipmaps,
        material.alphaMode == assets::MaterialAlphaMode::Mask
            ? render3d::BaseColorAlphaMipMode::Mask : render3d::BaseColorAlphaMipMode::Opaque,
        material.alphaCutoff);
    if (!prepared) return core::Result<ResolvedTexture>::failure(prepared.error());
    const auto& chain = prepared.value();
    const auto& level = chain.levels.front();
    const auto uploadBytes = static_cast<std::uint64_t>(prepared.value().pixels.size());
    if (cachedTextureBytes_ > MaxCachedTextureBytes
        || uploadBytes > MaxCachedTextureBytes - cachedTextureBytes_)
        return core::Result<ResolvedTexture>::failure(invalid("Runtime BGFX base-color texture cache reached its 256 MiB limit"));
    const auto texture = bgfx::createTexture2D(static_cast<std::uint16_t>(level.width),
        static_cast<std::uint16_t>(level.height), minSelection->usesMipmaps, 1, bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_NONE);
    if (!bgfx::isValid(texture))
        return core::Result<ResolvedTexture>::failure({core::ErrorCode::InternalError,
            "BGFX could not allocate the base-color texture"});
    for (std::size_t mip = 0; mip < chain.levels.size(); ++mip)
    {
        const auto& mipLevel = chain.levels[mip];
        bgfx::updateTexture2D(texture, 0, static_cast<std::uint8_t>(mip), 0, 0,
            static_cast<std::uint16_t>(mipLevel.width), static_cast<std::uint16_t>(mipLevel.height),
            bgfx::copy(chain.pixels.data() + mipLevel.offset, static_cast<std::uint32_t>(mipLevel.byteSize)));
    }
    textures_.emplace(key, CachedTexture{texture, uploadBytes,
        static_cast<std::uint8_t>(chain.levels.size())});
    cachedTextureBytes_ += uploadBytes;
    return core::Result<ResolvedTexture>::success({texture, flags});
}

core::Result<std::uint32_t> BgfxSceneRenderer::render(const render3d::SceneRenderPacket& packet,
    const bgfx::ViewId view, const std::uint16_t width, const std::uint16_t height,
    const BaseColorImageResolver& resolveBaseColorImage,
    const BaseColorImageHashResolver& resolveBaseColorImageHash,
    const ExternalBaseColorImageResolver& resolveExternalBaseColorImage,
    const bool outputIsSrgbAttachment)
{
    if (!bgfx::isValid(program_))
        return core::Result<std::uint32_t>::failure({core::ErrorCode::InvalidState,
            "BGFX scene renderer requires a valid host shader program"});
    if (width == 0 || height == 0)
        return core::Result<std::uint32_t>::failure(invalid("BGFX scene viewport dimensions must be non-zero"));
    const auto cameraView = packet.camera.viewMatrix();
    const auto projection = packet.camera.projectionMatrix(static_cast<float>(width) / height);
    if (!cameraView) return core::Result<std::uint32_t>::failure(cameraView.error());
    if (!projection) return core::Result<std::uint32_t>::failure(projection.error());
    struct ResolvedItem final
    {
        CachedMesh* mesh = nullptr;
        ResolvedTexture texture{};
    };
    std::vector<ResolvedItem> resolvedItems;
    resolvedItems.reserve(packet.items.size());
    for (const auto& item : packet.items)
    {
        if (item.material)
        {
            const auto valid = assets::validateMaterialAsset(*item.material);
            if (!valid) return core::Result<std::uint32_t>::failure(valid.error());
            if (item.material->metallicRoughnessTexture || item.material->normalTexture
                || item.material->occlusionTexture || item.material->emissiveTexture
                || item.material->alphaMode == assets::MaterialAlphaMode::Blend)
                return core::Result<std::uint32_t>::failure({core::ErrorCode::InvalidState,
                    "This BGFX adapter supports opaque/masked baseColorFactor/base-color textures only"});
            if (item.material->alphaMode == assets::MaterialAlphaMode::Mask
                && !bgfx::isValid(alphaParams_))
                return core::Result<std::uint32_t>::failure({core::ErrorCode::InvalidState,
                    "Runtime BGFX masked materials require the host alpha shader uniform"});
        }
        const bool hasValidNormalTransform = render3d::inverseTransposeNormalMatrix(item.worldTransform).hasValue();
        const bool lit = hasNormalAttribute_ && (!packet.directionalLights.empty() || !packet.pointLights.empty())
            && item.geometry && item.geometry->hasNormals() && hasValidNormalTransform;
        auto geometry = resolveGeometry(item, !lit);
        if (!geometry) return core::Result<std::uint32_t>::failure(geometry.error());
        auto texture = resolveBaseColorTexture(item, resolveBaseColorImage, resolveBaseColorImageHash,
            resolveExternalBaseColorImage);
        if (!texture) return core::Result<std::uint32_t>::failure(texture.error());
        resolvedItems.push_back({geometry.value(), texture.value()});
    }

    bgfx::setViewTransform(view, cameraView.value().values.data(), projection.value().values.data());
    std::array<float, 16> lightDirections{};
    std::array<float, 16> lightColors{};
    const auto lightCount = static_cast<std::uint8_t>(std::min<std::size_t>(4, packet.directionalLights.size()));
    std::array<float, 16> pointPositions{};
    std::array<float, 16> pointColors{};
    std::array<float, 16> pointRanges{};
    const auto pointCount = static_cast<std::uint8_t>(std::min<std::size_t>(4, packet.pointLights.size()));
    for (std::size_t i = 0; i < lightCount; ++i)
    {
        const auto& light = packet.directionalLights[i];
        lightDirections[i * 4] = light.direction.x;
        lightDirections[i * 4 + 1] = light.direction.y;
        lightDirections[i * 4 + 2] = light.direction.z;
        lightColors[i * 4] = std::max(0.0f, light.color[0]) * std::max(0.0f, light.intensity);
        lightColors[i * 4 + 1] = std::max(0.0f, light.color[1]) * std::max(0.0f, light.intensity);
        lightColors[i * 4 + 2] = std::max(0.0f, light.color[2]) * std::max(0.0f, light.intensity);
    }
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        const auto& light = packet.pointLights[i];
        pointPositions[i * 4] = light.position.x;
        pointPositions[i * 4 + 1] = light.position.y;
        pointPositions[i * 4 + 2] = light.position.z;
        pointColors[i * 4] = std::max(0.0f, light.color[0]) * std::max(0.0f, light.intensity);
        pointColors[i * 4 + 1] = std::max(0.0f, light.color[1]) * std::max(0.0f, light.intensity);
        pointColors[i * 4 + 2] = std::max(0.0f, light.color[2]) * std::max(0.0f, light.intensity);
        pointRanges[i * 4] = light.range;
    }
    if (bgfx::isValid(pointPositions_)) bgfx::setUniform(pointPositions_, pointPositions.data(), 4);
    if (bgfx::isValid(pointColors_)) bgfx::setUniform(pointColors_, pointColors.data(), 4);
    if (bgfx::isValid(pointRanges_)) bgfx::setUniform(pointRanges_, pointRanges.data(), 4);
    if (hasNormalAttribute_)
    {
        bgfx::setUniform(lightDirections_, lightDirections.data(), 4);
        bgfx::setUniform(lightColors_, lightColors.data(), 4);
    }
    for (std::size_t i = 0; i < packet.items.size(); ++i)
    {
        const auto& item = packet.items[i];
        const auto& resolved = resolvedItems[i];
        bgfx::setTransform(item.worldTransform.values.data());
        if (hasNormalAttribute_)
        {
            const auto normalMatrix = render3d::inverseTransposeNormalMatrix(item.worldTransform);
            const bool canLight = item.geometry->hasNormals() && normalMatrix;
            const float params[4]{canLight ? static_cast<float>(lightCount) : 0.0f,
                canLight ? static_cast<float>(pointCount) : 0.0f, 0.0f, 0.0f};
            // Base-color textures already contain their linear factor after CPU preparation.
            // Vertex-color materials still need the shader multiplier when lighting is enabled.
            const auto factor = bgfx::isValid(resolved.texture.texture)
                ? std::array<std::uint32_t, 4>{std::bit_cast<std::uint32_t>(1.0f),
                    std::bit_cast<std::uint32_t>(1.0f), std::bit_cast<std::uint32_t>(1.0f),
                    std::bit_cast<std::uint32_t>(1.0f)}
                : materialFactor(item);
            const float baseColorFactor[4]{std::bit_cast<float>(factor[0]), std::bit_cast<float>(factor[1]),
                std::bit_cast<float>(factor[2]), std::bit_cast<float>(factor[3])};
            const std::array<float, 9> identity{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
            bgfx::setUniform(normalMatrix_, normalMatrix ? normalMatrix.value().values.data() : identity.data());
            bgfx::setUniform(lightParams_, params);
            bgfx::setUniform(baseColorFactor_, baseColorFactor);
        }
        bgfx::setVertexBuffer(0, resolved.mesh->vertices);
        bgfx::setIndexBuffer(resolved.mesh->indices);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS);
        const float alphaParams[4]{item.material
                && item.material->alphaMode == assets::MaterialAlphaMode::Mask ? 1.0f : 0.0f,
            item.material ? item.material->alphaCutoff : 0.5f, outputIsSrgbAttachment ? 1.0f : 0.0f, 0.0f};
        if (bgfx::isValid(alphaParams_)) bgfx::setUniform(alphaParams_, alphaParams);
        if (bgfx::isValid(resolved.texture.texture))
        {
            bgfx::setTexture(0, baseColorSampler_, resolved.texture.texture, resolved.texture.samplerFlags);
            bgfx::submit(view, texturedProgram_);
        }
        else bgfx::submit(view, program_);
    }
    return core::Result<std::uint32_t>::success(static_cast<std::uint32_t>(resolvedItems.size()));
}

void BgfxSceneRenderer::clearCache() noexcept
{
    for (auto& [key, mesh] : meshes_)
    {
        (void)key;
        if (bgfx::isValid(mesh.vertices)) bgfx::destroy(mesh.vertices);
        if (bgfx::isValid(mesh.indices)) bgfx::destroy(mesh.indices);
    }
    meshes_.clear();
    for (auto& [key, texture] : textures_)
    {
        (void)key;
        if (bgfx::isValid(texture.texture)) bgfx::destroy(texture.texture);
    }
    textures_.clear();
    cachedTextureBytes_ = 0;
}

std::size_t BgfxSceneRenderer::cachedMipTextureCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(textures_.begin(), textures_.end(), [](const auto& entry) {
        return entry.second.mipLevels > 1;
    }));
}
}
