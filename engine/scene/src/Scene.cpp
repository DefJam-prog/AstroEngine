#include "astro/scene/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace astro::scene
{
namespace
{
bool blank(const std::string& value)
{
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool validUtf8(const std::string_view value)
{
    const auto continuation = [&value](const std::size_t index) {
        return index < value.size() && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80;
    };
    for (std::size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i]);
        if (lead <= 0x7F) { ++i; continue; }
        if (lead >= 0xC2 && lead <= 0xDF && continuation(i + 1)) { i += 2; continue; }
        if (lead >= 0xE0 && lead <= 0xEF && continuation(i + 1) && continuation(i + 2))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xE0 || second >= 0xA0) && (lead != 0xED || second < 0xA0)) { i += 3; continue; }
        }
        if (lead >= 0xF0 && lead <= 0xF4 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xF0 || second >= 0x90) && (lead != 0xF4 || second <= 0x8F)) { i += 4; continue; }
        }
        return false;
    }
    return true;
}

bool validName(const std::string_view name)
{
    const auto maxLength = entityPropertySchema().front().maxLengthBytes;
    if (blank(std::string(name)) || name.size() > maxLength || !validUtf8(name)) return false;
    for (const auto character : name)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7F) return false;
    }
    return true;
}

bool validTransform(const Transform& value)
{
    const auto finite = [](const Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    return finite(value.position) && finite(value.rotationDegrees) && finite(value.scale);
}

const PropertyDefinition* findProperty(const std::string_view id)
{
    for (const auto& definition : entityPropertySchema())
        if (definition.id == id) return &definition;
    return nullptr;
}
}

std::span<const PropertyDefinition> entityPropertySchema() noexcept
{
    static const std::array schema{
        PropertyDefinition{"name", "Name", PropertyType::Utf8String, true, true,
            PropertyValue{std::string{"Entity"}}, 240, 0, 0},
        PropertyDefinition{"id", "ID", PropertyType::UnsignedInteger, false, true,
            std::nullopt, 0, 1, std::numeric_limits<EntityId>::max()}
    };
    return schema;
}

Scene::Scene(const SceneId id, std::string name) : id_(id), name_(std::move(name)) {}

core::Result<Scene> restoreScene(const SceneId id, std::string name, std::vector<Entity> entities)
{
    if (id == 0 || id == std::numeric_limits<SceneId>::max() || !validName(name))
        return core::Result<Scene>::failure({core::ErrorCode::ValidationError, "Scene ID or name is invalid"});
    if (entities.size() > 100000)
        return core::Result<Scene>::failure({core::ErrorCode::ValidationError, "Scene exceeds the entity-count limit"});

    std::unordered_map<EntityId, const Entity*> byId;
    std::unordered_set<ComponentId> componentIds;
    byId.reserve(entities.size());
    componentIds.reserve(entities.size());
    EntityId maximumId = 0;
    ComponentId maximumComponentId = 0;
    for (const auto& entity : entities)
    {
        if (entity.id == 0 || entity.id == std::numeric_limits<EntityId>::max() || !validName(entity.name))
            return core::Result<Scene>::failure({core::ErrorCode::ValidationError, "Scene contains an invalid entity ID or name"});
        if (!byId.emplace(entity.id, &entity).second)
            return core::Result<Scene>::failure({core::ErrorCode::AlreadyExists, "Scene contains a duplicate entity ID"});
        maximumId = std::max(maximumId, entity.id);
        if (entity.components.size() != 1 || entity.components.front().id == 0
            || entity.components.front().id == std::numeric_limits<ComponentId>::max()
            || entity.components.front().schemaVersion != 1 || !validTransform(entity.components.front().value))
            return core::Result<Scene>::failure({core::ErrorCode::ValidationError,
                "Each entity must contain exactly one valid Transform component"});
        const auto componentId = entity.components.front().id;
        if (!componentIds.emplace(componentId).second)
            return core::Result<Scene>::failure({core::ErrorCode::AlreadyExists, "Scene contains a duplicate component ID"});
        maximumComponentId = std::max(maximumComponentId, componentId);
    }
    for (const auto& entity : entities)
        if (entity.parent && !byId.contains(*entity.parent))
            return core::Result<Scene>::failure({core::ErrorCode::NotFound, "Scene contains a missing parent entity"});

    std::unordered_map<EntityId, std::uint8_t> state;
    state.reserve(entities.size());
    for (const auto& entity : entities)
    {
        std::vector<EntityId> path;
        auto current = std::optional<EntityId>{entity.id};
        while (current && state[*current] == 0)
        {
            state[*current] = 1;
            path.push_back(*current);
            current = byId.at(*current)->parent;
        }
        if (current && state[*current] == 1)
            return core::Result<Scene>::failure({core::ErrorCode::ValidationError, "Scene hierarchy contains a cycle"});
        for (const auto visited : path) state[visited] = 2;
    }

    Scene restored(id, std::move(name));
    restored.entities_ = std::move(entities);
    restored.nextEntityId_ = maximumId + 1;
    restored.nextComponentId_ = maximumComponentId + 1;
    return core::Result<Scene>::success(std::move(restored));
}

const Entity* Scene::find(const EntityId id) const noexcept
{
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    return it == entities_.end() ? nullptr : &*it;
}

Entity* Scene::find(const EntityId id) noexcept
{
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    return it == entities_.end() ? nullptr : &*it;
}

TransformComponent* Scene::findTransform(const ComponentId id) noexcept
{
    for (auto& entity : entities_)
        for (auto& component : entity.components)
            if (component.id == id) return &component;
    return nullptr;
}

const TransformComponent* Scene::findTransform(const ComponentId id) const noexcept
{
    for (const auto& entity : entities_)
        for (const auto& component : entity.components)
            if (component.id == id) return &component;
    return nullptr;
}

const TransformComponent* Scene::transform(const EntityId entityId) const noexcept
{
    const auto* entity = find(entityId);
    return entity && !entity->components.empty() ? &entity->components.front() : nullptr;
}

core::Result<void> Scene::setTransform(const EntityId entityId, const Transform& value)
{
    auto* entity = find(entityId);
    if (!entity)
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
    if (!validTransform(value))
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Transform values must be finite"});
    entity->components.front().value = value;
    return core::Result<void>::success();
}

core::Result<PropertyValue> Scene::getProperty(const EntityId entityId, const std::string_view property) const
{
    const auto* entity = find(entityId);
    if (!entity)
        return core::Result<PropertyValue>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
    const auto* definition = findProperty(property);
    if (!definition)
        return core::Result<PropertyValue>::failure({core::ErrorCode::NotFound, "Entity property is not in the schema"});
    if (property == "name") return core::Result<PropertyValue>::success(entity->name);
    if (property == "id") return core::Result<PropertyValue>::success(entity->id);
    return core::Result<PropertyValue>::failure({core::ErrorCode::InternalError, "Schema property has no getter"});
}

core::Result<void> Scene::validateProperty(const EntityId entityId, const std::string_view property,
    const PropertyValue& value) const
{
    if (!find(entityId))
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
    const auto* definition = findProperty(property);
    if (!definition)
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity property is not in the schema"});
    if (!definition->editable)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Entity property is read-only"});
    if (definition->type == PropertyType::Utf8String)
    {
        const auto* text = std::get_if<std::string>(&value);
        if (!text)
            return core::Result<void>::failure({core::ErrorCode::TypeMismatch, "Property requires a UTF-8 string"});
        if ((definition->required && blank(*text)) || text->find('\0') != std::string::npos
            || !validName(*text))
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Property must be valid non-empty UTF-8 without control bytes"});
        if (text->size() > definition->maxLengthBytes)
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Property exceeds its byte limit"});
    }
    else if (definition->type == PropertyType::UnsignedInteger)
    {
        const auto* number = std::get_if<EntityId>(&value);
        if (!number)
            return core::Result<void>::failure({core::ErrorCode::TypeMismatch, "Property requires an unsigned integer"});
        if (*number < definition->minimum || *number > definition->maximum)
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Property is outside its schema range"});
    }
    return core::Result<void>::success();
}

core::Result<void> Scene::setProperty(const EntityId entityId, const std::string_view property,
    const PropertyValue& value)
{
    auto validation = validateProperty(entityId, property, value);
    if (!validation) return validation;
    auto* entity = find(entityId);
    if (property == "name")
    {
        entity->name = std::get<std::string>(value);
        return core::Result<void>::success();
    }
    return core::Result<void>::failure({core::ErrorCode::ValidationError, "Entity property is not editable"});
}

core::Result<EntityId> Scene::createEntity(std::string name, const std::optional<EntityId> parent)
{
    if (blank(name))
        return core::Result<EntityId>::failure({core::ErrorCode::InvalidArgument, "Entity name must not be empty"});
    if (!validUtf8(name) || name.find('\0') != std::string::npos)
        return core::Result<EntityId>::failure({core::ErrorCode::ValidationError, "Entity name must contain valid UTF-8 without NUL bytes"});
    if (name.size() > entityPropertySchema().front().maxLengthBytes)
        return core::Result<EntityId>::failure({core::ErrorCode::ValidationError, "Entity name exceeds its schema byte limit"});
    for (const auto character : name)
        if (static_cast<unsigned char>(character) < 0x20 || static_cast<unsigned char>(character) == 0x7F)
            return core::Result<EntityId>::failure({core::ErrorCode::ValidationError, "Entity name must not contain control bytes"});
    if (parent && !find(*parent))
        return core::Result<EntityId>::failure({core::ErrorCode::NotFound, "Parent entity does not exist"});
    if (nextEntityId_ == 0 || nextEntityId_ == std::numeric_limits<EntityId>::max())
        return core::Result<EntityId>::failure({core::ErrorCode::InvalidState, "Entity ID space is exhausted"});
    if (nextComponentId_ == 0 || nextComponentId_ == std::numeric_limits<ComponentId>::max())
        return core::Result<EntityId>::failure({core::ErrorCode::InvalidState, "Component ID space is exhausted"});

    const auto id = nextEntityId_++;
    entities_.push_back({id, std::move(name), parent, {{nextComponentId_++, 1, {}}}});
    return core::Result<EntityId>::success(id);
}

core::Result<void> Scene::reparent(const EntityId id, const std::optional<EntityId> parent)
{
    auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    if (it == entities_.end())
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
    if (parent && !find(*parent))
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Parent entity does not exist"});
    if (parent == id)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Entity cannot be its own parent"});

    auto ancestor = parent;
    while (ancestor)
    {
        if (*ancestor == id)
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Reparenting would create a hierarchy cycle"});
        const auto* parentEntity = find(*ancestor);
        ancestor = parentEntity ? parentEntity->parent : std::nullopt;
    }
    it->parent = parent;
    return core::Result<void>::success();
}

core::Result<void> Scene::erase(const EntityId id, const bool recursive)
{
    if (!find(id))
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});

    std::vector<EntityId> removals{id};
    for (std::size_t index = 0; index < removals.size(); ++index)
    {
        for (const auto& entity : entities_)
        {
            if (entity.parent == removals[index])
            {
                if (!recursive)
                    return core::Result<void>::failure({core::ErrorCode::InvalidState, "Entity has children; recursive deletion was not requested"});
                removals.push_back(entity.id);
            }
        }
    }
    std::erase_if(entities_, [&removals](const Entity& entity) {
        return std::find(removals.begin(), removals.end(), entity.id) != removals.end();
    });
    return core::Result<void>::success();
}
}
