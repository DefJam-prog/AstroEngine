#pragma once

#include "astro/render3d/Camera3D.hpp"
#include "astro/scene/Scene.hpp"

#include <array>

namespace astro::editor
{
[[nodiscard]] core::Result<std::array<float, 16>> sceneEntityWorldMatrix(
    const scene::Scene& scene, scene::EntityId entity);

[[nodiscard]] core::Result<render3d::Camera3D> sceneCameraPose(
    const scene::Scene& scene, scene::EntityId cameraEntity);
}
