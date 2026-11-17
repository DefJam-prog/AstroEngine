#pragma once

#include "astro/core/Result.hpp"
#include "astro/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <utility>

namespace astro::runtime
{
using SaveCustomValue = std::variant<std::string, double, bool>;
using SaveCustomState = std::map<std::string, SaveCustomValue>;

namespace SaveProperty
{
inline constexpr std::string_view entityName = "entity.name";
inline constexpr std::string_view positionX = "transform.position.x";
inline constexpr std::string_view positionY = "transform.position.y";
inline constexpr std::string_view positionZ = "transform.position.z";
inline constexpr std::string_view rotationX = "transform.rotation.x";
inline constexpr std::string_view rotationY = "transform.rotation.y";
inline constexpr std::string_view rotationZ = "transform.rotation.z";
inline constexpr std::string_view scaleX = "transform.scale.x";
inline constexpr std::string_view scaleY = "transform.scale.y";
inline constexpr std::string_view scaleZ = "transform.scale.z";
inline constexpr std::string_view spriteVisible = "sprite_renderer.visible";
}

// A profile starts empty; save operations persist only explicitly added fields.
class SaveProfile final
{
public:
    static core::Result<SaveProfile> create(std::string id, std::uint32_t schemaVersion = 1);
    core::Result<void> addField(scene::EntityId entity, std::string property);
    core::Result<void> removeField(scene::EntityId entity, std::string_view property);
    core::Result<void> addCustomField(std::string key, SaveCustomValue typeExample);
    core::Result<void> removeCustomField(std::string_view key);
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] std::uint32_t schemaVersion() const noexcept { return schemaVersion_; }
    [[nodiscard]] const std::map<scene::EntityId, std::vector<std::string>>& fields() const noexcept { return fields_; }
    [[nodiscard]] const SaveCustomState& customFields() const noexcept { return customFields_; }
private:
    SaveProfile(std::string id, std::uint32_t schemaVersion) : id_(std::move(id)), schemaVersion_(schemaVersion) {}
    std::string id_;
    std::uint32_t schemaVersion_;
    std::map<scene::EntityId, std::vector<std::string>> fields_;
    SaveCustomState customFields_;
};

struct SaveSlotInfo
{
    std::size_t slot = 0;
    scene::SceneId scene = 0;
    std::string profileId;
    std::uint32_t profileVersion = 0;
    std::string gameVersion;
    std::uint64_t savedAtUnixSeconds = 0;
    std::size_t propertyCount = 0;
    bool fromBackup = false;
};

struct SaveSlotInspection final
{
    SaveSlotInfo info;
    SaveCustomState customFields;
    std::size_t serializedBytes = 0;
};

struct SaveWriteResult
{
    SaveSlotInfo slotInfo;
    std::size_t missingEntities = 0;
    std::size_t missingComponents = 0;
};

struct SaveLoadResult
{
    SaveSlotInfo slotInfo;
    std::size_t appliedProperties = 0;
    std::size_t missingEntities = 0;
    std::size_t missingComponents = 0;
};

// Stores manual game-progress slots beneath an explicitly selected user-data directory.
class SaveStore final
{
public:
    static core::Result<SaveStore> open(std::filesystem::path directory,
        std::size_t slotCount = 10, std::size_t maximumFileBytes = 4 * 1024 * 1024);
    // Inspects an existing directory without creating it or exposing a writable store.
    static core::Result<SaveSlotInspection> inspectExistingSlot(const std::filesystem::path& directory,
        std::size_t slot, std::size_t maximumFileBytes = 4 * 1024 * 1024);

    [[nodiscard]] std::size_t slotCount() const noexcept { return slotCount_; }
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    core::Result<SaveWriteResult> save(std::size_t slot, const scene::Scene& state,
        const SaveProfile& profile, std::string_view gameVersion) const;
    core::Result<SaveWriteResult> save(std::size_t slot, const scene::Scene& state,
        const SaveProfile& profile, std::string_view gameVersion, const SaveCustomState& customState) const;
    core::Result<SaveLoadResult> load(std::size_t slot, scene::Scene& state,
        const SaveProfile& profile, std::string_view gameVersion) const;
    core::Result<SaveLoadResult> load(std::size_t slot, scene::Scene& state,
        const SaveProfile& profile, std::string_view gameVersion, SaveCustomState& customState) const;
    core::Result<SaveSlotInfo> inspect(std::size_t slot) const;
    core::Result<SaveSlotInspection> inspectDetailed(std::size_t slot) const;

private:
    SaveStore(std::filesystem::path directory, std::size_t slotCount, std::size_t maximumFileBytes)
        : directory_(std::move(directory)), slotCount_(slotCount), maximumFileBytes_(maximumFileBytes) {}
    core::Result<std::filesystem::path> slotPath(std::size_t slot) const;
    std::filesystem::path directory_;
    std::size_t slotCount_;
    std::size_t maximumFileBytes_;
};
}
