#pragma once

#include "astro/ai/CodexProjectAdapter.hpp"

#include <filesystem>
#include <optional>

namespace astro::ai
{
struct PendingPropertyChange
{
    scene::SetScenePropertyPayload payload;
    PropertyChangePreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

struct PendingSpriteRendererVisibilityChange
{
    scene::SetSpriteRendererPayload payload;
    SpriteRendererVisibilityPreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

struct PendingPrimitiveCreationChange
{
    scene::CreatePrimitiveEntityPayload payload;
    PrimitiveCreationPreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

struct PendingCamera3DAdditionChange
{
    scene::AddCamera3DPayload payload;
    Camera3DAdditionPreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

struct PendingCamera3DLensChange
{
    scene::SetCamera3DLensPayload payload;
    Camera3DLensPreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

struct PendingCamera3DActiveSelectionChange
{
    scene::SetActiveCamera3DPayload payload;
    Camera3DActiveSelectionPreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

struct PendingTransformChange
{
    scene::SetTransformPayload payload;
    TransformChangePreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

struct PendingEntityParentChange
{
    scene::SetEntityParentPayload payload;
    EntityParentChangePreview preview;
    std::uint32_t commandSchemaVersion = 1;
    std::filesystem::path projectIdentity;
    std::filesystem::path sceneIdentity;
};

// Editor-side review state. confirmAndApply is intended to be called only by a
// direct Editor confirmation action; it is not an OS/security boundary.
class CodexChangeReviewModel final
{
public:
    [[nodiscard]] core::Result<void> preparePropertyChange(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::SetScenePropertyPayload payload, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> prepareSpriteRendererVisibilityChange(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::EntityId entity, bool visible, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> preparePrimitiveCreation(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::CreatePrimitiveEntityPayload payload, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> prepareCamera3DAddition(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::AddCamera3DPayload payload, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> prepareCamera3DLensChange(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::SetCamera3DLensPayload payload, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> prepareCamera3DActiveSelectionChange(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::SetActiveCamera3DPayload payload, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> prepareTransformChange(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::SetTransformPayload payload, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> prepareEntityParentChange(
        const CodexProjectAdapter& adapter, const scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity,
        scene::SetEntityParentPayload payload, std::uint32_t commandSchemaVersion = 1);

    [[nodiscard]] core::Result<void> confirmAndApply(
        core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity);

    [[nodiscard]] core::Result<scene::EntityId> confirmAndCreatePrimitive(
        core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity);

    [[nodiscard]] core::Result<void> confirmAndAddCamera3D(
        core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity);

    [[nodiscard]] core::Result<void> confirmAndSetCamera3DLens(
        core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity);

    [[nodiscard]] core::Result<void> confirmAndSetActiveCamera3D(
        core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity);

    [[nodiscard]] core::Result<void> confirmAndReparent(
        core::AIEditorApi& api, const CodexProjectAdapter& adapter, scene::Scene& activeScene,
        const std::filesystem::path& projectIdentity, const std::filesystem::path& sceneIdentity);

    void cancel() noexcept
    { pending_.reset(); pendingSpriteRenderer_.reset(); pendingPrimitive_.reset(); pendingCamera3D_.reset();
        pendingCamera3DLens_.reset(); pendingCamera3DActiveSelection_.reset(); pendingTransform_.reset();
        pendingEntityParent_.reset(); }
    [[nodiscard]] const std::optional<PendingPropertyChange>& pending() const noexcept { return pending_; }
    [[nodiscard]] const std::optional<PendingSpriteRendererVisibilityChange>& pendingSpriteRenderer() const noexcept
    { return pendingSpriteRenderer_; }
    [[nodiscard]] const std::optional<PendingPrimitiveCreationChange>& pendingPrimitive() const noexcept
    { return pendingPrimitive_; }
    [[nodiscard]] const std::optional<PendingCamera3DAdditionChange>& pendingCamera3D() const noexcept
    { return pendingCamera3D_; }
    [[nodiscard]] const std::optional<PendingCamera3DLensChange>& pendingCamera3DLens() const noexcept
    { return pendingCamera3DLens_; }
    [[nodiscard]] const std::optional<PendingCamera3DActiveSelectionChange>& pendingCamera3DActiveSelection() const noexcept
    { return pendingCamera3DActiveSelection_; }
    [[nodiscard]] const std::optional<PendingTransformChange>& pendingTransform() const noexcept
    { return pendingTransform_; }
    [[nodiscard]] const std::optional<PendingEntityParentChange>& pendingEntityParent() const noexcept
    { return pendingEntityParent_; }
    [[nodiscard]] bool hasPending() const noexcept
    { return pending_.has_value() || pendingSpriteRenderer_.has_value() || pendingPrimitive_.has_value()
        || pendingCamera3D_.has_value() || pendingCamera3DLens_.has_value()
        || pendingCamera3DActiveSelection_.has_value() || pendingTransform_.has_value()
        || pendingEntityParent_.has_value(); }

private:
    std::optional<PendingPropertyChange> pending_;
    std::optional<PendingSpriteRendererVisibilityChange> pendingSpriteRenderer_;
    std::optional<PendingPrimitiveCreationChange> pendingPrimitive_;
    std::optional<PendingCamera3DAdditionChange> pendingCamera3D_;
    std::optional<PendingCamera3DLensChange> pendingCamera3DLens_;
    std::optional<PendingCamera3DActiveSelectionChange> pendingCamera3DActiveSelection_;
    std::optional<PendingTransformChange> pendingTransform_;
    std::optional<PendingEntityParentChange> pendingEntityParent_;
};
}
