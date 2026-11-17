#include "CSharpPlayApproval.hpp"

#include <algorithm>

namespace astro::editor
{
namespace
{
core::Error invalid(std::string message)
{
    return {core::ErrorCode::ValidationError, std::move(message)};
}

std::string buildDiagnostic(const assets::ScriptBuildReport& report)
{
    if (!report.diagnostics.empty()) return report.diagnostics.front();
    for (const auto& binding : report.bindings)
        if (!binding.registered && !binding.diagnostic.empty()) return binding.diagnostic;
    return "The C# build or asset registration is not current";
}
}

core::Result<CSharpPlayApprovalSnapshot> CSharpPlayApproval::capture(
    const scene::Scene& scene, const std::filesystem::path& scenePath,
    const assets::AssetRegistry& registry)
{
    CSharpPlayApprovalSnapshot snapshot;
    snapshot.projectRoot = registry.projectRoot();
    snapshot.scenePath = scenePath;
    snapshot.sceneId = scene.id();
    snapshot.sceneName = scene.name();

    for (const auto& entity : scene.entities())
        for (const auto& behavior : entity.scriptBehaviors)
            if (behavior.enabled)
                snapshot.scripts.push_back({entity.id, behavior.id, behavior.scriptAsset, {}, {}, behavior.managedTypeName});

    std::sort(snapshot.scripts.begin(), snapshot.scripts.end(), [](const auto& left, const auto& right) {
        if (left.entity != right.entity) return left.entity < right.entity;
        return left.component < right.component;
    });
    if (snapshot.scripts.empty()) return core::Result<CSharpPlayApprovalSnapshot>::success(std::move(snapshot));

    const auto report = registry.inspectScriptBuild();
    if (report.freshness != assets::ScriptBuildFreshness::Current)
        return core::Result<CSharpPlayApprovalSnapshot>::failure(invalid(buildDiagnostic(report)));

    snapshot.sdkVersion = report.sdkVersion;
    snapshot.buildFingerprint = report.fingerprint;
    snapshot.assemblyRelativePath = report.assemblyRelativePath;
    snapshot.assemblySha256 = report.assemblySha256;
    snapshot.runtimeConfigRelativePath = report.runtimeConfigRelativePath;
    snapshot.runtimeConfigSha256 = report.runtimeConfigSha256;
    if (snapshot.buildFingerprint.empty() || snapshot.assemblySha256.empty()
        || snapshot.runtimeConfigSha256.empty())
        return core::Result<CSharpPlayApprovalSnapshot>::failure(invalid("The current C# build is missing approval identity hashes"));

    for (auto& script : snapshot.scripts)
    {
        const auto binding = std::find_if(report.bindings.begin(), report.bindings.end(), [&](const auto& item) {
            return item.assetId && *item.assetId == script.asset;
        });
        if (binding == report.bindings.end() || !binding->registered
            || binding->managedTypeName != script.managedTypeName)
            return core::Result<CSharpPlayApprovalSnapshot>::failure(invalid(
                binding == report.bindings.end() ? "An enabled ScriptBehavior is not registered in the current C# build"
                                                  : (binding->diagnostic.empty() ? "ScriptBehavior type does not match its registered managed type"
                                                                                 : binding->diagnostic)));

        const auto* reference = registry.findScript(script.asset);
        if (!reference || reference->sourceRelativePath != binding->sourceRelativePath
            || reference->sourceSha256 != binding->sourceSha256
            || reference->managedTypeName != script.managedTypeName)
            return core::Result<CSharpPlayApprovalSnapshot>::failure(invalid("ScriptBehavior asset metadata changed; refresh registration before Play"));
        const auto resolved = registry.resolveScript(script.asset);
        if (!resolved)
            return core::Result<CSharpPlayApprovalSnapshot>::failure(resolved.error());
        if (reference->assemblyRelativePath != report.assemblyRelativePath
            || reference->runtimeConfigRelativePath != report.runtimeConfigRelativePath)
            return core::Result<CSharpPlayApprovalSnapshot>::failure(invalid("ScriptBehavior output mapping differs from the current C# build"));

        script.sourceRelativePath = binding->sourceRelativePath;
        script.sourceSha256 = binding->sourceSha256;
    }
    return core::Result<CSharpPlayApprovalSnapshot>::success(std::move(snapshot));
}

core::Result<void> CSharpPlayApproval::approve(const CSharpPlayApprovalSnapshot& displayed,
    const CSharpPlayApprovalSnapshot& current)
{
    revoke();
    if (displayed.scripts.empty() || displayed != current)
        return core::Result<void>::failure(invalid("The enabled C# script set or build changed after the approval prompt was shown"));
    if (current.projectRoot.empty() || current.sceneId == 0 || current.buildFingerprint.empty()
        || current.assemblySha256.empty() || current.runtimeConfigSha256.empty())
        return core::Result<void>::failure(invalid("The approval snapshot is missing project, scene, or build identity"));
    for (const auto& script : current.scripts)
        if (script.entity == 0 || script.component == 0 || script.asset.value == 0
            || script.sourceRelativePath.empty() || script.sourceSha256.empty() || script.managedTypeName.empty())
            return core::Result<void>::failure(invalid("The approval snapshot contains an incomplete ScriptBehavior binding"));
    approved_ = current;
    return core::Result<void>::success();
}

bool CSharpPlayApproval::isApprovedFor(const CSharpPlayApprovalSnapshot& current) const noexcept
{
    return approved_ && *approved_ == current;
}

const CSharpPlayApprovalSnapshot* CSharpPlayApproval::snapshot() const noexcept
{
    return approved_ ? &*approved_ : nullptr;
}
}
