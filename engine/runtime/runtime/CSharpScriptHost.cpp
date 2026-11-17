#include "astro/runtime/CSharpScriptHost.hpp"

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>
#include <windows.h>

#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace astro::runtime
{
core::Result<void> validateCSharpScriptUpdateV1(std::span<const std::byte> payload)
{
    if (payload.size() != sizeof(CSharpScriptUpdateV1))
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "C# update payload has an incompatible size"});
    CSharpScriptUpdateV1 update{};
    std::memcpy(&update, payload.data(), sizeof(update));
    if (update.structSize != sizeof(CSharpScriptUpdateV1))
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "C# update struct size does not match ABI v1"});
    if (update.apiVersion != CSharpScriptApiVersion)
        return core::Result<void>::failure({core::ErrorCode::VersionMismatch, "C# update uses an unsupported ABI version"});
    if (update.entityId == 0 || !std::isfinite(update.fixedDeltaSeconds) || update.fixedDeltaSeconds <= 0.0
        || !update.api)
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "C# update context is incomplete or invalid"});
    return core::Result<void>::success();
}

core::Result<void> validateCSharpScriptLifecycleV1(std::span<const std::byte> payload)
{
    if (payload.size() != sizeof(CSharpScriptLifecycleV1))
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "C# lifecycle payload has an incompatible size"});
    CSharpScriptLifecycleV1 lifecycle{};
    std::memcpy(&lifecycle, payload.data(), sizeof(lifecycle));
    if (lifecycle.structSize != sizeof(CSharpScriptLifecycleV1))
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "C# lifecycle struct size does not match ABI v1"});
    if (lifecycle.apiVersion != CSharpScriptApiVersion)
        return core::Result<void>::failure({core::ErrorCode::VersionMismatch,
            "C# lifecycle uses an unsupported ABI version"});
    if (lifecycle.entityId == 0 || !lifecycle.api)
        return core::Result<void>::failure({core::ErrorCode::ValidationError,
            "C# lifecycle context is incomplete or invalid"});
    return core::Result<void>::success();
}

namespace
{
core::Error hostError(std::string message)
{
    return {core::ErrorCode::InternalError, std::move(message)};
}

std::string errorWithCode(const char* message, int code)
{
    return std::string(message) + " (0x" + std::to_string(static_cast<unsigned int>(code)) + ")";
}

core::Result<std::wstring> utf8ToWide(const std::string_view text)
{
    if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX))
        return core::Result<std::wstring>::failure({core::ErrorCode::ValidationError,
            "Managed type name is empty or too long"});
    const auto sourceLength = static_cast<int>(text.size());
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, nullptr, 0);
    if (required <= 0)
        return core::Result<std::wstring>::failure({core::ErrorCode::ValidationError,
            "Managed type name is not valid UTF-8"});
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength,
            result.data(), required) != required)
        return core::Result<std::wstring>::failure({core::ErrorCode::ValidationError,
            "Managed type name could not be converted to UTF-16"});
    return core::Result<std::wstring>::success(std::move(result));
}

scene::Transform toEngineTransform(const CSharpScriptTransformV1& value)
{
    scene::Transform transform;
    transform.position = {value.positionX, value.positionY, value.positionZ};
    transform.rotationDegrees = {value.rotationX, value.rotationY, value.rotationZ};
    transform.scale = {value.scaleX, value.scaleY, value.scaleZ};
    return transform;
}

CSharpScriptTransformV1 toScriptTransform(const scene::Transform& value)
{
    CSharpScriptTransformV1 transform;
    transform.positionX = value.position.x;
    transform.positionY = value.position.y;
    transform.positionZ = value.position.z;
    transform.rotationX = value.rotationDegrees.x;
    transform.rotationY = value.rotationDegrees.y;
    transform.rotationZ = value.rotationDegrees.z;
    transform.scaleX = value.scale.x;
    transform.scaleY = value.scale.y;
    transform.scaleZ = value.scale.z;
    return transform;
}

std::int32_t getTransform(void* userData, std::uint64_t entityId, CSharpScriptTransformV1* output) noexcept
{
    if (!userData || !output)
        return static_cast<std::int32_t>(CSharpScriptStatus::InvalidContext);
    auto& currentScene = *static_cast<scene::Scene*>(userData);
    const auto* transform = currentScene.transform(entityId);
    if (!transform)
        return static_cast<std::int32_t>(CSharpScriptStatus::InvalidEntity);
    *output = toScriptTransform(transform->value);
    return static_cast<std::int32_t>(CSharpScriptStatus::Success);
}

std::int32_t setTransform(void* userData, std::uint64_t entityId,
    const CSharpScriptTransformV1* value) noexcept
{
    if (!userData || !value)
        return static_cast<std::int32_t>(CSharpScriptStatus::InvalidContext);
    if (value->structSize != sizeof(CSharpScriptTransformV1) || value->apiVersion != CSharpScriptApiVersion)
        return static_cast<std::int32_t>(CSharpScriptStatus::VersionMismatch);
    auto& currentScene = *static_cast<scene::Scene*>(userData);
    if (!currentScene.transform(entityId))
        return static_cast<std::int32_t>(CSharpScriptStatus::InvalidEntity);
    const auto result = currentScene.setTransform(entityId, toEngineTransform(*value));
    return static_cast<std::int32_t>(result ? CSharpScriptStatus::Success : CSharpScriptStatus::InvalidTransform);
}
}

struct CSharpScriptHost::Impl
{
    HMODULE hostfxrModule = nullptr;
    hostfxr_handle context = nullptr;
    std::filesystem::path assemblyPath;
    load_assembly_and_get_function_pointer_fn loadAssembly = nullptr;
    void* entryPoint = nullptr;
    void* onCreateEntryPoint = nullptr;
    void* onDestroyEntryPoint = nullptr;
    hostfxr_close_fn closeContext = nullptr;

    ~Impl()
    {
        if (context && closeContext)
            closeContext(context);
        if (hostfxrModule)
            FreeLibrary(hostfxrModule);
    }
};

CSharpScriptHost::CSharpScriptHost(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
CSharpScriptHost::~CSharpScriptHost() = default;

core::Result<std::unique_ptr<CSharpScriptHost>> CSharpScriptHost::load(
    const std::filesystem::path& dotnetRoot,
    const std::filesystem::path& runtimeConfigPath,
    const std::filesystem::path& assemblyPath,
    const wchar_t* qualifiedTypeName,
    const wchar_t* methodName)
{
    if (dotnetRoot.empty() || runtimeConfigPath.empty() || assemblyPath.empty()
        || !qualifiedTypeName || !methodName)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            {core::ErrorCode::InvalidArgument, "C# host paths and entry point names must be provided"});

    if (!std::filesystem::is_regular_file(runtimeConfigPath)
        || !std::filesystem::is_regular_file(assemblyPath))
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            {core::ErrorCode::NotFound, "C# runtimeconfig or assembly file was not found"});

    auto impl = std::make_unique<Impl>();
    std::array<char_t, 32768> hostfxrPath{};
    std::size_t hostfxrPathSize = hostfxrPath.size();
    const auto root = dotnetRoot.wstring();
    const get_hostfxr_parameters locateParameters{sizeof(get_hostfxr_parameters), nullptr, root.c_str()};
    const int locateStatus = get_hostfxr_path(hostfxrPath.data(), &hostfxrPathSize, &locateParameters);
    if (locateStatus != 0)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            hostError(errorWithCode("Could not locate hostfxr under the requested .NET root", locateStatus)));

    impl->hostfxrModule = LoadLibraryW(hostfxrPath.data());
    if (!impl->hostfxrModule)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            hostError("Could not load hostfxr: " + std::to_string(GetLastError())));

    const auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        GetProcAddress(impl->hostfxrModule, "hostfxr_initialize_for_runtime_config"));
    const auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        GetProcAddress(impl->hostfxrModule, "hostfxr_get_runtime_delegate"));
    impl->closeContext = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(impl->hostfxrModule, "hostfxr_close"));
    if (!initialize || !getDelegate || !impl->closeContext)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            hostError("hostfxr is missing a required hosting API export"));

    hostfxr_initialize_parameters initParameters{};
    initParameters.size = sizeof(initParameters);
    const auto runtimeConfig = runtimeConfigPath.wstring();
    int status = initialize(runtimeConfig.c_str(), &initParameters, &impl->context);
    if (status < 0 || !impl->context)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            hostError(errorWithCode("Could not initialize .NET from runtimeconfig", status)));

    void* loader = nullptr;
    status = getDelegate(impl->context, hdt_load_assembly_and_get_function_pointer, &loader);
    if (status < 0 || !loader)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            hostError(errorWithCode("Could not acquire managed component loader", status)));

    const auto loadAssembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loader);
    void* entryPoint = nullptr;
    const auto assembly = assemblyPath.wstring();
    status = loadAssembly(assembly.c_str(), qualifiedTypeName, methodName,
        UNMANAGEDCALLERSONLY_METHOD, nullptr, &entryPoint);
    if (status < 0 || !entryPoint)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            hostError(errorWithCode("Could not load the managed component entry point", status)));
    impl->loadAssembly = loadAssembly;
    impl->assemblyPath = assemblyPath;
    impl->entryPoint = entryPoint;
    return core::Result<std::unique_ptr<CSharpScriptHost>>::success(
        std::unique_ptr<CSharpScriptHost>(new CSharpScriptHost(std::move(impl))));
}

core::Result<void> CSharpScriptHost::loadAdditionalEntrypoint(const wchar_t* qualifiedTypeName,
    const wchar_t* methodName, void*& entryPoint)
{
    if (!impl_ || !impl_->loadAssembly || !qualifiedTypeName || !methodName)
        return core::Result<void>::failure({core::ErrorCode::InvalidState,
            "C# assembly loader or lifecycle entry point is unavailable"});
    void* loaded = nullptr;
    const auto assembly = impl_->assemblyPath.wstring();
    const int status = impl_->loadAssembly(assembly.c_str(), qualifiedTypeName, methodName,
        UNMANAGEDCALLERSONLY_METHOD, nullptr, &loaded);
    if (status < 0 || !loaded)
        return core::Result<void>::failure(hostError(errorWithCode(
            "Could not load the managed lifecycle entry point", status)));
    entryPoint = loaded;
    return core::Result<void>::success();
}

core::Result<std::unique_ptr<CSharpScriptHost>> CSharpScriptHost::loadScriptAsset(
    const std::filesystem::path& dotnetRoot, const assets::AssetRegistry& registry,
    const assets::AssetId scriptAsset)
{
    auto resolved = registry.resolveScript(scriptAsset);
    if (!resolved)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(resolved.error());
    auto managedType = utf8ToWide(resolved.value().managedTypeName);
    if (!managedType)
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(managedType.error());
    const auto assemblyName = resolved.value().assemblyPath.stem().wstring();
    if (assemblyName.empty())
        return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(
            {core::ErrorCode::ValidationError, "Managed assembly name is empty"});
    auto qualifiedType = std::move(managedType).value();
    qualifiedType.append(L", ").append(assemblyName);
    auto loaded = load(dotnetRoot, resolved.value().runtimeConfigPath, resolved.value().assemblyPath,
        qualifiedType.c_str(), L"FixedUpdate");
    if (!loaded) return loaded;
    auto host = std::move(loaded).value();
    host->impl_->assemblyPath = resolved.value().assemblyPath;
    auto onCreate = host->loadAdditionalEntrypoint(qualifiedType.c_str(), L"OnCreate",
        host->impl_->onCreateEntryPoint);
    if (!onCreate) return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(onCreate.error());
    auto onDestroy = host->loadAdditionalEntrypoint(qualifiedType.c_str(), L"OnDestroy",
        host->impl_->onDestroyEntryPoint);
    if (!onDestroy) return core::Result<std::unique_ptr<CSharpScriptHost>>::failure(onDestroy.error());
    return core::Result<std::unique_ptr<CSharpScriptHost>>::success(std::move(host));
}

core::Result<void> CSharpScriptHost::invoke(void* entryPoint, std::span<std::byte> payload) const
{
    if (!impl_ || !entryPoint)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "C# entry point is not loaded"});
    if (payload.size() > static_cast<std::size_t>(INT32_MAX))
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument, "C# payload exceeds the host ABI limit"});
    const auto callable = reinterpret_cast<component_entry_point_fn>(entryPoint);
    const int status = callable(payload.empty() ? nullptr : payload.data(), static_cast<int32_t>(payload.size()));
    if (status != 0)
        return core::Result<void>::failure(hostError(errorWithCode("Managed C# entry point returned an error", status)));
    return core::Result<void>::success();
}

core::Result<void> CSharpScriptHost::fixedUpdate(scene::Scene& runtimeScene, scene::EntityId entityId,
    double fixedDeltaSeconds) const
{
    if (entityId == 0 || !runtimeScene.find(entityId))
        return core::Result<void>::failure({core::ErrorCode::NotFound, "script entity does not exist in the runtime Scene"});
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0)
        return core::Result<void>::failure({core::ErrorCode::InvalidArgument, "script timestep must be finite and positive"});

    CSharpScriptApiV1 api;
    api.userData = &runtimeScene;
    api.getTransform = &getTransform;
    api.setTransform = &setTransform;
    CSharpScriptUpdateV1 update;
    update.entityId = entityId;
    update.fixedDeltaSeconds = fixedDeltaSeconds;
    update.api = &api;
    auto payload = std::span<std::byte>(reinterpret_cast<std::byte*>(&update), sizeof(update));
    const auto validation = validateCSharpScriptUpdateV1(payload);
    if (!validation)
        return validation;
    return invoke(impl_ ? impl_->entryPoint : nullptr, payload);
}

core::Result<void> CSharpScriptHost::onCreate(scene::Scene& runtimeScene, const scene::EntityId entityId) const
{
    if (!runtimeScene.find(entityId))
        return core::Result<void>::failure({core::ErrorCode::NotFound,
            "script entity does not exist in the runtime Scene"});
    if (!impl_ || !impl_->onCreateEntryPoint)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "C# OnCreate entry point is not loaded"});
    CSharpScriptApiV1 api;
    api.userData = &runtimeScene;
    api.getTransform = &getTransform;
    api.setTransform = &setTransform;
    CSharpScriptLifecycleV1 lifecycle;
    lifecycle.entityId = entityId;
    lifecycle.api = &api;
    auto payload = std::span<std::byte>(reinterpret_cast<std::byte*>(&lifecycle), sizeof(lifecycle));
    const auto validation = validateCSharpScriptLifecycleV1(payload);
    if (!validation) return validation;
    return invoke(impl_->onCreateEntryPoint, payload);
}

core::Result<void> CSharpScriptHost::onDestroy(scene::Scene& runtimeScene, const scene::EntityId entityId) const
{
    if (!runtimeScene.find(entityId))
        return core::Result<void>::failure({core::ErrorCode::NotFound,
            "script entity does not exist in the runtime Scene"});
    if (!impl_ || !impl_->onDestroyEntryPoint)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "C# OnDestroy entry point is not loaded"});
    CSharpScriptApiV1 api;
    api.userData = &runtimeScene;
    api.getTransform = &getTransform;
    api.setTransform = &setTransform;
    CSharpScriptLifecycleV1 lifecycle;
    lifecycle.entityId = entityId;
    lifecycle.api = &api;
    auto payload = std::span<std::byte>(reinterpret_cast<std::byte*>(&lifecycle), sizeof(lifecycle));
    const auto validation = validateCSharpScriptLifecycleV1(payload);
    if (!validation) return validation;
    return invoke(impl_->onDestroyEntryPoint, payload);
}
}
