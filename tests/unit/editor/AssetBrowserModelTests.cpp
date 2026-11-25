#include "AssetBrowserModel.hpp"

#include "astro/assets/MaterialAsset.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool writeText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}
}

int main()
{
    using namespace astro::editor;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("AstroEngine-AssetBrowser-" + std::to_string(unique));
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};

    std::filesystem::create_directories(root);
    auto opened = astro::assets::AssetRegistry::open(root);
    check(opened.hasValue(), "temporary project opens for registry-backed catalog tests");
    if (!opened) return 1;
    auto registry = std::move(opened).value();

    const auto imagePath = std::filesystem::path("Assets/Textures/texture.png");
    std::filesystem::create_directories((root / imagePath).parent_path());
    std::filesystem::copy_file(std::filesystem::path(ASTRO_EDITOR_ASSET_BROWSER_FIXTURE_DIR)
            / "Assets" / "Textures" / "uv-checker.png",
        root / imagePath, std::filesystem::copy_options::overwrite_existing);
    const auto image = registry.importImage(imagePath.lexically_relative("Assets"));
    if (!image)
    {
        check(false, image.error().message.c_str());
        return 1;
    }
    check(true, "image fixture is registered from a temporary project copy");

    const auto materialText = astro::assets::serializeMaterialAsset(astro::assets::MaterialAssetData{});
    check(materialText.hasValue(), "default material can be serialized for catalog fixture");
    if (materialText)
    {
        const auto materialPath = std::filesystem::path(std::u8string(u8"Assets/Materials/материал.astro-material"));
        check(writeText(root / materialPath, materialText.value()), "UTF-8 material fixture is written");
        const auto registered = registry.registerMaterial(materialPath.lexically_relative("Assets"));
        if (!registered)
        {
            check(false, registered.error().message.c_str());
            return 1;
        }
        check(true, "material fixture is registered");
    }

    const auto modelPath = std::filesystem::path(std::u8string(u8"Assets/Models/модель.gltf"));
    check(writeText(root / modelPath, "{}"), "UTF-8 model fixture is written");
    const auto model = registry.registerModel(modelPath.lexically_relative("Assets"), std::string(64, 'a'));
    if (!model)
    {
        check(false, model.error().message.c_str());
        return 1;
    }
    check(true, "model fixture is registered without importer side effects");

    const auto scriptPath = std::filesystem::path(std::u8string(u8"Scripts/логика.cs"));
    const auto assemblyPath = std::filesystem::path("Library/Scripts/AssetBrowser.Tests.dll");
    const auto runtimeConfigPath = std::filesystem::path("Library/Scripts/AssetBrowser.Tests.runtimeconfig.json");
    check(writeText(root / scriptPath, "public class AssetBrowserTests {}"), "UTF-8 script fixture is written");
    check(writeText(root / assemblyPath, "metadata fixture"), "script assembly metadata fixture is written");
    check(writeText(root / runtimeConfigPath, "{}"), "runtime config metadata fixture is written");
    const auto script = registry.registerScript(scriptPath, "AssetBrowser.Tests", assemblyPath, runtimeConfigPath);
    if (!script)
    {
        check(false, script.error().message.c_str());
        return 1;
    }
    check(true, "script fixture is registered without execution");

    const auto entries = buildAssetBrowserCatalog(registry);
    check(entries.size() == 4, "catalog lists every registered image, model, material and script fixture");
    check(std::is_sorted(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    }), "catalog order is deterministic by AssetId");

    const auto all = filterAssetBrowserCatalog(entries, {}, AssetBrowserTypeFilter::All);
    check(all.size() == 4, "All filter returns every registered type");
    const auto images = filterAssetBrowserCatalog(entries, {}, AssetBrowserTypeFilter::Images);
    check(images.size() == 1 && entries[images.front()].kind == AssetBrowserKind::Image,
        "type filter returns only images");
    const auto materials = filterAssetBrowserCatalog(entries, {}, AssetBrowserTypeFilter::Materials);
    check(materials.size() == 1 && entries[materials.front()].kind == AssetBrowserKind::Material,
        "type filter returns only materials");
    const auto russianScriptSearch = filterAssetBrowserCatalog(entries,
        std::string_view(reinterpret_cast<const char*>(u8"логика"), sizeof(u8"логика") - 1),
        AssetBrowserTypeFilter::All);
    const auto russianMaterialSearch = filterAssetBrowserCatalog(entries,
        std::string_view(reinterpret_cast<const char*>(u8"материал"), sizeof(u8"материал") - 1),
        AssetBrowserTypeFilter::All);
    check(russianScriptSearch.size() == 1 && entries[russianScriptSearch.front()].kind == AssetBrowserKind::Script
        && russianMaterialSearch.size() == 1 && entries[russianMaterialSearch.front()].kind == AssetBrowserKind::Material,
        "UTF-8 substring search finds Cyrillic material and script paths");
    const auto idSearch = filterAssetBrowserCatalog(entries, std::to_string(script.value().id.value),
        AssetBrowserTypeFilter::Scripts);
    check(idSearch.size() == 1 && entries[idSearch.front()].id == script.value().id,
        "decimal AssetId search finds the selected registered record");
    const auto noMatches = filterAssetBrowserCatalog(entries, "missing", AssetBrowserTypeFilter::All);
    check(noMatches.empty(), "unmatched search yields no entries");

    if (failures == 0) std::cout << "Asset Browser catalog checks passed\n";
    return failures == 0 ? 0 : 1;
}
