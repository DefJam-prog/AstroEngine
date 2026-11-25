#pragma once

#include "astro/core/AIEditorApi.hpp"
#include "astro/scene/Scene.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace astro::ai
{
struct SceneComponentSnapshot
{
    std::string typeId;
    scene::ComponentId componentId = 0;
    std::uint32_t schemaVersion = 0;
};

struct SceneEntitySnapshot
{
    scene::EntityId id = 0;
    std::string name;
    std::optional<scene::EntityId> parent;
    std::optional<scene::Transform> transform;
    std::vector<SceneComponentSnapshot> components;
};

struct CodexProjectContext
{
    std::uint32_t schemaVersion = 1;
    std::string projectLabel;
    scene::SceneId sceneId = 0;
    std::string sceneName;
    std::vector<SceneEntitySnapshot> entities;
    std::vector<core::RegisteredCommandSchema> commands;
};

struct CodexPlanStep
{
    std::string id;
    std::string commandId;
    std::uint32_t commandSchemaVersion = 0;
    std::string intent;
};

struct CodexChangePlan
{
    std::uint32_t schemaVersion = 1;
    std::string summary;
    std::vector<CodexPlanStep> steps;
};

struct PropertyChangePreview
{
    scene::SceneId sceneId = 0;
    scene::EntityId entityId = 0;
    std::string propertyId;
    scene::PropertyValue before;
    scene::PropertyValue after;
    bool changesValue = false;
};

struct SpriteRendererVisibilityPreview
{
    scene::SceneId sceneId = 0;
    scene::EntityId entityId = 0;
    scene::ComponentId componentId = 0;
    assets::AssetId texture{};
    bool beforeVisible = true;
    bool afterVisible = true;
    bool changesValue = false;
};

struct PrimitiveCreationPreview
{
    scene::SceneId sceneId = 0;
    scene::EntityId expectedEntityId = 0;
    std::string name;
    std::optional<scene::EntityId> parent;
    scene::Primitive3D primitive = scene::Primitive3D::Cube;
    bool visible = true;
};

struct Camera3DAdditionPreview
{
    scene::SceneId sceneId = 0;
    scene::EntityId entityId = 0;
};

struct Camera3DLensPreview
{
    scene::SceneId sceneId = 0;
    scene::EntityId entityId = 0;
    scene::ComponentId componentId = 0;
    double beforeVerticalFovDegrees = 60.0;
    double afterVerticalFovDegrees = 60.0;
    double beforeNearPlane = 0.1;
    double afterNearPlane = 0.1;
    double beforeFarPlane = 1000.0;
    double afterFarPlane = 1000.0;
    bool active = false;
    bool changesValue = false;
};

struct Camera3DActiveSelectionPreview
{
    scene::SceneId sceneId = 0;
    std::optional<scene::EntityId> previousEntity;
    std::optional<scene::EntityId> targetEntity;
    scene::ComponentId previousComponentId = 0;
    scene::ComponentId targetComponentId = 0;
    bool changesValue = false;
};

struct TransformHierarchySnapshot
{
    scene::EntityId entityId = 0;
    scene::ComponentId componentId = 0;
    std::optional<scene::EntityId> parent;
    scene::Transform localTransform{};
    friend bool operator==(const TransformHierarchySnapshot&, const TransformHierarchySnapshot&) = default;
};

struct TransformChangePreview
{
    scene::SceneId sceneId = 0;
    scene::EntityId entityId = 0;
    scene::ComponentId componentId = 0;
    scene::Transform before{};
    scene::Transform after{};
    // Includes the reviewed entity and its ancestors so a derived camera pose
    // cannot be confirmed after any part of the transform chain changes.
    std::vector<TransformHierarchySnapshot> hierarchy;
    bool changesValue = false;
};

struct EntityParentChangePreview
{
    scene::SceneId sceneId = 0;
    scene::EntityId entityId = 0;
    scene::ComponentId transformComponentId = 0;
    std::optional<scene::EntityId> beforeParent;
    std::optional<scene::EntityId> afterParent;
    scene::Transform localTransform{};
    std::vector<TransformHierarchySnapshot> sourceHierarchy;
    std::vector<TransformHierarchySnapshot> targetParentHierarchy;
    std::vector<TransformHierarchySnapshot> affectedSubtree;
    bool changesValue = false;
};

// Read-only bridge for a trusted Codex/editor caller. It snapshots only the
// already-loaded Scene and registered command metadata; it does not scan files,
// execute plans, or provide a security boundary.
class CodexProjectAdapter final
{
public:
    explicit CodexProjectAdapter(core::AIEditorApi& api) noexcept : api_(api) {}

    [[nodiscard]] core::Result<CodexProjectContext> readContext(
        std::string projectLabel, const scene::Scene& activeScene) const;
    [[nodiscard]] core::Result<void> validatePlan(const CodexChangePlan& plan) const;
    [[nodiscard]] core::Result<PropertyChangePreview> previewSetProperty(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        const scene::SetScenePropertyPayload& payload) const;
    [[nodiscard]] core::Result<SpriteRendererVisibilityPreview> previewSpriteRendererVisibility(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        scene::EntityId entity, bool visible) const;
    [[nodiscard]] core::Result<PrimitiveCreationPreview> previewCreatePrimitive(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        const scene::CreatePrimitiveEntityPayload& payload) const;
    [[nodiscard]] core::Result<Camera3DAdditionPreview> previewAddCamera3D(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        const scene::AddCamera3DPayload& payload) const;
    [[nodiscard]] core::Result<Camera3DLensPreview> previewSetCamera3DLens(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        const scene::SetCamera3DLensPayload& payload) const;
    [[nodiscard]] core::Result<Camera3DActiveSelectionPreview> previewSetActiveCamera3D(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        const scene::SetActiveCamera3DPayload& payload) const;
    [[nodiscard]] core::Result<TransformChangePreview> previewSetTransform(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        const scene::SetTransformPayload& payload) const;
    [[nodiscard]] core::Result<EntityParentChangePreview> previewSetEntityParent(
        const scene::Scene& activeScene, std::uint32_t commandSchemaVersion,
        const scene::SetEntityParentPayload& payload) const;

private:
    core::AIEditorApi& api_;
};
}
