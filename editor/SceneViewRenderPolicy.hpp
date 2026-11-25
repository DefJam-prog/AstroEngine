#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/scene/Scene.hpp"

#include <cstdint>
#include <vector>

namespace astro::editor
{
struct SceneViewSortItem final
{
    scene::EntityId entity = 0;
    assets::MaterialAlphaMode alphaMode = assets::MaterialAlphaMode::Opaque;
    float cameraSpaceDepth = 0.0f;
};

/// Stable opaque -> masked -> blended ordering. Blended entities are sorted
/// far-to-near by camera-space Z, then by stable EntityId for equal/non-finite Z.
void sortSceneViewItems(std::vector<SceneViewSortItem>& items);
}
