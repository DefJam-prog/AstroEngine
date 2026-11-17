#include "astro/render2d/SceneSprites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace astro::render2d
{
namespace
{
struct Affine2D final
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0;
    double x = 0.0, y = 0.0;
};

Affine2D multiply(const Affine2D& parent, const Affine2D& local)
{
    return {
        parent.a * local.a + parent.b * local.c,
        parent.a * local.b + parent.b * local.d,
        parent.c * local.a + parent.d * local.c,
        parent.c * local.b + parent.d * local.d,
        parent.x + parent.a * local.x + parent.b * local.y,
        parent.y + parent.c * local.x + parent.d * local.y};
}

Vec2 apply(const Affine2D& matrix, const Vec2 point)
{
    return {matrix.x + matrix.a * point.x + matrix.b * point.y,
        matrix.y + matrix.c * point.x + matrix.d * point.y};
}

struct CachedWorldMatrix final
{
    Affine2D value{};
    std::uint8_t state = 0; // 1 = being composed, 2 = complete.
};

using EntityIndex = std::unordered_map<scene::EntityId, std::size_t>;

core::Result<Affine2D> worldMatrix(const std::size_t entityIndex,
    const std::vector<scene::Entity>& entities, const EntityIndex& entityIndices,
    std::vector<CachedWorldMatrix>& cache, std::vector<std::size_t>& chain)
{
    chain.clear();
    auto current = entityIndex;
    Affine2D world;
    for (;;)
    {
        auto& existing = cache[current];
        if (existing.state != 0)
        {
            if (existing.state == 1)
                return core::Result<Affine2D>::failure({core::ErrorCode::ValidationError,
                    "Scene hierarchy contains a parent cycle"});
            world = existing.value;
            break;
        }
        existing.state = 1;
        chain.push_back(current);
        const auto& currentEntity = entities[current];
        if (!currentEntity.parent) break;
        const auto parent = entityIndices.find(*currentEntity.parent);
        if (parent == entityIndices.end())
            return core::Result<Affine2D>::failure({core::ErrorCode::NotFound,
                "Scene sprite parent entity is missing"});
        current = parent->second;
    }

    for (auto item = chain.rbegin(); item != chain.rend(); ++item)
    {
        const auto& entity = entities[*item];
        const auto* transform = entity.components.empty() ? nullptr : &entity.components.front();
        if (!transform)
            return core::Result<Affine2D>::failure({core::ErrorCode::NotFound,
                "Scene hierarchy entity is missing its Transform"});
        const auto& value = transform->value;
        if (!std::isfinite(value.position.x) || !std::isfinite(value.position.y)
            || !std::isfinite(value.rotationDegrees.z) || !std::isfinite(value.scale.x)
            || !std::isfinite(value.scale.y) || value.scale.x == 0.0 || value.scale.y == 0.0)
            return core::Result<Affine2D>::failure({core::ErrorCode::ValidationError,
                "Scene hierarchy has an invalid XY position, Z rotation, or XY scale"});
        Affine2D local;
        local.x = value.position.x;
        local.y = value.position.y;
        if (value.rotationDegrees.z == 0.0)
        {
            local.a = value.scale.x;
            local.d = value.scale.y;
        }
        else
        {
            const auto radians = value.rotationDegrees.z * 3.14159265358979323846 / 180.0;
            const auto cosine = std::cos(radians), sine = std::sin(radians);
            local = {cosine * value.scale.x, -sine * value.scale.y,
                sine * value.scale.x, cosine * value.scale.y,
                value.position.x, value.position.y};
        }
        world = multiply(world, local);
        if (!std::isfinite(world.a) || !std::isfinite(world.b) || !std::isfinite(world.c)
            || !std::isfinite(world.d) || !std::isfinite(world.x) || !std::isfinite(world.y))
            return core::Result<Affine2D>::failure({core::ErrorCode::ValidationError,
                "Composed scene sprite transform exceeds finite values"});
        auto& entry = cache[*item];
        entry.value = world;
        entry.state = 2;
    }
    return core::Result<Affine2D>::success(world);
}
}

core::Result<void> appendSceneSprites(const scene::Scene& scene,
    const assets::AssetRegistry& assets, Canvas2D& canvas, const SceneSpriteOptions options)
{
    if (!std::isfinite(options.pixelsPerUnit) || options.pixelsPerUnit <= 0.0
        || options.pixelsPerUnit > 10000.0)
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "Scene sprite pixels-per-unit must be finite and within (0, 10000]"});

    const auto& sceneEntities = scene.entities();
    EntityIndex entityIndices;
    entityIndices.reserve(sceneEntities.size());
    for (std::size_t index = 0; index < sceneEntities.size(); ++index)
        entityIndices.emplace(sceneEntities[index].id, index);
    std::vector<CachedWorldMatrix> worldMatrices(sceneEntities.size());
    std::vector<std::size_t> chain;
    chain.reserve(16);
    std::vector<Sprite> pending;
    pending.reserve(std::min(sceneEntities.size(), Canvas2D::maximumSprites));
    for (std::size_t entityIndex = 0; entityIndex < sceneEntities.size(); ++entityIndex)
    {
        const auto& entity = sceneEntities[entityIndex];
        if (!entity.spriteRenderer || !entity.spriteRenderer->visible) continue;
        const auto* asset = assets.find(entity.spriteRenderer->texture);
        if (!asset)
            return core::Result<void>::failure({core::ErrorCode::NotFound,
                "Visible SpriteRenderer references an asset missing from the registry"});
        if (asset->width == 0 || asset->height == 0)
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Sprite asset has invalid dimensions"});

        const auto matrix = worldMatrix(entityIndex, sceneEntities, entityIndices, worldMatrices, chain);
        if (!matrix) return core::Result<void>::failure(matrix.error());
        const auto halfWidth = static_cast<double>(asset->width) / options.pixelsPerUnit * 0.5;
        const auto halfHeight = static_cast<double>(asset->height) / options.pixelsPerUnit * 0.5;
        Sprite sprite;
        sprite.asset = asset->id;
        sprite.center = {matrix.value().x, matrix.value().y};
        const double xAxisScale = matrix.value().c == 0.0 ? std::abs(matrix.value().a)
            : std::hypot(matrix.value().a, matrix.value().c);
        const double yAxisScale = matrix.value().b == 0.0 ? std::abs(matrix.value().d)
            : std::hypot(matrix.value().b, matrix.value().d);
        sprite.size = {static_cast<double>(asset->width) / options.pixelsPerUnit * xAxisScale,
            static_cast<double>(asset->height) / options.pixelsPerUnit * yAxisScale};
        sprite.rotationDegrees = matrix.value().c == 0.0 && matrix.value().a > 0.0 ? 0.0
            : std::atan2(matrix.value().c, matrix.value().a) * 180.0 / 3.14159265358979323846;
        sprite.layer = options.layer;
        sprite.flipX = matrix.value().a * matrix.value().d - matrix.value().b * matrix.value().c < 0.0;
        sprite.worldCorners = {
            apply(matrix.value(), {-halfWidth, halfHeight}),
            apply(matrix.value(), {halfWidth, halfHeight}),
            apply(matrix.value(), {halfWidth, -halfHeight}),
            apply(matrix.value(), {-halfWidth, -halfHeight})};
        sprite.hasWorldCorners = true;
        pending.push_back(sprite);
    }
    if (pending.size() > Canvas2D::maximumSprites - canvas.sprites().size())
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "Scene sprite draw list exceeds the 2D canvas limit"});
    for (auto& sprite : pending)
    {
        const auto appended = canvas.addSprite(std::move(sprite));
        if (!appended) return appended;
    }
    return core::Result<void>::success();
}
}
