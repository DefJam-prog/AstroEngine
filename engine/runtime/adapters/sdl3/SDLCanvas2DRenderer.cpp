#include "astro/runtime/SDLCanvas2DRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace astro::runtime
{
core::Result<void> SDLCanvas2DRenderer::render(const render2d::Canvas2D& canvas,
    const render2d::Camera2D& camera, const render2d::Viewport viewport,
    const render2d::Color clearColor) const
{
    return render(canvas, camera, viewport, SDLTextureResolver{}, clearColor);
}

core::Result<void> SDLCanvas2DRenderer::render(const render2d::Canvas2D& canvas,
    const render2d::Camera2D& camera, const render2d::Viewport viewport,
    const SDLTextureResolver& resolveTexture, const render2d::Color clearColor) const
{
    if (!renderer_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "SDL 2D renderer is null"});
    if (!SDL_SetRenderDrawColor(renderer_, clearColor.r, clearColor.g, clearColor.b, clearColor.a)
        || !SDL_RenderClear(renderer_)
        || !SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND))
        return core::Result<void>::failure({core::ErrorCode::IoError,
            std::string("SDL could not clear the 2D frame: ") + SDL_GetError()});

    struct DrawCommand final
    {
        std::int32_t layer;
        const render2d::Rect* rectangle;
        const render2d::Sprite* sprite;
    };
    std::vector<DrawCommand> ordered;
    ordered.reserve(canvas.rectangles().size() + canvas.sprites().size());
    for (const auto& rectangle : canvas.rectangles()) ordered.push_back({rectangle.layer, &rectangle, nullptr});
    for (const auto& sprite : canvas.sprites()) ordered.push_back({sprite.layer, nullptr, &sprite});
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.layer < right.layer;
    });

    for (const auto& command : ordered)
    {
        const auto rectangle = command.rectangle;
        const auto sprite = command.sprite;
        const render2d::Rect geometry = rectangle
            ? *rectangle
            : render2d::Rect{sprite->center, sprite->size, sprite->tint, sprite->layer};
        const auto projected = render2d::project(geometry, camera, viewport);
        if (!projected) return core::Result<void>::failure(projected.error());
        const auto& bounds = projected.value();
        constexpr double coordinateLimit = 10000000.0;
        if (std::abs(bounds.x) > coordinateLimit || std::abs(bounds.y) > coordinateLimit
            || bounds.width > coordinateLimit || bounds.height > coordinateLimit)
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Projected rectangle is too large for the SDL software coordinates"});
        const SDL_FRect destination{
            static_cast<float>(bounds.x), static_cast<float>(bounds.y),
            std::max(1.0f, static_cast<float>(bounds.width)),
            std::max(1.0f, static_cast<float>(bounds.height))};
        if (rectangle)
        {
            if (!SDL_SetRenderDrawColor(renderer_, rectangle->color.r, rectangle->color.g,
                rectangle->color.b, rectangle->color.a)
                || !SDL_RenderFillRect(renderer_, &destination))
                return core::Result<void>::failure({core::ErrorCode::IoError,
                    std::string("SDL could not draw a 2D rectangle: ") + SDL_GetError()});
            continue;
        }
        if (!resolveTexture)
            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                "Canvas contains sprites but no texture resolver was provided"});
        auto* texture = resolveTexture(sprite->asset);
        if (!texture)
            return core::Result<void>::failure({core::ErrorCode::NotFound,
                "SDL texture resolver could not resolve a Sprite asset ID"});
        const auto flip = sprite->flipX
            ? (sprite->flipY ? SDL_FLIP_HORIZONTAL_AND_VERTICAL : SDL_FLIP_HORIZONTAL)
            : (sprite->flipY ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
        if (!SDL_SetTextureColorMod(texture, sprite->tint.r, sprite->tint.g, sprite->tint.b))
            return core::Result<void>::failure({core::ErrorCode::IoError,
                std::string("SDL could not apply sprite tint: ") + SDL_GetError()});
        if (!SDL_SetTextureAlphaMod(texture, sprite->tint.a))
        {
            SDL_SetTextureColorMod(texture, 255, 255, 255);
            return core::Result<void>::failure({core::ErrorCode::IoError,
                std::string("SDL could not apply sprite alpha: ") + SDL_GetError()});
        }
        const bool drawn = SDL_RenderTextureRotated(renderer_, texture, nullptr, &destination,
            -sprite->rotationDegrees, nullptr, flip);
        const bool resetColor = SDL_SetTextureColorMod(texture, 255, 255, 255);
        const bool resetAlpha = SDL_SetTextureAlphaMod(texture, 255);
        if (!drawn || !resetColor || !resetAlpha)
            return core::Result<void>::failure({core::ErrorCode::IoError,
                std::string("SDL could not draw or reset sprite texture: ") + SDL_GetError()});
    }
    if (!SDL_RenderPresent(renderer_))
        return core::Result<void>::failure({core::ErrorCode::IoError,
            std::string("SDL could not present the 2D frame: ") + SDL_GetError()});
    return core::Result<void>::success();
}
}
