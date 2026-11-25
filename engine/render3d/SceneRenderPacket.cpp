#include "astro/render3d/SceneRenderPacket.hpp"

#include "astro/assets/MaterialAsset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace astro::render3d
{
namespace
{
core::Error invalid(const char* message)
{
    return {core::ErrorCode::ValidationError, message};
}

using Matrix = std::array<float, 16>;

Matrix multiply(const Matrix& lhs, const Matrix& rhs)
{
    Matrix result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t inner = 0; inner < 4; ++inner)
                result[row * 4 + column] += lhs[row * 4 + inner] * rhs[inner * 4 + column];
    return result;
}

core::Result<Matrix> localTransform(const scene::Transform& value)
{
    const std::array values{value.position.x, value.position.y, value.position.z,
        value.rotationDegrees.x, value.rotationDegrees.y, value.rotationDegrees.z,
        value.scale.x, value.scale.y, value.scale.z};
    constexpr double maxMagnitude = 10'000'000.0;
    if (!std::all_of(values.begin(), values.end(), [maxMagnitude](const double item) {
            return std::isfinite(item) && std::abs(item) <= maxMagnitude;
        }) || value.scale.x == 0.0 || value.scale.y == 0.0 || value.scale.z == 0.0)
        return core::Result<Matrix>::failure(invalid("Scene Transform is outside supported finite bounds"));

    constexpr double radiansPerDegree = 3.14159265358979323846 / 180.0;
    const float sx = static_cast<float>(value.scale.x), sy = static_cast<float>(value.scale.y),
        sz = static_cast<float>(value.scale.z);
    const float ax = static_cast<float>(value.rotationDegrees.x * radiansPerDegree);
    const float ay = static_cast<float>(value.rotationDegrees.y * radiansPerDegree);
    const float az = static_cast<float>(value.rotationDegrees.z * radiansPerDegree);
    const float sinX = std::sin(ax), cosX = std::cos(ax), sinY = std::sin(ay), cosY = std::cos(ay),
        sinZ = std::sin(az), cosZ = std::cos(az);
    Matrix matrix{
        sx * cosY * cosZ, sx * (cosZ * sinX * sinY - cosX * sinZ), sx * (cosX * cosZ * sinY + sinX * sinZ), 0.0f,
        sy * cosY * sinZ, sy * (cosX * cosZ + sinX * sinY * sinZ), sy * (-cosZ * sinX + cosX * sinY * sinZ), 0.0f,
        sz * -sinY, sz * cosY * sinX, sz * cosX * cosY, 0.0f,
        static_cast<float>(value.position.x), static_cast<float>(value.position.y), static_cast<float>(value.position.z), 1.0f};
    if (!std::all_of(matrix.begin(), matrix.end(), [](const float item) { return std::isfinite(item); }))
        return core::Result<Matrix>::failure(invalid("Scene local Transform is not finite"));
    return core::Result<Matrix>::success(matrix);
}

class TransformResolver final
{
public:
    explicit TransformResolver(const scene::Scene& document) : document_(document) {}

    core::Result<Matrix> resolve(const scene::EntityId entityId)
    {
        if (const auto cached = resolved_.find(entityId); cached != resolved_.end())
            return core::Result<Matrix>::success(cached->second);
        std::vector<const scene::Entity*> chain;
        std::unordered_set<scene::EntityId> visiting;
        const scene::Entity* current = document_.find(entityId);
        if (!current)
            return core::Result<Matrix>::failure({core::ErrorCode::NotFound, "Scene entity does not exist"});
        while (current)
        {
            if (const auto cached = resolved_.find(current->id); cached != resolved_.end()) break;
            if (!visiting.insert(current->id).second || chain.size() >= document_.entities().size())
                return core::Result<Matrix>::failure(invalid("Scene hierarchy contains a parent cycle"));
            chain.push_back(current);
            if (!current->parent) { current = nullptr; break; }
            current = document_.find(*current->parent);
            if (!current)
                return core::Result<Matrix>::failure(invalid("Scene hierarchy contains a missing parent"));
        }

        Matrix world{};
        world[0] = world[5] = world[10] = world[15] = 1.0f;
        if (current)
            world = resolved_.at(current->id);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto* transform = document_.transform((*it)->id);
            if (!transform)
                return core::Result<Matrix>::failure(invalid("Scene entity has no Transform"));
            const auto local = localTransform(transform->value);
            if (!local) return local;
            world = multiply(local.value(), world);
            if (!std::all_of(world.begin(), world.end(), [](const float item) {
                    return std::isfinite(item) && std::abs(item) <= 1.0e12f;
                }))
                return core::Result<Matrix>::failure(invalid("Composed world Transform exceeds render bounds"));
            resolved_[(*it)->id] = world;
        }
        return core::Result<Matrix>::success(world);
    }

private:
    const scene::Scene& document_;
    std::unordered_map<scene::EntityId, Matrix> resolved_;
};

Vec3 transformPoint(const Vec3 point, const Matrix& matrix)
{
    return {point.x * matrix[0] + point.y * matrix[4] + point.z * matrix[8] + matrix[12],
        point.x * matrix[1] + point.y * matrix[5] + point.z * matrix[9] + matrix[13],
        point.x * matrix[2] + point.y * matrix[6] + point.z * matrix[10] + matrix[14]};
}

core::Result<Camera3D> resolveCameraPoseInternal(const scene::Scene& document,
    const scene::EntityId cameraEntity, TransformResolver& transforms)
{
    if (!document.find(cameraEntity))
        return core::Result<Camera3D>::failure({core::ErrorCode::NotFound, "Camera entity does not exist"});
    const auto* component = document.camera3D(cameraEntity);
    if (!component || !component->active)
        return core::Result<Camera3D>::failure({core::ErrorCode::InvalidState,
            component ? "Camera3D is not the active scene camera" : "Entity does not contain Camera3D"});
    const auto world = transforms.resolve(cameraEntity);
    if (!world) return core::Result<Camera3D>::failure(world.error());
    const auto position = transformPoint({0.0f, 0.0f, 0.0f}, world.value());
    const auto forward = transformPoint({0.0f, 0.0f, -1.0f}, world.value());
    const auto up = transformPoint({0.0f, 1.0f, 0.0f}, world.value());
    constexpr double radiansPerDegree = 3.14159265358979323846 / 180.0;
    Camera3D camera;
    camera.position = position;
    camera.target = forward;
    camera.up = {up.x - position.x, up.y - position.y, up.z - position.z};
    camera.verticalFovRadians = static_cast<float>(component->verticalFovDegrees * radiansPerDegree);
    camera.nearPlane = static_cast<float>(component->nearPlane);
    camera.farPlane = static_cast<float>(component->farPlane);
    if (!camera.viewMatrix() || !camera.projectionMatrix(1.0f))
        return core::Result<Camera3D>::failure(invalid("Active Camera3D pose or lens is invalid"));
    return core::Result<Camera3D>::success(camera);
}

void addDiagnostic(SceneRenderPacket& packet, const scene::EntityId entity, const assets::AssetId asset,
    const core::Error& error)
{
    packet.diagnostics.push_back({entity, asset, error.code, error.message});
}

std::optional<assets::MaterialAssetData> resolveOverride(const assets::AssetRegistry& registry,
    const assets::AssetId id, const scene::EntityId entity, SceneRenderPacket& packet)
{
    if (id.value == 0) return std::nullopt;
    const auto resolved = registry.resolveMaterial(id);
    if (!resolved)
    {
        addDiagnostic(packet, entity, id, resolved.error());
        return std::nullopt;
    }
    return resolved.value().material;
}
}

core::Result<Mat4> sceneEntityWorldTransform(const scene::Scene& document, const scene::EntityId entity)
{
    TransformResolver transforms(document);
    const auto resolved = transforms.resolve(entity);
    if (!resolved) return core::Result<Mat4>::failure(resolved.error());
    return core::Result<Mat4>::success({resolved.value()});
}

core::Result<Camera3D> resolveSceneCameraPose(const scene::Scene& document, const scene::EntityId cameraEntity)
{
    TransformResolver transforms(document);
    return resolveCameraPoseInternal(document, cameraEntity, transforms);
}

core::Result<Vec3> resolveSceneDirectionalLightDirection(const scene::Scene& document,
    const scene::EntityId lightEntity)
{
    if (!document.directionalLight3D(lightEntity))
        return core::Result<Vec3>::failure({core::ErrorCode::NotFound,
            "Entity does not contain a DirectionalLight3D"});
    std::vector<const scene::Entity*> chain;
    std::unordered_set<scene::EntityId> visiting;
    const scene::Entity* current = document.find(lightEntity);
    while (current)
    {
        if (!visiting.insert(current->id).second || chain.size() >= document.entities().size())
            return core::Result<Vec3>::failure(invalid("Scene hierarchy contains a parent cycle"));
        chain.push_back(current);
        if (!current->parent) break;
        current = document.find(*current->parent);
        if (!current)
            return core::Result<Vec3>::failure(invalid("Scene hierarchy contains a missing parent"));
    }
    Matrix world{};
    world[0] = world[5] = world[10] = world[15] = 1.0f;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        const auto* transform = document.transform((*it)->id);
        if (!transform)
            return core::Result<Vec3>::failure(invalid("Scene entity has no Transform"));
        auto rotation = transform->value;
        rotation.position = {};
        rotation.scale = {1.0, 1.0, 1.0};
        const auto local = localTransform(rotation);
        if (!local) return core::Result<Vec3>::failure(local.error());
        world = multiply(local.value(), world);
    }
    const auto forward = transformPoint({0.0f, 0.0f, -1.0f}, world);
    const float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (!std::isfinite(length) || length <= 1.0e-6f)
        return core::Result<Vec3>::failure(invalid("DirectionalLight3D world direction is degenerate"));
    return core::Result<Vec3>::success({forward.x / length, forward.y / length, forward.z / length});
}

core::Result<Vec3> resolveScenePointLightPosition(const scene::Scene& document,
    const scene::EntityId lightEntity)
{
    if (!document.pointLight3D(lightEntity))
        return core::Result<Vec3>::failure({core::ErrorCode::NotFound,
            "Entity does not contain a PointLight3D"});
    const auto world = sceneEntityWorldTransform(document, lightEntity);
    if (!world) return core::Result<Vec3>::failure(world.error());
    const auto& matrix = world.value().values;
    const Vec3 position{matrix[12], matrix[13], matrix[14]};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return core::Result<Vec3>::failure(invalid("PointLight3D world position is non-finite"));
    return core::Result<Vec3>::success(position);
}

core::Result<SceneRenderPacket> buildSceneRenderPacket(const scene::Scene& document,
    const assets::AssetRegistry* assetRegistry, const SceneMeshResolver& resolveMesh)
{
    TransformResolver transforms(document);
    SceneRenderPacket packet;
    const auto activeCamera = document.activeCamera3D();
    if (!activeCamera)
        return core::Result<SceneRenderPacket>::failure({core::ErrorCode::InvalidState,
            "Scene has no active Camera3D"});
    packet.cameraEntity = *activeCamera;
    auto camera = resolveCameraPoseInternal(document, packet.cameraEntity, transforms);
    if (!camera) return core::Result<SceneRenderPacket>::failure(camera.error());
    packet.camera = camera.value();

    for (const auto& entity : document.entities())
    {
        const auto* light = document.directionalLight3D(entity.id);
        if (!light || !light->enabled) continue;
        auto direction = resolveSceneDirectionalLightDirection(document, entity.id);
        if (!direction)
        {
            addDiagnostic(packet, entity.id, {}, direction.error());
            continue;
        }
        packet.directionalLights.push_back({entity.id, light->id, direction.value(), light->color, light->intensity});
    }
    std::sort(packet.directionalLights.begin(), packet.directionalLights.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });

    for (const auto& entity : document.entities())
    {
        const auto* light = document.pointLight3D(entity.id);
        if (!light || !light->enabled) continue;
        const auto position = resolveScenePointLightPosition(document, entity.id);
        if (!position)
        {
            addDiagnostic(packet, entity.id, {}, position.error());
            continue;
        }
        packet.pointLights.push_back({entity.id, light->id, position.value(), light->color,
            light->intensity, light->range});
    }
    std::sort(packet.pointLights.begin(), packet.pointLights.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    if (packet.pointLights.size() > 4)
    {
        const auto omitted = packet.pointLights.size() - 4;
        packet.diagnostics.push_back({packet.pointLights[4].entity, {}, core::ErrorCode::ValidationError,
            "PointLight3D shader limit is 4; higher EntityId lights are omitted (" + std::to_string(omitted) + ")"});
        packet.pointLights.resize(4);
    }

    std::array<std::shared_ptr<const MeshData>, 5> primitiveGeometry{};
    std::map<assets::AssetId, assets::MaterialAssetData> materialCache;
    const auto materialFor = [&](const assets::AssetId id, const scene::EntityId entity,
                                 const std::optional<assets::MaterialAssetData>& source)
        -> std::optional<assets::MaterialAssetData> {
        if (id.value == 0)
        {
            if (source)
            {
                const auto valid = assets::validateMaterialAsset(*source);
                if (!valid)
                {
                    addDiagnostic(packet, entity, {}, valid.error());
                    return std::nullopt;
                }
            }
            return source;
        }
        if (const auto found = materialCache.find(id); found != materialCache.end()) return found->second;
        if (!assetRegistry)
        {
            addDiagnostic(packet, entity, id,
                {core::ErrorCode::InvalidState, "AssetRegistry is required to resolve a material override"});
            return std::nullopt;
        }
        auto material = resolveOverride(*assetRegistry, id, entity, packet);
        if (material) materialCache.emplace(id, *material);
        return material;
    };

    for (const auto& entity : document.entities())
    {
        const auto* meshComponent = document.meshRenderer(entity.id);
        const auto* primitiveComponent = document.primitiveRenderer(entity.id);
        if (meshComponent && meshComponent->visible)
        {
            if (!resolveMesh)
            {
                addDiagnostic(packet, entity.id, meshComponent->model,
                    {core::ErrorCode::InvalidState, "No MeshRenderer asset resolver was supplied"});
                continue;
            }
            auto source = resolveMesh(*meshComponent);
            if (!source)
            {
                addDiagnostic(packet, entity.id, meshComponent->model, source.error());
                continue;
            }
            if (!source.value().geometry)
            {
                addDiagnostic(packet, entity.id, meshComponent->model,
                    {core::ErrorCode::InvalidState, "MeshRenderer resolver returned no geometry"});
                continue;
            }
            auto world = transforms.resolve(entity.id);
            if (!world) { addDiagnostic(packet, entity.id, meshComponent->model, world.error()); continue; }
            const auto overrideMaterial = meshComponent->materialOverride.value != 0
                ? materialFor(meshComponent->materialOverride, entity.id, {}) : source.value().sourceMaterial;
            if ((meshComponent->materialOverride.value != 0 || source.value().sourceMaterial.has_value())
                && !overrideMaterial) continue;
            if (overrideMaterial)
            {
                const auto valid = assets::validateMaterialAsset(*overrideMaterial);
                if (!valid)
                {
                    addDiagnostic(packet, entity.id, meshComponent->materialOverride, valid.error());
                    continue;
                }
            }
            SceneRenderItem item{entity.id, meshComponent->id, SceneRenderItemKind::Mesh,
                {world.value()}, std::move(source.value().geometry), meshComponent->materialOverride,
                overrideMaterial};
            if (meshComponent->materialOverride.value == 0 && source.value().sourceBaseColorImage)
            {
                item.sourceBaseColorImage = std::move(source.value().sourceBaseColorImage);
                item.sourceBaseColorImageHash = std::move(source.value().sourceBaseColorImageHash);
                item.sourceBaseColorTexCoord = source.value().sourceBaseColorTexCoord;
                item.sourceBaseColorWrapU = source.value().sourceBaseColorWrapU.value_or(
                    assets::MaterialTextureWrapMode::Repeat);
                item.sourceBaseColorWrapV = source.value().sourceBaseColorWrapV.value_or(
                    assets::MaterialTextureWrapMode::Repeat);
                item.sourceBaseColorMinFilter = source.value().sourceBaseColorMinFilter.value_or(
                    assets::MaterialTextureMinFilter::Linear);
                item.sourceBaseColorMagFilter = source.value().sourceBaseColorMagFilter.value_or(
                    assets::MaterialTextureMagFilter::Linear);
                item.sourceBaseColorUvTransform = source.value().sourceBaseColorUvTransform.value_or(
                    assets::MaterialUvTransform{});
            }
            packet.items.push_back(std::move(item));
            continue;
        }
        if (!primitiveComponent || !primitiveComponent->visible) continue;

        const auto kind = static_cast<std::uint8_t>(primitiveComponent->primitive);
        if (kind < 1 || kind > 4)
        {
            addDiagnostic(packet, entity.id, primitiveComponent->materialOverride,
                invalid("PrimitiveRenderer kind is unsupported"));
            continue;
        }
        if (!primitiveGeometry[kind])
        {
            auto geometry = MeshData::makePrimitive(kind);
            if (!geometry)
            {
                addDiagnostic(packet, entity.id, primitiveComponent->materialOverride, geometry.error());
                continue;
            }
            primitiveGeometry[kind] = std::make_shared<const MeshData>(std::move(geometry).value());
        }
        auto world = transforms.resolve(entity.id);
        if (!world) { addDiagnostic(packet, entity.id, primitiveComponent->materialOverride, world.error()); continue; }
        auto material = materialFor(primitiveComponent->materialOverride, entity.id, {});
        if (primitiveComponent->materialOverride.value != 0 && !material) continue;
        packet.items.push_back({entity.id, primitiveComponent->id, SceneRenderItemKind::Primitive,
            {world.value()}, primitiveGeometry[kind], primitiveComponent->materialOverride, std::move(material)});
    }
    return core::Result<SceneRenderPacket>::success(std::move(packet));
}
}
