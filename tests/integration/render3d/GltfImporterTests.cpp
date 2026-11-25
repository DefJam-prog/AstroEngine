#include "astro/render3d/GltfImporter.hpp"
#include "astro/render3d/SceneMeshResolver.hpp"
#include "astro/assets/MaterialAsset.hpp"
#include "astro/scene/Scene.hpp"
#include "astro/scene/SceneFile.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;
void expect(const bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

struct TempProject final
{
    std::filesystem::path root;
    TempProject()
    {
        root = std::filesystem::temp_directory_path()
            / ("Astro glTF integration " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "Assets" / "Models");
    }
    ~TempProject() { std::error_code error; std::filesystem::remove_all(root, error); }
};

std::vector<std::uint8_t> triangleBytes(const std::array<std::uint16_t, 3>& indices = {0, 1, 2})
{
    std::vector<std::uint8_t> bytes;
    for (const float value : std::array<float, 9>{0, 0, 0, 1, 0, 0, 0, 1, 0})
    {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
    for (const auto value : indices)
    { bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8)); }
    return bytes;
}

std::string base64(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        const auto remaining = bytes.size() - i;
        const std::uint32_t value = (static_cast<std::uint32_t>(bytes[i]) << 16)
            | (remaining > 1 ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0)
            | (remaining > 2 ? bytes[i + 2] : 0);
        output.push_back(alphabet[(value >> 18) & 63]); output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(remaining > 1 ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(remaining > 2 ? alphabet[value & 63] : '=');
    }
    return output;
}

void replaceOnce(std::string& value, std::string_view from, std::string_view to);

std::string document(const std::string& bufferSource, const std::size_t byteLength,
    const std::string& meshName = "Triangle Ω", const bool requiredExtension = false)
{
    std::string json = "{\"asset\":{\"version\":\"2.0\"},";
    if (requiredExtension) json += "\"extensionsRequired\":[\"EXT_unhandled_test\"],";
    json += "\"buffers\":[{";
    if (!bufferSource.empty()) json += "\"uri\":\"" + bufferSource + "\",";
    json += "\"byteLength\":" + std::to_string(byteLength) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"name\":\"" + meshName + "\",\"primitives\":[{\"attributes\":{\"POSITION\":0},"
        "\"indices\":1,\"mode\":4}]}]}";
    return json;
}

std::string materialDocument(const std::string& imageUri, const std::vector<std::uint8_t>& geometryBytes)
{
    auto json = document("data:application/octet-stream;base64," + base64(geometryBytes), geometryBytes.size());
    replaceOnce(json, "\"mode\":4}", "\"mode\":4,\"material\":0}");
    json.pop_back();
    json += ",\"images\":[{\"name\":\"Albedo Ω\",\"uri\":\"" + imageUri + "\"}],"
        "\"samplers\":[{\"magFilter\":9728,\"minFilter\":9729,\"wrapS\":33648,\"wrapT\":33071}],"
        "\"textures\":[{\"name\":\"Paint texture\",\"source\":0,\"sampler\":0}],"
        "\"extensionsUsed\":[\"KHR_texture_transform\",\"VENDOR_material_note\"],"
        "\"materials\":[{\"name\":\"Paint\",\"pbrMetallicRoughness\":{"
            "\"baseColorFactor\":[0.25,0.5,0.75,1],\"metallicFactor\":0.3,\"roughnessFactor\":0.7,"
            "\"baseColorTexture\":{\"index\":0,\"texCoord\":1,\"extensions\":{"
                "\"KHR_texture_transform\":{\"offset\":[0.25,0.5],\"rotation\":1.57079632679,"
                "\"scale\":[2,3],\"texCoord\":0}}},"
            "\"metallicRoughnessTexture\":{\"index\":0}},"
        "\"normalTexture\":{\"index\":0,\"scale\":0.8,\"texCoord\":2},"
        "\"occlusionTexture\":{\"index\":0,\"strength\":0.6},"
        "\"emissiveTexture\":{\"index\":0},\"emissiveFactor\":[0.1,0.2,0.3],"
        "\"alphaMode\":\"MASK\",\"alphaCutoff\":0.42,\"doubleSided\":true}]}";
    return json;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
void writeText(const std::filesystem::path& path, const std::string& value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
}
void appendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{ for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift)); }

void appendFloat(std::vector<std::uint8_t>& bytes, const float value)
{ appendU32(bytes, std::bit_cast<std::uint32_t>(value)); }

std::vector<std::uint8_t> normalUvTriangleBytes()
{
    std::vector<std::uint8_t> bytes;
    for (const float value : std::array<float, 9>{0, 0, 0, 1, 0, 0, 0, 1, 0}) appendFloat(bytes, value);
    for (const float value : std::array<float, 9>{0, 0, 1, 0, 0, 1, 0, 0, 1}) appendFloat(bytes, value);
    for (const float value : std::array<float, 6>{0, 0, 1, 0, 0, 1}) appendFloat(bytes, value);
    for (const auto value : std::array<std::uint16_t, 3>{0, 1, 2})
    { bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8)); }
    return bytes;
}

std::string normalUvDocument(const std::vector<std::uint8_t>& bytes)
{
    return "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,"
        + base64(bytes) + "\",\"byteLength\":" + std::to_string(bytes.size()) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"name\":\"Normal UV triangle\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"mode\":4}]}]}";
}

std::vector<std::uint8_t> normalizedByteUvTriangleBytes()
{
    std::vector<std::uint8_t> bytes;
    for (const float value : std::array<float, 9>{0, 0, 0, 1, 0, 0, 0, 1, 0}) appendFloat(bytes, value);
    for (const std::uint8_t value : std::array<std::uint8_t, 6>{0, 255, 128, 128, 255, 0}) bytes.push_back(value);
    for (const auto value : std::array<std::uint16_t, 3>{0, 1, 2})
    { bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8)); }
    return bytes;
}

std::string normalizedByteUvDocument(const std::vector<std::uint8_t>& bytes, const bool normalized)
{
    return "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,"
        + base64(bytes) + "\",\"byteLength\":" + std::to_string(bytes.size()) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":42,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5121,\"normalized\":" + (normalized ? "true" : "false")
        + ",\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2,\"mode\":4}]}]}";
}

void replaceOnce(std::string& value, const std::string_view from, const std::string_view to)
{
    const auto position = value.find(from);
    if (position != std::string::npos) value.replace(position, from.size(), to);
}

std::vector<std::uint8_t> makeGlb(const std::vector<std::uint8_t>& binary)
{
    auto json = document("", binary.size());
    while (json.size() % 4 != 0) json.push_back(' ');
    const auto paddedBinarySize = (binary.size() + 3u) & ~std::size_t(3u);
    const auto totalLength = static_cast<std::uint32_t>(12u + 8u + json.size() + 8u + paddedBinarySize);
    std::vector<std::uint8_t> bytes;
    appendU32(bytes, 0x46546C67u); appendU32(bytes, 2u); appendU32(bytes, totalLength);
    appendU32(bytes, static_cast<std::uint32_t>(json.size())); appendU32(bytes, 0x4E4F534Au);
    bytes.insert(bytes.end(), json.begin(), json.end());
    appendU32(bytes, static_cast<std::uint32_t>(paddedBinarySize)); appendU32(bytes, 0x004E4942u);
    bytes.insert(bytes.end(), binary.begin(), binary.end());
    while (bytes.size() < totalLength) bytes.push_back(0);
    return bytes;
}

bool verifyTriangle(const astro::render3d::GltfImportResult& imported)
{
    if (imported.meshes.size() != 1 || imported.meshes.front().primitives.size() != 1) return false;
    const auto& mesh = imported.meshes.front().primitives.front();
    return mesh.vertices().size() == 3 && mesh.triangleIndices() == std::vector<std::uint32_t>{0, 1, 2}
        && !mesh.hasNormals() && !mesh.hasTexCoords()
        && mesh.vertices()[1].position.x == 1.0f && mesh.vertices()[2].position.y == 1.0f;
}

void testAssetRegistryAndSceneReference()
{
    using namespace astro;
    TempProject project;
    auto registryResult = assets::AssetRegistry::open(project.root);
    if (!registryResult)
        std::cerr << "AssetRegistry open failed: " << registryResult.error().message << '\n';
    expect(registryResult.hasValue(), "open registry for imported model integration");
    if (!registryResult) return;
    auto registry = std::move(registryResult).value();

    auto sourceText = document("data:application/octet-stream;base64," + base64(triangleBytes()), triangleBytes().size());
    const auto sourcePath = project.root / "Assets" / "Models" / "scene-mesh.gltf";
    writeText(sourcePath, sourceText);
    auto imported = render3d::GltfImporter::importAndRegister(registry,
        std::filesystem::path("Assets/Models/scene-mesh.gltf"));
    if (!imported) std::cerr << "Model registration import error: " << imported.error().message << '\n';
    expect(imported && imported.value().modelAssetId.value != 0 && verifyTriangle(imported.value()),
        "import and register one stable model asset ID");
    if (!imported) return;
    const auto modelId = imported.value().modelAssetId;
    expect(registry.modelEntries().size() == 1 && registry.findModel(modelId)
        && registry.findModel(modelId)->importContentHash == imported.value().contentHash,
        "record imported model identity and geometry content hash");
    expect(registry.resolveModel(modelId).hasValue(), "resolve current imported model by AssetId");

    scene::Scene authored(1, "Mesh reference scene");
    const auto entity = authored.createEntity("Imported mesh");
    expect(entity && authored.setMeshRenderer(entity.value(), modelId, 0, 0, false),
        "attach model AssetId to the versioned MeshRenderer component");
    const auto scenePath = project.root / "Scenes" / "Mesh.scene";
    std::filesystem::create_directories(scenePath.parent_path());
    expect(entity && scene::saveSceneFile(scenePath, authored).hasValue(), "serialize Scene v7 model reference");
    auto reopenedScene = scene::loadSceneFile(scenePath);
    expect(reopenedScene && reopenedScene.value().meshRenderer(entity.value())
        && reopenedScene.value().meshRenderer(entity.value())->model == modelId,
        "load a Scene reference to the registered Model AssetId");
    const auto* loadedMesh = reopenedScene && entity
        ? reopenedScene.value().meshRenderer(entity.value()) : nullptr;
    expect(loadedMesh != nullptr, "read back the persisted Scene MeshRenderer component");
    if (!loadedMesh) return;
    auto resolvedGeometry = render3d::resolveMeshRenderer(registry, *loadedMesh);
    expect(resolvedGeometry && resolvedGeometry.value().geometry.vertices().size() == 3
        && resolvedGeometry.value().geometry.triangleIndices() == std::vector<std::uint32_t>{0, 1, 2}
        && resolvedGeometry.value().meshName == "Triangle Ω" && !resolvedGeometry.value().visible
        && !resolvedGeometry.value().sourceMaterialIndex && resolvedGeometry.value().materialOverride.value == 0,
        "resolve a Scene MeshRenderer to its current primitive geometry and visibility state");

    std::filesystem::create_directories(project.root / "Assets" / "Materials");
    const auto materialText = assets::serializeMaterialAsset(assets::MaterialAssetData{});
    expect(materialText.hasValue(), "serialize a valid project material fixture");
    if (materialText)
    {
        writeText(project.root / "Assets" / "Materials" / "override.astro-material", materialText.value());
        const auto material = registry.registerMaterial(std::filesystem::path("Materials/override.astro-material"));
        expect(material.hasValue(), "register a project material for a Scene primitive override");
        if (material)
        {
            auto overridden = *loadedMesh;
            overridden.materialOverride = material.value().id;
            overridden.schemaVersion = 2;
            const auto resolvedOverride = render3d::resolveMeshRenderer(registry, overridden);
            expect(resolvedOverride && resolvedOverride.value().materialOverride == material.value().id
                && !resolvedOverride.value().sourceMaterialIndex,
                "resolve a registered Scene override separately from the absent imported source material");
            overridden.materialOverride = {999999};
            expect(!render3d::resolveMeshRenderer(registry, overridden),
                "diagnose an unresolved project material override");
        }
    }

    auto invalidMeshSelection = *loadedMesh;
    invalidMeshSelection.meshIndex = 1;
    expect(!render3d::resolveMeshRenderer(registry, invalidMeshSelection),
        "reject an out-of-range MeshRenderer mesh index");
    auto invalidPrimitiveSelection = *loadedMesh;
    invalidPrimitiveSelection.primitiveIndex = 1;
    expect(!render3d::resolveMeshRenderer(registry, invalidPrimitiveSelection),
        "reject an out-of-range MeshRenderer primitive index");
    auto unknownAssetSelection = *loadedMesh;
    unknownAssetSelection.model = {999999};
    expect(!render3d::resolveMeshRenderer(registry, unknownAssetSelection),
        "reject an unregistered MeshRenderer model AssetId");
    auto unsupportedSchema = *loadedMesh;
    unsupportedSchema.schemaVersion = 99;
    expect(!render3d::resolveMeshRenderer(registry, unsupportedSchema),
        "reject an unsupported MeshRenderer schema version");
    auto zeroAssetSelection = *loadedMesh;
    zeroAssetSelection.model = {};
    expect(!render3d::resolveMeshRenderer(registry, zeroAssetSelection),
        "reject a zero MeshRenderer model AssetId");

    auto reopenedRegistry = assets::AssetRegistry::open(project.root);
    expect(reopenedRegistry && reopenedRegistry.value().findModel(modelId)
        && reopenedRegistry.value().resolveModel(modelId), "round-trip model record in Asset index v3");

    auto changedSource = sourceText;
    replaceOnce(changedSource, "Triangle Ω", "Triangle Changed");
    writeText(sourcePath, changedSource);
    expect(!registry.resolveModel(modelId), "diagnose changed model source using registered digest");
    expect(!render3d::resolveMeshRenderer(registry, *loadedMesh),
        "reject Scene geometry when registered model source has changed");
    auto reimported = render3d::GltfImporter::importAndRegister(registry,
        std::filesystem::path("Assets/Models/scene-mesh.gltf"));
    expect(reimported && reimported.value().modelAssetId == modelId
        && reimported.value().contentHash != imported.value().contentHash,
        "reimport changed model bytes while preserving the stable AssetId");
    expect(registry.resolveModel(modelId).hasValue(), "resolve the model after explicit reimport/register");
    expect(render3d::resolveMeshRenderer(registry, *loadedMesh).hasValue(),
        "resolve the Scene MeshRenderer again after explicit reimport/register");

    std::error_code error;
    std::filesystem::remove(sourcePath, error);
    const auto missing = registry.missingAssets();
    expect(std::find(missing.begin(), missing.end(), modelId) != missing.end()
        && !registry.resolveModel(modelId), "report a missing model source without deleting the Scene reference");
}

void testExternalBufferFreshness()
{
    using namespace astro;
    TempProject project;
    auto opened = assets::AssetRegistry::open(project.root);
    expect(opened.hasValue(), "open asset registry for external-buffer freshness test");
    if (!opened) return;
    auto registry = std::move(opened).value();

    auto bytes = triangleBytes();
    const auto bufferPath = project.root / "Assets/Models/triangle.bin";
    writeBytes(bufferPath, bytes);
    const auto modelPath = project.root / "Assets/Models/external.gltf";
    writeText(modelPath, document("triangle.bin", bytes.size()));
    auto imported = render3d::GltfImporter::importAndRegister(registry,
        std::filesystem::path("Assets/Models/external.gltf"));
    expect(imported.hasValue(), "import and register model with an external buffer");
    if (!imported) return;

    scene::MeshRendererComponent component;
    component.id = 1;
    component.model = imported.value().modelAssetId;
    expect(render3d::resolveMeshRenderer(registry, component).hasValue(),
        "resolve selected geometry before external-buffer modification");

    bytes[0] ^= 1u;
    writeBytes(bufferPath, bytes);
    expect(registry.resolveModel(component.model).hasValue(),
        "top-level model source remains current when only an external buffer changes");
    expect(!render3d::resolveMeshRenderer(registry, component),
        "reject stale imported geometry after an external buffer changes");

    auto refreshed = render3d::GltfImporter::importAndRegister(registry,
        std::filesystem::path("Assets/Models/external.gltf"));
    expect(refreshed && refreshed.value().modelAssetId == component.model,
        "explicit external-buffer reimport retains the stable model AssetId");
    expect(render3d::resolveMeshRenderer(registry, component).hasValue(),
        "resolve geometry after external-buffer reimport updates the registered digest");
}

void testMaterialMetadataAndImageReferences()
{
    using namespace astro;
    TempProject project;
    const auto geometry = triangleBytes();
    const auto modelPath = project.root / "Assets/Models/materials.gltf";
    const auto imagePath = project.root / "Assets/Models/albedo.png";
    writeBytes(imagePath, {137, 80, 78, 71, 1, 2, 3}); // Metadata-only fixture; no image decoder is invoked.
    writeText(modelPath, materialDocument("albedo.png", geometry));

    auto imported = render3d::GltfImporter::importFile(project.root, "Assets/Models/materials.gltf");
    expect(imported && verifyTriangle(imported.value()), "geometry remains available with glTF material metadata");
    if (!imported) return;
    const auto& result = imported.value();
    expect(result.meshes[0].primitiveMaterialIndices.size() == 1
        && result.meshes[0].primitiveMaterialIndices[0] == std::optional<std::uint32_t>(0),
        "primitive retains its source material index");
    expect(result.images.size() == 1 && result.images[0].name == "Albedo Ω"
        && result.images[0].sourceKind == render3d::GltfImageSourceKind::ExternalFile
        && result.images[0].reference == "Models/albedo.png" && result.images[0].available
        && result.images[0].sourceSize == 7 && !result.images[0].sourceHash.empty(),
        "external image reference is confined and hashed without rewriting the image");
    expect(result.textures.size() == 1 && result.textures[0].imageIndex == std::optional<std::uint32_t>(0)
        && result.textures[0].samplerIndex == std::optional<std::uint32_t>(0)
        && result.samplers.size() == 1
        && result.samplers[0].wrapS == render3d::GltfSamplerWrap::MirroredRepeat
        && result.samplers[0].wrapT == render3d::GltfSamplerWrap::ClampToEdge
        && result.samplers[0].magFilter == render3d::GltfSamplerFilter::Nearest
        && result.samplers[0].minFilter == render3d::GltfSamplerFilter::Linear,
        "glTF texture preserves sampler index and validated wrap/filter settings");
    expect(result.materials.size() == 1 && result.materials[0].name == "Paint"
        && result.materials[0].values.baseColorFactor == std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.0f}
        && result.materials[0].values.metallicFactor == 0.3f
        && result.materials[0].values.roughnessFactor == 0.7f
        && result.materials[0].values.alphaMode == assets::MaterialAlphaMode::Mask
        && result.materials[0].values.alphaCutoff == 0.42f && result.materials[0].values.doubleSided
        && result.materials[0].values.normalScale == 0.8f
        && result.materials[0].values.occlusionStrength == 0.6f,
        "supported metallic-roughness material factors and flags are preserved");
    const auto& material = result.materials[0];
    expect(material.baseColorTexture && material.baseColorTexture->textureIndex == 0
        && material.baseColorTexture->texCoord == 0
        && material.baseColorTexture->transform.has_value()
        && std::abs(material.baseColorTexture->transform->offsetU - 0.25f) < 1e-6f
        && std::abs(material.baseColorTexture->transform->offsetV - 0.5f) < 1e-6f
        && std::abs(material.baseColorTexture->transform->scaleU - 2.0f) < 1e-6f
        && std::abs(material.baseColorTexture->transform->scaleV - 3.0f) < 1e-6f
        && std::abs(material.baseColorTexture->transform->rotationRadians - 1.5707963f) < 1e-5f
        && material.metallicRoughnessTexture && material.metallicRoughnessTexture->texCoord == 0
        && material.normalTexture && material.normalTexture->texCoord == 2
        && material.occlusionTexture && material.emissiveTexture,
        "all supported texture roles retain source texture and texCoord selectors");
    expect(std::none_of(result.diagnostics.begin(), result.diagnostics.end(), [](const std::string& item) {
            return item.find("KHR_texture_transform") != std::string::npos;
        }) && std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const std::string& item) {
            return item.find("VENDOR_material_note") != std::string::npos;
        }), "supported texture transform metadata is retained and unsupported used extensions are diagnosed");

    TempProject defaultSamplerProject;
    writeBytes(defaultSamplerProject.root / "Assets/Models/albedo.png", {137, 80, 78, 71, 1, 2, 3});
    auto defaultSamplerDocument = materialDocument("albedo.png", geometry);
    replaceOnce(defaultSamplerDocument,
        "\"samplers\":[{\"magFilter\":9728,\"minFilter\":9729,\"wrapS\":33648,\"wrapT\":33071}]",
        "\"samplers\":[{}]");
    writeText(defaultSamplerProject.root / "Assets/Models/default-sampler.gltf", defaultSamplerDocument);
    const auto defaultSamplerImport = render3d::GltfImporter::importFile(defaultSamplerProject.root,
        "Assets/Models/default-sampler.gltf");
    expect(defaultSamplerImport && defaultSamplerImport.value().samplers.size() == 1
        && defaultSamplerImport.value().samplers[0].wrapS == render3d::GltfSamplerWrap::Repeat
        && defaultSamplerImport.value().samplers[0].wrapT == render3d::GltfSamplerWrap::Repeat
        && !defaultSamplerImport.value().samplers[0].magFilter
        && !defaultSamplerImport.value().samplers[0].minFilter,
        "sampler default wrap modes and implementation-selected filters are retained explicitly");

    const auto cacheHit = render3d::GltfImporter::importFile(project.root, "Assets/Models/materials.gltf");
    expect(cacheHit && cacheHit.value().cacheHit && cacheHit.value().materials.size() == 1
        && cacheHit.value().samplers.size() == 1
        && cacheHit.value().materials[0].baseColorTexture
        && cacheHit.value().materials[0].baseColorTexture->transform.has_value()
        && cacheHit.value().meshes[0].primitiveMaterialIndices[0] == std::optional<std::uint32_t>(0),
        "cache hit reconstructs sampler, transform and material metadata from source");

    writeBytes(imagePath, {137, 80, 78, 71, 9, 8, 7});
    const auto changedImage = render3d::GltfImporter::importFile(project.root, "Assets/Models/materials.gltf");
    expect(changedImage && !changedImage.value().cacheHit
        && changedImage.value().contentHash != result.contentHash,
        "external image byte changes invalidate the import content digest");

    TempProject missingProject;
    writeText(missingProject.root / "Assets/Models/missing-image.gltf",
        materialDocument("late.png", geometry));
    const auto missingImage = render3d::GltfImporter::importFile(missingProject.root, "Assets/Models/missing-image.gltf");
    expect(missingImage && missingImage.value().images.size() == 1 && !missingImage.value().images[0].available
        && std::any_of(missingImage.value().diagnostics.begin(), missingImage.value().diagnostics.end(), [](const std::string& item) {
            return item.find("missing local file") != std::string::npos;
        }), "missing texture image is preserved and diagnosed without dropping the material slot");
    writeBytes(missingProject.root / "Assets/Models/late.png", {1, 2, 3});
    const auto restoredImage = render3d::GltfImporter::importFile(missingProject.root, "Assets/Models/missing-image.gltf");
    expect(restoredImage && restoredImage.value().images[0].available
        && restoredImage.value().contentHash != missingImage.value().contentHash,
        "restoring a missing referenced image changes freshness state");

    TempProject embeddedProject;
    writeText(embeddedProject.root / "Assets/Models/embedded-image.gltf",
        materialDocument("data:image/png;base64," + base64({1, 2, 3, 4}), geometry));
    const auto embeddedImage = render3d::GltfImporter::importFile(embeddedProject.root,
        "Assets/Models/embedded-image.gltf");
    expect(embeddedImage && embeddedImage.value().images.size() == 1
        && embeddedImage.value().images[0].available && !embeddedImage.value().images[0].sourceHash.empty(),
        "embedded image bytes retain a bounded source reference and digest");

    TempProject remoteProject;
    writeText(remoteProject.root / "Assets/Models/remote-image.gltf",
        materialDocument("https://example.invalid/texture.png", geometry));
    const auto remoteImage = render3d::GltfImporter::importFile(remoteProject.root, "Assets/Models/remote-image.gltf");
    expect(remoteImage && remoteImage.value().images[0].sourceKind == render3d::GltfImageSourceKind::Unsupported
        && !remoteImage.value().images[0].available
        && std::any_of(remoteImage.value().diagnostics.begin(), remoteImage.value().diagnostics.end(), [](const std::string& item) {
            return item.find("not fetched") != std::string::npos;
        }), "remote image URI is retained as a diagnostic and never fetched");

    TempProject unsafeProject;
    writeText(unsafeProject.root / "Assets/Models/traversal-image.gltf",
        materialDocument("../../outside.png", geometry));
    expect(!render3d::GltfImporter::importFile(unsafeProject.root, "Assets/Models/traversal-image.gltf"),
        "image URI traversal outside Assets is rejected");

    TempProject noMaterialProject;
    writeText(noMaterialProject.root / "Assets/Models/no-material.gltf", document(
        "data:application/octet-stream;base64," + base64(geometry), geometry.size()));
    const auto noMaterial = render3d::GltfImporter::importFile(noMaterialProject.root, "Assets/Models/no-material.gltf");
    expect(noMaterial && noMaterial.value().materials.empty()
        && noMaterial.value().meshes[0].primitiveMaterialIndices.size() == 1
        && !noMaterial.value().meshes[0].primitiveMaterialIndices[0],
        "unassigned primitives retain an explicit empty material mapping");
}

void testSceneMeshSourceMaterialResolution()
{
    using namespace astro;
    TempProject project;
    const auto geometry = triangleBytes();
    writeBytes(project.root / "Assets/Models/albedo.png", {137, 80, 78, 71, 1, 2, 3});
    auto source = materialDocument("albedo.png", geometry);
    replaceOnce(source, "\"texCoord\":1", "\"texCoord\":0");
    writeText(project.root / "Assets/Models/source-material.gltf", source);
    auto registryResult = assets::AssetRegistry::open(project.root);
    expect(registryResult.hasValue(), "open registry for source material resolution");
    if (!registryResult) return;
    auto registry = std::move(registryResult).value();
    const auto imported = render3d::GltfImporter::importAndRegister(registry,
        std::filesystem::path("Assets/Models/source-material.gltf"));
    expect(imported.hasValue(), "import/register model with a source base-color material");
    if (!imported) return;

    scene::MeshRendererComponent component;
    component.id = 1;
    component.model = imported.value().modelAssetId;
    const auto resolvedSource = render3d::resolveMeshRenderer(registry, component);
    expect(resolvedSource && resolvedSource.value().sourceMaterialIndex == std::optional<std::uint32_t>(0)
        && resolvedSource.value().sourceMaterial.has_value()
        && resolvedSource.value().sourceMaterial->values.baseColorFactor
            == std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.0f}
        && resolvedSource.value().sourceMaterial->hasBaseColorTexture
        && resolvedSource.value().sourceMaterial->baseColorTexCoord == 0
        && resolvedSource.value().sourceMaterial->baseColorSampler
        && resolvedSource.value().sourceMaterial->baseColorSampler->wrapS == render3d::GltfSamplerWrap::MirroredRepeat
        && resolvedSource.value().sourceMaterial->baseColorTransform
        && std::abs(resolvedSource.value().sourceMaterial->baseColorTransform->scaleV - 3.0f) < 1e-6f
        && resolvedSource.value().sourceMaterial->baseColorImageAvailable
        && resolvedSource.value().sourceMaterial->baseColorImagePath
            == project.root / "Assets/Models/albedo.png",
        "without a project override resolve the primitive source factor, UV0 binding and confined external image path");

    std::filesystem::create_directories(project.root / "Assets/Materials");
    const auto materialText = assets::serializeMaterialAsset(assets::MaterialAssetData{});
    expect(materialText.hasValue(), "serialize a project override for source material precedence");
    if (!materialText) return;
    writeText(project.root / "Assets/Materials/override.astro-material", materialText.value());
    const auto registered = registry.registerMaterial(std::filesystem::path("Materials/override.astro-material"));
    expect(registered.hasValue(), "register project material for source precedence resolution");
    if (!registered) return;
    component.materialOverride = registered.value().id;
    component.schemaVersion = 2;
    const auto resolvedOverride = render3d::resolveMeshRenderer(registry, component);
    expect(resolvedOverride && resolvedOverride.value().materialOverride == registered.value().id
        && resolvedOverride.value().sourceMaterialIndex == std::optional<std::uint32_t>(0)
        && resolvedOverride.value().sourceMaterial.has_value(),
        "retain source material metadata while representing the explicit project override separately");
}

void testDataUriAndCache()
{
    using astro::render3d::GltfImporter;
    TempProject project;
    const auto binary = triangleBytes();
    const auto source = document("data:application/octet-stream;base64," + base64(binary), binary.size());
    const auto path = project.root / "Assets" / "Models" / "triangle.gltf";
    writeText(path, source);
    auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/triangle.gltf"));
    expect(imported.hasValue(), "data-URI glTF triangle imports");
    if (!imported) return;
    expect(!imported.value().cacheHit && verifyTriangle(imported.value()), "first import returns geometry and writes cache");
    expect(std::filesystem::is_regular_file(imported.value().cachePath), "cache is beneath project Library");
    auto repeated = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/triangle.gltf"));
    expect(repeated && repeated.value().cacheHit && verifyTriangle(repeated.value()), "repeated import uses verified cache");
    expect(repeated && repeated.value().contentHash == imported.value().contentHash, "content cache key is stable");

    writeText(imported.value().cachePath, "corrupt cache");
    auto repaired = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/triangle.gltf"));
    expect(repaired && !repaired.value().cacheHit && verifyTriangle(repaired.value()), "corrupt regular cache is regenerated");
    auto afterRepair = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/triangle.gltf"));
    expect(afterRepair && afterRepair.value().cacheHit, "regenerated cache passes checksum");
    std::ifstream original(path, std::ios::binary);
    const std::string sourceAfter((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>());
    expect(sourceAfter == source, "source glTF bytes remain unchanged");

    const auto sentinel = project.root / "cache-sentinel.bin";
    writeText(sentinel, "preserve target bytes");
    std::error_code linkError;
    std::filesystem::remove(imported.value().cachePath, linkError);
    linkError.clear();
    std::filesystem::create_symlink(sentinel, imported.value().cachePath, linkError);
    if (!linkError)
    {
        auto unsafeCache = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/triangle.gltf"));
        expect(!unsafeCache, "cache symlink is rejected");
        std::ifstream sentinelFile(sentinel, std::ios::binary);
        const std::string sentinelAfter((std::istreambuf_iterator<char>(sentinelFile)), std::istreambuf_iterator<char>());
        expect(sentinelAfter == "preserve target bytes", "cache symlink target is not modified");
    }
}

void testNormalUvAttributesAndCache()
{
    using astro::render3d::GltfImporter;
    TempProject project;
    const auto bytes = normalUvTriangleBytes();
    const auto path = project.root / "Assets" / "Models" / "normal-uv.gltf";
    writeText(path, normalUvDocument(bytes));
    const auto imported = GltfImporter::importFile(project.root, "Assets/Models/normal-uv.gltf");
    expect(imported && !imported.value().cacheHit, "first import accepts matching FLOAT NORMAL and TEXCOORD_0 accessors");
    if (!imported) return;
    const auto& mesh = imported.value().meshes[0].primitives[0];
    expect(mesh.hasNormals() && mesh.hasTexCoords(), "imported mesh advertises both imported vertex channels");
    expect(mesh.vertices()[0].normal == (astro::render3d::Vec3{0, 0, 1})
        && mesh.vertices()[2].normal == (astro::render3d::Vec3{0, 0, 1}),
        "NORMAL values remain aligned to their positions");
    expect(mesh.vertices()[0].texCoord0 == (astro::render3d::Vec2{0, 0})
        && mesh.vertices()[1].texCoord0 == (astro::render3d::Vec2{1, 0})
        && mesh.vertices()[2].texCoord0 == (astro::render3d::Vec2{0, 1}),
        "TEXCOORD_0 values remain aligned to their positions");
    expect(imported.value().cachePath.filename().string().ends_with(".v2.astro-mesh-cache"),
        "new vertex channel data uses the version 2 derived geometry cache");

    const auto cached = GltfImporter::importFile(project.root, "Assets/Models/normal-uv.gltf");
    expect(cached && cached.value().cacheHit && cached.value().meshes[0].primitives[0].hasNormals()
        && cached.value().meshes[0].primitives[0].hasTexCoords()
        && cached.value().meshes[0].primitives[0].vertices()[1].normal == (astro::render3d::Vec3{0, 0, 1})
        && cached.value().meshes[0].primitives[0].vertices()[1].texCoord0 == (astro::render3d::Vec2{1, 0}),
        "verified cache round-trip retains channel presence and vertex data");

    TempProject normalizedProject;
    const auto byteUv = normalizedByteUvTriangleBytes();
    writeText(normalizedProject.root / "Assets/Models/byte-uv.gltf", normalizedByteUvDocument(byteUv, true));
    const auto normalizedImport = GltfImporter::importFile(normalizedProject.root, "Assets/Models/byte-uv.gltf");
    expect(normalizedImport && normalizedImport.value().meshes[0].primitives[0].hasTexCoords()
        && normalizedImport.value().meshes[0].primitives[0].vertices()[0].texCoord0 == (astro::render3d::Vec2{0, 1})
        && std::abs(normalizedImport.value().meshes[0].primitives[0].vertices()[1].texCoord0.x
            - (128.0f / 255.0f)) < 1.0e-6f,
        "normalized UNSIGNED_BYTE TEXCOORD_0 is converted to float coordinates");
}

void testGlbAndExternalBuffers()
{
    using astro::render3d::GltfImporter;
    {
        TempProject project;
        const auto path = project.root / "Assets" / "Models" / "triangle.glb";
        writeBytes(path, makeGlb(triangleBytes()));
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/triangle.glb"));
        expect(imported && verifyTriangle(imported.value()), "GLB embedded buffer imports");
    }
    {
        TempProject project;
        const auto binary = triangleBytes();
        const auto modelDirectory = project.root / "Assets" / std::filesystem::path(std::u8string(u8"Модели"));
        writeBytes(modelDirectory / std::filesystem::path(std::u8string(u8"треугольник.bin")), binary);
        writeText(modelDirectory / std::filesystem::path(std::u8string(u8"треугольник.gltf")), document("%D1%82%D1%80%D0%B5%D1%83%D0%B3%D0%BE%D0%BB%D1%8C%D0%BD%D0%B8%D0%BA.bin", binary.size()));
        auto imported = GltfImporter::importFile(project.root,
            std::filesystem::path(std::u8string(u8"Assets/Модели/треугольник.gltf")));
        if (!imported) std::cerr << "Unicode external buffer error: " << imported.error().message << '\n';
        expect(imported && verifyTriangle(imported.value()), "Unicode project path and confined external buffer import");
    }
}

void testRejectUnsafeAndUnsupported()
{
    using astro::render3d::GltfImporter;
    {
        TempProject project;
        writeText(project.root / "Assets/Models/traversal.gltf", document("../../outside.bin", 42));
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/traversal.gltf"));
        expect(!imported, "external URI traversal outside Assets is rejected");
    }
    {
        TempProject project;
        writeText(project.root / "Assets/Models/encoded-traversal.gltf", document("..%2F..%2Foutside.bin", 42));
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/encoded-traversal.gltf"));
        expect(!imported, "percent-encoded external URI traversal is rejected");
    }
    {
        TempProject project;
        writeText(project.root / "Assets/Models/remote.gltf", document("https://example.invalid/buffer.bin", 42));
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/remote.gltf"));
        expect(!imported, "remote buffer URI is rejected without network access");
    }
    {
        TempProject project;
        const auto binary = triangleBytes();
        writeText(project.root / "Assets/Models/required.gltf",
            document("data:application/octet-stream;base64," + base64(binary), binary.size(), "Triangle", true));
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/required.gltf"));
        expect(!imported, "unsupported required extension is rejected");
    }
    {
        TempProject project;
        const auto invalidIndices = triangleBytes({0, 1, 9});
        writeText(project.root / "Assets/Models/index.gltf",
            document("data:application/octet-stream;base64," + base64(invalidIndices), invalidIndices.size()));
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/index.gltf"));
        expect(!imported, "out-of-range triangle index is rejected");
    }
    {
        TempProject project;
        const auto binary = triangleBytes();
        auto unsupported = document("data:application/octet-stream;base64," + base64(binary), binary.size());
        replaceOnce(unsupported, "\"attributes\":{\"POSITION\":0}", "\"attributes\":{\"POSITION\":0,\"NORMAL\":0}");
        writeText(project.root / "Assets/Models/extra-attribute.gltf", unsupported);
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/extra-attribute.gltf"));
        expect(!imported, "unrepresented vertex attributes are rejected instead of silently discarded");
    }
    {
        TempProject project;
        auto mismatched = normalUvDocument(normalUvTriangleBytes());
        replaceOnce(mismatched, "\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"",
            "\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"");
        writeText(project.root / "Assets/Models/mismatched-normal.gltf", mismatched);
        expect(!GltfImporter::importFile(project.root, "Assets/Models/mismatched-normal.gltf"),
            "NORMAL vertex count must match POSITION");
    }
    {
        TempProject project;
        auto bytes = normalUvTriangleBytes();
        std::fill(bytes.begin() + 36, bytes.begin() + 48, 0);
        writeText(project.root / "Assets/Models/zero-normal.gltf", normalUvDocument(bytes));
        expect(!GltfImporter::importFile(project.root, "Assets/Models/zero-normal.gltf"),
            "zero-length NORMAL values are rejected");
    }
    {
        TempProject project;
        auto mismatched = normalUvDocument(normalUvTriangleBytes());
        replaceOnce(mismatched, "\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"",
            "\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"VEC2\"");
        writeText(project.root / "Assets/Models/mismatched-uv.gltf", mismatched);
        expect(!GltfImporter::importFile(project.root, "Assets/Models/mismatched-uv.gltf"),
            "TEXCOORD_0 vertex count must match POSITION");
    }
    {
        TempProject project;
        const auto byteUv = normalizedByteUvTriangleBytes();
        writeText(project.root / "Assets/Models/unnormalized-byte-uv.gltf", normalizedByteUvDocument(byteUv, false));
        expect(!GltfImporter::importFile(project.root, "Assets/Models/unnormalized-byte-uv.gltf"),
            "integer TEXCOORD_0 values must be normalized");
    }
    {
        TempProject project;
        writeText(project.root / "Assets/Models/malformed.gltf", "{not-json");
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/malformed.gltf"));
        expect(!imported, "malformed JSON is rejected");
        auto traversal = GltfImporter::importFile(project.root, std::filesystem::path("Assets/../outside.gltf"));
        expect(!traversal, "source path traversal is rejected");
    }
    {
        TempProject project;
        writeText(project.root / "Assets/Models/oversized-buffer.gltf", document("large.bin", 67u * 1024u * 1024u));
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/oversized-buffer.gltf"));
        expect(!imported, "oversized declared buffer is rejected before reading the file");
    }
    {
        TempProject project;
        const auto path = project.root / "Assets" / "Models" / "oversized.gltf";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.seekp(8u * 1024u * 1024u); file.put('x'); file.close();
        auto imported = GltfImporter::importFile(project.root, std::filesystem::path("Assets/Models/oversized.gltf"));
        expect(!imported, "source above the 8 MiB limit is rejected before parsing");
    }
}
}

int main()
{
    testAssetRegistryAndSceneReference();
    testExternalBufferFreshness();
    testMaterialMetadataAndImageReferences();
    testSceneMeshSourceMaterialResolution();
    testDataUriAndCache();
    testNormalUvAttributesAndCache();
    testGlbAndExternalBuffers();
    testRejectUnsafeAndUnsupported();
    if (failures != 0) { std::cerr << failures << " glTF integration assertion(s) failed\n"; return 1; }
    std::cout << "glTF/GLB importer integration checks passed\n";
    return 0;
}
