#include "CSharpMetadataDiagnostics.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#define NOMINMAX
#include <windows.h>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; return 1; \
} } while (false)

int main()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"Astro metadata проект Ω " + std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / L"Scripts");
    fs::create_directories(root / L"Library" / L"Scripts");
    struct Cleanup final { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};

    {
        std::ofstream source(root / L"Scripts" / L"Scene.cs", std::ios::binary);
        source << "namespace Game; public static class Scene {}\n";
    }
    {
        std::ofstream assembly(root / L"Library" / L"Scripts" / L"GameScripts.dll", std::ios::binary);
        assembly << "fixture bytes";
        std::ofstream config(root / L"Library" / L"Scripts" / L"GameScripts.runtimeconfig.json", std::ios::binary);
        config << "{}";
    }

    auto registryResult = astro::assets::AssetRegistry::open(root);
    if (!registryResult)
        std::cerr << "AssetRegistry open failed: " << registryResult.error().message << '\n';
    CHECK(registryResult);
    auto registry = std::move(registryResult).value();
    const auto registered = registry.registerScript(fs::path(L"Scripts") / L"Scene.cs", "Game.Пример",
        fs::path(L"Library") / L"Scripts" / L"GameScripts.dll",
        fs::path(L"Library") / L"Scripts" / L"GameScripts.runtimeconfig.json");
    CHECK(registered);

    astro::assets::ScriptBuildReport report;
    report.freshness = astro::assets::ScriptBuildFreshness::Current;
    report.assemblyRelativePath = "Library/Scripts/GameScripts.dll";
    astro::assets::ScriptBuildBindingStatus binding;
    binding.sourceRelativePath = "Scripts/Scene.cs";
    binding.managedTypeName = "Game.Пример";
    binding.assetId = registered.value().id;
    binding.registered = true;
    report.bindings.push_back(binding);

    const auto plan = astro::editor::prepareCSharpMetadataValidation(registry, report);
    CHECK(plan);
    CHECK(plan.value().assemblyPath == fs::canonical(root / L"Library" / L"Scripts" / L"GameScripts.dll"));
    CHECK(plan.value().managedTypes.size() == 1 && plan.value().managedTypes.front() == "Game.Пример");

    auto largePlan = plan.value();
    largePlan.managedTypes.clear();
    for (std::size_t index = 0; index < 256; ++index)
        largePlan.managedTypes.push_back("Game.Binding" + std::to_string(index));
    const auto batches = astro::editor::splitCSharpMetadataValidationPlan(largePlan);
    CHECK(batches && batches.value().size() == 4);
    for (const auto& batch : batches.value()) CHECK(batch.managedTypes.size() <= 64);

    largePlan.managedTypes.assign(33, std::string(512, 'T'));
    const auto byteBoundedBatches = astro::editor::splitCSharpMetadataValidationPlan(largePlan);
    CHECK(byteBoundedBatches && byteBoundedBatches.value().size() == 3);
    for (const auto& batch : byteBoundedBatches.value())
    {
        std::size_t typeNameBytes = 0;
        for (const auto& typeName : batch.managedTypes) typeNameBytes += typeName.size();
        CHECK(batch.managedTypes.size() <= 64 && typeNameBytes <= 8192);
    }

    largePlan.managedTypes.assign(257, "Game.Overflow");
    CHECK(!astro::editor::splitCSharpMetadataValidationPlan(largePlan));
    largePlan.managedTypes.assign(1, std::string("Game.Bad\x01Type", 13));
    CHECK(!astro::editor::splitCSharpMetadataValidationPlan(largePlan));

    auto stale = report;
    stale.freshness = astro::assets::ScriptBuildFreshness::Stale;
    CHECK(!astro::editor::prepareCSharpMetadataValidation(registry, stale));

    auto traversal = report;
    traversal.assemblyRelativePath = "Library/Scripts/../../outside.dll";
    CHECK(!astro::editor::prepareCSharpMetadataValidation(registry, traversal));

    auto unregistered = report;
    unregistered.bindings.front().registered = false;
    unregistered.bindings.front().assetId.reset();
    CHECK(!astro::editor::prepareCSharpMetadataValidation(registry, unregistered));

    const auto outside = root / L"outside.dll";
    { std::ofstream file(outside, std::ios::binary); file << "outside"; }
    const auto junctionCandidate = root / L"Library" / L"Scripts" / L"linked.dll";
    fs::create_symlink(outside, junctionCandidate, ignored);
    if (!ignored)
    {
        auto linked = report;
        linked.assemblyRelativePath = "Library/Scripts/linked.dll";
        CHECK(!astro::editor::prepareCSharpMetadataValidation(registry, linked));
    }

    return 0;
}
