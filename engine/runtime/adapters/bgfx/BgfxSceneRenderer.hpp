#pragma once

#include "astro/render3d/SceneRenderPacket.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace astro::runtime
{
/// Optional BGFX consumer for backend-neutral Scene packets.
/// The host owns BGFX, the program handle and renderer lifetime ordering.
class BgfxSceneRenderer final
{
public:
    struct BaseColorImage final
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        // Read-only source data returned by the host's image decoder (BGRA8 sRGB).
        std::vector<std::uint8_t> srgbBgra8;
    };
    using BaseColorImageResolver = std::function<core::Result<BaseColorImage>(assets::AssetId)>;
    using ExternalBaseColorImageResolver = std::function<core::Result<BaseColorImage>(const std::filesystem::path&)>;
    using BaseColorImageHashResolver = std::function<core::Result<std::string>(assets::AssetId)>;

    explicit BgfxSceneRenderer(bgfx::ProgramHandle program);
    BgfxSceneRenderer(bgfx::ProgramHandle program, bgfx::ProgramHandle texturedProgram,
        bgfx::UniformHandle baseColorSampler, bgfx::UniformHandle alphaParams);
    /// Enables the project-owned directional-diffuse shader inputs. The supplied programs
    /// must consume Position, Color0, TexCoord0 and Normal, plus the named lighting uniforms.
    void configureDirectionalLighting(bgfx::UniformHandle normalMatrix, bgfx::UniformHandle lightParams,
        bgfx::UniformHandle lightDirections, bgfx::UniformHandle lightColors,
        bgfx::UniformHandle baseColorFactor) noexcept;
    void configurePointLighting(bgfx::UniformHandle positions, bgfx::UniformHandle colors,
        bgfx::UniformHandle ranges) noexcept;
    ~BgfxSceneRenderer();
    BgfxSceneRenderer(const BgfxSceneRenderer&) = delete;
    BgfxSceneRenderer& operator=(const BgfxSceneRenderer&) = delete;

    /// Submits visible packet items to an already configured BGFX view.
    /// Supports vertex colors, opaque baseColorFactor and project base-color textures when
    /// a host image resolver is supplied. Other texture/PBR/alpha passes remain unsupported.
    [[nodiscard]] core::Result<std::uint32_t> render(const render3d::SceneRenderPacket& packet,
        bgfx::ViewId view, std::uint16_t width, std::uint16_t height,
        const BaseColorImageResolver& resolveBaseColorImage = {},
        const BaseColorImageHashResolver& resolveBaseColorImageHash = {},
        const ExternalBaseColorImageResolver& resolveExternalBaseColorImage = {},
        bool outputIsSrgbAttachment = false);

    /// Release cached GPU geometry while the host BGFX context is still alive.
    void clearCache() noexcept;
    [[nodiscard]] std::size_t cachedMeshCount() const noexcept { return meshes_.size(); }
    [[nodiscard]] std::size_t cachedTextureCount() const noexcept { return textures_.size(); }
    [[nodiscard]] std::size_t cachedMipTextureCount() const noexcept;

private:
    struct CacheKey final
    {
        std::uint64_t geometry = 0;
        std::array<std::uint32_t, 4> factor{};
        friend bool operator==(const CacheKey&, const CacheKey&) = default;
    };
    struct CacheKeyHash final
    {
        std::size_t operator()(const CacheKey& key) const noexcept;
    };
    struct CachedMesh final
    {
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        std::shared_ptr<const render3d::MeshData> source;
    };
    struct CachedTexture final
    {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        std::uint64_t bytes = 0;
        std::uint8_t mipLevels = 1;
    };
    struct ResolvedTexture final
    {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        std::uint32_t samplerFlags = 0;
    };

    core::Result<CachedMesh*> resolveGeometry(const render3d::SceneRenderItem& item, bool bakeBaseColorFactor);
    core::Result<ResolvedTexture> resolveBaseColorTexture(const render3d::SceneRenderItem& item,
        const BaseColorImageResolver& resolver, const BaseColorImageHashResolver& hashResolver,
        const ExternalBaseColorImageResolver& externalResolver);
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::ProgramHandle texturedProgram_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle baseColorSampler_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle alphaParams_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle normalMatrix_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle lightParams_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle lightDirections_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle lightColors_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle pointPositions_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle pointColors_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle pointRanges_ = BGFX_INVALID_HANDLE; // borrowed from host
    bgfx::UniformHandle baseColorFactor_ = BGFX_INVALID_HANDLE; // borrowed from host
    bool hasNormalAttribute_ = false;
    bgfx::VertexLayout layout_{};
    std::unordered_map<CacheKey, CachedMesh, CacheKeyHash> meshes_;
    std::unordered_map<std::string, CachedTexture> textures_;
    std::uint64_t cachedTextureBytes_ = 0;
};
}
