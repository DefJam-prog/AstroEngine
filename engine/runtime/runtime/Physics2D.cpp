#include "astro/runtime/Physics2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace astro::runtime
{
namespace
{
constexpr double maximumMagnitude = 1000000.0;
constexpr std::size_t maximumBodies = 2048;

core::Result<void> physicsError(const core::ErrorCode code, std::string message)
{
    return core::Result<void>::failure({code, std::move(message)});
}

bool validVector(const Vec2 value, const double limit = maximumMagnitude)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::abs(value.x) <= limit && std::abs(value.y) <= limit;
}

bool validBody(const Body2D& body)
{
    return (body.type == BodyType2D::Static || body.type == BodyType2D::Dynamic)
        && validVector(body.velocity)
        && std::isfinite(body.gravityScale) && std::abs(body.gravityScale) <= 100.0
        && (body.type != BodyType2D::Static || body.velocity == Vec2{});
}

bool validCollider(const BoxCollider2D& collider)
{
    return validVector(collider.halfExtents) && collider.halfExtents.x > 0.0
        && collider.halfExtents.y > 0.0;
}

double component(const Vec2 value, const bool x) { return x ? value.x : value.y; }
double component(const scene::Vec3 value, const bool x) { return x ? value.x : value.y; }
void setComponent(Vec2& value, const bool x, const double next)
{
    if (x) value.x = next;
    else value.y = next;
}
void setComponent(scene::Vec3& value, const bool x, const double next)
{
    if (x) value.x = next;
    else value.y = next;
}
}

core::Result<void> PhysicsWorld2D::setGravity(const Vec2 gravity)
{
    if (!validVector(gravity))
        return physicsError(core::ErrorCode::ValidationError, "2D gravity must be finite and within supported bounds");
    gravity_ = gravity;
    return core::Result<void>::success();
}

core::Result<void> PhysicsWorld2D::addBody(const scene::Scene& scene, const scene::EntityId entity,
    Body2D body, BoxCollider2D collider)
{
    if (!entity || !scene.find(entity) || !scene.transform(entity))
        return physicsError(core::ErrorCode::NotFound, "2D physics body requires an existing entity with Transform");
    if (sceneId_ && *sceneId_ != scene.id())
        return physicsError(core::ErrorCode::InvalidArgument, "A 2D physics world can bind to only one scene");
    if (!validBody(body) || !validCollider(collider))
        return physicsError(core::ErrorCode::ValidationError, "2D body or box collider values are invalid");
    const auto* transform = scene.transform(entity);
    if (std::abs(transform->value.scale.x) > maximumMagnitude
        || std::abs(transform->value.scale.y) > maximumMagnitude)
        return physicsError(core::ErrorCode::ValidationError, "2D entity scale exceeds supported bounds");
    const Vec2 effectiveExtents{collider.halfExtents.x * std::abs(transform->value.scale.x),
        collider.halfExtents.y * std::abs(transform->value.scale.y)};
    if (!validVector(effectiveExtents))
        return physicsError(core::ErrorCode::ValidationError, "Scaled 2D collider extents exceed supported bounds");
    if (bodies_.size() >= maximumBodies && !bodies_.contains(entity))
        return physicsError(core::ErrorCode::ValidationError, "2D physics body limit reached");
    if (!bodies_.emplace(entity, BodyEntry{body, collider}).second)
        return physicsError(core::ErrorCode::AlreadyExists, "Entity already has a 2D physics body");
    sceneId_ = scene.id();
    return core::Result<void>::success();
}

core::Result<void> PhysicsWorld2D::removeBody(const scene::EntityId entity)
{
    if (!bodies_.erase(entity))
        return physicsError(core::ErrorCode::NotFound, "2D physics body was not registered");
    return core::Result<void>::success();
}

core::Result<void> PhysicsWorld2D::setVelocity(const scene::EntityId entity, const Vec2 velocity)
{
    const auto found = bodies_.find(entity);
    if (found == bodies_.end())
        return physicsError(core::ErrorCode::NotFound, "2D physics body was not registered");
    if (found->second.body.type != BodyType2D::Dynamic)
        return physicsError(core::ErrorCode::InvalidState, "Velocity can only be set on a dynamic 2D body");
    if (!validVector(velocity))
        return physicsError(core::ErrorCode::ValidationError, "2D velocity must be finite and within supported bounds");
    found->second.body.velocity = velocity;
    return core::Result<void>::success();
}

core::Result<Vec2> PhysicsWorld2D::velocity(const scene::EntityId entity) const
{
    const auto found = bodies_.find(entity);
    if (found == bodies_.end())
        return core::Result<Vec2>::failure({core::ErrorCode::NotFound, "2D physics body was not registered"});
    return core::Result<Vec2>::success(found->second.body.velocity);
}

core::Result<void> PhysicsWorld2D::step(scene::Scene& runtimeScene, core::EventBus& events,
    const double fixedDeltaSeconds)
{
    if (!sceneId_ || *sceneId_ != runtimeScene.id())
        return physicsError(core::ErrorCode::InvalidArgument, "2D physics world is bound to a different or unset scene");
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0 || fixedDeltaSeconds > 0.25)
        return physicsError(core::ErrorCode::ValidationError, "2D physics step must be finite and in (0, 0.25] seconds");

    for (auto iterator = bodies_.begin(); iterator != bodies_.end();)
    {
        if (!runtimeScene.find(iterator->first)) iterator = bodies_.erase(iterator);
        else ++iterator;
    }

    std::map<scene::EntityId, scene::Transform> nextTransforms;
    std::map<scene::EntityId, Vec2> nextVelocities;
    for (const auto& [entity, entry] : bodies_)
    {
        const auto* transform = runtimeScene.transform(entity);
        if (!transform)
            return physicsError(core::ErrorCode::InvalidState, "2D physics body entity has no Transform");
        if (std::abs(transform->value.scale.x) > maximumMagnitude
            || std::abs(transform->value.scale.y) > maximumMagnitude
            || !validVector({entry.collider.halfExtents.x * std::abs(transform->value.scale.x),
                entry.collider.halfExtents.y * std::abs(transform->value.scale.y)}))
            return physicsError(core::ErrorCode::ValidationError, "Scaled 2D collider extents exceed supported bounds");
        auto next = transform->value;
        auto velocity = entry.body.velocity;
        if (entry.body.type == BodyType2D::Dynamic)
        {
            velocity.x += gravity_.x * entry.body.gravityScale * fixedDeltaSeconds;
            velocity.y += gravity_.y * entry.body.gravityScale * fixedDeltaSeconds;
            next.position.x += velocity.x * fixedDeltaSeconds;
            next.position.y += velocity.y * fixedDeltaSeconds;
        }
        if (!validVector({next.position.x, next.position.y}) || !validVector(velocity))
            return physicsError(core::ErrorCode::ValidationError, "2D physics integration exceeded supported bounds");
        nextTransforms.emplace(entity, next);
        nextVelocities.emplace(entity, velocity);
    }

    std::map<Pair, Contact> nextContacts;
    for (auto first = bodies_.begin(); first != bodies_.end(); ++first)
    {
        auto second = first;
        for (++second; second != bodies_.end(); ++second)
        {
            const auto& bodyA = first->second;
            const auto& bodyB = second->second;
            if (!bodyA.collider.enabled || !bodyB.collider.enabled) continue;
            const auto& transformA = nextTransforms.at(first->first);
            const auto& transformB = nextTransforms.at(second->first);
            const Vec2 halfA{bodyA.collider.halfExtents.x * std::abs(transformA.scale.x),
                bodyA.collider.halfExtents.y * std::abs(transformA.scale.y)};
            const Vec2 halfB{bodyB.collider.halfExtents.x * std::abs(transformB.scale.x),
                bodyB.collider.halfExtents.y * std::abs(transformB.scale.y)};
            const auto dx = transformB.position.x - transformA.position.x;
            const auto dy = transformB.position.y - transformA.position.y;
            const auto overlapX = halfA.x + halfB.x - std::abs(dx);
            const auto overlapY = halfA.y + halfB.y - std::abs(dy);
            if (overlapX <= 0.0 || overlapY <= 0.0) continue;

            Vec2 normal{};
            const bool resolveX = overlapX <= overlapY;
            if (resolveX) normal.x = dx < 0.0 ? -1.0 : 1.0;
            else normal.y = dy < 0.0 ? -1.0 : 1.0;
            const bool isTrigger = bodyA.collider.trigger || bodyB.collider.trigger;
            nextContacts.emplace(Pair{first->first, second->first}, Contact{normal, isTrigger});
            if (isTrigger) continue;

            const bool dynamicA = bodyA.body.type == BodyType2D::Dynamic;
            const bool dynamicB = bodyB.body.type == BodyType2D::Dynamic;
            if (!dynamicA && !dynamicB) continue;
            const auto penetration = resolveX ? overlapX : overlapY;
            if (dynamicA && dynamicB)
            {
                setComponent(nextTransforms.at(first->first).position, resolveX,
                    component(nextTransforms.at(first->first).position, resolveX) - component(normal, resolveX) * penetration * 0.5);
                setComponent(nextTransforms.at(second->first).position, resolveX,
                    component(nextTransforms.at(second->first).position, resolveX) + component(normal, resolveX) * penetration * 0.5);
                setComponent(nextVelocities.at(first->first), resolveX, 0.0);
                setComponent(nextVelocities.at(second->first), resolveX, 0.0);
            }
            else
            {
                const auto dynamicId = dynamicA ? first->first : second->first;
                const auto direction = dynamicA ? -1.0 : 1.0;
                setComponent(nextTransforms.at(dynamicId).position, resolveX,
                    component(nextTransforms.at(dynamicId).position, resolveX)
                        + direction * component(normal, resolveX) * penetration);
                const auto towardOther = component(nextVelocities.at(dynamicId), resolveX)
                    * (dynamicA ? component(normal, resolveX) : -component(normal, resolveX));
                if (towardOther > 0.0) setComponent(nextVelocities.at(dynamicId), resolveX, 0.0);
            }
        }
    }

    for (const auto& [entity, transform] : nextTransforms)
    {
        const auto result = runtimeScene.setTransform(entity, transform);
        if (!result) return result;
        bodies_.at(entity).body.velocity = nextVelocities.at(entity);
    }

    std::vector<CollisionEvent> pendingEvents;
    pendingEvents.reserve(nextContacts.size() + contacts_.size());
    const auto appendEvent = [&](const Pair pair, const Contact contact, const CollisionEventKind kind) {
        pendingEvents.push_back({0, kind, pair.first, pair.second, contact.normal, contact.trigger});
    };
    for (const auto& [pair, contact] : nextContacts)
        appendEvent(pair, contact, contacts_.contains(pair) ? CollisionEventKind::Stay : CollisionEventKind::Enter);
    for (const auto& [pair, contact] : contacts_)
        if (!nextContacts.contains(pair)) appendEvent(pair, contact, CollisionEventKind::Exit);
    std::sort(pendingEvents.begin(), pendingEvents.end(), [](const auto& left, const auto& right) {
        if (left.first != right.first) return left.first < right.first;
        if (left.second != right.second) return left.second < right.second;
        return left.kind < right.kind;
    });

    // Commit contact state before dispatch. Observer failures do not roll back physics.
    contacts_ = std::move(nextContacts);
    if (bodies_.empty() && contacts_.empty()) sceneId_.reset();
    std::string dispatchErrors;
    for (auto& event : pendingEvents)
    {
        if (nextEventSequence_ == 0)
        {
            if (!dispatchErrors.empty()) dispatchErrors += '\n';
            dispatchErrors += "2D collision event sequence exhausted";
            break;
        }
        event.sequence = nextEventSequence_++;
        const auto published = events.publish(event);
        if (!published)
        {
            if (!dispatchErrors.empty()) dispatchErrors += '\n';
            dispatchErrors += published.error().message;
        }
    }
    if (!dispatchErrors.empty())
        return physicsError(core::ErrorCode::InternalError, std::move(dispatchErrors));
    return core::Result<void>::success();
}
}
