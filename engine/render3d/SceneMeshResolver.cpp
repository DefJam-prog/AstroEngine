#include "astro/render3d/SceneMeshResolver.hpp"

#include "astro/render3d/GltfImporter.hpp"

#include <algorithm>
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
    if ((component.schemaVersion != 1 && component.schemaVersion != 2) || component.model.value == 0
        || component.materialOverride.value == UINT64_MAX)
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

    std::optional<std::uint32_t> sourceMaterialIndex;
    if (component.primitiveIndex < mesh.primitiveMaterialIndices.size())
        sourceMaterialIndex = mesh.primitiveMaterialIndices[component.primitiveIndex];
    std::optional<ResolvedSceneSourceMaterial> sourceMaterial;
    if (sourceMaterialIndex)
    {
        if (*sourceMaterialIndex >= imported.value().materials.size())
            return failure(core::ErrorCode::ValidationError,
                "MeshRenderer source material index is outside the imported material table");
        const auto& importedMaterial = imported.value().materials[*sourceMaterialIndex];
        sourceMaterial.emplace();
        sourceMaterial->values = importedMaterial.values;
        if (importedMaterial.baseColorTexture)
        {
            const auto& binding = *importedMaterial.baseColorTexture;
            sourceMaterial->hasBaseColorTexture = true;
            sourceMaterial->baseColorTexCoord = binding.texCoord;
            sourceMaterial->baseColorTransform = binding.transform;
            if (binding.textureIndex >= imported.value().textures.size())
                return failure(core::ErrorCode::ValidationError,
                    "MeshRenderer source base-color texture index is outside the imported texture table");
            const auto& texture = imported.value().textures[binding.textureIndex];
            if (texture.samplerIndex)
            {
                if (*texture.samplerIndex >= imported.value().samplers.size())
                    return failure(core::ErrorCode::ValidationError,
                        "MeshRenderer source base-color sampler index is outside the imported sampler table");
                sourceMaterial->baseColorSampler = imported.value().samplers[*texture.samplerIndex];
            }
            if (!texture.imageIndex || *texture.imageIndex >= imported.value().images.size())
            {
                sourceMaterial->baseColorImageAvailable = false;
                sourceMaterial->diagnostic = "Source base-color texture has no supported image; using vertex-color fallback";
            }
            else
            {
                const auto& image = imported.value().images[*texture.imageIndex];
                if (image.sourceKind != GltfImageSourceKind::ExternalFile || !image.available)
                {
                    sourceMaterial->baseColorImageAvailable = false;
                    sourceMaterial->diagnostic = "Source base-color image is missing or not a local external file; using vertex-color fallback";
                }
                else
                {
                    const auto imageRelative = pathFromUtf8(image.reference);
                    const auto safeRelative = imageRelative.lexically_normal();
                    if (imageRelative.empty() || imageRelative.is_absolute() || imageRelative.has_root_name()
                        || safeRelative != imageRelative
                        || std::any_of(imageRelative.begin(), imageRelative.end(), [](const auto& part) {
                            return part == "..";
                        }))
                    {
                        sourceMaterial->baseColorImageAvailable = false;
                        sourceMaterial->diagnostic = "Source base-color image path is invalid; using vertex-color fallback";
                    }
                    else
                    {
                        sourceMaterial->baseColorImagePath = registry.projectRoot()
                            / std::filesystem::path("Assets") / imageRelative;
                        sourceMaterial->baseColorImageHash = image.sourceHash;
                    }
                }
            }
        }
    }
    if (component.materialOverride.value != 0)
    {
        auto material = registry.resolveMaterial(component.materialOverride);
        if (!material) return failure(material.error().code, material.error().message);
    }

    return core::Result<ResolvedSceneMesh>::success({component.model, component.meshIndex,
        component.primitiveIndex, sourceMaterialIndex, component.materialOverride, std::move(sourceMaterial),
        std::move(mesh.name),
        std::move(mesh.primitives[component.primitiveIndex]), component.visible});
}
}
