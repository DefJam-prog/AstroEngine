#pragma once

#include "astro/core/Result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace astro::editor
{
struct CSharpScriptBinding final
{
    std::string sourcePath;
    std::string managedTypeName;
    friend bool operator==(const CSharpScriptBinding&, const CSharpScriptBinding&) = default;
};

struct CSharpBuildManifest final
{
    int schemaVersion{1};
    std::string sdkVersion;
    std::string targetFramework{"net10.0"};
    std::string assemblyName{"GameScripts"};
    std::vector<CSharpScriptBinding> scriptBindings;
    friend bool operator==(const CSharpBuildManifest&, const CSharpBuildManifest&) = default;
};

class CSharpBuildManifestFile final
{
public:
    [[nodiscard]] static std::filesystem::path manifestPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] static core::Result<CSharpBuildManifest> read(const std::filesystem::path& projectRoot);
    [[nodiscard]] static core::Result<void> write(const std::filesystem::path& projectRoot,
        const CSharpBuildManifest& manifest);
    [[nodiscard]] static core::Result<CSharpBuildManifest> parse(const std::string& utf8Json);
    [[nodiscard]] static core::Result<void> validate(const CSharpBuildManifest& manifest,
        const std::vector<std::string>& projectScriptPaths = {});
    [[nodiscard]] static std::string serialize(const CSharpBuildManifest& manifest);
};
}
