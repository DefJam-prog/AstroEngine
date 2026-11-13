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

// Adds visible SpriteRenderer components from root entities to a canvas. Asset
// dimensions come from the project's AssetRegistry; no file decoding occurs here.
[[nodiscard]] core::Result<void> appendSceneSprites(const scene::Scene& scene,
    const assets::AssetRegistry& assets, Canvas2D& canvas,
    SceneSpriteOptions options = {});
}
