#pragma once

#include "astro/render2d/Canvas2D.hpp"
#include "astro/scene/Scene.hpp"

namespace astro::render2d
{
struct SceneSpriteOptions final
{
    double pixelsPerUnit = 100.0;
    std::int32_t layer = 0;
};

// Adds visible SpriteRenderer components to a canvas after composing each
// entity's XY affine transform through its parent chain. Asset dimensions come
// from AssetRegistry; no image decoding occurs here.
[[nodiscard]] core::Result<void> appendSceneSprites(const scene::Scene& scene,
    const assets::AssetRegistry& assets, Canvas2D& canvas,
    SceneSpriteOptions options = {});
}
