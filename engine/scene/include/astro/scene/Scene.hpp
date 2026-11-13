#pragma once

#include "astro/core/CommandRegistry.hpp"
#include "astro/core/Result.hpp"

#include <cstddef>
#include <cstdint>
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
using PropertyValue = std::variant<std::string, EntityId>;

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
};

// In-memory document model for the early editor. IDs are unique within a scene
// instance, but are not persisted across process restarts yet.
class Scene final
{
public:
    Scene(SceneId id, std::string name);

    [[nodiscard]] SceneId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<Entity>& entities() const noexcept { return entities_; }
    [[nodiscard]] Entity* find(EntityId id) noexcept;
    [[nodiscard]] const Entity* find(EntityId id) const noexcept;
    core::Result<PropertyValue> getProperty(EntityId entity, std::string_view property) const;
    core::Result<void> validateProperty(EntityId entity, std::string_view property, const PropertyValue& value) const;
    core::Result<void> setProperty(EntityId entity, std::string_view property, const PropertyValue& value);

    core::Result<EntityId> createEntity(std::string name = "Entity", std::optional<EntityId> parent = std::nullopt);
    core::Result<void> reparent(EntityId id, std::optional<EntityId> parent);
    core::Result<void> erase(EntityId id, bool recursive = false);

private:
    friend core::Result<Scene> restoreScene(SceneId id, std::string name, std::vector<Entity> entities);
    SceneId id_;
    std::string name_;
    EntityId nextEntityId_ = 1;
    std::vector<Entity> entities_;
};

core::Result<Scene> restoreScene(SceneId id, std::string name, std::vector<Entity> entities);

struct SetScenePropertyPayload
{
    EntityId entity = 0;
    std::string property;
    PropertyValue value;
};

core::Result<void> registerSceneCommands(core::CommandRegistry& commands, Scene& scene);
}
