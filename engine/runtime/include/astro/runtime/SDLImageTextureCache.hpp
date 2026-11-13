#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"

#include <SDL3/SDL_render.h>

#include <map>
#include <memory>

namespace astro::runtime
{
// Windows WIC image decoder at the SDL adapter boundary. The cache borrows the
// registry and renderer; the caller must keep both alive for its lifetime.
class SDLImageTextureCache final
{
public:
    SDLImageTextureCache(SDL_Renderer* renderer, const assets::AssetRegistry& registry) noexcept;
    ~SDLImageTextureCache();
    SDLImageTextureCache(const SDLImageTextureCache&) = delete;
    SDLImageTextureCache& operator=(const SDLImageTextureCache&) = delete;

    [[nodiscard]] core::Result<SDL_Texture*> texture(assets::AssetId id);
    void clear() noexcept;

private:
    struct TextureDeleter final { void operator()(SDL_Texture* value) const noexcept; };
    SDL_Renderer* renderer_ = nullptr;
    const assets::AssetRegistry* registry_ = nullptr;
    std::map<assets::AssetId, std::unique_ptr<SDL_Texture, TextureDeleter>> textures_;
};
}
