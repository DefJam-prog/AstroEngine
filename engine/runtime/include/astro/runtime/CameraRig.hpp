#pragma once

#include "astro/core/Result.hpp"
#include "astro/scene/Scene.hpp"

#include <optional>

namespace astro::runtime
{
enum class CameraMode : std::uint8_t { Custom, FirstPerson, ThirdPerson, Isometric, TopDown, Platformer, Strategy };

struct CameraBounds
{
    scene::Vec3 minimum{};
    scene::Vec3 maximum{};
};

struct CameraSettings
{
    CameraMode mode = CameraMode::Custom;
    std::optional<scene::EntityId> target;
    scene::Vec3 followOffset{};
    scene::Vec3 rotationDegrees{};
    double followSharpness = 0.0; // 0 snaps; higher values converge faster.
    std::optional<CameraBounds> bounds;
    double zoom = 1.0;
    double minZoom = 0.1;
    double maxZoom = 10.0;
    bool inheritTargetRotation = false;
};

struct CameraPose
{
    scene::Vec3 position{};
    scene::Vec3 rotationDegrees{};
    double zoom = 1.0;
    friend bool operator==(const CameraPose&, const CameraPose&) = default;
};

// Runtime camera rig data and follow calculation. It does not render a view or
// mutate the target Scene.
class CameraRig final
{
public:
    core::Result<void> configure(CameraSettings settings);
    core::Result<void> setMode(CameraMode mode);
    core::Result<void> setPose(CameraPose pose);
    core::Result<void> setZoom(double zoom);
    core::Result<void> snapToTarget(const scene::Scene& scene);
    core::Result<void> update(const scene::Scene& scene, double frameDeltaSeconds);

    [[nodiscard]] const CameraSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] const CameraPose& pose() const noexcept { return pose_; }

private:
    core::Result<CameraPose> desiredPose(const scene::Scene& scene) const;
    CameraSettings settings_;
    CameraPose pose_;
};
}
