#include "astro/render2d/SceneSprites.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace astro::render2d
{
core::Result<void> appendSceneSprites(const scene::Scene& scene,
    const assets::AssetRegistry& assets, Canvas2D& canvas, const SceneSpriteOptions options)
{
    if (!std::isfinite(options.pixelsPerUnit) || options.pixelsPerUnit <= 0.0
        || options.pixelsPerUnit > 10000.0)
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "Scene sprite pixels-per-unit must be finite and within (0, 10000]"});

    std::vector<Sprite> pending;
    for (const auto& entity : scene.entities())
    {
        if (!entity.spriteRenderer || !entity.spriteRenderer->visible) continue;
        if (entity.parent)
            return core::Result<void>::failure({core::ErrorCode::InvalidState,
                "2D scene rendering currently requires root entities; hierarchical transforms are not yet composed"});
        const auto* transform = scene.transform(entity.id);
        if (!transform)
            return core::Result<void>::failure({core::ErrorCode::NotFound,
                "Visible SpriteRenderer entity is missing its Transform"});
        const auto* asset = assets.find(entity.spriteRenderer->texture);
        if (!asset)
            return core::Result<void>::failure({core::ErrorCode::NotFound,
                "Visible SpriteRenderer references an asset missing from the registry"});
        if (asset->width == 0 || asset->height == 0)
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Sprite asset has invalid dimensions"});

        const auto& value = transform->value;
        if (!std::isfinite(value.position.x) || !std::isfinite(value.position.y)
            || !std::isfinite(value.rotationDegrees.z) || !std::isfinite(value.scale.x)
            || !std::isfinite(value.scale.y) || value.scale.x == 0.0 || value.scale.y == 0.0)
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Sprite Transform has invalid XY position, Z rotation, or XY scale"});
        Sprite sprite;
        sprite.asset = asset->id;
        sprite.center = {value.position.x, value.position.y};
        sprite.size = {static_cast<double>(asset->width) / options.pixelsPerUnit * std::abs(value.scale.x),
            static_cast<double>(asset->height) / options.pixelsPerUnit * std::abs(value.scale.y)};
        sprite.rotationDegrees = value.rotationDegrees.z;
        sprite.layer = options.layer;
        sprite.flipX = value.scale.x < 0.0;
        sprite.flipY = value.scale.y < 0.0;
        pending.push_back(sprite);
    }
    if (pending.size() > Canvas2D::maximumSprites - canvas.sprites().size())
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "Scene sprite draw list exceeds the 2D canvas limit"});
    for (const auto& sprite : pending)
    {
        const auto appended = canvas.addSprite(sprite);
        if (!appended) return appended;
    }
    return core::Result<void>::success();
}
}
