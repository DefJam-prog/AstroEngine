#include "CSharpBuildManifest.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
int failures{};
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::editor;
    const auto root = std::filesystem::temp_directory_path()
        / ("astro-csharp-manifest-" + std::to_string(static_cast<long long>(
            std::filesystem::file_time_type::clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(root / "Scripts");
    const auto sourcePath = std::filesystem::path(u8"Scripts/Игрок.cs");
    {
        std::ofstream script(root / sourcePath, std::ios::binary);
        script << "namespace Игра; public sealed class Игрок {}\n";
    }

    CSharpBuildManifest manifest;
    manifest.sdkVersion = "10.0.404";
    manifest.assemblyName = "ИграScripts";
    manifest.scriptBindings = {{"Scripts/Игрок.cs", "Игра.Игрок"}};
    check(!CSharpBuildManifestFile::validate(manifest), "assembly name validator rejects Unicode/pathless assembly names");
    manifest.assemblyName = "GameScripts";
    const auto json = CSharpBuildManifestFile::serialize(manifest);
    const auto parsed = CSharpBuildManifestFile::parse(json);
    check(parsed && parsed.value() == manifest, "manifest JSON round-trips UTF-8 binding data");
    const auto written = CSharpBuildManifestFile::write(root, manifest);
    if (!written) std::cerr << "Manifest write error: " << written.error().message << '\n';
    check(static_cast<bool>(written), "writes manifest through project settings path");
    const auto loaded = CSharpBuildManifestFile::read(root);
    if (!loaded) std::cerr << "Manifest read error: " << loaded.error().message << '\n';
    check(loaded && loaded.value() == manifest, "reads and validates manifest and existing source link");
    check(CSharpBuildManifestFile::manifestPath(root) == root / ".astro" / "csharp-build.json",
        "uses the documented project manifest path");

    check(!CSharpBuildManifestFile::parse("{\"schemaVersion\":1,\"schemaVersion\":1}"), "rejects duplicate JSON keys");
    check(!CSharpBuildManifestFile::parse("{\"schemaVersion\":1,\"sdkVersion\":\"10.0.404\",\"targetFramework\":\"net10.0\",\"assemblyName\":\"X\",\"unknown\":true}"),
        "rejects unknown manifest keys");
    check(!CSharpBuildManifestFile::parse("{\"schemaVersion\":2,\"sdkVersion\":\"10.0.404\",\"targetFramework\":\"net10.0\",\"assemblyName\":\"X\"}"),
        "rejects unsupported manifest schema version");
    check(!CSharpBuildManifestFile::parse("{\"schemaVersion\":1,\"sdkVersion\":\"10.0.404\",\"targetFramework\":\"net9.0\",\"assemblyName\":\"X\"}"),
        "rejects unsupported target framework");
    check(!CSharpBuildManifestFile::parse("{\"schemaVersion\":1,\"sdkVersion\":\"10.0.404\",\"targetFramework\":\"net10.0\",\"assemblyName\":\"../X\"}"),
        "rejects path characters in assembly name");

    auto duplicate = manifest;
    duplicate.scriptBindings.push_back(duplicate.scriptBindings.front());
    check(!CSharpBuildManifestFile::validate(duplicate), "rejects duplicate source bindings");
    auto external = manifest;
    external.scriptBindings[0].sourcePath = "Scripts/../outside.cs";
    check(!CSharpBuildManifestFile::validate(external), "rejects traversal in source bindings");
    auto absent = manifest;
    absent.scriptBindings[0].sourcePath = "Scripts/Missing.cs";
    check(!CSharpBuildManifestFile::write(root, absent), "rejects a binding whose script source does not exist");
    const auto preserved = CSharpBuildManifestFile::read(root);
    check(preserved && preserved.value() == manifest, "failed validation preserves the prior manifest");

    auto invalid = CSharpBuildManifestFile::manifestPath(root);
    { std::ofstream output(invalid, std::ios::binary | std::ios::trunc); output << "{broken"; }
    check(!CSharpBuildManifestFile::read(root), "reports malformed on-disk JSON instead of resetting it");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return failures == 0 ? 0 : 1;
}
