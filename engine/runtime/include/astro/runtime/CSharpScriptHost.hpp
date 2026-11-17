#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace astro::runtime
{
inline constexpr std::uint32_t CSharpScriptApiVersion = 1;

enum class CSharpScriptStatus : std::int32_t
{
    Success = 0,
    InvalidContext = 1,
    VersionMismatch = 2,
    InvalidEntity = 3,
    InvalidTransform = 4,
    CallbackFailure = 5
};

struct CSharpScriptTransformV1
{
    std::uint32_t structSize = sizeof(CSharpScriptTransformV1);
    std::uint32_t apiVersion = CSharpScriptApiVersion;
    double positionX = 0.0;
    double positionY = 0.0;
    double positionZ = 0.0;
    double rotationX = 0.0;
    double rotationY = 0.0;
    double rotationZ = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double scaleZ = 1.0;
};

struct CSharpScriptApiV1
{
    using GetTransform = std::int32_t (*)(void*, std::uint64_t, CSharpScriptTransformV1*) noexcept;
    using SetTransform = std::int32_t (*)(void*, std::uint64_t, const CSharpScriptTransformV1*) noexcept;

    std::uint32_t structSize = sizeof(CSharpScriptApiV1);
    std::uint32_t apiVersion = CSharpScriptApiVersion;
    void* userData = nullptr;
    GetTransform getTransform = nullptr;
    SetTransform setTransform = nullptr;
};

struct CSharpScriptUpdateV1
{
    std::uint32_t structSize = sizeof(CSharpScriptUpdateV1);
    std::uint32_t apiVersion = CSharpScriptApiVersion;
    std::uint64_t entityId = 0;
    double fixedDeltaSeconds = 0.0;
    const CSharpScriptApiV1* api = nullptr;
};

struct CSharpScriptLifecycleV1
{
    std::uint32_t structSize = sizeof(CSharpScriptLifecycleV1);
    std::uint32_t apiVersion = CSharpScriptApiVersion;
    std::uint64_t entityId = 0;
    const CSharpScriptApiV1* api = nullptr;
};

[[nodiscard]] core::Result<void> validateCSharpScriptUpdateV1(std::span<const std::byte> payload);
[[nodiscard]] core::Result<void> validateCSharpScriptLifecycleV1(std::span<const std::byte> payload);

// Windows desktop prototype for loading an SDK-built, framework-dependent
// managed component into the current process through .NET hostfxr.
class CSharpScriptHost final
{
public:
    static core::Result<std::unique_ptr<CSharpScriptHost>> load(
        const std::filesystem::path& dotnetRoot,
        const std::filesystem::path& runtimeConfigPath,
        const std::filesystem::path& assemblyPath,
        const wchar_t* qualifiedTypeName,
        const wchar_t* methodName);
    static core::Result<std::unique_ptr<CSharpScriptHost>> loadScriptAsset(
        const std::filesystem::path& dotnetRoot, const assets::AssetRegistry& registry, assets::AssetId scriptAsset);

    ~CSharpScriptHost();
    CSharpScriptHost(const CSharpScriptHost&) = delete;
    CSharpScriptHost& operator=(const CSharpScriptHost&) = delete;

    core::Result<void> fixedUpdate(scene::Scene& runtimeScene, scene::EntityId entityId,
        double fixedDeltaSeconds) const;
    core::Result<void> onCreate(scene::Scene& runtimeScene, scene::EntityId entityId) const;
    core::Result<void> onDestroy(scene::Scene& runtimeScene, scene::EntityId entityId) const;

private:
    struct Impl;
    explicit CSharpScriptHost(std::unique_ptr<Impl> impl) noexcept;
    core::Result<void> loadAdditionalEntrypoint(const wchar_t* qualifiedTypeName,
        const wchar_t* methodName, void*& entryPoint);
    core::Result<void> invoke(void* entryPoint, std::span<std::byte> payload) const;
    std::unique_ptr<Impl> impl_;
};
}
