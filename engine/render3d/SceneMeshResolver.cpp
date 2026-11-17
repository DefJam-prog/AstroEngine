#include "astro/render3d/SceneMeshResolver.hpp"

#include "astro/render3d/GltfImporter.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace astro::render3d
{
namespace
{
core::Result<ResolvedSceneMesh> failure(const core::ErrorCode code, std::string message)
{
    return core::Result<ResolvedSceneMesh>::failure({code, std::move(message)});
}

std::filesystem::path pathFromUtf8(const std::string& value)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
}

core::Result<ResolvedSceneMesh> resolveMeshRenderer(const assets::AssetRegistry& registry,
    const scene::MeshRendererComponent& component)
{
    if (component.schemaVersion != 1 || component.model.value == 0)
        return failure(core::ErrorCode::ValidationError,
            "MeshRenderer component schema or model AssetId is invalid");

    const auto* reference = registry.findModel(component.model);
    if (!reference)
        return failure(core::ErrorCode::NotFound, "MeshRenderer model AssetId is not registered");

    auto resolved = registry.resolveModel(component.model);
    if (!resolved) return failure(resolved.error().code, resolved.error().message);

    const auto sourceRelativePath = std::filesystem::path(std::u8string(u8"Assets"))
        / pathFromUtf8(reference->sourceRelativePath);
    auto imported = GltfImporter::importFile(registry.projectRoot(), sourceRelativePath);
    if (!imported) return failure(imported.error().code, imported.error().message);
    if (imported.value().contentHash != resolved.value().importContentHash)
        return failure(core::ErrorCode::InvalidState,
            "Model geometry inputs changed after registration; import and register the model again");

    const auto& meshes = imported.value().meshes;
    if (component.meshIndex >= meshes.size())
        return failure(core::ErrorCode::ValidationError,
            "MeshRenderer mesh index is outside the imported model");
    auto& mesh = imported.value().meshes[component.meshIndex];
    if (component.primitiveIndex >= mesh.primitives.size())
        return failure(core::ErrorCode::ValidationError,
            "MeshRenderer primitive index is outside the selected mesh");

    return core::Result<ResolvedSceneMesh>::success({component.model, component.meshIndex,
        component.primitiveIndex, std::move(mesh.name),
        std::move(mesh.primitives[component.primitiveIndex]), component.visible});
}
}
