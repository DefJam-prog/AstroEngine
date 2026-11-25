#include "SceneViewRenderPolicy.hpp"

#include <algorithm>
#include <cmath>

namespace astro::editor
{
namespace
{
int queue(const assets::MaterialAlphaMode mode) noexcept
{
    switch (mode)
    {
    case assets::MaterialAlphaMode::Opaque: return 0;
    case assets::MaterialAlphaMode::Mask: return 1;
    case assets::MaterialAlphaMode::Blend: return 2;
    }
    return 0;
}
}

void sortSceneViewItems(std::vector<SceneViewSortItem>& items)
{
    std::stable_sort(items.begin(), items.end(), [](const auto& lhs, const auto& rhs) {
        const auto lhsQueue = queue(lhs.alphaMode), rhsQueue = queue(rhs.alphaMode);
        if (lhsQueue != rhsQueue) return lhsQueue < rhsQueue;
        if (lhs.alphaMode != assets::MaterialAlphaMode::Blend) return lhs.entity < rhs.entity;
        const float lhsDepth = std::isfinite(lhs.cameraSpaceDepth) ? lhs.cameraSpaceDepth : 0.0f;
        const float rhsDepth = std::isfinite(rhs.cameraSpaceDepth) ? rhs.cameraSpaceDepth : 0.0f;
        if (lhsDepth != rhsDepth) return lhsDepth < rhsDepth;
        return lhs.entity < rhs.entity;
    });
}
}
