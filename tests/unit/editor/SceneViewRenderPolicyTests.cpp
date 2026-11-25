#include "SceneViewRenderPolicy.hpp"

#include <iostream>
#include <limits>
#include <vector>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using astro::assets::MaterialAlphaMode;
    using astro::editor::SceneViewSortItem;
    std::vector<SceneViewSortItem> items{
        {40, MaterialAlphaMode::Blend, -2.0f},
        {7, MaterialAlphaMode::Opaque, 0.0f},
        {9, MaterialAlphaMode::Blend, -8.0f},
        {3, MaterialAlphaMode::Mask, 0.0f},
        {11, MaterialAlphaMode::Blend, -8.0f},
        {5, MaterialAlphaMode::Blend, std::numeric_limits<float>::quiet_NaN()},
    };
    astro::editor::sortSceneViewItems(items);
    const std::vector<astro::scene::EntityId> expected{7, 3, 9, 11, 40, 5};
    std::vector<astro::scene::EntityId> actual;
    for (const auto& item : items) actual.push_back(item.entity);
    check(actual == expected, "opaque and mask precede back-to-front blend with stable finite/non-finite ties");

    std::vector<SceneViewSortItem> repeated{
        {2, MaterialAlphaMode::Blend, -3.0f}, {1, MaterialAlphaMode::Blend, -3.0f},
        {3, MaterialAlphaMode::Blend, -3.0f}};
    astro::editor::sortSceneViewItems(repeated);
    check(repeated[0].entity == 1 && repeated[1].entity == 2 && repeated[2].entity == 3,
        "same-depth transparent sort is deterministic by stable EntityId");

    if (failures == 0) std::cout << "Scene View alpha sort policy checks passed\n";
    return failures == 0 ? 0 : 1;
}
