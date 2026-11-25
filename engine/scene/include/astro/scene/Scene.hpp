#pragma once

#include "astro/core/CommandRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/assets/AssetRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace astro::scene
{
using SceneId = std::uint64_t;
using EntityId = std::uint64_t;
using ComponentId = std::uint64_t;
using PrefabId = std::uint64_t;
using PropertyValue = std::variant<std::string, EntityId>;

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    friend bool operator==(const Vec3&, const Vec3&) = default;
};

struct Transform
{
    Vec3 position{};
    Vec3 rotationDegrees{};
    Vec3 scale{1.0, 1.0, 1.0};
    friend bool operator==(const Transform&, const Transform&) = default;
};

struct TransformComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 1;
    Transform value{};
};

struct SpriteRendererComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 1;
    assets::AssetId texture{};
    bool visible = true;
};

// Persistent reference to one imported primitive. Rendering is provided by a
// separate optional module; this component only stores stable authoring data.
struct MeshRendererComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 1;
    assets::AssetId model{};
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
    bool visible = true;
    // Optional project material override for this selected primitive. A zero
    // AssetId means the imported primitive material remains authoritative.
    assets::AssetId materialOverride{};
};

enum class Primitive3D : std::uint8_t { Cube = 1, Plane = 2, Sphere = 3, Cylinder = 4 };

// Built-in geometry is generated in memory; no project asset is created or modified.
struct PrimitiveRendererComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 2;
    Primitive3D primitive = Primitive3D::Cube;
    bool visible = true;
    assets::AssetId materialOverride{};
};

// Persisted perspective-camera lens settings. The owning entity Transform is
// the camera pose; runtime/render consumers are introduced separately.
struct Camera3DComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 2;
    double verticalFovDegrees = 60.0;
    double nearPlane = 0.1;
    double farPlane = 1000.0;
    bool active = false;
};

// Backend-neutral directional light authoring data. Color is linear RGB in
// [0,1]; intensity is a unitless multiplier in [0,64].
struct DirectionalLight3DComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 1;
    bool enabled = true;
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

// Bounded point light: color is linear RGB, range is in scene units, and
// intensity is a unitless multiplier interpreted by the renderer attenuation.
struct PointLight3DComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 1;
    bool enabled = true;
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
};

// Persistent authoring reference to a managed script type. Runtime execution is
// provided by an optional scripting module, not by Scene serialization itself.
struct ScriptBehaviorComponent
{
    ComponentId id = 0;
    std::uint32_t schemaVersion = 1;
    assets::AssetId scriptAsset{};
    std::string managedTypeName;
    bool enabled = true;
};

struct PrefabSpriteValue
{
    assets::AssetId texture{};
    bool visible = true;
    friend bool operator==(const PrefabSpriteValue&, const PrefabSpriteValue&) = default;
};

enum class PrefabOverrideProperty : std::uint8_t { Name = 1, Transform = 2, SpriteRenderer = 3 };
using PrefabOverrideValue = std::variant<std::string, Transform, std::optional<PrefabSpriteValue>>;

struct PrefabOverride
{
    std::string sourceKey;
    PrefabOverrideProperty property = PrefabOverrideProperty::Name;
    PrefabOverrideValue value;
};

struct PrefabInstanceInfo
{
    EntityId rootEntity = 0;
    PrefabId prefab = 0;
    std::map<std::string, EntityId> sourceToEntity;
    std::vector<PrefabOverride> overrides;
};

enum class PropertyType
{
    Utf8String,
    UnsignedInteger
};

struct PropertyDefinition
{
    std::string_view id;
    std::string_view label;
    PropertyType type;
    bool editable;
    bool required;
    std::optional<PropertyValue> defaultValue;
    std::size_t maxLengthBytes = 0;
    EntityId minimum = 0;
    EntityId maximum = 0;
};

[[nodiscard]] std::span<const PropertyDefinition> entityPropertySchema() noexcept;

struct Entity
{
    EntityId id = 0;
    std::string name;
    std::optional<EntityId> parent;
    std::vector<TransformComponent> components;
    std::optional<SpriteRendererComponent> spriteRenderer;
    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<PrimitiveRendererComponent> primitiveRenderer;
    std::optional<Camera3DComponent> camera3d;
    std::optional<DirectionalLight3DComponent> directionalLight3d;
    std::optional<PointLight3DComponent> pointLight3d;
    std::vector<ScriptBehaviorComponent> scriptBehaviors;
};

// Scene and component IDs are stable across save/load. Entity IDs are unique in
// the scene; component IDs are unique within the scene.
class Scene final
{
public:
    Scene(SceneId id, std::string name);

    [[nodiscard]] SceneId id() const noexcept { return id_; }
    [[nodiscard]] EntityId nextEntityIdHint() const noexcept { return nextEntityId_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<Entity>& entities() const noexcept { return entities_; }
    [[nodiscard]] const std::vector<PrefabInstanceInfo>& prefabInstances() const noexcept { return prefabInstances_; }
    [[nodiscard]] Entity* find(EntityId id) noexcept;
    [[nodiscard]] const Entity* find(EntityId id) const noexcept;
    [[nodiscard]] TransformComponent* findTransform(ComponentId id) noexcept;
    [[nodiscard]] const TransformComponent* findTransform(ComponentId id) const noexcept;
    [[nodiscard]] const TransformComponent* transform(EntityId entity) const noexcept;
    [[nodiscard]] const SpriteRendererComponent* spriteRenderer(EntityId entity) const noexcept;
    [[nodiscard]] const MeshRendererComponent* meshRenderer(EntityId entity) const noexcept;
    [[nodiscard]] const PrimitiveRendererComponent* primitiveRenderer(EntityId entity) const noexcept;
    [[nodiscard]] const Camera3DComponent* camera3D(EntityId entity) const noexcept;
    [[nodiscard]] const DirectionalLight3DComponent* directionalLight3D(EntityId entity) const noexcept;
    [[nodiscard]] const PointLight3DComponent* pointLight3D(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<EntityId> activeCamera3D() const noexcept;
    [[nodiscard]] std::span<const ScriptBehaviorComponent> scriptBehaviors(EntityId entity) const noexcept;
    [[nodiscard]] ScriptBehaviorComponent* findScriptBehavior(ComponentId id) noexcept;
    [[nodiscard]] const ScriptBehaviorComponent* findScriptBehavior(ComponentId id) const noexcept;
    core::Result<void> setSpriteRenderer(EntityId entity, assets::AssetId texture, bool visible = true);
    core::Result<void> removeSpriteRenderer(EntityId entity);
    core::Result<void> setMeshRenderer(EntityId entity, assets::AssetId model,
        std::uint32_t meshIndex = 0, std::uint32_t primitiveIndex = 0, bool visible = true);
    core::Result<void> setMeshMaterialOverride(EntityId entity, assets::AssetId material);
    core::Result<void> restoreMeshRenderer(EntityId entity, const MeshRendererComponent& component);
    core::Result<void> removeMeshRenderer(EntityId entity);
    core::Result<void> setPrimitiveRenderer(EntityId entity, Primitive3D primitive, bool visible = true);
    core::Result<void> setPrimitiveMaterialOverride(EntityId entity, assets::AssetId material);
    core::Result<void> restorePrimitiveRenderer(EntityId entity, const PrimitiveRendererComponent& component);
    core::Result<void> removePrimitiveRenderer(EntityId entity);
    core::Result<void> setCamera3D(EntityId entity, double verticalFovDegrees = 60.0,
        double nearPlane = 0.1, double farPlane = 1000.0);
    core::Result<void> setCamera3DActive(EntityId entity, bool active);
    core::Result<void> restoreCamera3D(EntityId entity, const Camera3DComponent& component);
    core::Result<void> removeCamera3D(EntityId entity);
    core::Result<void> setDirectionalLight3D(EntityId entity, std::array<float, 3> color = {1, 1, 1},
        float intensity = 1.0f, bool enabled = true);
    core::Result<void> restoreDirectionalLight3D(EntityId entity, const DirectionalLight3DComponent& component);
    core::Result<void> removeDirectionalLight3D(EntityId entity);
    core::Result<void> setPointLight3D(EntityId entity, std::array<float, 3> color = {1, 1, 1},
        float intensity = 1.0f, float range = 10.0f, bool enabled = true);
    core::Result<void> restorePointLight3D(EntityId entity, const PointLight3DComponent& component);
    core::Result<void> removePointLight3D(EntityId entity);
    core::Result<ComponentId> addScriptBehavior(EntityId entity, assets::AssetId scriptAsset,
        std::string managedTypeName, bool enabled = true);
    core::Result<void> restoreScriptBehavior(EntityId entity, const ScriptBehaviorComponent& component,
        std::size_t insertionIndex);
    core::Result<void> updateScriptBehavior(EntityId entity, ComponentId component,
        assets::AssetId scriptAsset, std::string managedTypeName, bool enabled);
    core::Result<void> removeScriptBehavior(EntityId entity, ComponentId component);
    core::Result<void> setPrefabInstance(PrefabInstanceInfo instance);
    core::Result<void> removePrefabInstance(EntityId rootEntity);
    [[nodiscard]] PrefabInstanceInfo* prefabInstance(EntityId rootEntity) noexcept;
    [[nodiscard]] const PrefabInstanceInfo* prefabInstance(EntityId rootEntity) const noexcept;
    core::Result<void> setTransform(EntityId entity, const Transform& value);
    core::Result<PropertyValue> getProperty(EntityId entity, std::string_view property) const;
    core::Result<void> validateProperty(EntityId entity, std::string_view property, const PropertyValue& value) const;
    core::Result<void> setProperty(EntityId entity, std::string_view property, const PropertyValue& value);

    core::Result<EntityId> createEntity(std::string name = "Entity", std::optional<EntityId> parent = std::nullopt);
    core::Result<void> restoreEntity(Entity entity, std::size_t insertionIndex);
    core::Result<void> restoreSnapshot(std::vector<Entity> entities,
        std::vector<PrefabInstanceInfo> prefabInstances);
    core::Result<void> reparent(EntityId id, std::optional<EntityId> parent);
    core::Result<void> erase(EntityId id, bool recursive = false);

private:
    friend core::Result<Scene> restoreScene(SceneId id, std::string name, std::vector<Entity> entities,
        std::vector<PrefabInstanceInfo> prefabInstances);
    SceneId id_;
    std::string name_;
    EntityId nextEntityId_ = 1;
    ComponentId nextComponentId_ = 1;
    std::vector<Entity> entities_;
    std::vector<PrefabInstanceInfo> prefabInstances_;
};

core::Result<Scene> restoreScene(SceneId id, std::string name, std::vector<Entity> entities,
    std::vector<PrefabInstanceInfo> prefabInstances = {});

struct SetScenePropertyPayload
{
    EntityId entity = 0;
    std::string property;
    PropertyValue value;
};

struct SetTransformPayload
{
    EntityId entity = 0;
    // Local-to-parent Transform; world values are derived by hierarchy consumers.
    Transform value{};
};

struct EntityTransformAssignment
{
    EntityId entity = 0;
    // Local-to-parent Transform for this entity.
    Transform value{};
};

struct SetEntityTransformsPayload
{
    std::vector<EntityTransformAssignment> assignments;
};

struct SetEntityParentPayload
{
    EntityId entity = 0;
    std::optional<EntityId> parent;
};

struct SetEntityParentsPayload
{
    std::vector<EntityId> entities;
    std::optional<EntityId> parent;
};

struct SetSpriteRendererPayload
{
    EntityId entity = 0;
    assets::AssetId texture{};
    bool visible = true;
};

struct SetMeshRendererPayload
{
    EntityId entity = 0;
    assets::AssetId model{};
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
    bool visible = true;
};

struct SetMeshMaterialOverridePayload
{
    EntityId entity = 0;
    // Zero clears the override and uses the imported source material, if any.
    assets::AssetId material{};
};

struct SetPrimitiveRendererPayload
{
    EntityId entity = 0;
    Primitive3D primitive = Primitive3D::Cube;
    bool visible = true;
};

struct SetPrimitiveMaterialOverridePayload
{
    EntityId entity = 0;
    assets::AssetId material{}; // Zero clears the project material override.
};

struct CreatePrimitiveEntityPayload
{
    std::string name;
    std::optional<EntityId> parent;
    Primitive3D primitive = Primitive3D::Cube;
    bool visible = true;
};
struct DeleteEntitySubtreePayload { EntityId entity = 0; };

struct AddCamera3DPayload { EntityId entity = 0; };
struct RemoveCamera3DPayload { EntityId entity = 0; };
struct SetCamera3DLensPayload
{
    EntityId entity = 0;
    double verticalFovDegrees = 60.0;
    double nearPlane = 0.1;
    double farPlane = 1000.0;
};
struct SetActiveCamera3DPayload { EntityId entity = 0; };
struct AddDirectionalLight3DPayload { EntityId entity = 0; };
struct RemoveDirectionalLight3DPayload { EntityId entity = 0; };
struct SetDirectionalLight3DPayload
{
    EntityId entity = 0;
    std::array<float, 3> color{1, 1, 1};
    float intensity = 1.0f;
    bool enabled = true;
};
struct AddPointLight3DPayload { EntityId entity = 0; };
struct RemovePointLight3DPayload { EntityId entity = 0; };
struct SetPointLight3DPayload
{
    EntityId entity = 0;
    std::array<float, 3> color{1, 1, 1};
    float intensity = 1.0f;
    float range = 10.0f;
    bool enabled = true;
};

struct AddScriptBehaviorPayload
{
    EntityId entity = 0;
    assets::AssetId scriptAsset{};
    std::string managedTypeName;
};

struct SetScriptBehaviorEnabledPayload
{
    EntityId entity = 0;
    ComponentId component = 0;
    bool enabled = false;
};

struct RemoveScriptBehaviorPayload
{
    EntityId entity = 0;
    ComponentId component = 0;
};

core::Result<void> registerSceneCommands(core::CommandRegistry& commands, Scene& scene);
}
