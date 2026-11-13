#pragma once

#include "astro/core/EventBus.hpp"
#include "astro/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace astro::runtime
{
struct Vec2 final
{
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

enum class BodyType2D : std::uint8_t { Static, Dynamic };

struct Body2D final
{
    BodyType2D type = BodyType2D::Static;
    Vec2 velocity{};
    double gravityScale = 1.0;
};

struct BoxCollider2D final
{
    Vec2 halfExtents{0.5, 0.5};
    bool trigger = false;
    bool enabled = true;
};

enum class CollisionEventKind : std::uint8_t { Enter, Stay, Exit };

struct CollisionEvent final
{
    std::uint64_t sequence = 0;
    CollisionEventKind kind = CollisionEventKind::Enter;
    scene::EntityId first = 0;
    scene::EntityId second = 0;
    Vec2 normal{}; // Points from first entity toward second entity.
    bool trigger = false;
};

// Runtime-only discrete 2D physics. Bindings are scoped to one SceneId.
class PhysicsWorld2D final
{
public:
    core::Result<void> setGravity(Vec2 gravity);
    core::Result<void> addBody(const scene::Scene& scene, scene::EntityId entity,
        Body2D body = {}, BoxCollider2D collider = {});
    core::Result<void> removeBody(scene::EntityId entity);
    core::Result<void> setVelocity(scene::EntityId entity, Vec2 velocity);
    [[nodiscard]] core::Result<Vec2> velocity(scene::EntityId entity) const;
    core::Result<void> step(scene::Scene& runtimeScene, core::EventBus& events,
        double fixedDeltaSeconds);

    [[nodiscard]] std::size_t bodyCount() const noexcept { return bodies_.size(); }
    [[nodiscard]] Vec2 gravity() const noexcept { return gravity_; }

private:
    struct BodyEntry final
    {
        Body2D body;
        BoxCollider2D collider;
    };
    struct Pair final
    {
        scene::EntityId first = 0;
        scene::EntityId second = 0;
        friend bool operator<(const Pair& left, const Pair& right) noexcept
        {
            return left.first < right.first || (left.first == right.first && left.second < right.second);
        }
    };
    struct Contact final
    {
        Vec2 normal{};
        bool trigger = false;
    };

    std::optional<scene::SceneId> sceneId_;
    std::map<scene::EntityId, BodyEntry> bodies_;
    std::map<Pair, Contact> contacts_;
    Vec2 gravity_{0.0, -9.81};
    std::uint64_t nextEventSequence_ = 1;
};
}
