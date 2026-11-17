#include "astro/render3d/GltfImporter.hpp"
#include "astro/render3d/SceneMeshResolver.hpp"
#include "astro/scene/Scene.hpp"
#include "astro/scene/SceneFile.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
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
        && mesh.vertices()[1].position.x == 1.0f && mesh.vertices()[2].position.y == 1.0f;
}

void testAssetRegistryAndSceneReference()
{
    using namespace astro;
    TempProject project;
    auto registryResult = assets::AssetRegistry::open(project.root);
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
    expect(entity && scene::saveSceneFile(scenePath, authored).hasValue(), "serialize Scene v6 model reference");
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
        && resolvedGeometry.value().meshName == "Triangle Ω" && !resolvedGeometry.value().visible,
        "resolve a Scene MeshRenderer to its current primitive geometry and visibility state");

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
    unsupportedSchema.schemaVersion = 2;
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
    testDataUriAndCache();
    testGlbAndExternalBuffers();
    testRejectUnsafeAndUnsupported();
    if (failures != 0) { std::cerr << failures << " glTF integration assertion(s) failed\n"; return 1; }
    std::cout << "glTF/GLB importer integration checks passed\n";
    return 0;
}
