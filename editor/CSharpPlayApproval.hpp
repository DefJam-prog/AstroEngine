#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/scene/Scene.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace astro::editor
{
struct CSharpPlayApprovalEntry
{
    scene::EntityId entity = 0;
    scene::ComponentId component = 0;
    assets::AssetId asset{};
    std::string sourceRelativePath;
    std::string sourceSha256;
    std::string managedTypeName;
    friend bool operator==(const CSharpPlayApprovalEntry&, const CSharpPlayApprovalEntry&) = default;
};

struct CSharpPlayApprovalSnapshot
{
    std::filesystem::path projectRoot;
    std::filesystem::path scenePath;
    scene::SceneId sceneId = 0;
    std::string sceneName;
    std::string sdkVersion;
    std::string buildFingerprint;
    std::string assemblyRelativePath;
    std::string assemblySha256;
    std::string runtimeConfigRelativePath;
    std::string runtimeConfigSha256;
    std::vector<CSharpPlayApprovalEntry> scripts;
    friend bool operator==(const CSharpPlayApprovalSnapshot&, const CSharpPlayApprovalSnapshot&) = default;
};

// Editor consent state only. This is not a security sandbox or host-load API.
class CSharpPlayApproval final
{
public:
    [[nodiscard]] static core::Result<CSharpPlayApprovalSnapshot> capture(
        const scene::Scene& scene, const std::filesystem::path& scenePath,
        const assets::AssetRegistry& registry);

    [[nodiscard]] core::Result<void> approve(const CSharpPlayApprovalSnapshot& displayed,
        const CSharpPlayApprovalSnapshot& current);
    [[nodiscard]] bool isApprovedFor(const CSharpPlayApprovalSnapshot& current) const noexcept;
    [[nodiscard]] bool approved() const noexcept { return approved_.has_value(); }
    [[nodiscard]] const CSharpPlayApprovalSnapshot* snapshot() const noexcept;
    void revoke() noexcept { approved_.reset(); }

private:
    std::optional<CSharpPlayApprovalSnapshot> approved_;
};
}
