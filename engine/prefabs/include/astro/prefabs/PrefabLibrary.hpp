#pragma once

#include "astro/scene/Scene.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace astro::prefabs
{
using PrefabId = scene::PrefabId;

struct PrefabNode
{
    std::uint64_t localId = 0;
    std::uint64_t parentLocalId = 0;
    std::string name;
    scene::Transform transform{};
    std::optional<scene::PrefabSpriteValue> sprite;
    PrefabId nestedPrefab = 0;
};

struct PrefabAsset
{
    PrefabId id = 0;
    std::string name;
    std::vector<PrefabNode> nodes;
};

class PrefabLibrary final
{
public:
    static core::Result<PrefabLibrary> open(const std::filesystem::path& projectRoot);
    core::Result<void> save(const PrefabAsset& prefab) const;
    [[nodiscard]] core::Result<PrefabAsset> load(PrefabId id) const;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
private:
    explicit PrefabLibrary(std::filesystem::path directory) : directory_(std::move(directory)) {}
    std::filesystem::path directory_;
};

[[nodiscard]] core::Result<void> validatePrefab(const PrefabAsset& prefab);
[[nodiscard]] core::Result<PrefabAsset> capturePrefab(const scene::Scene& scene, scene::EntityId root,
    PrefabId id, std::string name);
[[nodiscard]] core::Result<scene::EntityId> instantiatePrefab(const PrefabLibrary& library,
    scene::Scene& scene, PrefabId prefab, std::optional<scene::EntityId> parent = std::nullopt);
[[nodiscard]] core::Result<void> setPrefabOverride(scene::Scene& scene, scene::EntityId instanceRoot,
    std::string sourceKey, scene::PrefabOverrideProperty property, scene::PrefabOverrideValue value);
[[nodiscard]] core::Result<void> updatePrefabInstance(const PrefabLibrary& library,
    scene::Scene& scene, scene::EntityId instanceRoot);

struct InstantiatePrefabPayload
{
    PrefabId prefab = 0;
    std::optional<scene::EntityId> parent;
};

struct SetPrefabOverridePayload
{
    scene::EntityId instanceRoot = 0;
    std::string sourceKey;
    scene::PrefabOverrideProperty property = scene::PrefabOverrideProperty::Name;
    scene::PrefabOverrideValue value;
};

struct UpdatePrefabInstancePayload
{
    scene::EntityId instanceRoot = 0;
};

core::Result<void> registerPrefabCommands(core::CommandRegistry& commands,
    const PrefabLibrary& library, scene::Scene& scene);
}
