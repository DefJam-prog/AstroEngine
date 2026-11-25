#include "astro/assets/AssetRegistry.hpp"
#include "astro/assets/AssetBuild.hpp"
#include "CSharpPlayApproval.hpp"
#include "astro/scene/Scene.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

void appendBe32(std::vector<unsigned char>& output, const std::uint32_t value)
{
    output.push_back(static_cast<unsigned char>(value >> 24));
    output.push_back(static_cast<unsigned char>(value >> 16));
    output.push_back(static_cast<unsigned char>(value >> 8));
    output.push_back(static_cast<unsigned char>(value));
}

std::uint32_t crc32(const unsigned char* data, const std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void addChunk(std::vector<unsigned char>& png, const std::array<char, 4> type, const std::vector<unsigned char>& data)
{
    appendBe32(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    for (const auto character : type) png.push_back(static_cast<unsigned char>(character));
    png.insert(png.end(), data.begin(), data.end());
    appendBe32(png, crc32(png.data() + start, png.size() - start));
}

std::vector<unsigned char> makePng(const std::uint32_t width, const std::uint32_t height)
{
    std::vector<unsigned char> png{137,80,78,71,13,10,26,10};
    std::vector<unsigned char> ihdr;
    appendBe32(ihdr, width); appendBe32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    addChunk(png, {'I','H','D','R'}, ihdr);

    std::vector<unsigned char> raw;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) raw.insert(raw.end(), {255, 0, 0, 255});
    }
    std::vector<unsigned char> zlib{0x78, 0x01, 0x01};
    const auto length = static_cast<std::uint16_t>(raw.size());
    zlib.push_back(static_cast<unsigned char>(length)); zlib.push_back(static_cast<unsigned char>(length >> 8));
    const auto inverse = static_cast<std::uint16_t>(~length);
    zlib.push_back(static_cast<unsigned char>(inverse)); zlib.push_back(static_cast<unsigned char>(inverse >> 8));
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (const auto value : raw) { a = (a + value) % 65521; b = (b + a) % 65521; }
    appendBe32(zlib, (b << 16) | a);
    addChunk(png, {'I','D','A','T'}, zlib);
    addChunk(png, {'I','E','N','D'}, {});
    return png;
}

std::vector<unsigned char> makeJpeg(const std::uint16_t width, const std::uint16_t height)
{
    return {0xFF,0xD8, 0xFF,0xC0, 0x00,0x11, 0x08,
        static_cast<unsigned char>(height >> 8), static_cast<unsigned char>(height),
        static_cast<unsigned char>(width >> 8), static_cast<unsigned char>(width),
        0x03, 0x01,0x11,0x00, 0x02,0x11,0x00, 0x03,0x11,0x00,
        0xFF,0xDA, 0x00,0x08, 0x01,0x01,0x00,0x00,0x3F,0x00, 0x00, 0xFF,0xD9};
}

void writeBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("failed to write asset fixture");
}

std::vector<unsigned char> readBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string hashBytes(const std::vector<unsigned char>& bytes)
{
    return astro::assets::sha256Hex(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    writeBytes(path, std::vector<unsigned char>(text.begin(), text.end()));
}

std::filesystem::path utf8Path(const std::u8string_view value)
{
    return std::filesystem::path(std::u8string(value));
}
}

int main()
{
    using namespace astro::assets;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("Astro Asset Tests " + std::to_string(unique));
    std::error_code error;
    const bool created = std::filesystem::create_directory(root, error);
    check(created && !error, "create a unique test-owned project directory");
    if (!created || error) return EXIT_FAILURE;

    try
    {
        auto opened = AssetRegistry::open(root);
        check(opened.hasValue(), "open an asset registry for a project");
        if (!opened) throw std::runtime_error("AssetRegistry::open failed");
        auto registry = std::move(opened).value();
        const auto pngPath = root / "Assets" / utf8Path(u8"картинка.png");
        const auto jpegPath = root / "Assets" / "background.jpeg";
        const auto textPath = root / "Assets" / "notes.txt";
        const auto invalidPath = root / "Assets" / "bad.png";
        const auto invalidJpegPath = root / "Assets" / "bad.jpg";
        const auto initialPng = makePng(1, 1);
        const auto jpeg = makeJpeg(3, 2);
        writeBytes(pngPath, initialPng);
        writeBytes(jpegPath, jpeg);
        writeBytes(textPath, {'n','o','t',' ','a','n',' ','i','m','a','g','e'});
        auto brokenPng = initialPng; brokenPng[29] ^= 0x01;
        writeBytes(invalidPath, brokenPng);
        writeBytes(invalidJpegPath, {0xFF,0xD8,0xFF,0xD9});
        const auto originalSourceBytes = readBytes(pngPath);

        auto imported = registry.importImage(utf8Path(u8"картинка.png"));
        if (!imported) std::cerr << "PNG import error: " << imported.error().message << '\n';
        check(imported && imported.value().id.value != 0 && imported.value().width == 1 && imported.value().height == 1,
            "import a UTF-8 PNG and extract its dimensions");
        if (imported)
        {
            const auto reference = imported.value();
            AssetDependencyGraph graph;
            const auto jpegReference = registry.importImage(std::filesystem::path("background.jpeg"));
            check(jpegReference && graph.addDependency(reference.id, jpegReference.value().id), "record a direct asset dependency");
            check(!graph.addDependency(jpegReference.value().id, reference.id), "reject a dependency cycle");
            const auto graphPath = root / ".astro" / "assets.dependencies";
            check(static_cast<bool>(graph.save(graphPath)), "persist dependency graph");
            auto loadedGraph = AssetDependencyGraph::load(graphPath);
            check(loadedGraph && loadedGraph.value().closure({reference.id})
                && loadedGraph.value().closure({reference.id}).value().size() == 2, "load graph and resolve transitive closure");
            if (!loadedGraph) throw std::runtime_error("dependency graph fixture failed to load");
            auto package = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
            check(package && package.value().assetCount == 2 && !package.value().reused, "build platform-tagged package of reachable assets");
            if (package) {
                const auto firstHash = package.value().sha256;
                auto again = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
                check(again && again.value().reused && again.value().sha256 == firstHash, "reuse identical derived package");
                writeBytes(package.value().path, {'b','a','d'});
                auto repaired = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
                check(repaired && !repaired.value().reused && readBytes(repaired.value().path).size() > 3, "replace a corrupted derived package");
                auto otherTarget = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Android);
                check(otherTarget && otherTarget.value().sha256 != firstHash, "target label participates in package bytes");
            }
            check(reference.sha256.size() == 64, "record a SHA-256 content digest");
            check(std::filesystem::exists(registry.cachedPath(reference.id)), "write encoded image to project cache");
            check(readBytes(registry.cachedPath(reference.id)) == originalSourceBytes, "cache preserves source bytes");
            check(readBytes(pngPath) == originalSourceBytes, "import does not modify the source image");
            auto repeated = registry.importImage(utf8Path(u8"картинка.png"));
            check(repeated && repeated.value().id == reference.id && registry.entries().size() == 2,
                "repeat import reuses the stable asset ID");

            writeBytes(pngPath, makePng(2, 1));
            check(!registry.importImage(utf8Path(u8"картинка.png")),
                "changed source requires explicit reimport");
            auto reimported = registry.reimport(reference.id);
            check(reimported && reimported.value().id == reference.id && reimported.value().width == 2
                && reimported.value().sha256 != reference.sha256,
                "reimport updates metadata and content while preserving Asset ID");
            if (reimported && package)
            {
                auto rebuilt = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
                check(rebuilt && rebuilt.value().sha256 != package.value().sha256
                    && loadedGraph.value().dependenciesOf(reference.id) == std::vector<AssetId>{jpegReference.value().id},
                    "reimport preserves graph references and rebuilds changed package bytes");
            }
            check(readBytes(pngPath) == makePng(2, 1), "reimport leaves updated source bytes untouched");
            if (reimported)
            {
                const auto cached = registry.cachedPath(reference.id);
                const auto symlinkTarget = cached.parent_path() / "cache-link-check.png";
                std::error_code symlinkError;
                std::filesystem::create_symlink(pngPath, symlinkTarget, symlinkError);
                if (!symlinkError)
                {
                    std::filesystem::remove(cached, error);
                    std::filesystem::create_symlink(pngPath, cached, symlinkError);
                    if (!symlinkError)
                    {
                        check(!registry.reimport(reference.id), "reject cache symlink instead of reading outside cache artifact");
                        std::filesystem::remove(cached, error);
                    }
                    std::filesystem::remove(symlinkTarget, error);
                }
                auto preparedCache = registry.reimport(reference.id);
                check(preparedCache.hasValue(), "recreate or validate cache after the symlink check");
                const auto safeCache = registry.cachedPath(reference.id);
                auto damaged = readBytes(safeCache);
                if (!damaged.empty())
                {
                    damaged.front() ^= 0x01; writeBytes(safeCache, damaged);
                    check(registry.reimport(reference.id).hasValue() && readBytes(safeCache) == readBytes(pngPath),
                        "reimport repairs a damaged cache artifact");
                }
                else check(false, "resolve the regular cache file before corruption test");
            }
            auto reopened = AssetRegistry::open(root);
            check(reopened && reopened.value().find(reference.id)
                && reopened.value().find(reference.id)->width == 2,
                "persist and reload asset IDs and metadata");
            if (reopened)
            {
                auto movedSource = pngPath; movedSource += ".temporarily-missing";
                std::filesystem::rename(pngPath, movedSource, error);
                const auto missing = reopened.value().missingAssets();
                check(!error && std::find(missing.begin(), missing.end(), reference.id) != missing.end(),
                    "report a registered asset whose source is missing");
                check(!reopened.value().reimport(reference.id), "reject reimport when source is missing");
                std::filesystem::rename(movedSource, pngPath, error);
                check(!error, "restore the exact temporary test source path");
            }
            const auto indexPath = root / ".astro" / "assets.index";
            auto indexBytes = readBytes(indexPath);
            std::vector<unsigned char> crlf;
            for (const auto character : indexBytes)
            {
                if (character == '\n') crlf.push_back('\r');
                crlf.push_back(character);
            }
            writeBytes(indexPath, crlf);
            auto crlfIndex = AssetRegistry::open(root);
            check(crlfIndex && crlfIndex.value().find(reference.id), "read the UTF-8 index with Windows CRLF endings");
        }

        auto importedJpeg = registry.importImage(std::filesystem::path("background.jpeg"));
        check(importedJpeg && importedJpeg.value().format == ImageFormat::Jpeg
            && importedJpeg.value().width == 3 && importedJpeg.value().height == 2,
            "import JPEG and read its frame dimensions");
        check(!registry.importImage(std::filesystem::path("../outside.png")), "reject path traversal outside Assets");
        check(!registry.importImage(std::filesystem::path("notes.txt")), "reject unsupported file extension");
        check(!registry.importImage(std::filesystem::path("bad.png")), "reject invalid PNG chunk checksum");
        check(!registry.importImage(std::filesystem::path("bad.jpg")), "reject JPEG without a frame header");
        check(!registry.reimport({999999}), "report a missing Asset ID");

        const auto scriptSource = root / utf8Path(u8"Scripts/Gameplay/Игрок.cs");
        const auto scriptOutput = root / "Library" / "Scripts";
        std::filesystem::create_directories(scriptSource.parent_path());
        std::filesystem::create_directories(scriptOutput);
        const std::string sourceText = "namespace Игра; public sealed class Игрок { }\n";
        const std::vector<unsigned char> sourceBytes(sourceText.begin(), sourceText.end());
        const auto assemblyPath = scriptOutput / "GameScripts.dll";
        const auto runtimeConfigPath = scriptOutput / "GameScripts.runtimeconfig.json";
        writeBytes(scriptSource, sourceBytes);
        writeBytes(assemblyPath, {'M','Z'});
        writeBytes(runtimeConfigPath, {'{','}','\n'});
        auto script = registry.registerScript(utf8Path(u8"Scripts/Gameplay/Игрок.cs"),
            "Игра.Игрок", std::filesystem::path("Library/Scripts/GameScripts.dll"),
            std::filesystem::path("Library/Scripts/GameScripts.runtimeconfig.json"));
        check(script && script.value().id.value > importedJpeg.value().id.value,
            "allocate a globally unique AssetId for a script after image assets");
        check(script && registry.findScript(script.value().id)
            && registry.find(script.value().id) == nullptr,
            "keep the script record typed and disjoint from image lookup");
        auto resolvedScript = script ? registry.resolveScript(script.value().id)
            : astro::core::Result<ResolvedScriptAsset>::failure({astro::core::ErrorCode::NotFound, "script fixture failed"});
        check(resolvedScript && resolvedScript.value().managedTypeName == "Игра.Игрок"
            && resolvedScript.value().sourcePath == std::filesystem::canonical(scriptSource)
            && resolvedScript.value().assemblyPath == std::filesystem::canonical(assemblyPath)
            && resolvedScript.value().runtimeConfigPath == std::filesystem::canonical(runtimeConfigPath),
            "resolve the stable script AssetId to bounded source, assembly, runtimeconfig and UTF-8 type");
        check(!registry.registerScript(utf8Path(u8"Scripts/Gameplay/Игрок.cs"), "Игра. Другой",
                std::filesystem::path("Library/Scripts/../Scripts/GameScripts.dll"),
                std::filesystem::path("Library/Scripts/GameScripts.runtimeconfig.json")),
            "reject traversal in a managed assembly path");
        check(!registry.registerScript(utf8Path(u8"Scripts/Gameplay/Игрок.cs"), "Игра. Игрок",
                std::filesystem::path("Library/Scripts/GameScripts.dll"),
                std::filesystem::path("Library/Scripts/GameScripts.runtimeconfig.json")),
            "reject whitespace in a managed type name");
        if (script)
        {
            auto changedSource = sourceBytes;
            changedSource.push_back(' ');
            writeBytes(scriptSource, changedSource);
            auto staleScript = registry.resolveScript(script.value().id);
            check(!staleScript && staleScript.error().code == astro::core::ErrorCode::InvalidState,
                "refuse to resolve an assembly after its registered source changed");
            writeBytes(scriptSource, sourceBytes);
            std::filesystem::remove(assemblyPath, error);
            check(!registry.resolveScript(script.value().id), "diagnose a missing compiled script assembly");
            writeBytes(assemblyPath, {'M','Z'});
            auto reopenedScripts = AssetRegistry::open(root);
            check(reopenedScripts && reopenedScripts.value().findScript(script.value().id)
                && reopenedScripts.value().resolveScript(script.value().id),
                "round-trip script asset mapping while preserving its global AssetId");
        }
        auto updatedScript = registry.registerScript(utf8Path(u8"Scripts/Gameplay/Игрок.cs"),
            "Игра.ИгрокОбновлённый", std::filesystem::path("Library/Scripts/GameScripts.dll"),
            std::filesystem::path("Library/Scripts/GameScripts.runtimeconfig.json"));
        check(script && updatedScript && updatedScript.value().id == script.value().id
            && registry.resolveScript(script.value().id).value().managedTypeName == "Игра.ИгрокОбновлённый",
            "update the managed type binding without changing the script AssetId");

        const auto additionalScript = std::string("namespace Игра; public sealed class Ввод { }\n");
        const std::u8string additionalRelativeU8 = u8"Scripts/Gameplay/Ввод.cs";
        const std::string additionalRelative(reinterpret_cast<const char*>(additionalRelativeU8.data()), additionalRelativeU8.size());
        const auto additionalSource = root / utf8Path(additionalRelativeU8);
        writeText(additionalSource, additionalScript);
        const auto manifest = std::string("{\"schemaVersion\":1,\"sdkVersion\":\"10.0.404\",\"targetFramework\":\"net10.0\",\"assemblyName\":\"GameScripts\",\"scriptBindings\":[")
            + "{\"sourcePath\":\"Scripts/Gameplay/Игрок.cs\",\"managedTypeName\":\"Игра.ИгрокОбновлённый\"},"
            + "{\"sourcePath\":\"Scripts/Gameplay/Ввод.cs\",\"managedTypeName\":\"Игра.Ввод\"}]}\n";
        writeText(root / ".astro" / "csharp-build.json", manifest);
        const auto scriptSourceBytesForBuild = readBytes(scriptSource);
        const auto additionalBytes = readBytes(additionalSource);
        const auto assemblyBytes = readBytes(assemblyPath);
        const auto runtimeConfigBytes = readBytes(runtimeConfigPath);
        std::ostringstream buildState;
        buildState << "ASTRO_CSHARP_PROJECT_BUILD 1\n"
            << "FINGERPRINT " << std::string(64, 'a') << "\n"
            << "MANIFEST_SHA256 " << hashBytes(std::vector<unsigned char>(manifest.begin(), manifest.end())) << "\n"
            << "SDK_VERSION \"10.0.404\"\nTARGET_FRAMEWORK \"net10.0\"\nASSEMBLY_NAME \"GameScripts\"\n"
            << "ASSEMBLY_PATH \"Library/Scripts/GameScripts.dll\"\nASSEMBLY_SHA256 " << hashBytes(assemblyBytes) << "\n"
            << "RUNTIME_CONFIG_PATH \"Library/Scripts/GameScripts.runtimeconfig.json\"\nRUNTIME_CONFIG_SHA256 "
            << hashBytes(runtimeConfigBytes) << "\nSOURCE_COUNT 2\n"
            << "SOURCE " << std::quoted(std::string("Scripts/Gameplay/Игрок.cs")) << ' ' << hashBytes(scriptSourceBytesForBuild) << "\n"
            << "SOURCE " << std::quoted(additionalRelative) << ' ' << hashBytes(additionalBytes) << "\n"
            << "BINDING_COUNT 2\n"
            << "BINDING \"Scripts/Gameplay/Игрок.cs\" \"Игра.ИгрокОбновлённый\"\n"
            << "BINDING " << std::quoted(additionalRelative) << " \"Игра.Ввод\"\nEND\n";
        const auto buildStateText = buildState.str();
        writeText(scriptOutput / "asset-bindings.state", buildStateText);
        const auto buildStateJson = std::string("{\"schemaVersion\":2,\"assetStatePath\":\"Library/Scripts/asset-bindings.state\",\"assetStateSha256\":\"")
            + hashBytes(std::vector<unsigned char>(buildStateText.begin(), buildStateText.end())) + "\"}\n";
        writeText(scriptOutput / "build-state.json", buildStateJson);
        const auto buildReport = registry.inspectScriptBuild();
        if (buildReport.freshness != ScriptBuildFreshness::Current)
            for (const auto& diagnostic : buildReport.diagnostics) std::cerr << "C# handoff: " << diagnostic << '\n';
        check(buildReport.freshness == ScriptBuildFreshness::Current && buildReport.bindings.size() == 2
            && buildReport.requiresRegistration,
            "inspect a fresh C# build handoff and report an unregistered explicit binding");
        check(buildReport.bindings[1].diagnostic
                == "Managed type is selected but not registered in the project asset index",
            "explain which binding is missing from the asset registry");
        auto registeredBuild = registry.registerScriptsFromBuild();
        if (!registeredBuild) std::cerr << "C# handoff registration: " << registeredBuild.error().message << '\n';
        check(registeredBuild && registeredBuild.value().size() == 2
            && registeredBuild.value()[0].id == script.value().id
            && registry.findScript(registeredBuild.value()[1].id)
            && registry.inspectScriptBuild().freshness == ScriptBuildFreshness::Current
            && !registry.inspectScriptBuild().requiresRegistration,
            "explicitly register build bindings atomically while retaining stable script AssetIds");
        if (registeredBuild && !registeredBuild.value().empty())
        {
            astro::scene::Scene approvalScene(9001, "Consent fixture Ω");
            const auto actor = approvalScene.createEntity("Actor");
            const auto behavior = actor ? approvalScene.addScriptBehavior(actor.value(),
                registeredBuild.value()[0].id, registeredBuild.value()[0].managedTypeName) :
                astro::core::Result<astro::scene::ComponentId>::failure(
                    {astro::core::ErrorCode::InvalidState, "Could not create approval fixture entity"});
            const auto snapshot = behavior ? astro::editor::CSharpPlayApproval::capture(
                approvalScene, root / "Scenes" / "consent.astro-scene", registry) :
                astro::core::Result<astro::editor::CSharpPlayApprovalSnapshot>::failure(
                    {astro::core::ErrorCode::InvalidState, "Could not create approval fixture behavior"});
            astro::editor::CSharpPlayApproval approval;
            const auto granted = snapshot ? approval.approve(snapshot.value(), snapshot.value()) :
                astro::core::Result<void>::failure(snapshot.error());
            check(snapshot && snapshot.value().scripts.size() == 1
                && snapshot.value().scripts[0].sourceSha256 == registeredBuild.value()[0].sourceSha256
                && snapshot.value().assemblySha256 == buildReport.assemblySha256
                && snapshot.value().runtimeConfigSha256 == buildReport.runtimeConfigSha256
                && granted && approval.isApprovedFor(snapshot.value()),
                "capture and approve the exact registered ScriptBehavior against real build hashes");
        }
        auto mismatchedBinding = registry.registerScript(utf8Path(u8"Scripts/Gameplay/Игрок.cs"),
            "Игра.ИгрокСтарый", std::filesystem::path("Library/Scripts/GameScripts.dll"),
            std::filesystem::path("Library/Scripts/GameScripts.runtimeconfig.json"));
        const auto mismatchReport = registry.inspectScriptBuild();
        check(mismatchedBinding && mismatchReport.freshness == ScriptBuildFreshness::Current
            && !mismatchReport.bindings.empty() && !mismatchReport.bindings[0].registered
            && mismatchReport.bindings[0].diagnostic
                == "Registered managed type differs from the current build binding; register the current build",
            "diagnose a registered managed type that disagrees with the latest build manifest");
        const auto repairedBindings = registry.registerScriptsFromBuild();
        const auto repairedReport = registry.inspectScriptBuild();
        check(repairedBindings && repairedBindings.value().size() == 2
            && repairedBindings.value()[0].id == script.value().id
            && repairedReport.freshness == ScriptBuildFreshness::Current
            && !repairedReport.requiresRegistration && repairedReport.bindings[0].diagnostic.empty(),
            "explicitly synchronize a mismatched binding while preserving its AssetId");

        auto changedSourceForRegistry = sourceBytes;
        changedSourceForRegistry.push_back(' ');
        writeBytes(scriptSource, changedSourceForRegistry);
        const auto staleHashRegistration = registry.registerScript(utf8Path(u8"Scripts/Gameplay/Игрок.cs"),
            "Игра.ИгрокОбновлённый", std::filesystem::path("Library/Scripts/GameScripts.dll"),
            std::filesystem::path("Library/Scripts/GameScripts.runtimeconfig.json"));
        writeBytes(scriptSource, sourceBytes);
        const auto hashMismatchReport = registry.inspectScriptBuild();
        check(staleHashRegistration && hashMismatchReport.freshness == ScriptBuildFreshness::Current
            && !hashMismatchReport.bindings[0].registered
            && hashMismatchReport.bindings[0].diagnostic
                == "Registered source hash differs from the current build; register the current build",
            "diagnose a registered source hash that differs from the successful build");
        const auto hashRepair = registry.registerScriptsFromBuild();
        check(hashRepair && hashRepair.value()[0].id == script.value().id,
            "repair a stale source hash through the explicit build registration action");

        writeBytes(scriptOutput / "OldGeneration.dll", {'M','Z'});
        writeBytes(scriptOutput / "OldGeneration.runtimeconfig.json", {'{','}','\n'});
        const auto oldOutputRegistration = registry.registerScript(utf8Path(u8"Scripts/Gameplay/Игрок.cs"),
            "Игра.ИгрокОбновлённый", std::filesystem::path("Library/Scripts/OldGeneration.dll"),
            std::filesystem::path("Library/Scripts/OldGeneration.runtimeconfig.json"));
        const auto outputMismatchReport = registry.inspectScriptBuild();
        check(oldOutputRegistration && outputMismatchReport.freshness == ScriptBuildFreshness::Current
            && !outputMismatchReport.bindings[0].registered
            && outputMismatchReport.bindings[0].diagnostic
                == "Registered assembly output differs from the current build; register the current build",
            "diagnose a registered assembly generation that differs from the successful build");
        const auto outputRepair = registry.registerScriptsFromBuild();
        check(outputRepair && outputRepair.value()[0].id == script.value().id
            && !registry.inspectScriptBuild().requiresRegistration,
            "repair output path drift through explicit registration without changing the script AssetId");

        writeText(scriptOutput / "asset-bindings.state", buildStateText + " ");
        check(registry.inspectScriptBuild().freshness == ScriptBuildFreshness::Stale,
            "detect a handoff changed after the builder recorded its digest");
        writeText(scriptOutput / "asset-bindings.state", buildStateText);
        const auto syncedBuildReport = registry.inspectScriptBuild();
        check(syncedBuildReport.freshness == ScriptBuildFreshness::Current,
            "keep build freshness separate from AssetRegistry synchronization state");

        const auto legacyRoot = root / "legacy-v1";
        std::filesystem::create_directory(legacyRoot);
        auto legacyRegistry = AssetRegistry::open(legacyRoot);
        check(legacyRegistry.hasValue(), "create project fixture for v1 index migration");
        const auto legacyIndex = legacyRoot / ".astro" / "assets.index";
        const std::string legacyContents = "ASTRO_ASSETS 1\nNEXT 1\nEND\n";
        writeBytes(legacyIndex, std::vector<unsigned char>(legacyContents.begin(), legacyContents.end()));
        auto legacyOpened = AssetRegistry::open(legacyRoot);
        check(legacyOpened && readBytes(legacyIndex) == std::vector<unsigned char>(legacyContents.begin(), legacyContents.end()),
            "read an existing v1 asset index without rewriting it on open");
        std::filesystem::create_directories(legacyRoot / "Scripts");
        std::filesystem::create_directories(legacyRoot / "Library" / "Scripts");
        writeBytes(legacyRoot / "Scripts" / "Legacy.cs", {'c','l','a','s','s',' ','L','e','g','a','c','y',' ','{','}','\n'});
        writeBytes(legacyRoot / "Library" / "Scripts" / "Legacy.dll", {'M','Z'});
        writeBytes(legacyRoot / "Library" / "Scripts" / "Legacy.runtimeconfig.json", {'{','}','\n'});
        if (legacyOpened)
        {
            auto migrated = legacyOpened.value().registerScript("Scripts/Legacy.cs", "Legacy.Script",
                "Library/Scripts/Legacy.dll", "Library/Scripts/Legacy.runtimeconfig.json");
            const auto migratedBytes = readBytes(legacyIndex);
            check(migrated && migrated.value().id.value == 1
                && std::string(migratedBytes.begin(), migratedBytes.end()).starts_with("ASTRO_ASSETS 4\n")
                && AssetRegistry::open(legacyRoot).value().resolveScript({1}),
                "upgrade the shared index to v4 only on explicit script registration and resolve legacy project files");
        }
        const auto indexPath = root / ".astro" / "assets.index";
        writeBytes(indexPath, {'B','A','D','\n'});
        check(!AssetRegistry::open(root), "reject corrupt index without silently replacing it");
        check(readBytes(indexPath) == std::vector<unsigned char>{'B','A','D','\n'},
            "leave a corrupt user index unchanged for diagnosis");
    }
    catch (const std::exception& exception)
    {
        std::cerr << "UNEXPECTED: " << exception.what() << '\n';
        ++failures;
    }

    std::filesystem::remove_all(root, error);
    check(!error, "remove only the test-owned project directory");
    if (failures == 0)
    {
        std::cout << "All asset registry integration tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
