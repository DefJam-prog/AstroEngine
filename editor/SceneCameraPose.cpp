#include "SceneCameraPose.hpp"

#include "astro/render3d/SceneRenderPacket.hpp"

namespace astro::editor
{
core::Result<std::array<float, 16>> sceneEntityWorldMatrix(
    const scene::Scene& scene, const scene::EntityId entity)
{
    const auto world = render3d::sceneEntityWorldTransform(scene, entity);
    if (!world) return core::Result<std::array<float, 16>>::failure(world.error());
    return core::Result<std::array<float, 16>>::success(world.value().values);
}

core::Result<render3d::Camera3D> sceneCameraPose(
    const scene::Scene& scene, const scene::EntityId cameraEntity)
{
    return render3d::resolveSceneCameraPose(scene, cameraEntity);
}
}
