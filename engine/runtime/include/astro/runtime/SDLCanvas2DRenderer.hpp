#pragma once

#include "astro/render2d/Canvas2D.hpp"

#include <SDL3/SDL_render.h>
#include <functional>

namespace astro::runtime
{
using SDLTextureResolver = std::function<SDL_Texture*(assets::AssetId)>;

// Non-owning SDL3 adapter. The caller owns SDL_Renderer and its window.
class SDLCanvas2DRenderer final
{
public:
    explicit SDLCanvas2DRenderer(SDL_Renderer* renderer) noexcept : renderer_(renderer) {}
    core::Result<void> render(const render2d::Canvas2D& canvas,
        const render2d::Camera2D& camera, render2d::Viewport viewport,
        render2d::Color clearColor = {18, 24, 34, 255}) const;
    core::Result<void> render(const render2d::Canvas2D& canvas,
        const render2d::Camera2D& camera, render2d::Viewport viewport,
        const SDLTextureResolver& resolveTexture,
        render2d::Color clearColor = {18, 24, 34, 255}) const;

private:
    SDL_Renderer* renderer_ = nullptr;
};
}
