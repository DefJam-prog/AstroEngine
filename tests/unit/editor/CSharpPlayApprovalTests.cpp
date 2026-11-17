#include "CSharpPlayApproval.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("astro-csharp-approval-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    auto opened = assets::AssetRegistry::open(root);
    check(static_cast<bool>(opened), "open isolated temporary project registry");
    if (!opened) return 1;

    scene::Scene scene(101, "Approval Scene Ω");
    const auto entity = scene.createEntity("Actor");
    check(static_cast<bool>(entity), "create test entity");
    if (!entity) return 1;
    const auto disabled = scene.addScriptBehavior(entity.value(), {501}, "Game.Disabled", false);
    check(static_cast<bool>(disabled), "add disabled ScriptBehavior");
    auto empty = editor::CSharpPlayApproval::capture(scene, root / "Scenes" / "main.astro-scene", opened.value());
    check(empty && empty.value().scripts.empty(), "disabled scripts do not require approval or a build");

    const auto enabled = scene.addScriptBehavior(entity.value(), {502}, "Game.Actor", true);
    check(static_cast<bool>(enabled), "add enabled ScriptBehavior");
    auto missingBuild = editor::CSharpPlayApproval::capture(scene, {}, opened.value());
    check(!missingBuild, "enabled script fails closed when the project build is missing");

    editor::CSharpPlayApprovalSnapshot shown;
    shown.projectRoot = root;
    shown.scenePath = root / "Scenes" / "main.astro-scene";
    shown.sceneId = 101;
    shown.sceneName = "Approval Scene Ω";
    shown.buildFingerprint = "fingerprint-1";
    shown.assemblySha256 = "assembly-hash-1";
    shown.runtimeConfigSha256 = "runtime-hash-1";
    shown.scripts.push_back({entity.value(), enabled.value(), {502}, "Scripts/Actor.cs", "source-hash-1", "Game.Actor"});
    editor::CSharpPlayApproval gate;
    auto emptySnapshot = shown;
    emptySnapshot.scripts.clear();
    check(!gate.approve(emptySnapshot, emptySnapshot).hasValue(), "an empty script set cannot be approved");

    auto validShown = shown;
    validShown.scripts.push_back({entity.value(), disabled.value(), {501}, "Scripts/Disabled.cs", "source-hash-2", "Game.Disabled"});
    auto approved = gate.approve(validShown, validShown);
    check(approved && gate.isApprovedFor(validShown), "direct approval binds the exact displayed set");

    auto changedSource = validShown;
    changedSource.scripts.front().sourceSha256 = "changed-source-hash";
    check(!gate.approve(validShown, changedSource) && !gate.approved(), "source hash change rejects and clears approval");
    approved = gate.approve(validShown, validShown);
    auto changedSet = validShown;
    changedSet.scripts.pop_back();
    check(!gate.approve(validShown, changedSet) && !gate.approved(), "changed enabled script set rejects approval");
    approved = gate.approve(validShown, validShown);
    auto changedBuild = validShown;
    changedBuild.assemblySha256 = "changed-assembly-hash";
    check(approved && !gate.isApprovedFor(changedBuild), "output hash change invalidates the approval snapshot");
    gate.revoke();
    check(!gate.approved(), "Stop revocation clears the ephemeral approval");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return failures == 0 ? 0 : 1;
}
