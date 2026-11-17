#include <filesystem>
#include <fstream>
#include <string>
#include "ProjectFile.hpp"
#include "Localization.hpp"
#include "CSharpPlayApproval.hpp"
#include "CSharpCodeWorkspace.hpp"
#include "CSharpBuildManifest.hpp"
#include "CSharpMetadataDiagnostics.hpp"
#include "astro/assets/AssetRegistry.hpp"
#include "astro/logic/VisualLogicGraph.hpp"
#include "astro/runtime/AnimationTimeline.hpp"
#include "astro/runtime/CameraRig.hpp"
#include "astro/runtime/RuntimeSession.hpp"
#if defined(ASTRO_EDITOR_HAS_CSHARP_RUNTIME)
#include "astro/runtime/CSharpScriptRuntime.hpp"
#endif
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
#include "astro/runtime/PuzzleCampaignSession.hpp"
#include "astro/runtime/PuzzleGame.hpp"
#endif
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
#include "astro/runtime/PuzzleSaveAdapter.hpp"
#include "astro/runtime/PuzzleCampaignSaveAdapter.hpp"
#endif
#include "astro/render2d/SceneSprites.hpp"
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
#include "astro/render3d/SceneMeshResolver.hpp"
#include "astro/render3d/Camera3D.hpp"
#include "astro/render3d/GltfImporter.hpp"
#include "SceneViewCamera3D.hpp"
#endif
#include "astro/save_editor/SaveSystemDocument.hpp"
#include "astro/scene/Scene.hpp"
#include "astro/scene/SceneFile.hpp"
#include "astro/ui/UiDocument.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <windows.h>
#include <psapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <iterator>
#include <map>
#include <memory>
#include <limits>
#include <utility>
#include <future>
#include <charconv>
#include <unordered_map>

// Upstream bgfx shader binaries; licenses remain in the dependency cache.
#include <vs_ocornut_imgui.bin.h>
#include <fs_ocornut_imgui.bin.h>

namespace
{
using astro::editor::EditorLanguage;
EditorLanguage gEditorLanguage = EditorLanguage::English;
const char* tr(const char* text) noexcept
{
    return astro::editor::translateEditorText(text, gEditorLanguage);
}
std::string pathUtf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}
std::filesystem::path pathFromUtf8(const char* text)
{
    const std::string bytes = text ? text : "";
    std::u8string encoded;
    encoded.reserve(bytes.size());
    for (const unsigned char byte : bytes) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}

using astro::scene::Entity;
using astro::scene::EntityId;
using astro::scene::Scene;
using Microsoft::WRL::ComPtr;

struct CSharpBuildResult final { DWORD exitCode{1}; std::string output; };

std::wstring quoteProcessArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : value)
    {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') { result.append(slashes * 2 + 1, L'\\'); result.push_back(ch); slashes = 0; continue; }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

CSharpBuildResult buildCSharpProject(const std::filesystem::path& projectRoot)
{
    CSharpBuildResult result;
#if defined(ASTRO_EDITOR_DOTNET_ROOT)
    const auto outputPath = std::filesystem::temp_directory_path()
        / (L"astro-csharp-build-output-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(GetTickCount64()) + L".log");
    std::error_code ec;
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) { result.output = "Cannot create C# build log"; return result; }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE)
    {
        CloseHandle(output);
        std::filesystem::remove(outputPath, ec);
        result.output = "Cannot open a non-interactive input handle for C# build";
        return result;
    }
    wchar_t pwsh[MAX_PATH]{};
    if (!SearchPathW(nullptr, L"pwsh.exe", nullptr, MAX_PATH, pwsh, nullptr))
    {
        CloseHandle(output);
        CloseHandle(input);
        std::filesystem::remove(outputPath, ec);
        result.output = "PowerShell 7 (pwsh.exe) is not available on PATH";
        return result;
    }
    const std::filesystem::path script = pathFromUtf8(ASTRO_EDITOR_REPOSITORY_ROOT)
        / L"tools" / L"csharp" / L"build-project.ps1";
    std::wstring command = quoteProcessArgument(pwsh) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "
        + quoteProcessArgument(script.wstring()) + L" -ProjectRoot " + quoteProcessArgument(projectRoot.wstring())
        + L" -DotnetRoot " + quoteProcessArgument(pathFromUtf8(ASTRO_EDITOR_DOTNET_ROOT).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = input;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(pwsh, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, projectRoot.c_str(), &startup, &process);
    CloseHandle(output);
    CloseHandle(input);
    if (!started)
    {
        std::filesystem::remove(outputPath, ec);
        result.output = "Could not start PowerShell C# build (Windows error " + std::to_string(GetLastError()) + ")";
        return result;
    }
    const DWORD waitResult = WaitForSingleObject(process.hProcess, 300000);
    if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
        result.exitCode = 124;
    }
    else GetExitCodeProcess(process.hProcess, &result.exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::ifstream log(outputPath, std::ios::binary);
    result.output.assign(std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>());
    std::filesystem::remove(outputPath, ec);
#else
    (void)projectRoot;
    result.output = "C# project build is unavailable in this Editor configuration";
#endif
    return result;
}

std::string detectConfiguredCSharpSdk()
{
#if defined(ASTRO_EDITOR_DOTNET_ROOT)
    const auto sdkDirectory = pathFromUtf8(ASTRO_EDITOR_DOTNET_ROOT) / L"sdk";
    std::error_code ec;
    std::uint32_t newest{};
    std::string result;
    for (std::filesystem::directory_iterator it(sdkDirectory, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_directory(ec)) continue;
        const auto name = pathUtf8(it->path().filename());
        constexpr std::string_view prefix{"10.0."};
        if (name.rfind(prefix, 0) != 0 || name.size() == prefix.size()) continue;
        std::uint32_t patch{};
        const auto parsed = std::from_chars(name.data() + prefix.size(), name.data() + name.size(), patch);
        if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() && patch >= newest)
        {
            newest = patch;
            result = name;
        }
    }
    return result;
#else
    return {};
#endif
}

struct CodeTextCallbackState final
{
    std::string* text{};
    int jumpLine{};
    bool jumpPending{};
    bool sizeLimitHit{};
};

int resizeCodeText(ImGuiInputTextCallbackData* data)
{
    auto* state = static_cast<CodeTextCallbackState*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        constexpr std::size_t maxBytes = 4 * 1024 * 1024;
        if (static_cast<std::size_t>(data->BufTextLen) > maxBytes)
        {
            data->BufTextLen = static_cast<int>(maxBytes);
            state->sizeLimitHit = true;
        }
        state->text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = state->text->data();
    }
    else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways && state->jumpPending)
    {
        const char* cursor = data->Buf;
        for (int line = 1; line < state->jumpLine && *cursor; ++line)
        {
            const char* next = std::strchr(cursor, '\n');
            if (!next) { cursor += std::strlen(cursor); break; }
            cursor = next + 1;
        }
        data->CursorPos = static_cast<int>(cursor - data->Buf);
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
        state->jumpPending = false;
    }
    return 0;
}

class SceneViewTarget final
{
public:
    struct Timings final
    {
        double prepareMs = 0.0, submitMs = 0.0;
        double composeMs = 0.0, boundsMs = 0.0, geometryMs = 0.0, transientCopyMs = 0.0;
        double meshPrepareMs = 0.0, meshSubmitMs = 0.0;
    };
    explicit SceneViewTarget(const bgfx::ViewId view) : view_(view) {}
    ~SceneViewTarget() { reset(); }
    SceneViewTarget(const SceneViewTarget&) = delete;
    SceneViewTarget& operator=(const SceneViewTarget&) = delete;
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
    void configure3d(bgfx::ProgramHandle program)
    {
        program3d_ = program;
        meshLayout_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
    }
    void invalidate3dCache() noexcept
    {
        for (auto& [id, mesh] : meshCache_) releaseMesh(mesh);
        meshCache_.clear();
        meshSubmissions_ = 0;
    }
    void orbit3d(const float deltaX, const float deltaY) noexcept
    { (void)camera3d_.orbitPixels(deltaX, deltaY); }
    void pan3d(const float deltaX, const float deltaY, const float viewportHeight) noexcept
    { (void)camera3d_.panPixels(deltaX, deltaY, viewportHeight); }
    void zoom3d(const float wheelDelta) noexcept
    { (void)camera3d_.zoomWheel(wheelDelta); }
    void requestFrameAll() noexcept { frameAllRequested_ = true; }
    [[nodiscard]] std::uint64_t meshSubmissions() const noexcept { return meshSubmissions_; }
    [[nodiscard]] std::uint32_t frameMeshSubmissions() const noexcept { return frameMeshSubmissions_; }
    [[nodiscard]] std::uint32_t frameMeshDiagnostics() const noexcept { return frameMeshDiagnostics_; }
    [[nodiscard]] bx::Vec3 lastMeshOrigin() const noexcept { return lastMeshOrigin_; }
    [[nodiscard]] const std::string& meshError() const noexcept { return meshError_; }
    [[nodiscard]] float cameraDistance() const noexcept { return camera3d_.distance(); }
    [[nodiscard]] astro::render3d::Vec3 cameraFocus() const noexcept { return camera3d_.focus(); }
    [[nodiscard]] bool frameAllApplied() const noexcept { return frameAllApplied_; }
#endif
    [[nodiscard]] int framebufferWidth() const noexcept { return width_; }
    [[nodiscard]] int framebufferHeight() const noexcept { return height_; }

    bool render(const Scene& scene, const astro::assets::AssetRegistry* assets,
        const std::map<std::uint64_t, bgfx::TextureHandle>& textures, const int width, const int height,
        const bgfx::ProgramHandle program, const bgfx::UniformHandle sampler,
        const bgfx::UniformHandle outputMode, const bgfx::VertexLayout& layout,
        bgfx::TextureHandle& output, std::string& error, Timings* timings = nullptr)
    {
        using Clock = std::chrono::steady_clock;
        const auto totalStart = Clock::now();
        const int targetWidth = std::clamp(width, 1, 1280);
        const int targetHeight = std::clamp(height, 1, 720);
        if (!resize(targetWidth, targetHeight, error)) return false;

        const auto composeStart = Clock::now();
        astro::render2d::Canvas2D canvas;
        if (assets)
        {
            const auto composed = astro::render2d::appendSceneSprites(scene, *assets, canvas);
            if (!composed) { error = composed.error().message; return false; }
        }
        const auto composeDone = Clock::now();
        astro::render2d::Camera2D camera;
        camera.pixelsPerUnit = 100.0;
        if (!canvas.sprites().empty())
        {
            double minX = std::numeric_limits<double>::max(), minY = minX;
            double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
            for (const auto& sprite : canvas.sprites()) for (const auto& point : sprite.worldCorners)
            {
                minX = std::min(minX, point.x); minY = std::min(minY, point.y);
                maxX = std::max(maxX, point.x); maxY = std::max(maxY, point.y);
            }
            camera.center = {(minX + maxX) * 0.5, (minY + maxY) * 0.5};
            const double worldWidth = std::max(1.0, maxX - minX);
            const double worldHeight = std::max(1.0, maxY - minY);
            camera.pixelsPerUnit = std::clamp(std::min(targetWidth / worldWidth, targetHeight / worldHeight) * 0.8,
                1.0, 10000.0);
        }
        const auto boundsDone = Clock::now();
        std::vector<ImDrawVert> vertices;
        std::vector<std::uint16_t> indices;
        vertices.reserve(canvas.sprites().size() * 4);
        indices.reserve(canvas.sprites().size() * 6);
        constexpr std::array<ImVec2, 4> uv{{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        const astro::render2d::Viewport viewport{static_cast<std::uint32_t>(targetWidth),
            static_cast<std::uint32_t>(targetHeight)};
        const double cameraScale = camera.pixelsPerUnit * camera.zoom;
        for (const auto& sprite : canvas.sprites())
        {
            const auto base = static_cast<std::uint16_t>(vertices.size());
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                const auto& world = sprite.worldCorners[corner];
                const double screenX = targetWidth * 0.5 + (world.x - camera.center.x) * cameraScale;
                const double screenY = targetHeight * 0.5 - (world.y - camera.center.y) * cameraScale;
                if (!std::isfinite(screenX) || !std::isfinite(screenY)
                    || std::abs(screenX) > 10000000.0 || std::abs(screenY) > 10000000.0)
                { error = "Projected sprite vertex is outside the supported view range"; return false; }
                vertices.push_back({ImVec2(static_cast<float>(screenX), static_cast<float>(screenY)),
                    uv[corner], IM_COL32(sprite.tint.r, sprite.tint.g, sprite.tint.b, sprite.tint.a)});
            }
            indices.insert(indices.end(), {base, static_cast<std::uint16_t>(base + 1),
                static_cast<std::uint16_t>(base + 2), base, static_cast<std::uint16_t>(base + 2),
                static_cast<std::uint16_t>(base + 3)});
        }
        if (vertices.size() > std::size_t(std::numeric_limits<std::uint16_t>::max()) + 1)
        { error = "Scene view exceeds the 16-bit transient index limit"; return false; }
        const auto geometryDone = Clock::now();
        if (!bgfx::isValid(frameBuffer_))
        { error = "bgfx could not allocate the Scene/Game framebuffer"; return false; }
        const auto prepareDone = Clock::now();
        bgfx::setViewName(view_, view_ == 1 ? "Scene View" : "Game View");
        bgfx::setViewFrameBuffer(view_, frameBuffer_);
        bgfx::setViewRect(view_, 0, 0, static_cast<std::uint16_t>(targetWidth),
            static_cast<std::uint16_t>(targetHeight));
        bgfx::setViewClear(view_, BGFX_CLEAR_COLOR, 0x121822ff);
        bgfx::setViewMode(view_, bgfx::ViewMode::Sequential);
        float projection[16];
        bx::mtxOrtho(projection, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0.0f,
            0.0f, 1000.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(view_, nullptr, projection);
        const float colorMode[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        bgfx::setFrameUniform(outputMode, colorMode);
        bgfx::touch(view_);

        bgfx::TransientVertexBuffer vertexBuffer;
        bgfx::TransientIndexBuffer indexBuffer;
        if (!vertices.empty())
        {
            if (!bgfx::allocTransientBuffers(&vertexBuffer, layout, static_cast<std::uint32_t>(vertices.size()),
                    &indexBuffer, static_cast<std::uint32_t>(indices.size())))
            { error = "bgfx transient buffer budget exceeded for Scene/Game View"; return false; }
            std::memcpy(vertexBuffer.data, vertices.data(), vertices.size() * sizeof(ImDrawVert));
            std::memcpy(indexBuffer.data, indices.data(), indices.size() * sizeof(std::uint16_t));
            const auto transientCopyDone = Clock::now();

            std::size_t first = 0;
            while (first < canvas.sprites().size())
            {
                const auto asset = canvas.sprites()[first].asset.value;
                auto end = first + 1;
                while (end < canvas.sprites().size() && canvas.sprites()[end].asset.value == asset) ++end;
                const auto texture = textures.find(asset);
                if (texture == textures.end() || !bgfx::isValid(texture->second))
                { error = "SpriteRenderer texture has not been uploaded to bgfx"; return false; }
                bgfx::setTexture(0, sampler, texture->second);
                bgfx::setVertexBuffer(0, &vertexBuffer, static_cast<std::uint32_t>(first * 4),
                    static_cast<std::uint32_t>((end - first) * 4));
                bgfx::setIndexBuffer(&indexBuffer, static_cast<std::uint32_t>(first * 6),
                    static_cast<std::uint32_t>((end - first) * 6));
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
                    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
                bgfx::submit(view_, program);
                first = end;
            }
            if (timings)
                timings->transientCopyMs = std::chrono::duration<double, std::milli>(transientCopyDone - geometryDone).count();
        }
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
        if (view_ == 1 && bgfx::isValid(program3d_))
            drawSceneMeshes(scene, assets, targetWidth, targetHeight, timings);
#endif
        const auto submitDone = Clock::now();
        if (timings)
        {
            timings->prepareMs = std::chrono::duration<double, std::milli>(prepareDone - totalStart).count();
            timings->submitMs = std::chrono::duration<double, std::milli>(submitDone - prepareDone).count();
            timings->composeMs = std::chrono::duration<double, std::milli>(composeDone - composeStart).count();
            timings->boundsMs = std::chrono::duration<double, std::milli>(boundsDone - composeDone).count();
            timings->geometryMs = std::chrono::duration<double, std::milli>(geometryDone - boundsDone).count();
        }
        output = bgfx::isValid(colorTexture_) ? colorTexture_ : bgfx::getTexture(frameBuffer_);
        if (!bgfx::isValid(output))
        { error = "bgfx framebuffer has no sampleable color attachment"; return false; }
        error.clear();
        return true;
    }

private:
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
    struct CachedMesh final
    {
        astro::assets::AssetId model{};
        std::uint32_t mesh = 0, primitive = 0;
        bx::Vec3 minimum{0.0f, 0.0f, 0.0f}, maximum{0.0f, 0.0f, 0.0f};
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    };

    struct DrawItem final
    {
        EntityId entity = 0;
        std::array<float, 16> transform{};
    };

    void releaseMesh(CachedMesh& mesh) noexcept
    {
        if (bgfx::isValid(mesh.indices)) bgfx::destroy(mesh.indices);
        if (bgfx::isValid(mesh.vertices)) bgfx::destroy(mesh.vertices);
        mesh = {};
    }

    bool getWorldMatrix(const Scene& scene, const Entity& entity, float* output)
    {
        std::vector<const Entity*> parents;
        const Entity* current = &entity;
        while (current)
        {
            if (parents.size() >= scene.entities().size()) return false;
            parents.push_back(current);
            if (!current->parent) break;
            current = scene.find(*current->parent);
            if (!current) return false;
        }
        bx::mtxIdentity(output);
        for (auto it = parents.rbegin(); it != parents.rend(); ++it)
        {
            const auto* transform = scene.transform((*it)->id);
            if (!transform) return false;
            const auto& t = transform->value;
            const std::array values{t.position.x, t.position.y, t.position.z, t.rotationDegrees.x,
                t.rotationDegrees.y, t.rotationDegrees.z, t.scale.x, t.scale.y, t.scale.z};
            if (!std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); })
                || std::any_of(values.begin(), values.end(), [](double v) {
                    return std::abs(v) > static_cast<double>(std::numeric_limits<float>::max());
                })
                || t.scale.x == 0.0 || t.scale.y == 0.0 || t.scale.z == 0.0) return false;
            constexpr double rad = 3.14159265358979323846 / 180.0;
            float local[16], world[16];
            bx::mtxSRT(local, float(t.scale.x), float(t.scale.y), float(t.scale.z),
                float(t.rotationDegrees.x * rad), float(t.rotationDegrees.y * rad),
                float(t.rotationDegrees.z * rad), float(t.position.x), float(t.position.y), float(t.position.z));
            bx::mtxMul(world, local, output);
            if (!std::all_of(std::begin(world), std::end(world), [](float v) { return std::isfinite(v); }))
                return false;
            std::memcpy(output, world, sizeof(world));
        }
        return true;
    }

    void drawSceneMeshes(const Scene& scene, const astro::assets::AssetRegistry* assets, int width, int height,
        Timings* timings)
    {
        const auto meshPrepareStart = std::chrono::steady_clock::now();
        meshError_.clear();
        frameMeshSubmissions_ = 0;
        frameMeshDiagnostics_ = 0;
        const auto view = static_cast<bgfx::ViewId>(view_ + 2);
        bgfx::setViewName(view, "Scene View 3D");
        bgfx::setViewFrameBuffer(view, frameBuffer_);
        bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
        bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0x121822ff, 1.0f, 0);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        bgfx::touch(view);
        std::unordered_map<EntityId, bool> live;
        std::vector<DrawItem> drawItems;
        drawItems.reserve(scene.entities().size());
        astro::editor::SceneViewBounds3D sceneBounds{
            {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
            {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()}};
        bool hasBounds = false;
        if (assets)
        {
            for (const auto& entity : scene.entities())
            {
                const auto* component = scene.meshRenderer(entity.id);
                if (!component || !component->visible) continue;
                live.emplace(entity.id, true);
                auto found = meshCache_.find(entity.id);
                const bool changed = found == meshCache_.end() || found->second.model != component->model
                    || found->second.mesh != component->meshIndex || found->second.primitive != component->primitiveIndex;
                if (changed)
                {
                    if (found != meshCache_.end()) { releaseMesh(found->second); meshCache_.erase(found); }
                    auto resolved = astro::render3d::resolveMeshRenderer(*assets, *component);
                    if (!resolved)
                    { meshError_ = resolved.error().message; ++frameMeshDiagnostics_; continue; }
                    const auto& geometry = resolved.value().geometry;
                    if (geometry.vertices().size() > 65536 || geometry.triangleIndices().size() > 196608)
                    { meshError_ = "3D Scene View mesh exceeds the 16-bit preview budget"; ++frameMeshDiagnostics_; continue; }
                    struct Vertex { float x, y, z; std::uint32_t color; };
                    static_assert(sizeof(Vertex) == 16);
                    std::vector<Vertex> vertices;
                    vertices.reserve(geometry.vertices().size());
                    for (const auto& v : geometry.vertices())
                    {
                        const auto color = std::uint32_t(v.color.r) | (std::uint32_t(v.color.g) << 8)
                            | (std::uint32_t(v.color.b) << 16) | (std::uint32_t(v.color.a) << 24);
                        vertices.push_back({v.position.x, v.position.y, v.position.z, color});
                    }
                    std::vector<std::uint16_t> indices;
                    indices.reserve(geometry.triangleIndices().size());
                    for (auto index : geometry.triangleIndices()) indices.push_back(static_cast<std::uint16_t>(index));
                    CachedMesh gpu;
                    gpu.model = component->model; gpu.mesh = component->meshIndex; gpu.primitive = component->primitiveIndex;
                    gpu.minimum = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max()};
                    gpu.maximum = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};
                    for (const auto& v : geometry.vertices())
                    {
                        gpu.minimum.x = std::min(gpu.minimum.x, v.position.x);
                        gpu.minimum.y = std::min(gpu.minimum.y, v.position.y);
                        gpu.minimum.z = std::min(gpu.minimum.z, v.position.z);
                        gpu.maximum.x = std::max(gpu.maximum.x, v.position.x);
                        gpu.maximum.y = std::max(gpu.maximum.y, v.position.y);
                        gpu.maximum.z = std::max(gpu.maximum.z, v.position.z);
                    }
                    gpu.vertices = bgfx::createVertexBuffer(bgfx::copy(vertices.data(),
                        static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex))), meshLayout_);
                    gpu.indices = bgfx::createIndexBuffer(bgfx::copy(indices.data(),
                        static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t))));
                    if (!bgfx::isValid(gpu.vertices) || !bgfx::isValid(gpu.indices))
                    { releaseMesh(gpu); meshError_ = "bgfx could not upload Scene MeshRenderer geometry";
                      ++frameMeshDiagnostics_; continue; }
                    meshCache_.emplace(entity.id, gpu);
                    found = meshCache_.find(entity.id);
                }
                if (found == meshCache_.end()) continue;
                float model[16];
                if (!getWorldMatrix(scene, entity, model))
                { meshError_ = "Scene hierarchy has a missing Transform, invalid value, or parent cycle";
                  ++frameMeshDiagnostics_; continue; }
                astro::editor::SceneViewBounds3D meshBounds{
                    {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
                    {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()}};
                bool validBounds = true;
                for (int corner = 0; corner < 8; ++corner)
                {
                    const bx::Vec3 local{
                        (corner & 1) ? found->second.maximum.x : found->second.minimum.x,
                        (corner & 2) ? found->second.maximum.y : found->second.minimum.y,
                        (corner & 4) ? found->second.maximum.z : found->second.minimum.z};
                    const auto world = bx::mul(local, model);
                    if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)
                        || std::abs(world.x) > 10'000'000.0f || std::abs(world.y) > 10'000'000.0f
                        || std::abs(world.z) > 10'000'000.0f)
                    { validBounds = false; break; }
                    meshBounds.minimum.x = std::min(meshBounds.minimum.x, world.x);
                    meshBounds.minimum.y = std::min(meshBounds.minimum.y, world.y);
                    meshBounds.minimum.z = std::min(meshBounds.minimum.z, world.z);
                    meshBounds.maximum.x = std::max(meshBounds.maximum.x, world.x);
                    meshBounds.maximum.y = std::max(meshBounds.maximum.y, world.y);
                    meshBounds.maximum.z = std::max(meshBounds.maximum.z, world.z);
                }
                if (!validBounds)
                {
                    meshError_ = "Transformed mesh bounds exceed the Scene View range";
                    ++frameMeshDiagnostics_;
                    continue;
                }
                drawItems.push_back({entity.id, {model[0], model[1], model[2], model[3],
                    model[4], model[5], model[6], model[7], model[8], model[9], model[10], model[11],
                    model[12], model[13], model[14], model[15]}});
                sceneBounds.minimum.x = std::min(sceneBounds.minimum.x, meshBounds.minimum.x);
                sceneBounds.minimum.y = std::min(sceneBounds.minimum.y, meshBounds.minimum.y);
                sceneBounds.minimum.z = std::min(sceneBounds.minimum.z, meshBounds.minimum.z);
                sceneBounds.maximum.x = std::max(sceneBounds.maximum.x, meshBounds.maximum.x);
                sceneBounds.maximum.y = std::max(sceneBounds.maximum.y, meshBounds.maximum.y);
                sceneBounds.maximum.z = std::max(sceneBounds.maximum.z, meshBounds.maximum.z);
                hasBounds = true;
            }
        }
        for (auto it = meshCache_.begin(); it != meshCache_.end();)
        {
            if (!live.contains(it->first)) { releaseMesh(it->second); it = meshCache_.erase(it); }
            else ++it;
        }

        if (frameAllRequested_)
        {
            frameAllRequested_ = false;
            frameAllApplied_ = hasBounds && camera3d_.frame(sceneBounds, float(width) / float(height));
            if (!frameAllApplied_)
            {
                meshError_ = "Frame All could not fit visible mesh bounds in this Scene View";
                ++frameMeshDiagnostics_;
            }
        }
        const auto camera = camera3d_.camera();
        const auto viewMatrix = camera.viewMatrix();
        const auto projection = camera.projectionMatrix(float(width) / float(height));
        if (!viewMatrix || !projection)
        {
            meshError_ = "Scene View camera produced an invalid view or projection matrix";
            return;
        }
        bgfx::setViewTransform(view, viewMatrix.value().values.data(), projection.value().values.data());
        const auto meshPrepareDone = std::chrono::steady_clock::now();
        if (timings)
            timings->meshPrepareMs = std::chrono::duration<double, std::milli>(meshPrepareDone - meshPrepareStart).count();
        const auto meshSubmitStart = std::chrono::steady_clock::now();
        for (const auto& item : drawItems)
        {
            const auto found = meshCache_.find(item.entity);
            if (found == meshCache_.end()) continue;
            float model[16];
            std::copy(item.transform.begin(), item.transform.end(), model);
            lastMeshOrigin_ = bx::mul(bx::Vec3{0.0f, 0.0f, 0.0f}, model);
            bgfx::setTransform(model);
            bgfx::setVertexBuffer(0, found->second.vertices);
            bgfx::setIndexBuffer(found->second.indices);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
            bgfx::submit(view, program3d_);
            ++meshSubmissions_;
            ++frameMeshSubmissions_;
        }
        if (timings)
            timings->meshSubmitMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - meshSubmitStart).count();
    }

#endif

    void reset() noexcept
    {
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
        for (auto& [id, mesh] : meshCache_) releaseMesh(mesh);
        meshCache_.clear();
        bgfx::setViewFrameBuffer(static_cast<bgfx::ViewId>(view_ + 2), BGFX_INVALID_HANDLE);
#endif
        bgfx::setViewFrameBuffer(view_, BGFX_INVALID_HANDLE);
        if (bgfx::isValid(frameBuffer_)) bgfx::destroy(frameBuffer_);
        if (bgfx::isValid(colorTexture_)) bgfx::destroy(colorTexture_);
        if (bgfx::isValid(depthTexture_)) bgfx::destroy(depthTexture_);
        colorTexture_ = depthTexture_ = BGFX_INVALID_HANDLE;
        frameBuffer_ = BGFX_INVALID_HANDLE;
        width_ = height_ = 0;
    }

    bool resize(const int width, const int height, std::string& error)
    {
        if (width == width_ && height == height_) return true;
        bgfx::setViewFrameBuffer(view_, BGFX_INVALID_HANDLE);
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
        bgfx::setViewFrameBuffer(static_cast<bgfx::ViewId>(view_ + 2), BGFX_INVALID_HANDLE);
#endif
        if (bgfx::isValid(frameBuffer_)) bgfx::destroy(frameBuffer_);
        if (bgfx::isValid(colorTexture_)) bgfx::destroy(colorTexture_);
        if (bgfx::isValid(depthTexture_)) bgfx::destroy(depthTexture_);
        colorTexture_ = depthTexture_ = BGFX_INVALID_HANDLE;
        if (view_ == 1)
        {
            colorTexture_ = bgfx::createTexture2D(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                false, 1, bgfx::TextureFormat::BGRA8,
                BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            depthTexture_ = bgfx::createTexture2D(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
            const bgfx::TextureHandle attachments[]{colorTexture_, depthTexture_};
            if (bgfx::isValid(colorTexture_) && bgfx::isValid(depthTexture_))
                frameBuffer_ = bgfx::createFrameBuffer(2, attachments, false);
        }
        else frameBuffer_ = bgfx::createFrameBuffer(static_cast<std::uint16_t>(width),
            static_cast<std::uint16_t>(height), bgfx::TextureFormat::BGRA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(frameBuffer_))
        {
            if (bgfx::isValid(colorTexture_)) bgfx::destroy(colorTexture_);
            if (bgfx::isValid(depthTexture_)) bgfx::destroy(depthTexture_);
            colorTexture_ = depthTexture_ = BGFX_INVALID_HANDLE;
            error = "bgfx could not allocate the Scene/Game framebuffer attachment";
            return false;
        }
        bgfx::setName(frameBuffer_, view_ == 1 ? "Scene View BGRA8" : "Game View BGRA8");
        width_ = width; height_ = height;
        return true;
    }

    bgfx::ViewId view_ = 0;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle colorTexture_ = BGFX_INVALID_HANDLE, depthTexture_ = BGFX_INVALID_HANDLE;
    int width_ = 0, height_ = 0;
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
    bgfx::ProgramHandle program3d_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout meshLayout_;
    std::unordered_map<EntityId, CachedMesh> meshCache_;
    std::uint64_t meshSubmissions_ = 0;
    std::uint32_t frameMeshSubmissions_ = 0, frameMeshDiagnostics_ = 0;
    bx::Vec3 lastMeshOrigin_{0.0f, 0.0f, 0.0f};
    std::string meshError_;
    astro::editor::SceneViewCamera3D camera3d_;
    bool frameAllRequested_ = false, frameAllApplied_ = false;
#endif
};

ImVec2 framebufferUv0() noexcept
{
    return bgfx::getCaps()->originBottomLeft ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
}

ImVec2 framebufferUv1() noexcept
{
    return bgfx::getCaps()->originBottomLeft ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
}

void appendPngBe32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 24));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t pngCrc32(const std::uint8_t* bytes, const std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void appendPngChunk(std::vector<std::uint8_t>& png, const std::array<char, 4> type,
    const std::vector<std::uint8_t>& data)
{
    appendPngBe32(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    for (const auto c : type) png.push_back(static_cast<std::uint8_t>(c));
    png.insert(png.end(), data.begin(), data.end());
    appendPngBe32(png, pngCrc32(png.data() + start, png.size() - start));
}

std::vector<std::uint8_t> makeSmokePng(const std::array<std::uint8_t, 4> color,
    const std::uint32_t width = 1, const std::uint32_t height = 1)
{
    if (width == 0 || height == 0 || std::uint64_t(width) * height * 4 + height > 65535)
        throw std::runtime_error("Smoke PNG dimensions exceed the test encoder limit");
    std::vector<std::uint8_t> png{137,80,78,71,13,10,26,10};
    std::vector<std::uint8_t> header;
    appendPngBe32(header, width); appendPngBe32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    appendPngChunk(png, {'I','H','D','R'}, header);
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (static_cast<std::size_t>(width) * 4 + 1));
    for (std::uint32_t y = 0; y < height; ++y)
    {
        raw.push_back(0); // PNG filter: None
        for (std::uint32_t x = 0; x < width; ++x) raw.insert(raw.end(), color.begin(), color.end());
    }
    std::vector<std::uint8_t> compressed{0x78, 0x01, 0x01};
    const auto length = static_cast<std::uint16_t>(raw.size());
    compressed.push_back(static_cast<std::uint8_t>(length));
    compressed.push_back(static_cast<std::uint8_t>(length >> 8));
    const auto inverse = static_cast<std::uint16_t>(~length);
    compressed.push_back(static_cast<std::uint8_t>(inverse));
    compressed.push_back(static_cast<std::uint8_t>(inverse >> 8));
    compressed.insert(compressed.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    appendPngBe32(compressed, (b << 16) | a);
    appendPngChunk(png, {'I','D','A','T'}, compressed);
    appendPngChunk(png, {'I','E','N','D'}, {});
    return png;
}

bool smokeScreenshotContainsSpriteColors(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary);
    std::array<std::uint8_t, 18> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size()) || header[2] != 2 || header[16] != 32)
        return false;
    const auto width = std::uint32_t(header[12]) | (std::uint32_t(header[13]) << 8);
    const auto height = std::uint32_t(header[14]) | (std::uint32_t(header[15]) << 8);
    const auto offset = 18u + header[0];
    if (!width || !height || std::uint64_t(width) * height > 100'000'000) return false;
    input.seekg(offset, std::ios::beg);
    std::uint64_t red = 0, blue = 0, blendedGreen = 0;
    std::array<std::uint8_t, 4> pixel{};
    for (std::uint64_t i = 0; i < std::uint64_t(width) * height; ++i)
    {
        if (!input.read(reinterpret_cast<char*>(pixel.data()), pixel.size())) return false;
        // bgfx screenshot is BGRA. The blend reference is green (128 alpha) over #121822.
        if (pixel[2] > 235 && pixel[1] < 20 && pixel[0] < 20) ++red;
        if (pixel[0] > 235 && pixel[1] < 20 && pixel[2] < 20) ++blue;
        if (pixel[1] > 100 && pixel[1] < 180 && pixel[2] < 25 && pixel[0] < 25) ++blendedGreen;
    }
    return red >= 8 && blue >= 8 && blendedGreen >= 8;
}

std::uint64_t smokeScreenshotSceneViewWhitePixels(const std::filesystem::path& file,
    const std::array<float, 4>& normalizedSceneViewRect)
{
    std::ifstream input(file, std::ios::binary);
    std::array<std::uint8_t, 18> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size()) || header[2] != 2 || header[16] != 32)
        return 0;
    const auto width = std::uint32_t(header[12]) | (std::uint32_t(header[13]) << 8);
    const auto height = std::uint32_t(header[14]) | (std::uint32_t(header[15]) << 8);
    const auto pixelCount = std::uint64_t(width) * height;
    if (!width || !height || pixelCount > 100'000'000 || normalizedSceneViewRect[0] < 0.0f
        || normalizedSceneViewRect[1] < 0.0f || normalizedSceneViewRect[2] > 1.0f
        || normalizedSceneViewRect[3] > 1.0f || normalizedSceneViewRect[0] >= normalizedSceneViewRect[2]
        || normalizedSceneViewRect[1] >= normalizedSceneViewRect[3])
        return 0;
    const auto dataOffset = 18u + header[0];
    input.seekg(dataOffset, std::ios::beg);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(pixelCount) * 4);
    if (!input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()))) return 0;

    const auto x0 = static_cast<std::uint32_t>(normalizedSceneViewRect[0] * float(width));
    const auto y0 = static_cast<std::uint32_t>(normalizedSceneViewRect[1] * float(height));
    const auto x1 = std::min(width, static_cast<std::uint32_t>(std::ceil(normalizedSceneViewRect[2] * float(width))));
    const auto y1 = std::min(height, static_cast<std::uint32_t>(std::ceil(normalizedSceneViewRect[3] * float(height))));
    const bool topOrigin = (header[17] & 0x20u) != 0;
    const bool rightOrigin = (header[17] & 0x10u) != 0;
    std::uint64_t whitePixels = 0;
    for (std::uint32_t y = y0; y < y1; ++y)
    {
        const auto storedY = topOrigin ? y : height - 1u - y;
        for (std::uint32_t x = x0; x < x1; ++x)
        {
            const auto storedX = rightOrigin ? width - 1u - x : x;
            const auto offset = (std::size_t(storedY) * width + storedX) * 4;
            const auto blue = pixels[offset], green = pixels[offset + 1], red = pixels[offset + 2];
            if (red >= 220 && green >= 220 && blue >= 220 && pixels[offset + 3] >= 200)
                ++whitePixels;
        }
    }
    return whitePixels;
}

#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
std::string makeSyntheticMetadataReportUiFixture()
{
    constexpr std::size_t reportBytes = 128 * 1024;
    constexpr std::string_view truncationMarker = "\n[additional metadata results truncated at 128 KiB]";
    std::string report;
    report.reserve(reportBytes);
    for (std::size_t index = 0; report.size() + truncationMarker.size() < reportBytes; ++index)
    {
        const auto row = std::string("{\"syntheticUiFixture\":true,\"managedType\":\"AstroEngine.MetadataPreview.Binding")
            + std::to_string(index) + "\",\"status\":\"ok\",\"methods\":[\"OnCreate\",\"FixedUpdate\",\"OnDestroy\"]}\n";
        const auto remaining = reportBytes - truncationMarker.size() - report.size();
        report.append(row.data(), std::min(row.size(), remaining));
    }
    report += truncationMarker;
    return report;
}
#endif

#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
using PuzzleCellPair = std::pair<astro::runtime::GridPosition, astro::runtime::GridPosition>;

bool adjacentPuzzleCells(const astro::runtime::GridPosition lhs, const astro::runtime::GridPosition rhs)
{
    return (lhs.column == rhs.column && (lhs.row + 1 == rhs.row || rhs.row + 1 == lhs.row))
        || (lhs.row == rhs.row && (lhs.column + 1 == rhs.column || rhs.column + 1 == lhs.column));
}

bool clickPuzzleCell(const astro::runtime::GridPosition cell,
    std::optional<astro::runtime::GridPosition>& selected,
    std::optional<PuzzleCellPair>& pending)
{
    if (pending) return false;
    if (!selected) { selected = cell; return false; }
    if (*selected == cell) { selected.reset(); return false; }
    if (adjacentPuzzleCells(*selected, cell))
    {
        pending = PuzzleCellPair{*selected, cell};
        selected.reset();
        return true;
    }
    selected = cell;
    return false;
}

std::optional<PuzzleCellPair> firstLegalPuzzleSwap(const astro::runtime::PuzzleGame& puzzle)
{
    for (std::size_t row = 0; row < puzzle.height(); ++row)
        for (std::size_t column = 0; column < puzzle.width(); ++column)
            for (const auto offset : {astro::runtime::GridPosition{1, 0}, astro::runtime::GridPosition{0, 1}})
            {
                const astro::runtime::GridPosition next{column + offset.column, row + offset.row};
                if (next.column >= puzzle.width() || next.row >= puzzle.height()) continue;
                auto candidate = puzzle;
                const auto moved = candidate.swap({column, row}, next);
                if (moved && moved.value().accepted) return PuzzleCellPair{{column, row}, next};
            }
    return std::nullopt;
}

std::optional<PuzzleCellPair> firstCompletingPuzzleSwap(const astro::runtime::PuzzleGame& puzzle)
{
    for (std::size_t row = 0; row < puzzle.height(); ++row)
        for (std::size_t column = 0; column < puzzle.width(); ++column)
            for (const auto offset : {astro::runtime::GridPosition{1, 0}, astro::runtime::GridPosition{0, 1}})
            {
                const astro::runtime::GridPosition next{column + offset.column, row + offset.row};
                if (next.column >= puzzle.width() || next.row >= puzzle.height()) continue;
                auto candidate = puzzle;
                const auto moved = candidate.swap({column, row}, next);
                if (moved && moved.value().levelCompleted) return PuzzleCellPair{{column, row}, next};
            }
    return std::nullopt;
}

astro::runtime::PuzzleLevelDefinition defaultPuzzleLevelDefinition()
{
    const auto generated = astro::runtime::PuzzleGame::create(8, 8, 9123, 5, 30);
    if (!generated) throw std::runtime_error(generated.error().message);
    return {"level-001", "Puzzle Level", 8, 8, 5, 9123, 30, generated.value().tiles()};
}

bool samePuzzleSnapshot(const astro::runtime::PuzzleSnapshot& lhs, const astro::runtime::PuzzleSnapshot& rhs)
{
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.tileKinds == rhs.tileKinds
        && lhs.levelTarget == rhs.levelTarget && lhs.score == rhs.score && lhs.level == rhs.level
        && lhs.tiles == rhs.tiles && lhs.randomState == rhs.randomState;
}

bool drawPuzzleGameView(const astro::runtime::PuzzleGame& puzzle,
    std::optional<astro::runtime::GridPosition>& selected,
    std::optional<PuzzleCellPair>& pending,
    const std::optional<astro::runtime::PuzzleMoveResult>& lastMove)
{
    ImGui::Text(tr("Puzzle   Score: %llu   Level: %llu / %llu"),
        static_cast<unsigned long long>(puzzle.score()),
        static_cast<unsigned long long>(puzzle.level()),
        static_cast<unsigned long long>(puzzle.levelTarget()));
    ImGui::TextDisabled(tr("Click two neighboring tiles to swap. Moves resolve on the fixed runtime step."));
    if (pending) ImGui::TextDisabled(tr("Move queued for the next runtime step."));
    else if (lastMove && !lastMove->accepted) ImGui::TextDisabled(tr("That swap made no match."));

    const auto available = ImGui::GetContentRegionAvail();
    const float tileSize = std::floor(std::min(available.x / static_cast<float>(puzzle.width()),
        available.y / static_cast<float>(puzzle.height())));
    if (tileSize < 4.0f) { ImGui::TextDisabled(tr("Game View is too small to display the Puzzle board.")); return false; }
    const ImVec2 boardSize(tileSize * static_cast<float>(puzzle.width()),
        tileSize * static_cast<float>(puzzle.height()));
    const auto cursor = ImGui::GetCursorScreenPos();
    const ImVec2 boardMin(cursor.x + (available.x - boardSize.x) * 0.5f,
        cursor.y + (available.y - boardSize.y) * 0.5f);
    ImGui::SetCursorScreenPos(boardMin);
    ImGui::InvisibleButton("PuzzleBoardInput", boardSize);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        const auto mouse = ImGui::GetIO().MousePos;
        const auto column = static_cast<std::size_t>((mouse.x - boardMin.x) / tileSize);
        const auto row = static_cast<std::size_t>((mouse.y - boardMin.y) / tileSize);
        if (column < puzzle.width() && row < puzzle.height())
            clickPuzzleCell({column, row}, selected, pending);
    }

    static constexpr std::array<ImU32, 5> colors{{
        IM_COL32(228, 92, 92, 255), IM_COL32(75, 157, 235, 255), IM_COL32(245, 204, 64, 255),
        IM_COL32(110, 203, 123, 255), IM_COL32(185, 113, 224, 255)}};
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(boardMin, ImVec2(boardMin.x + boardSize.x, boardMin.y + boardSize.y),
        IM_COL32(18, 24, 34, 255));
    for (std::size_t row = 0; row < puzzle.height(); ++row)
        for (std::size_t column = 0; column < puzzle.width(); ++column)
        {
            const auto value = puzzle.tileAt({column, row});
            const ImVec2 cellMin(boardMin.x + static_cast<float>(column) * tileSize + 2.0f,
                boardMin.y + static_cast<float>(row) * tileSize + 2.0f);
            const ImVec2 cellMax(boardMin.x + static_cast<float>(column + 1) * tileSize - 2.0f,
                boardMin.y + static_cast<float>(row + 1) * tileSize - 2.0f);
            drawList->AddRectFilled(cellMin, cellMax, colors[value % colors.size()], 4.0f);
            if (selected && selected->column == column && selected->row == row)
                drawList->AddRect(cellMin, cellMax, IM_COL32(255, 255, 255, 255), 4.0f, 0, 3.0f);
            if (pending && ((pending->first.column == column && pending->first.row == row)
                || (pending->second.column == column && pending->second.row == row)))
                drawList->AddRect(cellMin, cellMax, IM_COL32(255, 245, 180, 255), 4.0f, 0, 2.0f);
        }
    return true;
}
#endif

bool hasChildren(const Scene& scene, const EntityId id)
{
    return std::any_of(scene.entities().begin(), scene.entities().end(), [id](const Entity& entity) {
        return entity.parent == id;
    });
}

void drawHierarchyBranch(const Scene& scene, const std::optional<EntityId> parent,
    std::optional<EntityId>& selected)
{
    for (const auto& entity : scene.entities())
    {
        if (entity.parent != parent) continue;
        const bool children = hasChildren(scene, entity.id);
        const std::string label = entity.name + "##" + std::to_string(entity.id);
        const auto flags = (children ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf)
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | (selected == entity.id ? ImGuiTreeNodeFlags_Selected : 0);
        if (ImGui::TreeNodeEx(label.c_str(), flags))
        {
            if (ImGui::IsItemClicked()) selected = entity.id;
            if (children) drawHierarchyBranch(scene, entity.id, selected);
            if (children) ImGui::TreePop();
        }
        else if (ImGui::IsItemClicked()) selected = entity.id;
    }
}

astro::logic::VisualLogicGraph makeScoreGateGraph()
{
    using astro::logic::NodeKind;
    astro::logic::VisualLogicGraph graph("Score gate example");
    const auto score = graph.addNode(NodeKind::NumberConstant);
    const auto bonus = graph.addNode(NodeKind::NumberConstant);
    const auto total = graph.addNode(NodeKind::Add);
    const auto threshold = graph.addNode(NodeKind::NumberConstant);
    const auto passed = graph.addNode(NodeKind::GreaterThan);
    const auto enabled = graph.addNode(NodeKind::BooleanConstant);
    const auto result = graph.addNode(NodeKind::And);
    if (!score || !bonus || !total || !threshold || !passed || !enabled || !result)
        throw std::runtime_error("Could not create Visual Logic example nodes");
    if (!graph.setLiteral(score.value(), 4.0) || !graph.setLiteral(bonus.value(), 3.0)
        || !graph.setLiteral(threshold.value(), 6.0) || !graph.setLiteral(enabled.value(), true)
        || !graph.connect(score.value(), total.value(), 0) || !graph.connect(bonus.value(), total.value(), 1)
        || !graph.connect(total.value(), passed.value(), 0) || !graph.connect(threshold.value(), passed.value(), 1)
        || !graph.connect(passed.value(), result.value(), 0) || !graph.connect(enabled.value(), result.value(), 1))
        throw std::runtime_error("Could not connect Visual Logic example graph");
    return graph;
}

void reportLogicResult(const astro::core::Result<void>& result,
    const std::function<void(const std::string&)>& reportError)
{
    if (!result) reportError(result.error().message);
}

void drawVisualLogic(astro::logic::VisualLogicGraph& graph, astro::logic::VisualLogicDebugger& debugger,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    using namespace astro::logic;
    if (!ImGui::Begin(tr("Visual Logic"))) { ImGui::End(); return; }
    windowDrawn = true;
    ImGui::Text(tr("%s"), graph.name().c_str());
    ImGui::TextDisabled(tr("Typed dataflow prototype. Inputs connect through typed menus; graph is not saved yet."));

    const auto add = [&](const NodeKind kind) {
        const auto result = graph.addNode(kind);
        if (!result) reportError(result.error().message);
        else debugger.reset();
    };
    const auto addButton = [&](const char* label, const NodeKind kind) {
        ImGui::PushID(static_cast<int>(kind));
        if (ImGui::Button(label)) add(kind);
        ImGui::PopID();
    };
    addButton(tr("+ Number"), NodeKind::NumberConstant);
    ImGui::SameLine(); addButton(tr("+ Boolean"), NodeKind::BooleanConstant);
    ImGui::SameLine(); addButton(tr("+ String"), NodeKind::StringConstant);
    ImGui::SameLine();
    if (ImGui::BeginMenu(tr("+ Math")))
    {
        if (ImGui::MenuItem(tr("Add"))) add(NodeKind::Add);
        if (ImGui::MenuItem(tr("Subtract"))) add(NodeKind::Subtract);
        if (ImGui::MenuItem(tr("Multiply"))) add(NodeKind::Multiply);
        if (ImGui::MenuItem(tr("Divide"))) add(NodeKind::Divide);
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::BeginMenu(tr("+ Compare")))
    {
        if (ImGui::MenuItem(tr("Greater than"))) add(NodeKind::GreaterThan);
        if (ImGui::MenuItem(tr("Less than"))) add(NodeKind::LessThan);
        if (ImGui::MenuItem(tr("Equal number"))) add(NodeKind::EqualNumber);
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::BeginMenu(tr("+ Logic")))
    {
        if (ImGui::MenuItem(tr("And"))) add(NodeKind::And);
        if (ImGui::MenuItem(tr("Or"))) add(NodeKind::Or);
        if (ImGui::MenuItem(tr("Not"))) add(NodeKind::Not);
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::BeginMenu(tr("+ Text")))
    {
        if (ImGui::MenuItem(tr("Concatenate"))) add(NodeKind::Concatenate);
        ImGui::EndMenu();
    }

    if (ImGui::Button(tr("Run")))
    {
        auto started = debugger.start(graph);
        if (!started) reportError(started.error().message);
        else reportLogicResult(debugger.runToEnd(), reportError);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Step")))
    {
        if (debugger.state() != DebugState::Running)
        {
            const auto started = debugger.start(graph);
            if (!started) reportError(started.error().message);
        }
        if (debugger.state() == DebugState::Running)
        {
            const auto stepped = debugger.step();
            if (!stepped) reportError(stepped.error().message);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset debug"))) debugger.reset();
    ImGui::SameLine();
    ImGui::TextDisabled(tr("Debugger: %s"), debugger.state() == DebugState::Idle ? "idle"
        : debugger.state() == DebugState::Running ? tr("running")
        : debugger.state() == DebugState::Completed ? tr("complete") : tr("faulted"));

    const auto available = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("LogicNodes", ImVec2(0, available.y), true);
    for (const auto& node : graph.nodes())
    {
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::Separator();
        ImGui::Text(tr("Node %u — %s"), node.id, tr(nodeKindName(node.kind)));
        ImGui::SameLine();
        ImGui::TextDisabled(tr("Output: %s"), tr(valueTypeName(outputType(node.kind))));

        if (node.kind == NodeKind::NumberConstant)
        {
            auto value = std::get<double>(node.literal);
            if (ImGui::InputDouble(tr("Value"), &value))
            {
                const auto changed = graph.setLiteral(node.id, value);
                if (!changed) reportError(changed.error().message);
                else debugger.reset();
            }
        }
        else if (node.kind == NodeKind::BooleanConstant)
        {
            auto value = std::get<bool>(node.literal);
            if (ImGui::Checkbox(tr("Value"), &value))
            {
                const auto changed = graph.setLiteral(node.id, value);
                if (!changed) reportError(changed.error().message);
                else debugger.reset();
            }
        }
        else if (node.kind == NodeKind::StringConstant)
        {
            std::array<char, 256> text{};
            const auto& value = std::get<std::string>(node.literal);
            std::copy_n(value.data(), std::min(value.size(), text.size() - 1), text.data());
            if (ImGui::InputText(tr("Value"), text.data(), text.size()))
            {
                const auto changed = graph.setLiteral(node.id, std::string(text.data()));
                if (!changed) reportError(changed.error().message);
                else debugger.reset();
            }
        }

        for (std::size_t input = 0; input < node.inputs.size(); ++input)
        {
            const auto expected = inputType(node.kind, input);
            if (!expected) continue;
            std::string selected = tr("<unconnected>");
            if (node.inputs[input])
            {
                if (const auto* source = graph.find(*node.inputs[input]))
                {
                    std::array<char, 256> text{};
                    std::snprintf(text.data(), text.size(), tr("Node %u — %s"), source->id,
                        tr(nodeKindName(source->kind)));
                    selected = text.data();
                }
            }
            std::array<char, 128> inputLabel{};
            std::snprintf(inputLabel.data(), inputLabel.size(), tr("Input %u (%s)"),
                static_cast<unsigned>(input + 1), tr(valueTypeName(*expected)));
            const auto label = std::string(inputLabel.data());
            if (ImGui::BeginCombo(label.c_str(), selected.c_str()))
            {
                if (ImGui::Selectable(tr("<unconnected>")))
                {
                    const auto changed = graph.disconnect(node.id, input);
                    if (!changed) reportError(changed.error().message);
                    else debugger.reset();
                }
                for (const auto& candidate : graph.nodes())
                {
                    if (candidate.id == node.id || outputType(candidate.kind) != *expected) continue;
                    std::array<char, 256> candidateLabel{};
                    std::snprintf(candidateLabel.data(), candidateLabel.size(), tr("Node %u — %s"),
                        candidate.id, tr(nodeKindName(candidate.kind)));
                    if (ImGui::Selectable(candidateLabel.data(), node.inputs[input] == candidate.id))
                    {
                        const auto disconnected = graph.disconnect(node.id, input);
                        if (!disconnected) reportError(disconnected.error().message);
                        else
                        {
                            debugger.reset();
                            const auto changed = graph.connect(candidate.id, node.id, input);
                            if (!changed) reportError(changed.error().message);
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (const auto* state = debugger.trace(node.id))
        {
            if (state->state == NodeState::Succeeded && state->output)
                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), tr("Output: %s"), formatValue(*state->output).c_str());
            else if (state->state == NodeState::Failed)
            {
                const auto diagnostic = astro::editor::translateEditorDiagnostic(state->diagnostic, gEditorLanguage);
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), tr("Error: %s"), diagnostic.c_str());
            }
            else ImGui::TextDisabled(tr("Debug state: pending"));
        }
        if (ImGui::SmallButton(tr("Remove node")))
        {
            const auto removed = graph.removeNode(node.id);
            if (!removed) reportError(removed.error().message);
            else debugger.reset();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

ImU32 uiColor(const std::uint32_t rgba)
{
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}

void drawUiEditor(astro::ui::UiDocument& document, const Scene& scene,
    std::optional<astro::ui::ElementId>& selected, int& viewportPreset,
    std::array<char, 4097>& textDraft, std::optional<astro::ui::ElementId>& draftElement,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    using namespace astro::ui;
    if (!ImGui::Begin(tr("UI Editor"))) { ImGui::End(); return; }
    windowDrawn = true;

    static constexpr const char* themeNames[]{"Dark", "Light"};
    int themeIndex = document.theme() == ThemeKind::Light ? 1 : 0;
    if (ImGui::Combo(tr("Theme"), &themeIndex, themeNames, 2))
    {
        const auto changed = document.setTheme(themeIndex == 0 ? ThemeKind::Dark : ThemeKind::Light);
        if (!changed) reportError(changed.error().message);
    }
    static constexpr const char* viewportNames[]{"Desktop 1280 x 720", "Tablet 800 x 1000", "Phone 390 x 844"};
    ImGui::Combo(tr("Preview size"), &viewportPreset, viewportNames, 3);

    const auto create = [&](const ElementKind kind, const char* name) {
        const auto added = document.addElement(kind, name);
        if (!added) reportError(added.error().message);
        else { selected = added.value(); draftElement.reset(); }
    };
    if (ImGui::Button(tr("+ Panel"))) create(ElementKind::Panel, "Panel");
    ImGui::SameLine();
    if (ImGui::Button(tr("+ Text"))) create(ElementKind::Text, "Text");
    ImGui::SameLine();
    if (ImGui::Button(tr("+ Button"))) create(ElementKind::Button, "Button");
    ImGui::SameLine();
    ImGui::TextDisabled(tr("UI layout is an in-memory editor document in 06.2."));

    const auto body = ImGui::GetContentRegionAvail();
    const float listWidth = std::clamp(body.x * 0.22f, 150.0f, 230.0f);
    if (ImGui::BeginChild("UiElementList", ImVec2(listWidth, body.y), true))
    {
        for (const auto& element : document.elements())
        {
            if (ImGui::Selectable((element.name + "  [" + std::string(tr(elementKindName(element.kind).data())) + "]##ui"
                + std::to_string(element.id)).c_str(), selected == element.id))
            {
                selected = element.id;
                draftElement.reset();
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    const float inspectorWidth = std::clamp(body.x * 0.30f, 230.0f, 330.0f);
    if (ImGui::BeginChild("UiInspector", ImVec2(inspectorWidth, body.y), true))
    {
        const auto* element = selected ? document.find(*selected) : nullptr;
        if (!element) ImGui::TextDisabled(tr("Create or select an element."));
        else
        {
        ImGui::Text(tr("%s — %s"), element->name.c_str(), tr(elementKindName(element->kind).data()));
            if (ImGui::Button(tr("Remove selected element")))
            {
                const auto removed = document.removeElement(element->id);
                if (!removed) reportError(removed.error().message);
                else { selected.reset(); draftElement.reset(); element = nullptr; }
            }
            if (!element)
            {
                ImGui::TextDisabled(tr("Element removed."));
            }
            else
            {
                ImGui::SeparatorText(tr("Content"));
                if (element->kind != ElementKind::Panel)
                {
                    if (draftElement != selected)
                    {
                        textDraft.fill('\0');
                        std::copy_n(element->text.data(), std::min(element->text.size(), textDraft.size() - 1), textDraft.data());
                        draftElement = selected;
                    }
                    const bool boundText = element->binding && element->binding->target == BindingTarget::Text;
                    ImGui::BeginDisabled(boundText);
                    if (ImGui::InputTextMultiline(tr("Text"), textDraft.data(), textDraft.size(), ImVec2(-1.0f, 68.0f)))
                    {
                        const auto changed = document.setText(element->id, textDraft.data());
                        if (!changed) reportError(changed.error().message);
                    }
                    ImGui::EndDisabled();
                    if (boundText) ImGui::TextDisabled(tr("Text is driven by its scene binding."));
                }

                ImGui::SeparatorText(tr("Responsive layout"));
                ResponsiveRect layout = element->layout;
                bool layoutChanged = false;
                ImGui::TextDisabled(tr("Anchors are normalized to the parent area; offsets are pixels."));
                layoutChanged |= ImGui::InputDouble(tr("Anchor min X"), &layout.anchorMinX, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble(tr("Anchor min Y"), &layout.anchorMinY, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble(tr("Anchor max X"), &layout.anchorMaxX, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble(tr("Anchor max Y"), &layout.anchorMaxY, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble(tr("Offset min X"), &layout.offsetMinX, 1.0, 10.0, "%.0f");
                layoutChanged |= ImGui::InputDouble(tr("Offset min Y"), &layout.offsetMinY, 1.0, 10.0, "%.0f");
                layoutChanged |= ImGui::InputDouble(tr("Offset max X"), &layout.offsetMaxX, 1.0, 10.0, "%.0f");
                layoutChanged |= ImGui::InputDouble(tr("Offset max Y"), &layout.offsetMaxY, 1.0, 10.0, "%.0f");
                if (layoutChanged)
                {
                    const auto changed = document.setLayout(element->id, layout);
                    if (!changed) reportError(changed.error().message);
                }

                ImGui::SeparatorText(tr("Scene binding"));
                std::vector<const char*> entityLabels;
                entityLabels.reserve(scene.entities().size());
                int entityIndex = 0;
                for (std::size_t i = 0; i < scene.entities().size(); ++i)
                {
                    entityLabels.push_back(scene.entities()[i].name.c_str());
                    if (element->binding && scene.entities()[i].id == element->binding->entity)
                        entityIndex = static_cast<int>(i);
                }
                if (entityLabels.empty()) ImGui::TextDisabled(tr("Create a scene entity to bind values."));
                else
                {
                    ImGui::Combo(tr("Entity"), &entityIndex, entityLabels.data(), static_cast<int>(entityLabels.size()));
                    static constexpr const char* textBindingNames[]{"No binding", "Entity.Name → Text"};
                    static constexpr const char* visibleBindingNames[]{"No binding", "SpriteRenderer.Visible → Visible"};
                    const bool textAllowed = element->kind == ElementKind::Text || element->kind == ElementKind::Button;
                    const auto* boundEntity = scene.find(scene.entities()[static_cast<std::size_t>(entityIndex)].id);
                    const bool visibleAllowed = boundEntity && scene.spriteRenderer(boundEntity->id);
                    int bindingIndex = 0;
                    if (element->binding && element->binding->entity == scene.entities()[static_cast<std::size_t>(entityIndex)].id)
                    {
                        if (element->binding->source == BindingSource::EntityName) bindingIndex = 1;
                        else if (element->binding->source == BindingSource::SpriteVisible) bindingIndex = 1;
                    }
                    const char* const* labels = textAllowed ? textBindingNames : visibleBindingNames;
                    if (textAllowed || visibleAllowed)
                    {
                        if (ImGui::Combo(tr("Bind property"), &bindingIndex, labels, 2))
                        {
                            std::optional<UiBinding> binding;
                            if (bindingIndex == 1)
                                binding = textAllowed
                                    ? UiBinding{boundEntity->id, BindingSource::EntityName, BindingTarget::Text}
                                    : UiBinding{boundEntity->id, BindingSource::SpriteVisible, BindingTarget::Visible};
                            const auto changed = document.setBinding(element->id, binding, scene);
                            if (!changed) reportError(changed.error().message);
                        }
                    }
                    else ImGui::TextDisabled(tr("This element has no supported binding target for this entity."));
                }
                if (element->binding && ImGui::Button(tr("Clear binding")))
                {
                    const auto cleared = document.setBinding(element->id, std::nullopt, scene);
                    if (!cleared) reportError(cleared.error().message);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("UiPreview", ImVec2(0, body.y), true))
    {
        const double viewportWidth = viewportPreset == 0 ? 1280.0 : viewportPreset == 1 ? 800.0 : 390.0;
        const double viewportHeight = viewportPreset == 0 ? 720.0 : viewportPreset == 1 ? 1000.0 : 844.0;
        ImGui::Text(tr("Preview — %.0f x %.0f"), viewportWidth, viewportHeight);
        ImGui::TextDisabled(tr("Change preview size to check anchors and offsets."));
        const auto available = ImGui::GetContentRegionAvail();
        const double scale = std::min(static_cast<double>(available.x) / viewportWidth,
            static_cast<double>(available.y) / viewportHeight);
        const double canvasWidth = viewportWidth * std::max(0.01, scale);
        const double canvasHeight = viewportHeight * std::max(0.01, scale);
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("UiPreviewCanvas", ImVec2(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight)));
        auto* draw = ImGui::GetWindowDrawList();
        const auto theme = document.resolvedTheme();
        const ImVec2 canvasMax(origin.x + static_cast<float>(canvasWidth), origin.y + static_cast<float>(canvasHeight));
        draw->AddRectFilled(origin, canvasMax, uiColor(theme.background));
        draw->PushClipRect(origin, canvasMax, true);
        for (const auto& item : document.elements())
        {
            if (!item.visible) continue;
            const auto resolved = document.resolveLayout(item.id, viewportWidth, viewportHeight);
            if (!resolved) { reportError(resolved.error().message); continue; }
            const auto& rect = resolved.value();
            const ImVec2 min(origin.x + static_cast<float>(rect.x * scale), origin.y + static_cast<float>(rect.y * scale));
            const ImVec2 max(min.x + static_cast<float>(rect.width * scale), min.y + static_cast<float>(rect.height * scale));
            if (item.kind == ElementKind::Panel) draw->AddRectFilled(min, max, uiColor(theme.surface), 3.0f);
            else if (item.kind == ElementKind::Button) draw->AddRectFilled(min, max, uiColor(theme.accent), 4.0f);
            const auto& caption = item.text.empty() ? item.name : item.text;
            draw->AddText(ImVec2(min.x + 5.0f, min.y + 5.0f), uiColor(theme.text), caption.c_str());
            if (selected == item.id) draw->AddRect(min, max, IM_COL32(255, 205, 90, 255), 2.0f, 0, 2.0f);
        }
        draw->PopClipRect();
    }
    ImGui::EndChild();
    ImGui::End();
}

void drawTimelineEditor(astro::runtime::AnimationTimeline& timeline, const float deltaSeconds,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    if (!ImGui::Begin(tr("Timeline"))) { ImGui::End(); return; }
    windowDrawn = true;
    if (timeline.isPlaying())
    {
        const auto advanced = timeline.advance(deltaSeconds);
        if (!advanced) reportError(advanced.error().message);
    }
    if (ImGui::Button(tr(timeline.isPlaying() ? "Pause" : "Play")))
    {
        if (timeline.isPlaying()) timeline.pause();
        else timeline.play();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset"))) timeline.reset();
    ImGui::SameLine();
    if (ImGui::Button(tr("Add key at end")))
    {
        const auto added = timeline.addKeyframe({timeline.endTime() + 1.0, timeline.sample()});
        if (!added) reportError(added.error().message);
    }
    ImGui::Text(tr("Scalar track — %.3f"), timeline.sample());
    double playhead = timeline.playhead();
    const double start = timeline.startTime(), end = timeline.endTime();
    if (start < end && ImGui::SliderScalar(tr("Playhead"), ImGuiDataType_Double, &playhead, &start, &end, "%.2f s"))
    {
        const auto sought = timeline.seek(playhead);
        if (!sought) reportError(sought.error().message);
    }

    const auto graphSize = ImVec2(-1.0f, 120.0f);
    if (ImGui::BeginChild("TimelineCurve", graphSize, true))
    {
        const auto origin = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 max(origin.x + available.x, origin.y + available.y);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, max, IM_COL32(24, 29, 38, 255));
        double minimum = timeline.keyframes().front().value;
        double maximum = minimum;
        for (const auto& key : timeline.keyframes())
        {
            minimum = std::min(minimum, key.value);
            maximum = std::max(maximum, key.value);
        }
        if (minimum == maximum) { minimum -= 1.0; maximum += 1.0; }
        const double timeRange = end - start;
        const auto point = [&](const double time) {
            const double x = (time - start) / timeRange;
            const double y = (timeline.sampleAt(time) - minimum) / (maximum - minimum);
            return ImVec2(origin.x + static_cast<float>(x * available.x),
                max.y - static_cast<float>(y * available.y));
        };
        ImVec2 previous = point(start);
        for (int sample = 1; sample <= 64; ++sample)
        {
            const double time = start + timeRange * static_cast<double>(sample) / 64.0;
            const ImVec2 current = point(time);
            draw->AddLine(previous, current, IM_COL32(100, 175, 255, 255), 2.0f);
            previous = current;
        }
        for (const auto& key : timeline.keyframes())
        {
            const double x = (key.timeSeconds - start) / timeRange;
            const double y = (key.value - minimum) / (maximum - minimum);
            draw->AddCircleFilled(ImVec2(origin.x + static_cast<float>(x * available.x),
                max.y - static_cast<float>(y * available.y)), 4.0f, IM_COL32(255, 205, 90, 255));
        }
        const double headX = (timeline.playhead() - start) / timeRange;
        const float x = origin.x + static_cast<float>(headX * available.x);
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, max.y), IM_COL32(255, 90, 90, 220), 1.0f);
    }
    ImGui::EndChild();

    ImGui::SeparatorText(tr("Keyframes"));
    bool removed = false;
    for (std::size_t index = 0; index < timeline.keyframes().size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        double keyTime = timeline.keyframes()[index].timeSeconds;
        double value = timeline.keyframes()[index].value;
        bool changed = ImGui::InputDouble(tr("Time"), &keyTime, 0.01, 0.1, "%.3f");
        ImGui::SameLine();
        changed |= ImGui::InputDouble(tr("Value"), &value, 0.1, 1.0, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Remove")))
        {
            const auto result = timeline.removeKeyframe(index);
            if (!result) reportError(result.error().message);
            removed = true;
        }
        else if (changed)
        {
            const auto result = timeline.updateKeyframe(index, {keyTime, value});
            if (!result) reportError(result.error().message);
        }
        ImGui::PopID();
        if (removed) break;
    }
    ImGui::End();
}

astro::core::Result<void> testCheckpointRestore(const astro::save_editor::SaveSystemDocument& document,
    const Scene& source, const EntityId checkpointId)
{
    using namespace astro;
    const auto profiles = document.compileProfiles(source);
    if (!profiles) return core::Result<void>::failure(profiles.error());
    const auto checkpoints = document.validateCheckpoints(source, profiles.value());
    if (!checkpoints) return core::Result<void>::failure(checkpoints.error());
    const auto definition = document.checkpoints().find(checkpointId);
    if (definition == document.checkpoints().end())
        return core::Result<void>::failure({core::ErrorCode::NotFound, "Select an authored checkpoint first"});
    EntityId actor = 0;
    for (const auto& entity : source.entities()) if (entity.id != checkpointId) { actor = entity.id; break; }
    if (!actor) return core::Result<void>::failure({core::ErrorCode::InvalidState, "Create another scene entity to act as the checkpoint player"});
    if (definition->second.mode == runtime::CheckpointMode::SaveSelectedProfile)
    {
        const auto profile = profiles.value().find(definition->second.saveProfileId);
        if (profile == profiles.value().end() || !profile->second.fields().contains(actor)
            || std::find(profile->second.fields().at(actor).begin(), profile->second.fields().at(actor).end(),
                runtime::SaveProperty::entityName) == profile->second.fields().at(actor).end())
            return core::Result<void>::failure({core::ErrorCode::ValidationError,
                "Restore preview needs entity.name explicitly selected for the test actor in this profile"});
    }

    auto root = std::filesystem::temp_directory_path()
        / ("AstroEngine-Checkpoint-Preview-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    if (!std::filesystem::create_directory(root, ec) || ec)
        return core::Result<void>::failure({core::ErrorCode::IoError, "Could not create isolated checkpoint preview directory"});
    struct OwnedDirectory
    {
        std::filesystem::path path;
        ~OwnedDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};

    auto storeResult = runtime::SaveStore::open(root, 100);
    if (!storeResult) return core::Result<void>::failure(storeResult.error());
    auto store = std::move(storeResult).value();
    runtime::CheckpointSystem system;
    auto registered = system.registerCheckpoint(source, definition->second);
    if (!registered) return registered;
    auto preview = source;
    auto actorTransform = preview.transform(actor)->value;
    actorTransform.position.x = -100000.0;
    if (!preview.setTransform(actor, actorTransform))
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Could not position preview actor outside checkpoint"});
    core::EventBus events;
    runtime::CheckpointSaveContext context{&store, &profiles.value(), "editor-preview"};
    auto update = system.update(preview, actor, events,
        definition->second.mode == runtime::CheckpointMode::SaveSelectedProfile ? &context : nullptr);
    if (!update) return core::Result<void>::failure(update.error());
    actorTransform.position = preview.transform(checkpointId)->value.position;
    if (!preview.setTransform(actor, actorTransform))
        return core::Result<void>::failure({core::ErrorCode::ValidationError, "Could not move preview actor into checkpoint"});
    update = system.update(preview, actor, events,
        definition->second.mode == runtime::CheckpointMode::SaveSelectedProfile ? &context : nullptr);
    if (!update || update.value().activated != 1)
        return core::Result<void>::failure(update ? core::Error{core::ErrorCode::InvalidState, "Checkpoint did not activate"} : update.error());
    if (definition->second.mode == runtime::CheckpointMode::SaveSelectedProfile)
    {
        preview.find(actor)->name = "Preview mutated";
        auto restored = system.respawn(preview, actor,
            {runtime::RespawnRestoreMode::SelectedProfile, {}}, events, &context);
        if (!restored) return core::Result<void>::failure(restored.error());
        if (preview.find(actor)->name != source.find(actor)->name)
            return core::Result<void>::failure({core::ErrorCode::ValidationError, "Selected profile did not restore the actor name"});
    }
    else
    {
        auto restored = system.respawn(preview, actor,
            {runtime::RespawnRestoreMode::PositionOnly, {}}, events);
        if (!restored) return core::Result<void>::failure(restored.error());
    }
    return core::Result<void>::success();
}

void drawSaveSystemEditor(astro::save_editor::SaveSystemDocument& document, const Scene& scene,
    const std::optional<EntityId> selectedEntity, const std::function<void(const std::string&)>& reportError,
    bool& windowDrawn)
{
    using namespace astro::runtime;
    if (!ImGui::Begin(tr("Save System"))) { ImGui::End(); return; }
    windowDrawn = true;
    ImGui::TextDisabled(tr("Explicitly selected fields only. Authoring currently stays in editor memory."));
    static std::array<char, 65> groupId{"progress"};
    static std::array<char, 65> profileId{"player-progress"};
    ImGui::SeparatorText(tr("Groups"));
    ImGui::InputText(tr("New group ID"), groupId.data(), groupId.size());
    ImGui::SameLine();
    if (ImGui::Button(tr("Create group")))
    {
        auto result = document.createGroup(groupId.data());
        if (!result) reportError(result.error().message);
    }
    static int groupIndex = 0;
    std::vector<const char*> groups;
    for (const auto& [id, value] : document.groups()) { (void)value; groups.push_back(id.c_str()); }
    if (!groups.empty())
    {
        groupIndex = std::clamp(groupIndex, 0, static_cast<int>(groups.size()) - 1);
        ImGui::Combo(tr("Group"), &groupIndex, groups.data(), static_cast<int>(groups.size()));
        const auto& group = std::next(document.groups().begin(), groupIndex)->second;
        ImGui::Text(tr("Members: %zu"), group.entities.size());
        if (selectedEntity && scene.find(*selectedEntity))
        {
            const bool member = group.entities.contains(*selectedEntity);
            if (ImGui::Button(tr(member ? "Remove selected entity from group" : "Add selected entity to group")))
            {
                auto result = document.setGroupMember(group.id, *selectedEntity, !member);
                if (!result) reportError(result.error().message);
            }
        }
        else ImGui::TextDisabled(tr("Select a Hierarchy entity to edit group membership."));
    }
    else ImGui::TextDisabled(tr("Groups are explicit entity lists; no tag inference is performed."));

    ImGui::SeparatorText(tr("Save profiles"));
    ImGui::InputText(tr("New profile ID"), profileId.data(), profileId.size());
    ImGui::SameLine();
    if (ImGui::Button(tr("Create empty profile")))
    {
        auto result = document.createProfile(profileId.data());
        if (!result) reportError(result.error().message);
    }
    static int profileIndex = 0;
    std::vector<const char*> profiles;
    for (const auto& [id, value] : document.profiles()) { (void)value; profiles.push_back(id.c_str()); }
    static constexpr const char* properties[]{"entity.name", "transform.position.x", "transform.position.y",
        "transform.position.z", "transform.rotation.x", "transform.rotation.y", "transform.rotation.z",
        "transform.scale.x", "transform.scale.y", "transform.scale.z", "sprite_renderer.visible"};
    static int propertyIndex = 0;
    if (!profiles.empty())
    {
        profileIndex = std::clamp(profileIndex, 0, static_cast<int>(profiles.size()) - 1);
        ImGui::Combo(tr("Profile"), &profileIndex, profiles.data(), static_cast<int>(profiles.size()));
        const auto& profile = std::next(document.profiles().begin(), profileIndex)->second;
        ImGui::Combo(tr("Property"), &propertyIndex, properties, static_cast<int>(std::size(properties)));
        if (selectedEntity && scene.find(*selectedEntity))
        {
            const bool fieldSelected = profile.fields.contains(*selectedEntity)
                && profile.fields.at(*selectedEntity).contains(properties[propertyIndex]);
            if (ImGui::Button(tr(fieldSelected ? "Remove field from selected entity" : "Save field on selected entity")))
            {
                auto result = document.setField(profile.id, *selectedEntity, properties[propertyIndex], !fieldSelected);
                if (!result) reportError(result.error().message);
            }
            ImGui::SameLine();
            ImGui::TextDisabled(tr("Group rule:"));
            if (!groups.empty())
            {
                const auto& group = std::next(document.groups().begin(), groupIndex)->second;
                const auto rules = profile.groupFields.find(group.id);
                const bool enabled = rules != profile.groupFields.end() && rules->second.contains(properties[propertyIndex]);
                if (ImGui::SmallButton(tr(enabled ? "Remove group rule" : "Add group rule")))
                {
                    auto result = document.setGroupField(profile.id, group.id, properties[propertyIndex], !enabled);
                    if (!result) reportError(result.error().message);
                }
            }
            else ImGui::TextDisabled(tr("create a group first"));
        }
        else ImGui::TextDisabled(tr("Select a scene entity to assign explicit fields."));
        const auto compiled = document.compileProfiles(scene);
        if (!compiled)
        {
            const auto diagnostic = astro::editor::translateEditorDiagnostic(compiled.error().message, gEditorLanguage);
            ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), tr("Profile validation: %s"), diagnostic.c_str());
        }
        else
        {
            const auto& effective = compiled.value().at(profile.id).fields();
            std::size_t count = 0;
            for (const auto& [entity, fields] : effective) { (void)entity; count += fields.size(); }
            ImGui::Text(tr("Effective selected fields: %zu"), count);
            for (const auto& [entity, fields] : effective)
                for (const auto& field : fields)
                    ImGui::BulletText(tr("Entity %llu — %s"), static_cast<unsigned long long>(entity), field.c_str());
        }
    }
    else ImGui::TextDisabled(tr("Profiles start empty; add fields explicitly."));

    ImGui::SeparatorText(tr("Checkpoints"));
    if (selectedEntity && scene.find(*selectedEntity) && scene.transform(*selectedEntity))
    {
        auto found = document.checkpoints().find(*selectedEntity);
        const bool checkpointExists = found != document.checkpoints().end();
        CheckpointDefinition definition;
        if (checkpointExists) definition = found->second;
        else { definition.entity = *selectedEntity; definition.respawnPosition = scene.transform(*selectedEntity)->value.position; }
        ImGui::Text(tr("Checkpoint entity: %llu"), static_cast<unsigned long long>(*selectedEntity));
        bool changed = false;
        changed |= ImGui::InputDouble(tr("Area half-width"), &definition.area.halfWidth, 0.1, 1.0, "%.2f");
        changed |= ImGui::InputDouble(tr("Area half-height"), &definition.area.halfHeight, 0.1, 1.0, "%.2f");
        changed |= ImGui::InputDouble(tr("Respawn X"), &definition.respawnPosition.x, 0.1, 1.0, "%.2f");
        changed |= ImGui::InputDouble(tr("Respawn Y"), &definition.respawnPosition.y, 0.1, 1.0, "%.2f");
        changed |= ImGui::InputDouble(tr("Respawn Z"), &definition.respawnPosition.z, 0.1, 1.0, "%.2f");
        int mode = static_cast<int>(definition.mode);
        const char* activationOptions[]{tr("Respawn only"), tr("Save selected profile")};
        if (ImGui::Combo(tr("Activation"), &mode, activationOptions, 2))
        { definition.mode = static_cast<CheckpointMode>(mode); if (definition.mode == CheckpointMode::RespawnOnly) definition.saveProfileId.clear(); changed = true; }
        int repeat = static_cast<int>(definition.repeatMode);
        const char* repeatOptions[]{tr("Repeatable"), tr("One shot")};
        if (ImGui::Combo(tr("Repeat"), &repeat, repeatOptions, 2)) { definition.repeatMode = static_cast<CheckpointRepeatMode>(repeat); changed = true; }
        if (definition.mode == CheckpointMode::SaveSelectedProfile)
        {
            if (!profiles.empty())
            {
                const auto& selectedProfile = std::next(document.profiles().begin(), profileIndex)->first;
                std::array<char, 256> useProfileLabel{};
                std::snprintf(useProfileLabel.data(), useProfileLabel.size(), tr("Use profile: %s"), selectedProfile.c_str());
                if (ImGui::Button(useProfileLabel.data())) { definition.saveProfileId = selectedProfile; changed = true; }
            }
        ImGui::Text(tr("Profile: %s"), definition.saveProfileId.empty() ? tr("(select a profile)") : definition.saveProfileId.c_str());
            int slot = static_cast<int>(definition.slot);
            if (ImGui::InputInt(tr("Save slot"), &slot)) { definition.slot = static_cast<std::size_t>(std::max(slot, 0)); changed = true; }
        }
        ImGui::Checkbox(tr("Enabled"), &definition.enabled);
        changed |= ImGui::IsItemEdited();
        if (ImGui::Button(tr(checkpointExists ? "Apply checkpoint" : "Add / validate checkpoint"))) changed = true;
        if (changed)
        {
            auto result = document.setCheckpoint(definition);
            if (!result) reportError(result.error().message);
            else
            {
                auto compiled = document.compileProfiles(scene);
                if (compiled)
                {
                    auto valid = document.validateCheckpoints(scene, compiled.value());
                    if (!valid) reportError(valid.error().message);
                }
                else reportError(compiled.error().message);
            }
        }
        bool removedCheckpoint = false;
        if (checkpointExists && ImGui::Button(tr("Remove checkpoint")))
        {
            auto result = document.removeCheckpoint(*selectedEntity);
            if (!result) reportError(result.error().message);
            removedCheckpoint = static_cast<bool>(result);
        }
        if (checkpointExists && !removedCheckpoint && ImGui::Button(tr("Test checkpoint activation and restore")))
        {
            const auto tested = testCheckpointRestore(document, scene, *selectedEntity);
            if (!tested) reportError(tested.error().message);
        }
    }
    else ImGui::TextDisabled(tr("Select a scene entity with Transform to author its checkpoint."));

    ImGui::SeparatorText(tr("Checkpoint XY map"));
    const auto mapSize = ImVec2(-1.0f, 150.0f);
    if (ImGui::BeginChild("CheckpointMap", mapSize, true))
    {
        const auto origin = ImGui::GetCursorScreenPos();
        const auto size = ImGui::GetContentRegionAvail();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(24, 29, 38, 255));
        constexpr float scale = 8.0f;
        const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
        for (const auto& entity : scene.entities())
        {
            const auto* transform = scene.transform(entity.id);
            if (!transform) continue;
            const ImVec2 point(center.x + static_cast<float>(transform->value.position.x) * scale,
                center.y - static_cast<float>(transform->value.position.y) * scale);
            draw->AddCircleFilled(point, entity.id == selectedEntity.value_or(0) ? 5.0f : 3.0f,
                entity.id == selectedEntity.value_or(0) ? IM_COL32(255, 210, 90, 255) : IM_COL32(120, 180, 230, 255));
            draw->AddText(ImVec2(point.x + 5, point.y), IM_COL32(225, 230, 240, 255), entity.name.c_str());
            const auto checkpoint = document.checkpoints().find(entity.id);
            if (checkpoint != document.checkpoints().end())
                draw->AddRect(ImVec2(point.x - static_cast<float>(checkpoint->second.area.halfWidth) * scale,
                    point.y - static_cast<float>(checkpoint->second.area.halfHeight) * scale),
                    ImVec2(point.x + static_cast<float>(checkpoint->second.area.halfWidth) * scale,
                        point.y + static_cast<float>(checkpoint->second.area.halfHeight) * scale), IM_COL32(100, 230, 130, 230));
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled(tr("XY preview only; does not move Scene entities or render gameplay triggers."));
    ImGui::End();
}

void drawCameraPresets(astro::runtime::CameraRig& camera, const Scene& scene,
    const std::optional<EntityId> selectedEntity, const float deltaSeconds,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    using astro::runtime::CameraMode;
    if (!ImGui::Begin(tr("Camera Presets"))) { ImGui::End(); return; }
    windowDrawn = true;
    static constexpr const char* modes[]{"Custom", "First Person", "Third Person", "Isometric", "Top Down", "Platformer", "Strategy"};
    int mode = static_cast<int>(camera.settings().mode);
    if (ImGui::Combo(tr("Preset"), &mode, modes, 7))
    {
        const auto changed = camera.setMode(static_cast<CameraMode>(mode));
        if (!changed) reportError(changed.error().message);
    }
    std::vector<const char*> entityNames;
    entityNames.reserve(scene.entities().size());
    int entityIndex = 0;
    for (std::size_t index = 0; index < scene.entities().size(); ++index)
    {
        entityNames.push_back(scene.entities()[index].name.c_str());
        if (camera.settings().target == scene.entities()[index].id) entityIndex = static_cast<int>(index);
    }
    if (!entityNames.empty())
    {
        if (ImGui::Combo(tr("Follow entity"), &entityIndex, entityNames.data(), static_cast<int>(entityNames.size())))
        {
            auto settings = camera.settings();
            settings.target = scene.entities()[static_cast<std::size_t>(entityIndex)].id;
            const auto configured = camera.configure(settings);
            if (!configured) reportError(configured.error().message);
            else if (camera.settings().mode != CameraMode::Custom)
                reportLogicResult(camera.snapToTarget(scene), reportError);
        }
    }
    else ImGui::TextDisabled(tr("Create a scene entity to use a follow preset."));
    if (selectedEntity && scene.find(*selectedEntity) && ImGui::Button(tr("Use selected Hierarchy entity")))
    {
        auto settings = camera.settings();
        settings.target = selectedEntity;
        const auto configured = camera.configure(settings);
        if (!configured) reportError(configured.error().message);
        else if (camera.settings().mode != CameraMode::Custom)
            reportLogicResult(camera.snapToTarget(scene), reportError);
    }
    if (camera.settings().mode != CameraMode::Custom && camera.settings().target
        && scene.find(*camera.settings().target))
    {
        const auto updated = camera.update(scene, deltaSeconds);
        if (!updated) reportError(updated.error().message);
    }
    else if (camera.settings().mode != CameraMode::Custom)
        ImGui::TextDisabled(tr("Select a valid target entity for this follow preset."));
    if (camera.settings().mode == CameraMode::Custom)
    {
        auto pose = camera.pose();
        bool changed = false;
        ImGui::SeparatorText(tr("Custom pose"));
        changed |= ImGui::InputDouble(tr("Position X"), &pose.position.x, 0.1, 1.0, "%.3f");
        changed |= ImGui::InputDouble(tr("Position Y"), &pose.position.y, 0.1, 1.0, "%.3f");
        changed |= ImGui::InputDouble(tr("Position Z"), &pose.position.z, 0.1, 1.0, "%.3f");
        changed |= ImGui::InputDouble(tr("Rotation X"), &pose.rotationDegrees.x, 1.0, 10.0, "%.2f");
        changed |= ImGui::InputDouble(tr("Rotation Y"), &pose.rotationDegrees.y, 1.0, 10.0, "%.2f");
        changed |= ImGui::InputDouble(tr("Rotation Z"), &pose.rotationDegrees.z, 1.0, 10.0, "%.2f");
        changed |= ImGui::InputDouble(tr("Zoom"), &pose.zoom, 0.1, 1.0, "%.2f");
        if (changed)
        {
            const auto updated = camera.setPose(pose);
            if (!updated) reportError(updated.error().message);
        }
    }
    const auto& pose = camera.pose();
    ImGui::SeparatorText(tr("Computed pose"));
    ImGui::Text(tr("Position: %.2f, %.2f, %.2f"), pose.position.x, pose.position.y, pose.position.z);
    ImGui::Text(tr("Rotation: %.2f, %.2f, %.2f"), pose.rotationDegrees.x, pose.rotationDegrees.y, pose.rotationDegrees.z);
    ImGui::Text(tr("Zoom: %.2f"), pose.zoom);
    ImGui::TextDisabled(tr("Pose preview only; camera image rendering is not connected."));
    ImGui::End();
}

struct SceneNodeLayout
{
    const Entity* entity;
    int depth;
    int row;
};

void drawInspector(Scene& scene, std::optional<EntityId>& selected, astro::core::CommandRegistry& commands,
    std::array<char, 256>& textDraft, std::optional<EntityId>& draftEntity,
    std::optional<astro::scene::SceneId>& draftScene, bool& sceneDirty, std::string& message,
    const astro::assets::AssetRegistry* assets, astro::assets::ScriptBuildReport& csharpBuildReport,
    std::uint64_t& selectedScriptAsset, bool& scriptAuthoringPanelDrawn,
    const std::map<std::uint64_t, bgfx::TextureHandle>& textures,
    const std::function<void(const std::string&)>& reportError)
{
    if (!selected || !scene.find(*selected))
    {
        draftEntity.reset();
        draftScene.reset();
        ImGui::TextDisabled(tr("Select an entity in Hierarchy or Scene View."));
        return;
    }

    const auto entityId = *selected;
    ImGui::Text(tr("Entity %llu"), static_cast<unsigned long long>(entityId));
    ImGui::Separator();
    for (const auto& property : astro::scene::entityPropertySchema())
    {
        const auto value = scene.getProperty(entityId, property.id);
        if (!value)
        {
            reportError(value.error().message);
            continue;
        }
        const std::string label(property.label);
        if (property.type == astro::scene::PropertyType::UnsignedInteger)
        {
            const auto* number = std::get_if<EntityId>(&value.value());
            if (number)
                ImGui::Text(tr("%s: %llu"), label.c_str(), static_cast<unsigned long long>(*number));
            continue;
        }

        const auto* text = std::get_if<std::string>(&value.value());
        if (!text) continue;
        if (draftEntity != entityId || draftScene != scene.id())
        {
            const auto count = std::min(text->size(), textDraft.size() - 1);
            std::copy_n(text->data(), count, textDraft.data());
            textDraft[count] = '\0';
            draftEntity = entityId;
            draftScene = scene.id();
        }
        ImGui::InputText(label.c_str(), textDraft.data(), textDraft.size());
        const std::string draft(textDraft.data());
        const bool changed = draft != *text;
        ImGui::BeginDisabled(!changed);
        const auto applyLabel = std::string(tr("Apply: ")) + label;
        if (ImGui::Button(applyLabel.c_str()))
        {
            astro::scene::SetScenePropertyPayload payload{entityId, std::string(property.id), draft};
            auto applied = commands.execute<astro::scene::SetScenePropertyPayload, void>(
                "entity.set_property", 1, payload);
            if (applied) { sceneDirty = true; message = tr("Property applied through entity.set_property"); }
            else reportError(applied.error().message);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(tr("UTF-8, max %zu bytes"), property.maxLengthBytes);
    }
    ImGui::SeparatorText(tr("C# ScriptBehavior"));
    scriptAuthoringPanelDrawn = true;
    const auto scriptIsCurrent = [&](const astro::assets::AssetId assetId, const std::string_view managedType) {
        if (!assets) return false;
        csharpBuildReport = assets->inspectScriptBuild();
        if (csharpBuildReport.freshness != astro::assets::ScriptBuildFreshness::Current) return false;
        const auto status = std::find_if(csharpBuildReport.bindings.begin(), csharpBuildReport.bindings.end(),
            [&](const auto& binding) { return binding.assetId && *binding.assetId == assetId; });
        const auto* registered = assets->findScript(assetId);
        if (status == csharpBuildReport.bindings.end() || !status->registered
            || status->managedTypeName != managedType || !registered
            || registered->managedTypeName != managedType
            || registered->sourceRelativePath != status->sourceRelativePath)
            return false;
        return static_cast<bool>(assets->resolveScript(assetId));
    };

    std::vector<const astro::assets::ScriptBuildBindingStatus*> scriptChoices;
    if (assets && csharpBuildReport.freshness == astro::assets::ScriptBuildFreshness::Current)
    {
        for (const auto& binding : csharpBuildReport.bindings)
        {
            if (!binding.registered || !binding.assetId) continue;
            const auto* registered = assets->findScript(*binding.assetId);
            if (registered && registered->managedTypeName == binding.managedTypeName
                && registered->sourceRelativePath == binding.sourceRelativePath)
                scriptChoices.push_back(&binding);
        }
    }
    if (!scriptChoices.empty())
    {
        const auto selectedChoice = std::find_if(scriptChoices.begin(), scriptChoices.end(),
            [&](const auto* binding) { return binding->assetId->value == selectedScriptAsset; });
        if (selectedChoice == scriptChoices.end()) selectedScriptAsset = scriptChoices.front()->assetId->value;
        const auto selectedLabel = [&] {
            const auto found = std::find_if(scriptChoices.begin(), scriptChoices.end(),
                [&](const auto* binding) { return binding->assetId->value == selectedScriptAsset; });
            if (found == scriptChoices.end()) return std::string(tr("Select a registered script"));
            return std::to_string((*found)->assetId->value) + " — " + (*found)->managedTypeName;
        }();
        if (ImGui::BeginCombo(tr("Registered script"), selectedLabel.c_str()))
        {
            for (const auto* binding : scriptChoices)
            {
                const auto label = std::to_string(binding->assetId->value) + " — "
                    + binding->managedTypeName + " — " + binding->sourceRelativePath;
                const bool isSelected = binding->assetId->value == selectedScriptAsset;
                if (ImGui::Selectable(label.c_str(), isSelected)) selectedScriptAsset = binding->assetId->value;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Add disabled script")))
        {
            const auto fresh = assets->inspectScriptBuild();
            csharpBuildReport = fresh;
            const auto binding = std::find_if(fresh.bindings.begin(), fresh.bindings.end(), [&](const auto& item) {
                return item.assetId && item.assetId->value == selectedScriptAsset;
            });
            if (fresh.freshness != astro::assets::ScriptBuildFreshness::Current
                || binding == fresh.bindings.end() || !binding->registered || !binding->assetId
                || !scriptIsCurrent(*binding->assetId, binding->managedTypeName))
                reportError("The selected C# script binding is no longer current; refresh and register the build again");
            else
            {
                const astro::scene::AddScriptBehaviorPayload payload{
                    entityId, *binding->assetId, binding->managedTypeName};
                const auto added = commands.execute<astro::scene::AddScriptBehaviorPayload, void>(
                    "script_behavior.add", 1, payload);
                if (!added) reportError(added.error().message);
                else
                {
                    sceneDirty = true;
                    message = tr("C# ScriptBehavior added disabled");
                }
            }
        }
    }
    else
        ImGui::TextDisabled(tr("No current registered C# scripts. Refresh and register a current build in Project."));

    const auto behaviors = std::vector<astro::scene::ScriptBehaviorComponent>(scene.scriptBehaviors(entityId).begin(),
        scene.scriptBehaviors(entityId).end());
    if (behaviors.empty()) ImGui::TextDisabled(tr("This entity has no ScriptBehavior components."));
    for (const auto& behavior : behaviors)
    {
        const auto componentScope = std::to_string(behavior.id);
        ImGui::PushID(componentScope.c_str());
        bool enabled = behavior.enabled;
        if (ImGui::Checkbox(tr("Enabled"), &enabled) && enabled != behavior.enabled)
        {
            bool mayEnable = true;
            if (enabled && !scriptIsCurrent(behavior.scriptAsset, behavior.managedTypeName))
            {
                mayEnable = false;
                reportError("Enable requires a current registered C# build for this script");
            }
            if (mayEnable)
            {
                const astro::scene::SetScriptBehaviorEnabledPayload payload{entityId, behavior.id, enabled};
                const auto changed = commands.execute<astro::scene::SetScriptBehaviorEnabledPayload, void>(
                    "script_behavior.set_enabled", 1, payload);
                if (!changed) reportError(changed.error().message);
                else
                {
                    sceneDirty = true;
                    message = enabled ? tr("C# ScriptBehavior enabled") : tr("C# ScriptBehavior disabled");
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextWrapped(tr("Component %llu — AssetId %llu — %s"),
            static_cast<unsigned long long>(behavior.id),
            static_cast<unsigned long long>(behavior.scriptAsset.value), behavior.managedTypeName.c_str());
        if (assets)
        {
            const auto* registered = assets->findScript(behavior.scriptAsset);
            if (!registered || registered->managedTypeName != behavior.managedTypeName)
                ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), tr("Script binding is missing or does not match the registry."));
            else
            {
                ImGui::TextWrapped(tr("Source: %s"), registered->sourceRelativePath.c_str());
                if (csharpBuildReport.freshness != astro::assets::ScriptBuildFreshness::Current)
                    ImGui::TextColored(ImVec4(1, 0.65f, 0.25f, 1), tr("C# build is not current; this binding cannot run."));
            }
        }
        if (ImGui::Button(tr("Remove script")))
        {
            const astro::scene::RemoveScriptBehaviorPayload payload{entityId, behavior.id};
            const auto removed = commands.execute<astro::scene::RemoveScriptBehaviorPayload, void>(
                "script_behavior.remove", 1, payload);
            if (!removed) reportError(removed.error().message);
            else
            {
                sceneDirty = true;
                message = tr("C# ScriptBehavior removed");
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(tr("SpriteRenderer"));
    if (!assets || assets->entries().empty()) ImGui::TextDisabled(tr("Import a PNG or JPEG in Project first."));
    else
    {
        const auto* sprite = scene.spriteRenderer(entityId);
        std::uint64_t selectedAsset = sprite ? sprite->texture.value : assets->entries().begin()->first.value;
        if (ImGui::BeginCombo(tr("Texture Asset"), std::to_string(selectedAsset).c_str()))
        {
            for (const auto& [id, reference] : assets->entries())
            {
                const bool isSelected = selectedAsset == id.value;
                const auto label = std::to_string(id.value) + " — " + reference.sourceRelativePath;
                if (ImGui::Selectable(label.c_str(), isSelected)) selectedAsset = id.value;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const bool hasMatchingAsset = assets->find({selectedAsset}) != nullptr;
        const bool changed = !sprite || sprite->texture.value != selectedAsset;
        ImGui::BeginDisabled(!changed || !hasMatchingAsset);
        if (ImGui::Button(tr(sprite ? "Assign texture" : "Add SpriteRenderer")))
        {
            const astro::scene::SetSpriteRendererPayload payload{entityId, {selectedAsset}, true};
            auto result = commands.execute<astro::scene::SetSpriteRendererPayload, void>(
                "entity.set_sprite_renderer", 1, payload);
            if (result) { sceneDirty = true; message = tr("SpriteRenderer assigned through command API"); }
            else reportError(result.error().message);
        }
        ImGui::EndDisabled();
        if (sprite)
        {
            const auto asset = assets->find(sprite->texture);
            if (!asset) ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), tr("Missing texture AssetId %llu"),
                static_cast<unsigned long long>(sprite->texture.value));
            else
            {
                const auto texture = textures.find(sprite->texture.value);
                if (texture != textures.end() && bgfx::isValid(texture->second))
                    ImGui::Image(static_cast<ImTextureID>(texture->second.idx + 1), ImVec2(160, 120));
                else ImGui::TextDisabled(tr("Image preview unavailable (decode/upload failed)"));
                ImGui::TextWrapped(tr("AssetId %llu — %s"), static_cast<unsigned long long>(asset->id.value),
                    asset->sourceRelativePath.c_str());
            }
        }
    }
}

void layoutSceneBranch(const Scene& scene, const std::optional<EntityId> parent, const int depth,
    int& nextRow, std::vector<SceneNodeLayout>& nodes)
{
    for (const auto& entity : scene.entities())
    {
        if (entity.parent != parent) continue;
        nodes.push_back({&entity, depth, nextRow++});
        layoutSceneBranch(scene, entity.id, depth + 1, nextRow, nodes);
    }
}

void drawSceneView(const Scene& scene, std::optional<EntityId>& selected)
{
    ImGui::Text(tr("%s — hierarchy preview"), scene.name().c_str());
    ImGui::TextDisabled(tr("Transform data is stored; this view remains structural and has no spatial rendering."));
    std::vector<SceneNodeLayout> nodes;
    int nextRow = 0;
    layoutSceneBranch(scene, std::nullopt, 0, nextRow, nodes);
    int maxDepth = 0;
    for (const auto& node : nodes) maxDepth = std::max(maxDepth, node.depth);
    const float nodeWidth = 150.0f, nodeHeight = 48.0f, xStep = 190.0f, yStep = 78.0f;
    ImGui::SetNextWindowContentSize(ImVec2(40.0f + maxDepth * xStep + nodeWidth,
        40.0f + std::max(1, nextRow) * yStep));
    if (!ImGui::BeginChild("SceneCanvas", ImVec2(0, 0), true))
    {
        ImGui::EndChild();
        return;
    }
    const auto origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    auto* draw = ImGui::GetWindowDrawList();
    const auto clip = ImGui::GetWindowPos();
    const auto clipEnd = ImVec2(clip.x + ImGui::GetWindowSize().x, clip.y + ImGui::GetWindowSize().y);
    draw->PushClipRect(clip, clipEnd, true);
    const auto gridRight = origin.x + std::max(available.x, 320.0f + maxDepth * xStep);
    const auto gridBottom = origin.y + std::max(available.y, 80.0f + nextRow * yStep);
    for (float x = origin.x; x < gridRight; x += 32.0f)
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, gridBottom), IM_COL32(255, 255, 255, 10));
    for (float y = origin.y; y < gridBottom; y += 32.0f)
        draw->AddLine(ImVec2(origin.x, y), ImVec2(gridRight, y), IM_COL32(255, 255, 255, 10));

    auto nodePosition = [origin, xStep, yStep](const SceneNodeLayout& node) {
        return ImVec2(origin.x + 16.0f + node.depth * xStep, origin.y + 16.0f + node.row * yStep);
    };
    for (const auto& node : nodes)
    {
        if (!node.entity->parent) continue;
        const auto parent = std::find_if(nodes.begin(), nodes.end(), [&node](const SceneNodeLayout& candidate) {
            return candidate.entity->id == *node.entity->parent;
        });
        if (parent == nodes.end()) continue;
        const auto from = nodePosition(*parent), to = nodePosition(node);
        draw->AddLine(ImVec2(from.x + nodeWidth, from.y + nodeHeight * 0.5f),
            ImVec2(to.x, to.y + nodeHeight * 0.5f), IM_COL32(130, 160, 210, 220), 2.0f);
    }
    for (const auto& node : nodes)
    {
        const auto position = nodePosition(node);
        ImGui::SetCursorScreenPos(position);
        const auto id = "entity-" + std::to_string(node.entity->id);
        const bool clicked = ImGui::InvisibleButton(id.c_str(), ImVec2(nodeWidth, nodeHeight));
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const auto fill = selected == node.entity->id ? IM_COL32(65, 105, 170, 255)
            : ImGui::IsItemHovered() ? IM_COL32(65, 75, 95, 255) : IM_COL32(45, 52, 65, 255);
        draw->AddRectFilled(min, max, fill, 5.0f);
        draw->AddRect(min, max, IM_COL32(145, 158, 180, 220), 5.0f);
        draw->AddText(ImVec2(min.x + 8.0f, min.y + 8.0f), IM_COL32(240, 242, 248, 255), node.entity->name.c_str());
        draw->AddText(ImVec2(min.x + 8.0f, min.y + 27.0f), IM_COL32(175, 184, 200, 255),
            ("Entity " + std::to_string(node.entity->id)).c_str());
        if (clicked) selected = node.entity->id;
    }
    draw->PopClipRect();
    ImGui::EndChild();
}

class Editor
{
    SDL_Window* window = nullptr;
    bool sdl = false, graphics = false, gui = false, platform = false;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
    bgfx::ProgramHandle program3d = BGFX_INVALID_HANDLE;
#endif
    bgfx::TextureHandle font = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE, outputMode = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout;
    std::map<std::uint64_t, bgfx::TextureHandle> spriteTextures;

public:
    ~Editor()
    {
        if (platform) ImGui_ImplSDL3_Shutdown();
        if (gui) ImGui::DestroyContext();
        if (graphics)
        {
            for (const auto& [id, texture] : spriteTextures)
                if (bgfx::isValid(texture)) bgfx::destroy(texture);
            if (bgfx::isValid(font)) bgfx::destroy(font);
            if (bgfx::isValid(program)) bgfx::destroy(program);
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
            if (bgfx::isValid(program3d)) bgfx::destroy(program3d);
#endif
            if (bgfx::isValid(sampler)) bgfx::destroy(sampler);
            if (bgfx::isValid(outputMode)) bgfx::destroy(outputMode);
            bgfx::shutdown();
        }
        if (window) SDL_DestroyWindow(window);
        if (sdl) SDL_Quit();
    }

    bgfx::TextureHandle loadSpriteTexture(const std::filesystem::path& file)
    {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) return BGFX_INVALID_HANDLE;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder))) return BGFX_INVALID_HANDLE;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) return BGFX_INVALID_HANDLE;
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0
            || width > 16384 || height > 16384 || std::uint64_t(width) * height > 64ull * 1024 * 1024)
            return BGFX_INVALID_HANDLE;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return BGFX_INVALID_HANDLE;
        const auto byteCount = std::uint64_t(width) * height * 4;
        if (byteCount > std::numeric_limits<UINT>::max()) return BGFX_INVALID_HANDLE;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(byteCount));
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(byteCount), pixels.data())))
            return BGFX_INVALID_HANDLE;
        const auto* memory = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
        return bgfx::createTexture2D(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
            false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_NONE, memory);
    }

    void init()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        sdl = true;
        window = SDL_CreateWindow("AstroEngine Editor", 1100, 720, SDL_WINDOW_RESIZABLE);
        if (!window) throw std::runtime_error(SDL_GetError());
        bgfx::Init config;
        config.type = bgfx::RendererType::Direct3D11;
        config.fallback = false;
        config.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        config.swapChain.width = 1100;
        config.swapChain.height = 720;
        config.reset = BGFX_RESET_VSYNC;
        if (!config.swapChain.nwh || !bgfx::init(config)) throw std::runtime_error("bgfx D3D11 initialization failed");
        graphics = true;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        gui = true;
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // No changes to user layout or project files.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        if (!ImGui_ImplSDL3_InitForD3D(window)) throw std::runtime_error("ImGui SDL3 backend failed");
        platform = true;
        std::array<wchar_t, MAX_PATH> windowsDirectory{};
        const auto windowsLength = GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
        if (windowsLength == 0 || windowsLength >= windowsDirectory.size())
            throw std::runtime_error("Could not locate the Windows font directory for Cyrillic UI support");
        const std::filesystem::path fontDirectory = std::filesystem::path(windowsDirectory.data()) / L"Fonts";
        bool cyrillicFontLoaded = false;
        static constexpr ImWchar cyrillicUiRanges[]{0x0020, 0x00FF, 0x0370, 0x03FF, 0x0400, 0x052F,
            0x2000, 0x206F, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x25A0, 0x25FF, 0};
        ImFontConfig cyrillicFontConfig{};
        for (const auto* fontName : {L"segoeui.ttf", L"arial.ttf"})
        {
            const auto fontPath = fontDirectory / fontName;
            if (!std::filesystem::exists(fontPath)) continue;
            if (io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 16.0f,
                    &cyrillicFontConfig, cyrillicUiRanges))
            {
                cyrillicFontLoaded = true;
                break;
            }
        }
        if (!cyrillicFontLoaded)
            throw std::runtime_error("Could not load Segoe UI or Arial with Cyrillic glyphs");
        ImGui::StyleColorsDark();
        layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
        static_assert(sizeof(ImDrawVert) == 20);
        program = bgfx::createProgram(
            bgfx::createShader(bgfx::copy(vs_ocornut_imgui_dxbc, sizeof(vs_ocornut_imgui_dxbc))),
            bgfx::createShader(bgfx::copy(fs_ocornut_imgui_dxbc, sizeof(fs_ocornut_imgui_dxbc))), true);
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
        const auto readShader = [](const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read 3D Scene View shader: " + path.string());
            std::vector<std::uint8_t> data(std::istreambuf_iterator<char>(input), {});
            if (data.empty() || data.size() > 4 * 1024 * 1024)
                throw std::runtime_error("Invalid 3D Scene View shader size: " + path.string());
            return data;
        };
        const auto vertexBytes = readShader(std::filesystem::path(ASTRO_EDITOR_3D_SHADER_DIR) / "vs_cubes.bin");
        const auto fragmentBytes = readShader(std::filesystem::path(ASTRO_EDITOR_3D_SHADER_DIR) / "fs_cubes.bin");
        program3d = bgfx::createProgram(
            bgfx::createShader(bgfx::copy(vertexBytes.data(), static_cast<std::uint32_t>(vertexBytes.size()))),
            bgfx::createShader(bgfx::copy(fragmentBytes.data(), static_cast<std::uint32_t>(fragmentBytes.size()))), true);
#endif
        sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        outputMode = bgfx::createUniform("u_imguiOutput", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4);
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        font = bgfx::createTexture2D(static_cast<uint16_t>(width), static_cast<uint16_t>(height), false, 1,
            bgfx::TextureFormat::RGBA8, 0, bgfx::copy(pixels, width * height * 4));
        if (!bgfx::isValid(program) || !bgfx::isValid(font) || !bgfx::isValid(sampler) || !bgfx::isValid(outputMode)
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
            || !bgfx::isValid(program3d)
#endif
            )
            throw std::runtime_error("Renderer resource creation failed");
        io.Fonts->SetTexID(static_cast<ImTextureID>(font.idx + 1));
        std::cout << "[info] SDL " << SDL_GetVersion() << "; ImGui " << ImGui::GetVersion()
                  << "; renderer " << bgfx::getRendererName(bgfx::getRendererType()) << '\n';
    }

    void draw(ImDrawData* data, int width, int height)
    {
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
        constexpr bgfx::ViewId renderOrder[] = {1, 3, 2, 0};
#else
        constexpr bgfx::ViewId renderOrder[] = {1, 2, 0};
#endif
        bgfx::setViewOrder(0, static_cast<std::uint16_t>(std::size(renderOrder)), renderOrder);
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x18202aff);
        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        float projection[16];
        bx::mtxOrtho(projection, data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
            data->DisplayPos.y + data->DisplaySize.y, data->DisplayPos.y, 0.0f, 1000.0f, 0.0f,
            bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(0, nullptr, projection);
        const float colorMode[4] = {0, 1, 0, 0};
        bgfx::setFrameUniform(outputMode, colorMode);
        bgfx::touch(0);
        for (const auto* list : data->CmdLists)
        {
            bgfx::TransientVertexBuffer vertices;
            bgfx::TransientIndexBuffer indices;
            if (!bgfx::allocTransientBuffers(&vertices, layout, list->VtxBuffer.Size, &indices, list->IdxBuffer.Size, sizeof(ImDrawIdx) == 4))
                throw std::runtime_error("Transient geometry budget exceeded");
            std::memcpy(vertices.data, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
            std::memcpy(indices.data, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
            for (const auto& command : list->CmdBuffer)
            {
                if (command.UserCallback)
                {
                    if (command.UserCallback != ImDrawCallback_ResetRenderState) command.UserCallback(list, &command);
                    continue;
                }
                const auto clip = command.ClipRect;
                const auto scale = data->FramebufferScale;
                const float x = std::clamp((clip.x - data->DisplayPos.x) * scale.x, 0.0f, float(width));
                const float y = std::clamp((clip.y - data->DisplayPos.y) * scale.y, 0.0f, float(height));
                const float right = std::clamp((clip.z - data->DisplayPos.x) * scale.x, 0.0f, float(width));
                const float bottom = std::clamp((clip.w - data->DisplayPos.y) * scale.y, 0.0f, float(height));
                if (right <= x || bottom <= y || command.ElemCount == 0) continue;
                bgfx::setScissor(uint16_t(x), uint16_t(y), uint16_t(right - x), uint16_t(bottom - y));
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
                bgfx::setTexture(0, sampler, bgfx::TextureHandle{uint16_t(command.GetTexID() - 1)});
                bgfx::setVertexBuffer(0, &vertices, command.VtxOffset, list->VtxBuffer.Size - command.VtxOffset);
                bgfx::setIndexBuffer(&indices, command.IdxOffset, command.ElemCount);
                bgfx::submit(0, program);
            }
        }
    }

    int run(bool smoke, bool benchmark, bool benchmark3d, bool inspectCopyDialog)
    {
        std::filesystem::path languageSettingsPath;
        bool languageSettingsReadable = true;
        if (smoke) gEditorLanguage = EditorLanguage::Russian;
        else
        {
            if (char* rawPath = SDL_GetPrefPath("AstroEngine", "AstroEngine"))
            {
                const std::u8string utf8Path(reinterpret_cast<const char8_t*>(rawPath));
                SDL_free(rawPath);
                languageSettingsPath = std::filesystem::path(utf8Path) / "editor-settings.cfg";
                const auto loadedLanguage = astro::editor::readEditorLanguage(languageSettingsPath);
                if (loadedLanguage) gEditorLanguage = loadedLanguage.value();
                else { gEditorLanguage = EditorLanguage::English; languageSettingsReadable = false; }
            }
            else languageSettingsReadable = false;
        }
        int width = 1100, height = 720, frames = 0;
        bool running = true, resetLayout = true, docked = false;
        bool showProject = true, showDiagnostics = true, showVisualLogic = true, showGameView = true;
        bool showCodeWorkspace = true;
        bool showEditorSettings = smoke;
        auto pendingEditorLanguage = gEditorLanguage;
        bool editorSettingsWindowDrawn = false;
        SceneViewTarget sceneTarget{1}, gameTarget{2};
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
        sceneTarget.configure3d(program3d);
        std::array<float, 4> sceneViewRectNormalized{};
        bool sceneViewRectValid = false;
        std::uint64_t sceneViewWhitePixels = 0;
#endif
        std::vector<Scene> benchmarkScenes;
        std::vector<std::string> benchmarkSceneNames;
        std::vector<double> meshSceneBuildMs;
        double meshModelImportMs = 0.0;
        astro::runtime::RuntimeSession playSession;
        int sceneViewFrames = 0, gameViewFrames = 0;
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
        std::optional<astro::runtime::PuzzleGame> gameViewPuzzle;
        std::optional<astro::runtime::PuzzleCampaignSession> gameViewPuzzleCampaign;
        std::array<char, 65> puzzleCampaignIdInput{};
        std::string puzzleCampaignMessage;
        std::vector<astro::runtime::PuzzleCampaignCatalogEntry> puzzleCampaignCatalog;
        std::filesystem::path puzzleCampaignCatalogProjectRoot;
        std::string selectedPuzzleCampaignCatalogId;
        astro::runtime::PuzzleCampaignDefinition puzzleCampaignDraft{"new-campaign", "New Campaign", "", {}, {}};
        std::optional<astro::runtime::PuzzleCampaignDefinition> puzzleCampaignSavedBaseline;
        std::array<char, 65> puzzleCampaignDraftIdInput{};
        std::array<char, 257> puzzleCampaignDraftNameInput{};
        std::string puzzleCampaignLevelChoice;
        std::optional<std::string> pendingPuzzleCampaignOpen;
        bool pendingPuzzleCampaignReset = false;
        std::string puzzleCampaignAuthoringMessage;
        bool showPuzzleCampaignBrowser = false;
        int puzzleCampaignBoardFrames = 0;
        bool puzzleCampaignSmokePassed = false, puzzleCampaignCatalogDrawn = false;
        bool puzzleCampaignSaveSmokePassed = false;
        bool puzzleCampaignAuthoringDrawn = false;
        bool puzzleCampaignLinkDiagnosticsDrawn = false, puzzleCampaignMissingLinkDrawn = false;
        std::optional<astro::runtime::GridPosition> selectedPuzzleCell;
        std::optional<PuzzleCellPair> pendingPuzzleSwap;
        std::optional<astro::runtime::PuzzleMoveResult> lastPuzzleMove;
        int puzzleAppliedMoves = 0, puzzleBoardFrames = 0, puzzleBoardRenderedFrames = 0;
        bool puzzleSmokeReady = false, puzzleSmokeLifecyclePassed = false;
        bool puzzleLevelAuthoringDrawn = false, puzzleLevelSmokePassed = false, puzzleLevelSaveSmokePassed = false;
        bool puzzleLevelCatalogDrawn = false, puzzleLevelCatalogSmokePassed = false;
        std::optional<std::string> activePuzzleLevelId;
        auto puzzleLevelDraft = defaultPuzzleLevelDefinition();
        auto puzzleLevelSavedBaseline = std::optional<astro::runtime::PuzzleLevelDefinition>{};
        std::vector<astro::runtime::PuzzleLevelCatalogEntry> puzzleLevelCatalog;
        std::filesystem::path puzzleLevelCatalogProjectRoot;
        std::string selectedPuzzleCatalogId;
        std::optional<std::string> pendingPuzzleLevelOpen;
        bool pendingPuzzleLevelReset = false;
        std::array<char, 65> puzzleLevelIdInput{};
        std::array<char, 257> puzzleLevelNameInput{};
        std::array<char, 65> puzzleLevelNewIdInput{};
        std::array<char, 257> puzzleLevelDuplicateNameInput{};
        std::string puzzleLevelMessage;
#endif
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
        std::optional<astro::runtime::SaveStore> gameViewPuzzleSaveStore;
        std::optional<astro::runtime::SaveProfile> gameViewPuzzleSaveProfile;
        std::filesystem::path gameViewPuzzleSaveDirectory;
        int gameViewPuzzleSaveSlot = 0;
        std::string gameViewPuzzleSaveMessage;
        std::optional<astro::runtime::SaveStore> gameViewPuzzleCampaignSaveStore;
        std::optional<astro::runtime::SaveProfile> gameViewPuzzleCampaignSaveProfile;
        std::filesystem::path gameViewPuzzleCampaignSaveDirectory;
        int gameViewPuzzleCampaignSaveSlot = 0;
        std::string gameViewPuzzleCampaignSaveMessage;
        std::optional<astro::runtime::PuzzleCampaignSaveTransferPreview> gameViewPuzzleCampaignTransferPreview;
        std::optional<astro::runtime::PuzzleCampaignSaveTransferCopyResult> gameViewPuzzleCampaignTransferCopyResult;
        std::optional<astro::runtime::PuzzleCampaignSaveArchiveInspection> puzzleCampaignArchiveInspection;
        std::optional<astro::runtime::PuzzleCampaignSaveArchiveConflictCopyPreview> puzzleCampaignArchiveConflictCopyPreview;
        std::string puzzleCampaignTransferPreviewMessage;
        std::string puzzleCampaignTransferRecoveryMessage;
        bool puzzleCampaignTransferPreviewDrawn = false, puzzleCampaignTransferPreviewSmokePassed = false;
        bool puzzleCampaignTransferConfirmationDrawn = false, puzzleCampaignTransferCopySmokePassed = false;
        bool puzzleCampaignTransferRecoveryDrawn = false, puzzleCampaignTransferRecoverySmokePassed = false;
        bool puzzleCampaignTransferPartialPolicySmokePassed = false;
        bool puzzleCampaignTransferInvalidJournalSmokePassed = false;
        bool puzzleCampaignTransferArchivePromptDrawn = false, puzzleCampaignTransferArchiveSmokePassed = false;
        bool puzzleCampaignArchiveConflictSmokePassed = false;
        bool puzzleCampaignArchiveConflictCopyPromptDrawn = false;
        bool puzzleCampaignArchiveConflictCopySmokePassed = false;
        std::string puzzleCampaignArchiveConflictCopyMessage;
        std::string puzzleCampaignInvalidJournalSmokeBytes;
        bool puzzleCampaignTransferRefreshPending = false;
        astro::editor::PuzzleSaveProjectSettings gameViewPuzzleProjectSettings;
        std::array<char, 65> puzzleSaveProfileIdInput{};
        std::array<char, 513> puzzleSaveDirectoryInput{};
        std::uint32_t puzzleSaveProfileVersionInput = 1;
        bool puzzleSaveSettingsReady = false, puzzleSaveSettingsDirty = false, puzzleSaveSettingsWindowDrawn = false;
#endif
        auto logicGraph = makeScoreGateGraph();
        astro::logic::VisualLogicDebugger logicDebugger;
        bool visualLogicWindowDrawn = false;
        auto timelineCreated = astro::runtime::AnimationTimeline::create({{0.0, 0.0}, {1.0, 10.0}, {2.0, 0.0}});
        if (!timelineCreated) throw std::runtime_error(timelineCreated.error().message);
        auto timeline = std::move(timelineCreated).value();
        astro::runtime::CameraRig cameraRig;
        bool showTimeline = false, showCameraPresets = false;
        bool timelineWindowDrawn = false, cameraWindowDrawn = false;
        bool showSaveSystem = false, saveSystemWindowDrawn = false;
        astro::save_editor::SaveSystemDocument saveSystemDocument;
        bool showUiEditor = false, uiEditorWindowDrawn = false;
        astro::ui::UiDocument uiDocument;
        std::optional<astro::ui::ElementId> selectedUiElement;
        std::optional<astro::ui::ElementId> uiTextDraftElement;
        std::array<char, 4097> uiTextDraft{};
        int uiPreviewPreset = 0;
        bool uiBindingErrorReported = false;
        char path[1024] = "";
        char scenePathInput[1024] = "";
        char assetPathInput[512] = "";
        char name[256] = "My Game";
        std::string activeProject, message = tr("Create or open a project using File.");
        Scene scene(1, "Main Scene");
        astro::core::EngineContext commandContext;
        astro::core::CommandRegistry commands(commandContext);
        const auto commandRegistration = astro::scene::registerSceneCommands(commands, scene);
        if (!commandRegistration) throw std::runtime_error(commandRegistration.error().message);
        std::optional<EntityId> selectedEntity;
        std::uint64_t selectedScriptAsset = 0;
        bool scriptAuthoringPanelDrawn = false;
        std::array<char, 256> inspectorDraft{};
        std::optional<EntityId> inspectorDraftEntity;
        std::optional<astro::scene::SceneId> inspectorDraftScene;
        std::uint64_t nextSceneId = 2;
        std::uint64_t nextEntityName = 1;
        bool sceneDirty = false;
        bool dialog = false, create = false, sceneDialog = false;
        std::filesystem::path activeProjectRoot, currentScenePath, smokeDirectory;
        std::optional<astro::assets::AssetRegistry> assetRegistry;
        astro::assets::ScriptBuildReport csharpBuildReport;
        bool csharpBuildPanelDrawn = false;
        astro::editor::CSharpCodeWorkspace csharpCodeWorkspace;
        std::array<char, 512> csharpCodePathInput{};
        std::array<char, 512> csharpNewFileInput{};
        std::vector<astro::editor::CSharpDiagnostic> csharpCodeDiagnostics;
        std::string csharpCodeMessage;
        std::string csharpCodeBuildOutput;
        int csharpCodeJumpLine = 0;
        std::optional<std::filesystem::path> pendingCSharpCodePath;
        bool pendingCSharpCodeCreate = false;
        bool csharpCodeWorkspaceDrawn = false;
        bool csharpBuildRunning = false;
        std::future<CSharpBuildResult> csharpBuildFuture;
        bool csharpMetadataValidationRunning = false;
        std::future<astro::editor::CSharpMetadataValidationResult> csharpMetadataValidationFuture;
        std::string csharpMetadataValidationOutput;
        std::size_t csharpMetadataValidationTypeCount = 0;
        std::size_t csharpMetadataValidationBatchCount = 0;
        bool csharpMetadataValidationPanelDrawn = false;
#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
        bool csharpMetadataReportScrollableInSmoke = false;
        double csharpMetadataReportMaxDrawMs = 0.0;
#endif
        std::filesystem::path csharpMetadataValidationProjectRoot;
        astro::editor::CSharpBuildManifest csharpManifestDraft;
        astro::editor::CSharpBuildManifest csharpManifestSaved;
        std::array<char, 33> csharpManifestSdkInput{};
        std::array<char, 101> csharpManifestAssemblyInput{};
        std::array<char, 513> csharpManifestNewTypeInput{};
        std::vector<std::array<char, 513>> csharpManifestTypeInputs;
        std::vector<std::string> csharpManifestSourcePaths;
        std::string csharpManifestMessage;
        std::string csharpManifestNewSource;
        bool csharpManifestExists = false;
        bool csharpManifestInvalid = false;
        bool csharpManifestWindowDrawn = false;
        astro::editor::CSharpPlayApproval csharpPlayApproval;
        std::optional<astro::editor::CSharpPlayApprovalSnapshot> pendingCSharpApproval;
        std::string csharpApprovalDiagnostic;
#if defined(ASTRO_EDITOR_HAS_CSHARP_RUNTIME)
        std::unique_ptr<astro::runtime::CSharpScriptRuntime> csharpScriptRuntime;
#endif
        std::vector<std::string> diagnostics;
        std::optional<astro::assets::AssetId> smokeSpriteAsset;
        std::vector<astro::assets::AssetId> benchmarkSpriteAssets;
        int undoShortcuts = 0, redoShortcuts = 0, saveShortcuts = 0, openShortcuts = 0;
        auto reportError = [&](const std::string& error) {
            message = astro::editor::translateEditorDiagnostic(error, gEditorLanguage);
            diagnostics.push_back(message);
            if (diagnostics.size() > 32) diagnostics.erase(diagnostics.begin());
        };
        auto stopPlaySession = [&] {
            const auto stopped = playSession.stopChecked();
            if (!stopped) reportError(stopped.error().message);
#if defined(ASTRO_EDITOR_HAS_CSHARP_RUNTIME)
            csharpScriptRuntime.reset();
#endif
            csharpPlayApproval.revoke();
            pendingCSharpApproval.reset();
        };
        if (!languageSettingsReadable)
            reportError(tr("Language preference could not be read; using English."));
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
        auto loadPuzzleProjectSettings = [&](const std::filesystem::path& projectRoot) {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            gameViewPuzzleSaveStore.reset();
            gameViewPuzzleSaveProfile.reset();
            gameViewPuzzleSaveDirectory.clear();
            gameViewPuzzleCampaignSaveStore.reset();
            gameViewPuzzleCampaignSaveProfile.reset();
            gameViewPuzzleCampaignSaveDirectory.clear();
            gameViewPuzzleCampaignSaveMessage.clear();
            const auto loaded = astro::editor::readPuzzleSaveProjectSettings(projectRoot);
            if (loaded)
            {
                gameViewPuzzleProjectSettings = loaded.value();
                puzzleSaveSettingsReady = true;
            }
            else
            {
                gameViewPuzzleProjectSettings = astro::editor::defaultPuzzleSaveProjectSettings(projectRoot);
                puzzleSaveSettingsReady = false;
                reportError(tr("Puzzle project settings need repair: ") + loaded.error().message);
            }
            puzzleSaveProfileIdInput.fill('\0');
            std::copy_n(gameViewPuzzleProjectSettings.profileId.data(),
                std::min(gameViewPuzzleProjectSettings.profileId.size(), puzzleSaveProfileIdInput.size() - 1),
                puzzleSaveProfileIdInput.data());
            puzzleSaveDirectoryInput.fill('\0');
            std::copy_n(gameViewPuzzleProjectSettings.userDataSubdirectory.data(),
                std::min(gameViewPuzzleProjectSettings.userDataSubdirectory.size(), puzzleSaveDirectoryInput.size() - 1),
                puzzleSaveDirectoryInput.data());
            puzzleSaveProfileVersionInput = gameViewPuzzleProjectSettings.profileVersion;
            puzzleSaveSettingsDirty = false;
            gameViewPuzzleSaveMessage = puzzleSaveSettingsReady ? std::string{} : "Project settings could not be loaded";
#else
            static_cast<void>(projectRoot);
#endif
        };
        auto applyPuzzleProjectSettings = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project before configuring Puzzle saves"});
            astro::editor::PuzzleSaveProjectSettings candidate{
                std::string(puzzleSaveProfileIdInput.data()),
                puzzleSaveProfileVersionInput,
                std::string(puzzleSaveDirectoryInput.data())};
            const auto written = astro::editor::writePuzzleSaveProjectSettings(activeProjectRoot, candidate);
            if (!written) return written;
            gameViewPuzzleProjectSettings = std::move(candidate);
            gameViewPuzzleSaveStore.reset();
            gameViewPuzzleSaveProfile.reset();
            gameViewPuzzleSaveDirectory.clear();
            gameViewPuzzleCampaignSaveStore.reset();
            gameViewPuzzleCampaignSaveProfile.reset();
            gameViewPuzzleCampaignSaveDirectory.clear();
            gameViewPuzzleCampaignSaveMessage.clear();
            gameViewPuzzleCampaignTransferPreview.reset();
            gameViewPuzzleCampaignTransferCopyResult.reset();
            puzzleCampaignTransferPreviewMessage.clear();
            puzzleCampaignTransferRecoveryMessage.clear();
            puzzleSaveSettingsReady = true;
            puzzleSaveSettingsDirty = false;
            gameViewPuzzleSaveMessage = tr("Puzzle SaveProfile and project save folder settings applied");
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle SaveStore adapter is not enabled in this Editor build"});
#endif
        };
        auto openPuzzleSaveStore = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (gameViewPuzzleSaveStore && gameViewPuzzleSaveProfile)
                return astro::core::Result<void>::success();
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project before saving Puzzle progress"});
            if (!puzzleSaveSettingsReady || puzzleSaveSettingsDirty)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Apply valid Puzzle project settings before saving or loading"});
            const auto settingsFile = activeProjectRoot / L".astro" / L"puzzle-save.settings";
            std::error_code settingsError;
            if (!std::filesystem::exists(settingsFile, settingsError) && !settingsError)
            {
                const auto initialized = astro::editor::writePuzzleSaveProjectSettings(
                    activeProjectRoot, gameViewPuzzleProjectSettings);
                if (!initialized) return initialized;
            }
            if (smoke)
                gameViewPuzzleSaveDirectory = smokeDirectory / "puzzle-saves";
            else
            {
                char* rawPreferencePath = SDL_GetPrefPath("AstroEngine", "AstroEngine");
                if (!rawPreferencePath)
                    return astro::core::Result<void>::failure({astro::core::ErrorCode::IoError,
                        std::string("Could not resolve the user save directory: ") + SDL_GetError()});
                std::unique_ptr<char, decltype(&SDL_free)> preferencePath(rawPreferencePath, &SDL_free);
                const auto utf8Path = std::u8string(reinterpret_cast<const char8_t*>(preferencePath.get()));
                const auto relativeDirectory = std::u8string(
                    reinterpret_cast<const char8_t*>(gameViewPuzzleProjectSettings.userDataSubdirectory.data()),
                    gameViewPuzzleProjectSettings.userDataSubdirectory.size());
                gameViewPuzzleSaveDirectory = std::filesystem::path(utf8Path) / std::filesystem::path(relativeDirectory);
            }
            if (activePuzzleLevelId)
                gameViewPuzzleSaveDirectory /= std::filesystem::path("PuzzleLevels") / *activePuzzleLevelId;
            auto store = astro::runtime::SaveStore::open(gameViewPuzzleSaveDirectory, 10);
            if (!store) return astro::core::Result<void>::failure(store.error());
            auto profile = astro::runtime::SaveProfile::create(gameViewPuzzleProjectSettings.profileId,
                gameViewPuzzleProjectSettings.profileVersion);
            if (!profile) return astro::core::Result<void>::failure(profile.error());
            const auto selected = astro::runtime::selectPuzzleSnapshotField(profile.value());
            if (!selected) return selected;
            if (activePuzzleLevelId)
            {
                const auto identity = astro::runtime::selectPuzzleLevelIdentityField(profile.value());
                if (!identity) return identity;
            }
            gameViewPuzzleSaveStore = std::move(store).value();
            gameViewPuzzleSaveProfile = std::move(profile).value();
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle SaveStore adapter is not enabled in this Editor build"});
#endif
        };
        auto savePuzzleGameViewSlot = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            const auto opened = openPuzzleSaveStore();
            if (!opened) return opened;
            auto* runtimeScene = playSession.runtimeScene();
            if (!runtimeScene || !gameViewPuzzle)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Start Puzzle before saving its progress"});
            const auto saved = activePuzzleLevelId
                ? astro::runtime::savePuzzleLevelSnapshot(*gameViewPuzzleSaveStore,
                    static_cast<std::size_t>(gameViewPuzzleSaveSlot), *runtimeScene, *gameViewPuzzleSaveProfile,
                    "1.0.0", *gameViewPuzzle, *activePuzzleLevelId)
                : astro::runtime::savePuzzleSnapshot(*gameViewPuzzleSaveStore,
                    static_cast<std::size_t>(gameViewPuzzleSaveSlot), *runtimeScene, *gameViewPuzzleSaveProfile,
                    "1.0.0", *gameViewPuzzle);
            if (!saved) return astro::core::Result<void>::failure(saved.error());
            gameViewPuzzleSaveMessage = tr("Saved Puzzle progress to slot ") + std::to_string(gameViewPuzzleSaveSlot);
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle SaveStore adapter is not enabled in this Editor build"});
#endif
        };
        auto loadPuzzleGameViewSlot = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            const auto opened = openPuzzleSaveStore();
            if (!opened) return opened;
            const auto* runtimeScene = playSession.runtimeScene();
            if (!runtimeScene || !gameViewPuzzle)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Start Puzzle before loading its progress"});
            auto runtimeSceneCandidate = *runtimeScene;
            auto loaded = activePuzzleLevelId
                ? astro::runtime::loadPuzzleLevelSnapshot(*gameViewPuzzleSaveStore,
                    static_cast<std::size_t>(gameViewPuzzleSaveSlot), runtimeSceneCandidate,
                    *gameViewPuzzleSaveProfile, "1.0.0", *activePuzzleLevelId)
                : astro::runtime::loadPuzzleSnapshot(*gameViewPuzzleSaveStore,
                    static_cast<std::size_t>(gameViewPuzzleSaveSlot), runtimeSceneCandidate,
                    *gameViewPuzzleSaveProfile, "1.0.0");
            if (!loaded) return astro::core::Result<void>::failure(loaded.error());
            gameViewPuzzle = std::move(loaded).value().puzzle;
            selectedPuzzleCell.reset();
            pendingPuzzleSwap.reset();
            lastPuzzleMove.reset();
            gameViewPuzzleSaveMessage = tr("Loaded Puzzle progress from slot ") + std::to_string(gameViewPuzzleSaveSlot);
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle SaveStore adapter is not enabled in this Editor build"});
#endif
        };
        auto openPuzzleCampaignSaveStore = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (gameViewPuzzleCampaignSaveStore && gameViewPuzzleCampaignSaveProfile)
                return astro::core::Result<void>::success();
            if (activeProjectRoot.empty() || !gameViewPuzzleCampaign)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Start a Puzzle campaign before saving its progress"});
            if (!puzzleSaveSettingsReady || puzzleSaveSettingsDirty)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Apply valid Puzzle project settings before saving or loading"});

            const auto settingsFile = activeProjectRoot / L".astro" / L"puzzle-save.settings";
            std::error_code settingsError;
            if (!std::filesystem::exists(settingsFile, settingsError) && !settingsError)
            {
                const auto initialized = astro::editor::writePuzzleSaveProjectSettings(
                    activeProjectRoot, gameViewPuzzleProjectSettings);
                if (!initialized) return initialized;
            }
            if (settingsError)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::IoError,
                    "Cannot inspect Puzzle project save settings: " + settingsError.message()});

            if (smoke)
            {
                auto smokeSettings = gameViewPuzzleProjectSettings;
                smokeSettings.userDataSubdirectory = "puzzle-saves";
                const auto resolved = astro::editor::resolvePuzzleCampaignSaveDirectory(smokeDirectory,
                    smokeSettings, gameViewPuzzleCampaign->campaign().id);
                if (!resolved) return astro::core::Result<void>::failure(resolved.error());
                gameViewPuzzleCampaignSaveDirectory = resolved.value();
            }
            else
            {
                char* rawPreferencePath = SDL_GetPrefPath("AstroEngine", "AstroEngine");
                if (!rawPreferencePath)
                    return astro::core::Result<void>::failure({astro::core::ErrorCode::IoError,
                        std::string("Could not resolve the user save directory: ") + SDL_GetError()});
                std::unique_ptr<char, decltype(&SDL_free)> preferencePath(rawPreferencePath, &SDL_free);
                const auto utf8Path = std::u8string(reinterpret_cast<const char8_t*>(preferencePath.get()));
                const auto resolved = astro::editor::resolvePuzzleCampaignSaveDirectory(
                    std::filesystem::path(utf8Path), gameViewPuzzleProjectSettings,
                    gameViewPuzzleCampaign->campaign().id);
                if (!resolved) return astro::core::Result<void>::failure(resolved.error());
                gameViewPuzzleCampaignSaveDirectory = resolved.value();
            }
            auto store = astro::runtime::SaveStore::open(gameViewPuzzleCampaignSaveDirectory, 10);
            if (!store) return astro::core::Result<void>::failure(store.error());
            auto profile = astro::runtime::SaveProfile::create(gameViewPuzzleProjectSettings.profileId,
                gameViewPuzzleProjectSettings.profileVersion);
            if (!profile) return astro::core::Result<void>::failure(profile.error());
            const auto selected = astro::runtime::selectPuzzleCampaignProgressFields(profile.value());
            if (!selected) return selected;
            gameViewPuzzleCampaignSaveStore = std::move(store).value();
            gameViewPuzzleCampaignSaveProfile = std::move(profile).value();
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle campaign SaveStore adapter is not enabled in this Editor build"});
#endif
        };
        auto savePuzzleCampaignGameViewSlot = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            const auto opened = openPuzzleCampaignSaveStore();
            if (!opened) return opened;
            auto* runtimeScene = playSession.runtimeScene();
            if (!runtimeScene || !gameViewPuzzleCampaign)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Start a Puzzle campaign before saving its progress"});
            const auto saved = astro::runtime::savePuzzleCampaignProgress(*gameViewPuzzleCampaignSaveStore,
                static_cast<std::size_t>(gameViewPuzzleCampaignSaveSlot), *runtimeScene,
                *gameViewPuzzleCampaignSaveProfile, "1.0.0", *gameViewPuzzleCampaign);
            if (!saved) return astro::core::Result<void>::failure(saved.error());
            gameViewPuzzleCampaignSaveMessage = tr("Saved campaign progress to slot ")
                + std::to_string(gameViewPuzzleCampaignSaveSlot);
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle campaign SaveStore adapter is not enabled in this Editor build"});
#endif
        };
        auto loadPuzzleCampaignGameViewSlot = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            const auto opened = openPuzzleCampaignSaveStore();
            if (!opened) return opened;
            const auto* runtimeScene = playSession.runtimeScene();
            if (!runtimeScene || !gameViewPuzzleCampaign)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Start a Puzzle campaign before loading its progress"});
            auto sceneCandidate = *runtimeScene;
            auto loaded = astro::runtime::loadPuzzleCampaignProgress(*gameViewPuzzleCampaignSaveStore,
                static_cast<std::size_t>(gameViewPuzzleCampaignSaveSlot), sceneCandidate,
                *gameViewPuzzleCampaignSaveProfile, "1.0.0", activeProjectRoot,
                gameViewPuzzleCampaign->campaign().id);
            if (!loaded) return astro::core::Result<void>::failure(loaded.error());

            gameViewPuzzleCampaign = std::move(loaded).value().campaign;
            activePuzzleLevelId = gameViewPuzzleCampaign->currentLevel().id;
            selectedPuzzleCell.reset();
            pendingPuzzleSwap.reset();
            lastPuzzleMove.reset();
            puzzleCampaignMessage.clear();
            gameViewPuzzleCampaignSaveMessage = tr("Loaded campaign progress from slot ")
                + std::to_string(gameViewPuzzleCampaignSaveSlot);
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle campaign SaveStore adapter is not enabled in this Editor build"});
#endif
        };
        auto previewPuzzleCampaignTransfer = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (activeProjectRoot.empty() || !gameViewPuzzleCampaign)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project and start a Puzzle campaign before previewing saves"});

            std::filesystem::path userDataRoot;
            if (smoke)
                userDataRoot = smokeDirectory;
            else
            {
                std::wstring appData(32768, L'\0');
                const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData.data(),
                    static_cast<DWORD>(appData.size()));
                if (length == 0 || static_cast<std::size_t>(length) >= appData.size())
                    return astro::core::Result<void>::failure({astro::core::ErrorCode::IoError,
                        "Could not resolve APPDATA without creating a directory"});
                appData.resize(length);
                userDataRoot = std::filesystem::path(appData) / L"AstroEngine" / L"AstroEngine";
            }

            auto sourceSettings = gameViewPuzzleProjectSettings;
            if (smoke) sourceSettings.userDataSubdirectory = "puzzle-saves";
            auto targetSettings = sourceSettings;
            targetSettings.userDataSubdirectory = std::string(puzzleSaveDirectoryInput.data());
            const auto campaignId = gameViewPuzzleCampaign->campaign().id;
            const auto source = astro::editor::resolvePuzzleCampaignSaveDirectory(
                userDataRoot, sourceSettings, campaignId);
            if (!source) return astro::core::Result<void>::failure(source.error());
            const auto target = astro::editor::resolvePuzzleCampaignSaveDirectory(
                userDataRoot, targetSettings, campaignId);
            if (!target) return astro::core::Result<void>::failure(target.error());

            auto preview = astro::runtime::previewPuzzleCampaignSaveTransfer(
                source.value(), target.value(), campaignId);
            if (!preview) return astro::core::Result<void>::failure(preview.error());
            gameViewPuzzleCampaignTransferPreview = std::move(preview).value();
            gameViewPuzzleCampaignTransferCopyResult.reset();
            puzzleCampaignArchiveInspection.reset();
            puzzleCampaignArchiveConflictCopyPreview.reset();
            puzzleCampaignArchiveConflictCopyMessage.clear();
            puzzleCampaignTransferPreviewMessage.clear();
            puzzleCampaignTransferRecoveryMessage.clear();
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle campaign save preview is unavailable in this Editor build"});
#endif
        };
        auto confirmPuzzleCampaignTransferCopy = [&](const bool explicitlyConfirmed) -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (!gameViewPuzzleCampaignTransferPreview)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Create a campaign save transfer preview before copying"});
            auto copied = astro::runtime::copyPuzzleCampaignSaveTransfer(
                *gameViewPuzzleCampaignTransferPreview, explicitlyConfirmed);
            if (!copied) return astro::core::Result<void>::failure(copied.error());
            gameViewPuzzleCampaignTransferCopyResult = std::move(copied).value();
            if (smoke && explicitlyConfirmed)
            {
                const auto& result = *gameViewPuzzleCampaignTransferCopyResult;
                const auto sourceSlot = gameViewPuzzleCampaignTransferPreview->sourceDirectory / "slot-4.astro-save";
                const auto targetSlot = gameViewPuzzleCampaignTransferPreview->targetDirectory / "slot-4.astro-save";
                std::error_code sourceError, targetError;
                const auto sourceSize = std::filesystem::file_size(sourceSlot, sourceError);
                const auto targetSize = std::filesystem::file_size(targetSlot, targetError);
                std::ifstream sourceFile(sourceSlot, std::ios::binary), targetFile(targetSlot, std::ios::binary);
                const std::string sourceBytes(std::istreambuf_iterator<char>(sourceFile), {});
                const std::string targetBytes(std::istreambuf_iterator<char>(targetFile), {});
                if (result.state != astro::runtime::PuzzleCampaignSaveTransferCopyState::Copied
                    || result.filesCopied == 0 || sourceError || targetError || sourceSize != targetSize
                    || !sourceFile.is_open() || !targetFile.is_open() || sourceBytes != targetBytes)
                    return astro::core::Result<void>::failure({astro::core::ErrorCode::IoError,
                        "Editor campaign transfer smoke did not copy the isolated reviewed fixture"});
            }
            return astro::core::Result<void>::success();
#else
            (void)explicitlyConfirmed;
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle campaign save copy is unavailable in this Editor build"});
#endif
        };
        auto resumePuzzleCampaignTransfer = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (!gameViewPuzzleCampaignTransferPreview)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Create a campaign save recovery preview before resuming"});
            auto resumed = astro::runtime::resumePuzzleCampaignSaveTransfer(
                *gameViewPuzzleCampaignTransferPreview, true);
            if (!resumed) return astro::core::Result<void>::failure(resumed.error());
            gameViewPuzzleCampaignTransferCopyResult = std::move(resumed).value();
            puzzleCampaignTransferRefreshPending = true;
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Campaign transfer resume is unavailable in this Editor build"});
#endif
        };
        auto acknowledgePuzzleCampaignTransferRecovery = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (!gameViewPuzzleCampaignTransferPreview
                || !gameViewPuzzleCampaignTransferPreview->pendingRecovery)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "No campaign save transfer recovery record is displayed"});
            auto acknowledged = astro::runtime::acknowledgePuzzleCampaignSaveTransferRecovery(
                *gameViewPuzzleCampaignTransferPreview->pendingRecovery, true);
            if (!acknowledged) return acknowledged;
            gameViewPuzzleCampaignTransferPreview.reset();
            gameViewPuzzleCampaignTransferCopyResult.reset();
            auto refreshed = previewPuzzleCampaignTransfer();
            if (!refreshed) return refreshed;
            puzzleCampaignTransferRecoveryMessage = tr("Recovery record acknowledged. Save files were not changed.");
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Campaign transfer recovery is unavailable in this Editor build"});
#endif
        };
        auto archivePuzzleCampaignTransferJournal = [&]() -> astro::core::Result<void> {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (!gameViewPuzzleCampaignTransferPreview
                || !gameViewPuzzleCampaignTransferPreview->pendingRecovery)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "No campaign transfer recovery journal is displayed"});
            auto archived = astro::runtime::archiveMalformedPuzzleCampaignSaveTransferJournal(
                *gameViewPuzzleCampaignTransferPreview->pendingRecovery, true);
            if (!archived)
            {
                auto inspection = astro::runtime::inspectMalformedPuzzleCampaignSaveArchive(
                    *gameViewPuzzleCampaignTransferPreview->pendingRecovery);
                if (inspection) puzzleCampaignArchiveInspection = std::move(inspection).value();
                return astro::core::Result<void>::failure(archived.error());
            }
            puzzleCampaignArchiveInspection.reset();
            const auto archivePathText = archived.value().u8string();
            puzzleCampaignTransferRecoveryMessage = std::string(tr("Malformed journal archive copy created; original journal remains."))
                + " " + std::string(reinterpret_cast<const char*>(archivePathText.c_str()));
            if (smoke)
            {
                const auto archivePath = archived.value();
                const auto journalPath = gameViewPuzzleCampaignTransferPreview->pendingRecovery->journalPath;
                const auto candidatePath = gameViewPuzzleCampaignTransferPreview->targetDirectory / "slot-4.astro-save";
                std::ifstream archiveInput(archivePath, std::ios::binary), candidateInput(candidatePath, std::ios::binary);
                const std::string archiveBytes(std::istreambuf_iterator<char>(archiveInput), {});
                const std::string candidateBytes(std::istreambuf_iterator<char>(candidateInput), {});
                std::ifstream journalInput(journalPath, std::ios::binary);
                const std::string journalBytes(std::istreambuf_iterator<char>(journalInput), {});
                const bool archiveOpened = archiveInput.is_open();
                const bool candidateOpened = candidateInput.is_open();
                const bool journalOpened = journalInput.is_open();
                archiveInput.close();
                candidateInput.close();
                journalInput.close();
                std::error_code existsError;
                puzzleCampaignTransferArchiveSmokePassed = archiveOpened
                    && archiveBytes == puzzleCampaignInvalidJournalSmokeBytes
                    && journalOpened && journalBytes == puzzleCampaignInvalidJournalSmokeBytes
                    && std::filesystem::exists(journalPath, existsError) && !existsError
                    && candidateOpened && candidateBytes == "keep candidate save untouched";
                if (!puzzleCampaignTransferArchiveSmokePassed)
                    return astro::core::Result<void>::failure({astro::core::ErrorCode::ValidationError,
                        "Editor journal archive smoke changed the candidate save or did not preserve exact journal bytes"});
                constexpr std::string_view archiveOwner = "pre-existing archive owner";
                { std::ofstream occupant(archivePath, std::ios::binary | std::ios::trunc);
                    occupant.write(archiveOwner.data(), static_cast<std::streamsize>(archiveOwner.size())); }
                auto conflictInspection = astro::runtime::inspectMalformedPuzzleCampaignSaveArchive(
                    *gameViewPuzzleCampaignTransferPreview->pendingRecovery);
                const auto readSmokeBytes = [](const std::filesystem::path& path) {
                    std::ifstream input(path, std::ios::binary);
                    return std::string(std::istreambuf_iterator<char>(input), {});
                };
                puzzleCampaignArchiveConflictSmokePassed = conflictInspection
                    && conflictInspection.value().pathKind == astro::runtime::PuzzleCampaignSaveArchivePathKind::RegularFile
                    && conflictInspection.value().contentMatch
                        == astro::runtime::PuzzleCampaignSaveArchiveContentMatch::Different
                    && conflictInspection.value().byteSize == archiveOwner.size()
                    && readSmokeBytes(archivePath) == archiveOwner
                    && readSmokeBytes(journalPath) == puzzleCampaignInvalidJournalSmokeBytes
                    && readSmokeBytes(candidatePath) == "keep candidate save untouched";
                if (!puzzleCampaignArchiveConflictSmokePassed)
                    return astro::core::Result<void>::failure({astro::core::ErrorCode::ValidationError,
                        "Editor archive conflict inspection changed or misclassified its isolated sentinel files"});
                puzzleCampaignArchiveInspection = std::move(conflictInspection).value();
                puzzleCampaignTransferRecoveryMessage = std::string(tr(
                    "Existing archive conflict inspected; no files were changed."));
            }
            return astro::core::Result<void>::success();
#else
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Malformed campaign transfer journal archive is unavailable in this Editor build"});
#endif
        };
        auto drawPuzzleSaveSettingsPanel = [&]() {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (smoke) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            if (ImGui::CollapsingHeader(tr("Puzzle Save Settings")))
            {
                puzzleSaveSettingsWindowDrawn = true;
                if (activeProjectRoot.empty()) ImGui::TextDisabled(tr("Open a project to configure Puzzle saves."));
                else
                {
                    if (ImGui::InputText(tr("SaveProfile ID"), puzzleSaveProfileIdInput.data(),
                        puzzleSaveProfileIdInput.size())) puzzleSaveSettingsDirty = true;
                    if (ImGui::InputScalar(tr("SaveProfile version"), ImGuiDataType_U32,
                        &puzzleSaveProfileVersionInput)) puzzleSaveSettingsDirty = true;
                    if (ImGui::InputText(tr("User-data subdirectory"), puzzleSaveDirectoryInput.data(),
                        puzzleSaveDirectoryInput.size()))
                    {
                        puzzleSaveSettingsDirty = true;
                        gameViewPuzzleCampaignTransferPreview.reset();
                        gameViewPuzzleCampaignTransferCopyResult.reset();
                        puzzleCampaignTransferPreviewMessage.clear();
                    }
                    ImGui::TextDisabled(tr("Relative to SDL user-data root; use / separators. Parent paths are rejected."));
                    ImGui::TextDisabled(tr("Changing profile version or folder does not delete previous slot files."));
                    ImGui::BeginDisabled(!puzzleSaveSettingsDirty);
                    if (ImGui::Button(tr("Apply project settings")))
                    {
                        const auto applied = applyPuzzleProjectSettings();
                        if (!applied) reportError(applied.error().message);
                    }
                    ImGui::EndDisabled();
                    if (!puzzleSaveSettingsReady)
                        ImGui::TextWrapped(tr("Settings file needs repair. Apply valid values to replace it: %s"),
                            gameViewPuzzleSaveMessage.c_str());

                    const std::string draftDirectory(puzzleSaveDirectoryInput.data());
                    const bool folderChanged = draftDirectory != gameViewPuzzleProjectSettings.userDataSubdirectory;
                    ImGui::TextDisabled(tr("Preview is limited to the active campaign ID and does not change files."));
                    ImGui::BeginDisabled(!puzzleSaveSettingsReady || !gameViewPuzzleCampaign || !folderChanged);
                    if (ImGui::Button(tr("Preview save transfer")))
                    {
                        const auto previewed = previewPuzzleCampaignTransfer();
                        if (!previewed)
                        {
                            gameViewPuzzleCampaignTransferPreview.reset();
                            puzzleCampaignTransferPreviewMessage = previewed.error().message;
                        }
                    }
                    ImGui::EndDisabled();
                    if (!folderChanged)
                        ImGui::TextDisabled(tr("Change the folder above to preview another save namespace."));
                    if (!puzzleCampaignTransferPreviewMessage.empty())
                        ImGui::TextWrapped(tr("Preview error: %s"), puzzleCampaignTransferPreviewMessage.c_str());
                    if (gameViewPuzzleCampaignTransferPreview)
                    {
                        const auto& preview = *gameViewPuzzleCampaignTransferPreview;
                        puzzleCampaignTransferPreviewDrawn = true;
                        const auto sourcePath = preview.sourceDirectory.u8string();
                        const auto targetPath = preview.targetDirectory.u8string();
                        ImGui::TextWrapped(tr("Source: %s"), reinterpret_cast<const char*>(sourcePath.c_str()));
                        ImGui::TextWrapped(tr("Target: %s"), reinterpret_cast<const char*>(targetPath.c_str()));
                        ImGui::TextDisabled(tr("Slots: %llu valid, %llu recovered, %llu invalid, %llu campaign mismatch; %llu conflicts; %llu bytes."),
                            static_cast<unsigned long long>(preview.validSlots),
                            static_cast<unsigned long long>(preview.recoveredSlots),
                            static_cast<unsigned long long>(preview.invalidSlots),
                            static_cast<unsigned long long>(preview.campaignMismatchSlots),
                            static_cast<unsigned long long>(preview.conflictingSlots),
                            static_cast<unsigned long long>(preview.sourceBytes));
                        if (ImGui::BeginChild("##CampaignTransferPreviewRows", ImVec2(0.0f, 160.0f), true))
                        {
                            if (ImGui::BeginTable("##CampaignTransferSlots", 5,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableSetupColumn(tr("Slot"), ImGuiTableColumnFlags_WidthFixed, 42.0f);
                                ImGui::TableSetupColumn(tr("Status"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
                                ImGui::TableSetupColumn(tr("Level"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                                ImGui::TableSetupColumn(tr("Bytes"), ImGuiTableColumnFlags_WidthFixed, 72.0f);
                                ImGui::TableSetupColumn(tr("Target"), ImGuiTableColumnFlags_WidthFixed, 86.0f);
                                ImGui::TableHeadersRow();
                                for (const auto& slot : preview.slots)
                                {
                                    const char* status = tr("Invalid");
                                    switch (slot.state)
                                    {
                                    case astro::runtime::PuzzleCampaignSaveSlotPreviewState::Empty:
                                        status = tr("Empty"); break;
                                    case astro::runtime::PuzzleCampaignSaveSlotPreviewState::Valid:
                                        status = tr("Valid"); break;
                                    case astro::runtime::PuzzleCampaignSaveSlotPreviewState::RecoveredFromBackup:
                                        status = tr("Recovered from backup"); break;
                                    case astro::runtime::PuzzleCampaignSaveSlotPreviewState::CampaignMismatch:
                                        status = tr("Campaign ID mismatch"); break;
                                    case astro::runtime::PuzzleCampaignSaveSlotPreviewState::Invalid:
                                        status = tr("Invalid"); break;
                                    }
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn(); ImGui::Text(tr("Slot %d"), static_cast<int>(slot.slot));
                                    ImGui::TableNextColumn(); ImGui::TextUnformatted(status);
                                    ImGui::TableNextColumn(); ImGui::TextUnformatted(slot.savedLevelId.c_str());
                                    ImGui::TableNextColumn(); ImGui::Text(tr("%llu"),
                                        static_cast<unsigned long long>(slot.sourceBytes));
                                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                                        slot.primaryConflict || slot.backupConflict ? tr("Conflict") : tr("Clear"));
                                }
                                ImGui::EndTable();
                            }
                            for (const auto& artifact : preview.unexpectedSourceArtifacts)
                                ImGui::TextWrapped(tr("Unclassified source file: %s"), artifact.c_str());
                        }
                        ImGui::EndChild();
                        ImGui::TextDisabled(tr("Snapshot compatibility still requires the normal campaign load validation."));
                        if (preview.pendingRecovery)
                        {
                            puzzleCampaignTransferRecoveryDrawn = true;
                            const auto& recovery = *preview.pendingRecovery;
                            const auto journalPath = recovery.journalPath.u8string();
                            ImGui::Separator();
                            ImGui::TextWrapped(tr("Recovery journal: %s"),
                                reinterpret_cast<const char*>(journalPath.c_str()));
                            if (recovery.journalValid)
                                ImGui::TextWrapped(tr("An interrupted campaign copy was detected. Review its files before another copy."));
                            else
                            {
                                const char* journalState = tr("Unknown journal problem");
                                switch (recovery.journalState)
                                {
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Missing:
                                    journalState = tr("Journal is missing"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Valid:
                                    journalState = tr("Journal is valid"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Unreadable:
                                    journalState = tr("Journal could not be read"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Unsafe:
                                    journalState = tr("Journal path is unsafe or not a regular file"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Oversized:
                                    journalState = tr("Journal exceeds the supported size limit"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Malformed:
                                    journalState = tr("Journal is malformed, truncated, or uses an unsupported version"); break;
                                }
                                ImGui::TextWrapped(tr("Recovery journal status: %s"), journalState);
                                ImGui::TextWrapped(tr("No save artifact paths were inferred and no save files were changed. Copy and resume are unavailable. A readable malformed journal can be copied to an archive after confirmation; the original remains and continues to block transfers. Other cases require manual handling."));
                                if (smoke && recovery.journalState == astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Malformed
                                    && recovery.artifacts.empty())
                                    puzzleCampaignTransferInvalidJournalSmokePassed = true;
                                if (recovery.journalState == astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Malformed
                                    && recovery.journalBytes <= 4096)
                                {
                                    if (ImGui::Button(tr("Create archive copy of malformed journal…")))
                                        ImGui::OpenPopup("##ConfirmMalformedCampaignTransferJournalArchive");
                                    if (smoke && !puzzleCampaignTransferArchivePromptDrawn)
                                        ImGui::OpenPopup("##ConfirmMalformedCampaignTransferJournalArchive");
                                    if (ImGui::BeginPopupModal("##ConfirmMalformedCampaignTransferJournalArchive", nullptr,
                                        ImGuiWindowFlags_AlwaysAutoResize))
                                    {
                                        puzzleCampaignTransferArchivePromptDrawn = true;
                                        ImGui::TextWrapped(tr("This copies the exact journal bytes to a new adjacent archive file and verifies that copy. The original journal remains in place and continues to block transfers until manually reviewed."));
                                        ImGui::TextWrapped(tr("No campaign slot/save files are inspected, inferred, changed or removed. A conflicting archive file is never replaced."));
                                        ImGui::TextWrapped(tr("Journal: %s"), reinterpret_cast<const char*>(journalPath.c_str()));
                                        ImGui::TextWrapped(tr("Archive folder: %s"), reinterpret_cast<const char*>(targetPath.c_str()));
                                        if (smoke)
                                        {
                                            const auto archived = archivePuzzleCampaignTransferJournal();
                                            if (!archived) puzzleCampaignTransferRecoveryMessage = archived.error().message;
                                            ImGui::CloseCurrentPopup();
                                        }
                                        else if (ImGui::Button(tr("Create archive copy")))
                                        {
                                            const auto archived = archivePuzzleCampaignTransferJournal();
                                            if (!archived) puzzleCampaignTransferRecoveryMessage = archived.error().message;
                                            ImGui::CloseCurrentPopup();
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::Button(tr("Cancel journal archive"))) ImGui::CloseCurrentPopup();
                                        ImGui::EndPopup();
                                    }
                                }
                            }
                            if (!recovery.campaignId.empty())
                                ImGui::TextWrapped(tr("Journal campaign ID: %s"), recovery.campaignId.c_str());
                            if (!recovery.diagnostic.empty())
                                ImGui::TextWrapped(tr("Recovery detail: %s"), recovery.diagnostic.c_str());
                            if (smoke && recovery.journalValid && recovery.campaignId == "smoke-campaign")
                                puzzleCampaignTransferRecoverySmokePassed = true;
                            for (const auto& artifact : recovery.artifacts)
                            {
                                const char* state = tr("Missing");
                                switch (artifact.state)
                                {
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Missing:
                                    state = tr("Missing"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Complete:
                                    state = tr("Complete"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Partial:
                                    state = tr("Partial"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Changed:
                                    state = tr("Changed"); break;
                                case astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Unsafe:
                                    state = tr("Unsafe path"); break;
                                }
                                const auto artifactPath = artifact.targetPath.u8string();
                                ImGui::TextWrapped(tr("Recovery artifact — slot %d, %s, %llu/%llu bytes: %s"),
                                    static_cast<int>(artifact.slot), state,
                                    static_cast<unsigned long long>(artifact.observedBytes),
                                    static_cast<unsigned long long>(artifact.expectedBytes),
                                    reinterpret_cast<const char*>(artifactPath.c_str()));
                            }
                            const bool resumableArtifacts = recovery.journalValid
                                && recovery.campaignId == preview.campaignId
                                && std::all_of(recovery.artifacts.begin(), recovery.artifacts.end(), [](const auto& artifact) {
                                    return artifact.state == astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Complete
                                        || artifact.state == astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Missing;
                                });
                            const bool hasPartialArtifact = std::any_of(recovery.artifacts.begin(),
                                recovery.artifacts.end(), [](const auto& artifact) {
                                    return artifact.state == astro::runtime::PuzzleCampaignSaveTransferRecoveryArtifactState::Partial;
                                });
                            if (smoke && hasPartialArtifact && !resumableArtifacts)
                                puzzleCampaignTransferPartialPolicySmokePassed = true;
                            if (hasPartialArtifact)
                                ImGui::TextWrapped(tr("Partial target files are left untouched. Review their contents manually; resume will not delete, repair, append to, or overwrite them."));
                            ImGui::BeginDisabled(!resumableArtifacts);
                            if (ImGui::Button(tr("Resume interrupted transfer…")))
                                ImGui::OpenPopup("##ConfirmCampaignTransferResume");
                            ImGui::EndDisabled();
                            if (ImGui::BeginPopupModal("##ConfirmCampaignTransferResume", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
                            {
                                ImGui::TextWrapped(tr("Resume rechecks every source file against the journal. Complete target files are kept; only missing files are created exclusively."));
                                ImGui::TextWrapped(tr("Any changed source or partial, changed, or unsafe target blocks the operation. No existing save is repaired or overwritten."));
                                ImGui::TextWrapped(tr("Source: %s"), reinterpret_cast<const char*>(sourcePath.c_str()));
                                ImGui::TextWrapped(tr("Target: %s"), reinterpret_cast<const char*>(targetPath.c_str()));
                                if (ImGui::Button(tr("Resume reviewed transfer")))
                                {
                                    const auto resumed = resumePuzzleCampaignTransfer();
                                    if (!resumed) puzzleCampaignTransferRecoveryMessage = resumed.error().message;
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(tr("Cancel resume"))) ImGui::CloseCurrentPopup();
                                ImGui::EndPopup();
                            }
                            if (recovery.journalValid
                                && ImGui::Button(tr("Review and dismiss recovery record…")))
                                ImGui::OpenPopup("##ConfirmCampaignTransferRecoveryAcknowledge");
                            if (ImGui::BeginPopupModal("##ConfirmCampaignTransferRecoveryAcknowledge", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
                            {
                                ImGui::TextWrapped(tr("This removes only the recovery journal. It does not remove, repair, or overwrite any save file."));
                                ImGui::TextWrapped(tr("Inspect or clean partial save files manually before starting another copy."));
                                ImGui::TextWrapped(tr("Journal: %s"), reinterpret_cast<const char*>(journalPath.c_str()));
                                if (ImGui::Button(tr("Acknowledge recovery record")))
                                {
                                    const auto acknowledged = acknowledgePuzzleCampaignTransferRecovery();
                                    if (!acknowledged)
                                        puzzleCampaignTransferRecoveryMessage = acknowledged.error().message;
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(tr("Cancel recovery review"))) ImGui::CloseCurrentPopup();
                                ImGui::EndPopup();
                            }
                            if (!puzzleCampaignTransferRecoveryMessage.empty())
                                ImGui::TextWrapped(tr("Recovery result: %s"),
                                    puzzleCampaignTransferRecoveryMessage.c_str());
                            if (puzzleCampaignArchiveInspection)
                            {
                                const auto pathText = puzzleCampaignArchiveInspection->archivePath.u8string();
                                ImGui::TextWrapped(tr("Archive path: %s"),
                                    reinterpret_cast<const char*>(pathText.c_str()));
                                const auto pathKind = [&]() -> const char* {
                                    using Kind = astro::runtime::PuzzleCampaignSaveArchivePathKind;
                                    switch (puzzleCampaignArchiveInspection->pathKind)
                                    {
                                    case Kind::Missing: return tr("Archive path is missing");
                                    case Kind::RegularFile: return tr("Archive path is a regular file");
                                    case Kind::Directory: return tr("Archive path is a directory");
                                    case Kind::SymlinkOrReparsePoint: return tr("Archive path is a link or reparse point");
                                    case Kind::Other: return tr("Archive path is another file type");
                                    case Kind::Unavailable: return tr("Archive path is unavailable");
                                    }
                                    return tr("Archive path is unavailable");
                                }();
                                ImGui::TextWrapped(tr("Archive object: %s"), pathKind);
                                if (puzzleCampaignArchiveInspection->byteSize)
                                    ImGui::TextWrapped(tr("Observed bytes: %llu"),
                                        static_cast<unsigned long long>(*puzzleCampaignArchiveInspection->byteSize));
                                const auto contentMatch = [&]() -> const char* {
                                    using Match = astro::runtime::PuzzleCampaignSaveArchiveContentMatch;
                                    switch (puzzleCampaignArchiveInspection->contentMatch)
                                    {
                                    case Match::NotApplicable: return tr("Content was not read");
                                    case Match::ExactMatch: return tr("Bytes exactly match the reviewed journal");
                                    case Match::Different: return tr("Bytes differ from the reviewed journal");
                                    case Match::PrefixMatch: return tr("Bytes are a prefix of the reviewed journal");
                                    case Match::Oversized: return tr("File exceeds the journal size limit");
                                    case Match::Unavailable: return tr("Content could not be compared safely");
                                    }
                                    return tr("Content could not be compared safely");
                                }();
                                ImGui::TextWrapped(tr("Archive comparison: %s"), contentMatch);
                                if (!puzzleCampaignArchiveInspection->diagnostic.empty())
                                    ImGui::TextWrapped(tr("%s"), puzzleCampaignArchiveInspection->diagnostic.c_str());
                                ImGui::TextWrapped(tr("A prefix match does not identify who created the file. No files were changed by this inspection."));
                            }
                            const bool archiveConflictCopyEligible = puzzleCampaignArchiveInspection
                                && puzzleCampaignArchiveInspection->pathKind
                                    == astro::runtime::PuzzleCampaignSaveArchivePathKind::RegularFile
                                && (puzzleCampaignArchiveInspection->contentMatch
                                        == astro::runtime::PuzzleCampaignSaveArchiveContentMatch::Different
                                    || puzzleCampaignArchiveInspection->contentMatch
                                        == astro::runtime::PuzzleCampaignSaveArchiveContentMatch::PrefixMatch);
                            auto prepareArchiveConflictCopy = [&]() {
                                if (!preview.pendingRecovery || !puzzleCampaignArchiveInspection)
                                {
                                    puzzleCampaignArchiveConflictCopyMessage = std::string(tr(
                                        "No reviewed archive conflict is available for copying."));
                                    return;
                                }
                                auto copyPreview = astro::runtime::previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                    *preview.pendingRecovery, *puzzleCampaignArchiveInspection);
                                if (!copyPreview)
                                {
                                    puzzleCampaignArchiveConflictCopyPreview.reset();
                                    puzzleCampaignArchiveConflictCopyMessage = copyPreview.error().message;
                                    return;
                                }
                                puzzleCampaignArchiveConflictCopyPreview = std::move(copyPreview).value();
                                puzzleCampaignArchiveConflictCopyMessage.clear();
                                ImGui::OpenPopup("##ConfirmArchiveConflictEvidenceCopy");
                            };
                            ImGui::BeginDisabled(!archiveConflictCopyEligible);
                            if (ImGui::Button(tr("Prepare separate unverified copy…")))
                                prepareArchiveConflictCopy();
                            ImGui::EndDisabled();
                            if (smoke && archiveConflictCopyEligible
                                && !puzzleCampaignArchiveConflictCopyPromptDrawn)
                                prepareArchiveConflictCopy();
                            if (ImGui::BeginPopupModal("##ConfirmArchiveConflictEvidenceCopy", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
                            {
                                puzzleCampaignArchiveConflictCopyPromptDrawn = true;
                                if (puzzleCampaignArchiveConflictCopyPreview)
                                {
                                    const auto sourceText = puzzleCampaignArchiveConflictCopyPreview->sourcePath.u8string();
                                    const auto destinationText = puzzleCampaignArchiveConflictCopyPreview->destinationPath.u8string();
                                    ImGui::TextWrapped(tr("A separate byte-for-byte copy will be created. The original remains untouched."));
                                    ImGui::TextWrapped(tr("Copy source: %s"), reinterpret_cast<const char*>(sourceText.c_str()));
                                    ImGui::TextWrapped(tr("Copy destination: %s"),
                                        reinterpret_cast<const char*>(destinationText.c_str()));
                                    ImGui::TextWrapped(tr("Copy size: %llu bytes"),
                                        static_cast<unsigned long long>(puzzleCampaignArchiveConflictCopyPreview->reviewedBytes.size()));
                                    ImGui::TextWrapped(tr("The file creator is unknown. This copy is unverified and will not clear the original conflict."));
                                    if (smoke && !inspectCopyDialog)
                                    {
                                        const auto preserved = astro::runtime::preserveMalformedPuzzleCampaignSaveArchiveConflict(
                                            *preview.pendingRecovery, *puzzleCampaignArchiveConflictCopyPreview, true);
                                        const auto readSmokeBytes = [](const std::filesystem::path& path) {
                                            std::ifstream input(path, std::ios::binary);
                                            return std::string(std::istreambuf_iterator<char>(input), {});
                                        };
                                        const auto candidatePath = preview.targetDirectory / "slot-4.astro-save";
                                        puzzleCampaignArchiveConflictCopySmokePassed = preserved
                                            && preserved.value() == puzzleCampaignArchiveConflictCopyPreview->destinationPath
                                            && readSmokeBytes(preserved.value()) == "pre-existing archive owner"
                                            && readSmokeBytes(puzzleCampaignArchiveConflictCopyPreview->sourcePath)
                                                == "pre-existing archive owner"
                                            && readSmokeBytes(preview.pendingRecovery->journalPath)
                                                == puzzleCampaignInvalidJournalSmokeBytes
                                            && readSmokeBytes(candidatePath) == "keep candidate save untouched";
                                        if (puzzleCampaignArchiveConflictCopySmokePassed)
                                            puzzleCampaignArchiveConflictCopyMessage = std::string(tr(
                                                "Unverified copy created; the original conflict remains."));
                                        else
                                            puzzleCampaignArchiveConflictCopyMessage = preserved
                                                ? std::string(tr("Archive conflict copy smoke did not preserve its source files."))
                                                : preserved.error().message;
                                        ImGui::CloseCurrentPopup();
                                    }
                                    else if (ImGui::Button(tr("Create separate copy")))
                                    {
                                        const auto preserved = astro::runtime::preserveMalformedPuzzleCampaignSaveArchiveConflict(
                                            *preview.pendingRecovery, *puzzleCampaignArchiveConflictCopyPreview, true);
                                        if (preserved)
                                            puzzleCampaignArchiveConflictCopyMessage = std::string(tr(
                                                "Unverified copy created; the original conflict remains.")) + " "
                                                + preserved.value().string();
                                        else
                                            puzzleCampaignArchiveConflictCopyMessage = preserved.error().message;
                                        puzzleCampaignArchiveConflictCopyPreview.reset();
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::SameLine();
                                    if ((!smoke || inspectCopyDialog)
                                        && ImGui::Button(tr("Cancel separate copy")))
                                    {
                                        if (inspectCopyDialog)
                                        {
                                            const auto readSmokeBytes = [](const std::filesystem::path& path) {
                                                std::ifstream input(path, std::ios::binary);
                                                return std::string(std::istreambuf_iterator<char>(input), {});
                                            };
                                            const auto candidatePath = preview.targetDirectory / "slot-4.astro-save";
                                            std::error_code existsError;
                                            puzzleCampaignArchiveConflictCopySmokePassed
                                                = !std::filesystem::exists(
                                                    puzzleCampaignArchiveConflictCopyPreview->destinationPath, existsError)
                                                && !existsError
                                                && readSmokeBytes(puzzleCampaignArchiveConflictCopyPreview->sourcePath)
                                                    == "pre-existing archive owner"
                                                && readSmokeBytes(preview.pendingRecovery->journalPath)
                                                    == puzzleCampaignInvalidJournalSmokeBytes
                                                && readSmokeBytes(candidatePath) == "keep candidate save untouched";
                                        }
                                        puzzleCampaignArchiveConflictCopyPreview.reset();
                                        ImGui::CloseCurrentPopup();
                                    }
                                }
                                else
                                {
                                    ImGui::TextWrapped(tr("Copy preview is no longer available."));
                                    if (ImGui::Button(tr("Close copy preview"))) ImGui::CloseCurrentPopup();
                                }
                                ImGui::EndPopup();
                            }
                            if (!puzzleCampaignArchiveConflictCopyMessage.empty())
                                ImGui::TextWrapped(tr("Copy-aside result: %s"),
                                    puzzleCampaignArchiveConflictCopyMessage.c_str());
                        }
                        const bool hasTransferableSlots = preview.validSlots + preview.recoveredSlots > 0;
                        ImGui::BeginDisabled(!hasTransferableSlots || preview.conflictingSlots != 0
                            || preview.pendingRecovery.has_value());
                        if (ImGui::Button(tr("Copy campaign saves…")))
                            ImGui::OpenPopup("##ConfirmCampaignSaveTransfer");
                        ImGui::EndDisabled();

                        if (smoke && !puzzleCampaignTransferConfirmationDrawn)
                            ImGui::OpenPopup("##ConfirmCampaignSaveTransfer");
                        if (ImGui::BeginPopupModal("##ConfirmCampaignSaveTransfer", nullptr,
                            ImGuiWindowFlags_AlwaysAutoResize))
                        {
                            puzzleCampaignTransferConfirmationDrawn = true;
                            ImGui::TextWrapped(tr("Review these paths before copying campaign saves."));
                            ImGui::TextWrapped(tr("Source: %s"), reinterpret_cast<const char*>(sourcePath.c_str()));
                            ImGui::TextWrapped(tr("Target: %s"), reinterpret_cast<const char*>(targetPath.c_str()));
                        ImGui::TextWrapped(tr("Copy summary: %llu save slots, %llu bytes. Existing files will not be overwritten."),
                                static_cast<unsigned long long>(preview.validSlots + preview.recoveredSlots),
                                static_cast<unsigned long long>(preview.sourceBytes));
                            ImGui::TextWrapped(tr("Copying will create files in the target folder. The source remains unchanged."));
                            if (smoke)
                            {
                                // The smoke invokes the shared confirm handler against an isolated fixture.
                                ImGui::CloseCurrentPopup();
                            }
                            else if (ImGui::Button(tr("Copy reviewed save files")))
                            {
                                const auto copied = confirmPuzzleCampaignTransferCopy(true);
                                if (!copied) puzzleCampaignTransferPreviewMessage = copied.error().message;
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(tr("Cancel copy"))) ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                        }
                        if (gameViewPuzzleCampaignTransferCopyResult)
                        {
                            const auto& copy = *gameViewPuzzleCampaignTransferCopyResult;
                            const char* state = tr("Copy failed; rollback completed");
                            switch (copy.state)
                            {
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::Copied:
                                state = tr("Campaign saves copied"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::Resumed:
                                state = tr("Interrupted campaign transfer resumed"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::ConfirmationRequired:
                                state = tr("Copy confirmation required"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::StaleSource:
                                state = tr("Source changed; preview again"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::StaleDestination:
                                state = tr("Target changed; preview again"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::DestinationConflict:
                                state = tr("Target conflict; no files overwritten"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::SourceNotTransferable:
                                state = tr("Source is not transferable"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::CancelledRolledBack:
                                state = tr("Copy cancelled; created files rolled back"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::FailedRolledBack:
                                state = tr("Copy failed; rollback completed"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::RollbackIncomplete:
                                state = tr("Rollback incomplete; inspect remaining files"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::RecoveryRequired:
                                state = tr("Interrupted transfer needs review"); break;
                            case astro::runtime::PuzzleCampaignSaveTransferCopyState::CopiedRecoveryPending:
                                state = tr("Saves copied; recovery journal remains"); break;
                            }
                        ImGui::TextWrapped(tr("%s — %llu copied, %llu skipped, %llu attempted, %llu bytes"), state,
                                static_cast<unsigned long long>(copy.filesCopied),
                                static_cast<unsigned long long>(copy.filesSkipped),
                                static_cast<unsigned long long>(copy.filesAttempted),
                                static_cast<unsigned long long>(copy.bytesCopied));
                            if (!copy.diagnostic.empty()) ImGui::TextWrapped(tr("%s"), copy.diagnostic.c_str());
                            for (const auto& remaining : copy.remainingArtifacts)
                            {
                                const auto remainingPath = remaining.u8string();
                                ImGui::TextWrapped(tr("Remaining file: %s"),
                                    reinterpret_cast<const char*>(remainingPath.c_str()));
                            }
                        }
                    }
                }
            }
#endif
        };
        auto startPuzzleGame = [&](const astro::runtime::PuzzleLevelDefinition* definition = nullptr) -> astro::core::Result<void> {
            csharpPlayApproval.revoke();
            pendingCSharpApproval.reset();
            auto created = definition ? astro::runtime::PuzzleGame::create(*definition)
                : astro::runtime::PuzzleGame::create(8, 8, 9123, 5, 30);
            if (!created) return astro::core::Result<void>::failure(created.error());
            gameViewPuzzleCampaign.reset();
            puzzleCampaignMessage.clear();
            gameViewPuzzle = std::move(created).value();
            activePuzzleLevelId = definition ? std::optional<std::string>{definition->id} : std::nullopt;
            gameViewPuzzleSaveStore.reset();
            gameViewPuzzleSaveProfile.reset();
            gameViewPuzzleSaveDirectory.clear();
            gameViewPuzzleCampaignSaveStore.reset();
            gameViewPuzzleCampaignSaveProfile.reset();
            gameViewPuzzleCampaignSaveDirectory.clear();
            gameViewPuzzleCampaignSaveMessage.clear();
            selectedPuzzleCell.reset();
            pendingPuzzleSwap.reset();
            lastPuzzleMove.reset();
            if (!definition) puzzleAppliedMoves = 0;
            stopPlaySession();
            const auto started = playSession.play(scene,
                [&](Scene&, const double) -> astro::core::Result<void> {
                    if (!gameViewPuzzle && !gameViewPuzzleCampaign)
                        return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                            "Puzzle runtime state is unavailable"});
                    if (!pendingPuzzleSwap) return astro::core::Result<void>::success();
                    const auto swap = *pendingPuzzleSwap;
                    pendingPuzzleSwap.reset();
                    const auto moved = gameViewPuzzleCampaign
                        ? gameViewPuzzleCampaign->swap(swap.first, swap.second)
                        : gameViewPuzzle->swap(swap.first, swap.second);
                    if (!moved) return astro::core::Result<void>::failure(moved.error());
                    lastPuzzleMove = moved.value();
                    if (moved.value().accepted) ++puzzleAppliedMoves;
                    return astro::core::Result<void>::success();
                });
            if (!started) gameViewPuzzle.reset();
            return started;
        };
        auto startPuzzleCampaign = [&]() -> astro::core::Result<void> {
            csharpPlayApproval.revoke();
            pendingCSharpApproval.reset();
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project before starting a Puzzle campaign"});
            const std::string campaignId = puzzleCampaignIdInput.data();
            auto created = astro::runtime::PuzzleCampaignSession::start(activeProjectRoot, campaignId);
            if (!created) return astro::core::Result<void>::failure(created.error());
            gameViewPuzzleSaveStore.reset();
            gameViewPuzzleSaveProfile.reset();
            gameViewPuzzleSaveDirectory.clear();
            gameViewPuzzleCampaignSaveStore.reset();
            gameViewPuzzleCampaignSaveProfile.reset();
            gameViewPuzzleCampaignSaveDirectory.clear();
            gameViewPuzzleCampaignSaveMessage.clear();
            gameViewPuzzleCampaignTransferPreview.reset();
            gameViewPuzzleCampaignTransferCopyResult.reset();
            puzzleCampaignTransferPreviewMessage.clear();
            gameViewPuzzle.reset();
            activePuzzleLevelId = created.value().currentLevel().id;
            gameViewPuzzleCampaign = std::move(created).value();
            puzzleCampaignMessage = tr("Campaign started at level ") + *activePuzzleLevelId;
            selectedPuzzleCell.reset();
            pendingPuzzleSwap.reset();
            lastPuzzleMove.reset();
            puzzleAppliedMoves = 0;
            stopPlaySession();
            const auto started = playSession.play(scene,
                [&](Scene&, const double) -> astro::core::Result<void> {
                    if (!gameViewPuzzleCampaign)
                        return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                            "Puzzle campaign session is unavailable"});
                    if (!pendingPuzzleSwap) return astro::core::Result<void>::success();
                    const auto swap = *pendingPuzzleSwap;
                    pendingPuzzleSwap.reset();
                    const auto moved = gameViewPuzzleCampaign->swap(swap.first, swap.second);
                    if (!moved) return astro::core::Result<void>::failure(moved.error());
                    lastPuzzleMove = moved.value();
                    if (moved.value().accepted) ++puzzleAppliedMoves;
                    return astro::core::Result<void>::success();
                });
            if (!started)
            {
                gameViewPuzzleCampaign.reset();
                activePuzzleLevelId.reset();
            }
            return started;
        };
        auto advancePuzzleCampaign = [&]() -> astro::core::Result<void> {
            if (!gameViewPuzzleCampaign)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "No Puzzle campaign is active"});
            const auto advanced = gameViewPuzzleCampaign->completeCurrentLevel();
            if (!advanced) return astro::core::Result<void>::failure(advanced.error());
            selectedPuzzleCell.reset();
            pendingPuzzleSwap.reset();
            lastPuzzleMove.reset();
            if (advanced.value())
            {
                activePuzzleLevelId = *advanced.value();
                puzzleCampaignMessage = tr("Loaded campaign level ") + *advanced.value();
            }
            else
            {
                activePuzzleLevelId = gameViewPuzzleCampaign->currentLevel().id;
                puzzleCampaignMessage = tr("Campaign complete (END). Final level and score are retained.");
            }
            return astro::core::Result<void>::success();
        };
        auto copyPuzzleLevelToEditorFields = [&](const astro::runtime::PuzzleLevelDefinition& level) {
            puzzleLevelDraft = level;
            puzzleLevelIdInput.fill('\0');
            puzzleLevelNameInput.fill('\0');
            std::copy_n(level.id.data(), std::min(level.id.size(), puzzleLevelIdInput.size() - 1),
                puzzleLevelIdInput.data());
            std::copy_n(level.name.data(), std::min(level.name.size(), puzzleLevelNameInput.size() - 1),
                puzzleLevelNameInput.data());
        };
        auto refreshPuzzleLevelCatalog = [&]() -> astro::core::Result<void> {
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project to browse Puzzle levels"});
            auto listed = astro::runtime::PuzzleLevelFile::list(activeProjectRoot);
            if (!listed) return astro::core::Result<void>::failure(listed.error());
            puzzleLevelCatalog = std::move(listed).value();
            puzzleLevelCatalogProjectRoot = activeProjectRoot;
            return astro::core::Result<void>::success();
        };
        auto refreshPuzzleCampaignCatalog = [&]() -> astro::core::Result<void> {
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project before browsing Puzzle campaigns"});
            auto listed = astro::runtime::PuzzleCampaignFile::list(activeProjectRoot);
            if (!listed) return astro::core::Result<void>::failure(listed.error());
            puzzleCampaignCatalog = std::move(listed).value();
            puzzleCampaignCatalogProjectRoot = activeProjectRoot;
            return astro::core::Result<void>::success();
        };
        auto drawPuzzleCampaignBrowser = [&] {
            if (smoke) showPuzzleCampaignBrowser = frames < 5;
            if (!showPuzzleCampaignBrowser) return;
            if (!ImGui::Begin(tr("Puzzle Campaign Browser"), &showPuzzleCampaignBrowser))
            {
                ImGui::End();
                return;
            }
            puzzleCampaignCatalogDrawn = true;
            if (activeProjectRoot.empty())
            {
                ImGui::TextDisabled(tr("Open a project to browse its campaign files."));
                ImGui::End();
                return;
            }
            if (activeProjectRoot != puzzleCampaignCatalogProjectRoot)
            {
                const auto refreshed = refreshPuzzleCampaignCatalog();
                if (!refreshed) { puzzleCampaignMessage = refreshed.error().message; reportError(refreshed.error().message); }
            }
            if (ImGui::SmallButton(tr("Refresh campaigns")))
            {
                const auto refreshed = refreshPuzzleCampaignCatalog();
                if (!refreshed) { puzzleCampaignMessage = refreshed.error().message; reportError(refreshed.error().message); }
            }
            ImGui::TextDisabled(tr("Project content: Puzzle/Campaigns/*.astro-puzzle-campaign"));
            if (ImGui::BeginChild("PuzzleCampaignCatalog", ImVec2(0.0f, 105.0f), true))
            {
                for (const auto& entry : puzzleCampaignCatalog)
                {
                    if (!entry.readable())
                    {
                        const auto diagnostic = astro::editor::translateEditorDiagnostic(entry.diagnostic, gEditorLanguage);
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                            tr("%s — invalid: %s"), entry.id.c_str(), diagnostic.c_str());
                        continue;
                    }
                    const auto& definition = *entry.definition;
                    const auto label = definition.name + " [" + entry.id + "]";
                    const bool selected = ImGui::Selectable(label.c_str(),
                        selectedPuzzleCampaignCatalogId == entry.id);
                    if (selected)
                    {
                        selectedPuzzleCampaignCatalogId = entry.id;
                        puzzleCampaignIdInput.fill('\0');
                        std::copy_n(entry.id.data(), std::min(entry.id.size(), puzzleCampaignIdInput.size() - 1),
                            puzzleCampaignIdInput.data());
                    }
                    ImGui::SameLine();
                    if (entry.referencesValid()) ImGui::TextDisabled(tr("%zu levels — links OK"),
                        entry.levelReferences.size());
                    else ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                        tr("%zu levels — link issues"), entry.levelReferences.size());
                }
                if (puzzleCampaignCatalog.empty()) ImGui::TextDisabled(tr("No campaign files found."));
            }
            ImGui::EndChild();
            const auto selectedEntry = std::find_if(puzzleCampaignCatalog.begin(), puzzleCampaignCatalog.end(),
                [&](const auto& entry) { return entry.id == selectedPuzzleCampaignCatalogId; });
            if (selectedEntry != puzzleCampaignCatalog.end() && !selectedEntry->readable())
            {
                const auto diagnostic = astro::editor::translateEditorDiagnostic(selectedEntry->diagnostic, gEditorLanguage);
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", diagnostic.c_str());
            }
            else if (selectedEntry != puzzleCampaignCatalog.end())
            {
                puzzleCampaignLinkDiagnosticsDrawn = true;
                const auto& definition = *selectedEntry->definition;
                ImGui::Text(tr("Start: %s   Ordered levels: %zu   Transitions: %zu"),
                    definition.startLevelId.c_str(), definition.orderedLevelIds.size(), definition.transitions.size());
                for (const auto& reference : selectedEntry->levelReferences)
                {
                    if (reference.available) ImGui::BulletText(tr("Level %s — OK"), reference.levelId.c_str());
                    else
                    {
                        puzzleCampaignMissingLinkDrawn = true;
                        const auto diagnostic = astro::editor::translateEditorDiagnostic(reference.diagnostic, gEditorLanguage);
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                            tr("Level %s — MISSING/INVALID: %s"), reference.levelId.c_str(), diagnostic.c_str());
                    }
                }
                ImGui::TextUnformatted(tr("Transitions:"));
                for (const auto& transition : definition.transitions)
                {
                    const auto target = transition.targetLevelId.value_or("END");
                    if (transition.kind == astro::runtime::PuzzleCampaignRuleKind::Default)
                        ImGui::BulletText(tr("%s — DEFAULT → %s"), transition.sourceLevelId.c_str(), target.c_str());
                    else
                        ImGui::BulletText(tr("%s — score ≥ %llu → %s"), transition.sourceLevelId.c_str(),
                            static_cast<unsigned long long>(transition.scoreThreshold), target.c_str());
                }
            }

            auto loadCampaignDraft = [&](const astro::runtime::PuzzleCampaignDefinition& campaign) {
                puzzleCampaignDraft = campaign;
                puzzleCampaignSavedBaseline = campaign;
                puzzleCampaignDraftIdInput.fill('\0');
                puzzleCampaignDraftNameInput.fill('\0');
                std::copy_n(campaign.id.data(), std::min(campaign.id.size(), puzzleCampaignDraftIdInput.size() - 1),
                    puzzleCampaignDraftIdInput.data());
                std::copy_n(campaign.name.data(), std::min(campaign.name.size(), puzzleCampaignDraftNameInput.size() - 1),
                    puzzleCampaignDraftNameInput.data());
                puzzleCampaignAuthoringMessage = tr("Loaded campaign draft: ") + campaign.name;
            };
            auto resetCampaignDraft = [&] {
                puzzleCampaignDraft = {"new-campaign", "New Campaign", "", {}, {}};
                puzzleCampaignSavedBaseline.reset();
                puzzleCampaignDraftIdInput.fill('\0');
                puzzleCampaignDraftNameInput.fill('\0');
                std::copy_n(puzzleCampaignDraft.id.data(), puzzleCampaignDraft.id.size(), puzzleCampaignDraftIdInput.data());
                std::copy_n(puzzleCampaignDraft.name.data(), puzzleCampaignDraft.name.size(), puzzleCampaignDraftNameInput.data());
                puzzleCampaignAuthoringMessage = tr("New campaign draft");
            };
            puzzleCampaignDraft.id = puzzleCampaignDraftIdInput.data();
            puzzleCampaignDraft.name = puzzleCampaignDraftNameInput.data();
            const bool draftDirty = !puzzleCampaignSavedBaseline || puzzleCampaignDraft != *puzzleCampaignSavedBaseline;
            auto requestCampaignDraftChange = [&](std::optional<std::string> openId, bool reset) {
                pendingPuzzleCampaignOpen = std::move(openId);
                pendingPuzzleCampaignReset = reset;
                if (draftDirty) ImGui::OpenPopup(tr("Discard Puzzle campaign draft?"));
                else if (reset) resetCampaignDraft();
                else if (pendingPuzzleCampaignOpen)
                {
                    const auto entry = std::find_if(puzzleCampaignCatalog.begin(), puzzleCampaignCatalog.end(),
                        [&](const auto& candidate) { return candidate.id == *pendingPuzzleCampaignOpen; });
                    if (entry != puzzleCampaignCatalog.end() && entry->definition) loadCampaignDraft(*entry->definition);
                }
            };
            if (ImGui::Button(tr("New campaign"))) requestCampaignDraftChange(std::nullopt, true);
            ImGui::SameLine();
            if (ImGui::Button(tr("Load selected campaign")) && selectedEntry != puzzleCampaignCatalog.end()
                && selectedEntry->definition)
                requestCampaignDraftChange(selectedEntry->id, false);
            if (ImGui::BeginPopupModal(tr("Discard Puzzle campaign draft?"), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted(tr("Discard current campaign draft changes?"));
                if (ImGui::Button(tr("Discard and continue")))
                {
                    if (pendingPuzzleCampaignReset) resetCampaignDraft();
                    else if (pendingPuzzleCampaignOpen)
                    {
                        const auto entry = std::find_if(puzzleCampaignCatalog.begin(), puzzleCampaignCatalog.end(),
                            [&](const auto& candidate) { return candidate.id == *pendingPuzzleCampaignOpen; });
                        if (entry != puzzleCampaignCatalog.end() && entry->definition) loadCampaignDraft(*entry->definition);
                    }
                    pendingPuzzleCampaignOpen.reset();
                    pendingPuzzleCampaignReset = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("Keep editing")))
                {
                    pendingPuzzleCampaignOpen.reset();
                    pendingPuzzleCampaignReset = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            puzzleCampaignAuthoringDrawn = true;
            ImGui::SeparatorText(tr("Campaign authoring"));
            if (puzzleLevelCatalogProjectRoot != activeProjectRoot)
            {
                const auto refreshed = refreshPuzzleLevelCatalog();
                if (!refreshed) puzzleCampaignAuthoringMessage = refreshed.error().message;
            }
            if (ImGui::SmallButton(tr("Refresh level choices")))
            {
                const auto refreshed = refreshPuzzleLevelCatalog();
                if (!refreshed) puzzleCampaignAuthoringMessage = refreshed.error().message;
            }
            ImGui::InputText(tr("Campaign ID"), puzzleCampaignDraftIdInput.data(), puzzleCampaignDraftIdInput.size());
            ImGui::InputText(tr("Campaign name"), puzzleCampaignDraftNameInput.data(), puzzleCampaignDraftNameInput.size());
            puzzleCampaignDraft.id = puzzleCampaignDraftIdInput.data();
            puzzleCampaignDraft.name = puzzleCampaignDraftNameInput.data();

            if (ImGui::BeginCombo(tr("Add authored level"), puzzleCampaignLevelChoice.empty()
                ? "Choose a valid level" : puzzleCampaignLevelChoice.c_str()))
            {
                for (const auto& level : puzzleLevelCatalog)
                {
                    if (!level.readable()) continue;
                    const bool chosen = puzzleCampaignLevelChoice == level.id;
                    if (ImGui::Selectable((level.id + " — " + level.name.value_or(tr("Unnamed level"))).c_str(), chosen))
                        puzzleCampaignLevelChoice = level.id;
                    if (chosen) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Add level")) && !puzzleCampaignLevelChoice.empty())
            {
                if (std::find(puzzleCampaignDraft.orderedLevelIds.begin(), puzzleCampaignDraft.orderedLevelIds.end(),
                    puzzleCampaignLevelChoice) == puzzleCampaignDraft.orderedLevelIds.end())
                {
                    puzzleCampaignDraft.orderedLevelIds.push_back(puzzleCampaignLevelChoice);
                    puzzleCampaignDraft.transitions.push_back({puzzleCampaignLevelChoice,
                        astro::runtime::PuzzleCampaignRuleKind::Default, 0, std::nullopt});
                    if (puzzleCampaignDraft.startLevelId.empty())
                        puzzleCampaignDraft.startLevelId = puzzleCampaignLevelChoice;
                }
            }
            ImGui::Text(tr("START:"));
            if (ImGui::BeginCombo("##PuzzleCampaignStart", puzzleCampaignDraft.startLevelId.empty()
                ? "Choose START" : puzzleCampaignDraft.startLevelId.c_str()))
            {
                for (const auto& id : puzzleCampaignDraft.orderedLevelIds)
                    if (ImGui::Selectable(id.c_str(), puzzleCampaignDraft.startLevelId == id))
                        puzzleCampaignDraft.startLevelId = id;
                ImGui::EndCombo();
            }
            static int selectedCampaignLevelIndex = 0;
            if (selectedCampaignLevelIndex >= static_cast<int>(puzzleCampaignDraft.orderedLevelIds.size()))
                selectedCampaignLevelIndex = static_cast<int>(puzzleCampaignDraft.orderedLevelIds.size()) - 1;
            for (std::size_t i = 0; i < puzzleCampaignDraft.orderedLevelIds.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                const auto& id = puzzleCampaignDraft.orderedLevelIds[i];
                ImGui::Selectable((std::to_string(i) + ". " + id).c_str(), selectedCampaignLevelIndex == static_cast<int>(i));
                if (ImGui::IsItemClicked()) selectedCampaignLevelIndex = static_cast<int>(i);
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Up")) && i > 0)
                {
                    std::swap(puzzleCampaignDraft.orderedLevelIds[i], puzzleCampaignDraft.orderedLevelIds[i - 1]);
                    --selectedCampaignLevelIndex;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Down")) && i + 1 < puzzleCampaignDraft.orderedLevelIds.size())
                {
                    std::swap(puzzleCampaignDraft.orderedLevelIds[i], puzzleCampaignDraft.orderedLevelIds[i + 1]);
                    ++selectedCampaignLevelIndex;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Remove")))
                {
                    const bool start = puzzleCampaignDraft.startLevelId == id;
                    const bool incoming = std::any_of(puzzleCampaignDraft.transitions.begin(),
                        puzzleCampaignDraft.transitions.end(), [&](const auto& rule) {
                            return rule.sourceLevelId != id && rule.targetLevelId && *rule.targetLevelId == id;
                        });
                    if (start || incoming)
                        puzzleCampaignAuthoringMessage = tr("Change START and incoming targets before removing this level.");
                    else
                    {
                        puzzleCampaignDraft.orderedLevelIds.erase(puzzleCampaignDraft.orderedLevelIds.begin() + i);
                        std::erase_if(puzzleCampaignDraft.transitions, [&](const auto& rule) { return rule.sourceLevelId == id; });
                        selectedCampaignLevelIndex = std::max(0, selectedCampaignLevelIndex - (i <= static_cast<std::size_t>(selectedCampaignLevelIndex) ? 1 : 0));
                    }
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (selectedCampaignLevelIndex >= 0
                && selectedCampaignLevelIndex < static_cast<int>(puzzleCampaignDraft.orderedLevelIds.size()))
            {
                const auto sourceId = puzzleCampaignDraft.orderedLevelIds[static_cast<std::size_t>(selectedCampaignLevelIndex)];
                ImGui::SeparatorText((std::string(tr("Transitions: ")) + sourceId).c_str());
                auto drawTarget = [&](const char* label, std::optional<std::string>& target, const std::string& key) {
                    const std::string preview = target.value_or("END");
                    ImGui::PushID(key.c_str());
                    bool changed = false;
                    if (ImGui::BeginCombo(label, preview.c_str()))
                    {
                        if (ImGui::Selectable(tr("END"), !target)) { target.reset(); changed = true; }
                        for (const auto& candidate : puzzleCampaignDraft.orderedLevelIds)
                            if (ImGui::Selectable(candidate.c_str(), target && *target == candidate))
                            { target = candidate; changed = true; }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                    return changed;
                };
                for (std::size_t i = 0; i < puzzleCampaignDraft.transitions.size(); ++i)
                {
                    auto& rule = puzzleCampaignDraft.transitions[i];
                    if (rule.sourceLevelId != sourceId) continue;
                    ImGui::PushID(static_cast<int>(i));
                    if (rule.kind == astro::runtime::PuzzleCampaignRuleKind::Default)
                    {
                        ImGui::TextUnformatted(tr("DEFAULT"));
                        drawTarget("Target", rule.targetLevelId, sourceId + "-default");
                    }
                    else
                    {
                        ImGui::TextUnformatted(tr("Score ≥"));
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(150.0f);
                        ImGui::InputScalar("##threshold", ImGuiDataType_U64, &rule.scoreThreshold);
                        ImGui::SameLine();
                        drawTarget("Target", rule.targetLevelId, sourceId + "-score-" + std::to_string(i));
                        ImGui::SameLine();
                        if (ImGui::SmallButton(tr("Remove rule")))
                        {
                            puzzleCampaignDraft.transitions.erase(puzzleCampaignDraft.transitions.begin() + i);
                            ImGui::PopID();
                            break;
                        }
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button(tr("Add score rule")))
                {
                    std::uint64_t threshold = 100;
                    for (const auto& rule : puzzleCampaignDraft.transitions)
                        if (rule.sourceLevelId == sourceId && rule.kind == astro::runtime::PuzzleCampaignRuleKind::ScoreAtLeast)
                            threshold = std::max(threshold, rule.scoreThreshold + (rule.scoreThreshold < UINT64_MAX ? 1 : 0));
                    const auto defaultIt = std::find_if(puzzleCampaignDraft.transitions.begin(),
                        puzzleCampaignDraft.transitions.end(), [&](const auto& rule) {
                            return rule.sourceLevelId == sourceId && rule.kind == astro::runtime::PuzzleCampaignRuleKind::Default;
                        });
                    puzzleCampaignDraft.transitions.insert(defaultIt, {sourceId,
                        astro::runtime::PuzzleCampaignRuleKind::ScoreAtLeast, threshold, std::nullopt});
                }
            }
            if (ImGui::Button(tr("Save campaign…"))) ImGui::OpenPopup(tr("Save Puzzle campaign?"));
            ImGui::SameLine();
            if (draftDirty) ImGui::TextDisabled(tr("Draft has unsaved changes"));
            if (!puzzleCampaignAuthoringMessage.empty()) ImGui::TextWrapped(tr("%s"), puzzleCampaignAuthoringMessage.c_str());
            if (ImGui::BeginPopupModal(tr("Save Puzzle campaign?"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped(tr("Save '%s' to Puzzle/Campaigns/%s. An existing campaign with that ID will be replaced. Changing the ID saves a copy and keeps the old campaign."),
                    puzzleCampaignDraft.name.c_str(), puzzleCampaignDraft.id.c_str());
                if (ImGui::Button(tr("Save campaign")))
                {
                    const auto saved = astro::runtime::PuzzleCampaignFile::write(activeProjectRoot, puzzleCampaignDraft);
                    if (!saved) puzzleCampaignAuthoringMessage = saved.error().message;
                    else
                    {
                        puzzleCampaignSavedBaseline = puzzleCampaignDraft;
                        selectedPuzzleCampaignCatalogId = puzzleCampaignDraft.id;
                        puzzleCampaignIdInput.fill('\0');
                        std::copy_n(puzzleCampaignDraft.id.data(), std::min(puzzleCampaignDraft.id.size(), puzzleCampaignIdInput.size() - 1),
                            puzzleCampaignIdInput.data());
                        puzzleCampaignAuthoringMessage = tr("Saved campaign: ") + saved.value().filename().string();
                        const auto refreshed = refreshPuzzleCampaignCatalog();
                        if (!refreshed) puzzleCampaignAuthoringMessage = refreshed.error().message;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::End();
        };
        auto puzzleLevelDraftIsDirty = [&]() {
            const auto baseline = puzzleLevelSavedBaseline.value_or(defaultPuzzleLevelDefinition());
            return puzzleLevelDraft != baseline;
        };
        auto loadPuzzleLevelIntoDraft = [&](const std::string& id) -> astro::core::Result<void> {
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project before opening a Puzzle level"});
            const auto loaded = astro::runtime::PuzzleLevelFile::read(activeProjectRoot, id);
            if (!loaded) return astro::core::Result<void>::failure(loaded.error());
            copyPuzzleLevelToEditorFields(loaded.value());
            puzzleLevelSavedBaseline = loaded.value();
            selectedPuzzleCatalogId = id;
            puzzleLevelMessage = tr("Loaded level: ") + loaded.value().name;
            return astro::core::Result<void>::success();
        };
        auto writePuzzleLevelDraft = [&]() -> astro::core::Result<void> {
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project before saving a Puzzle level"});
            puzzleLevelDraft.id = puzzleLevelIdInput.data();
            puzzleLevelDraft.name = puzzleLevelNameInput.data();
            if (puzzleLevelSavedBaseline && puzzleLevelDraft.id != puzzleLevelSavedBaseline->id)
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Use Rename level to change the ID of an existing level"});
            const auto saved = puzzleLevelSavedBaseline
                ? astro::runtime::PuzzleLevelFile::write(activeProjectRoot, puzzleLevelDraft)
                : astro::runtime::PuzzleLevelFile::createNew(activeProjectRoot, puzzleLevelDraft);
            if (!saved) return astro::core::Result<void>::failure(saved.error());
            puzzleLevelSavedBaseline = puzzleLevelDraft;
            selectedPuzzleCatalogId = puzzleLevelDraft.id;
            auto refreshed = refreshPuzzleLevelCatalog();
            if (!refreshed) return refreshed;
            puzzleLevelMessage = tr("Saved level: ") + saved.value().filename().string();
            return astro::core::Result<void>::success();
        };
        auto readPuzzleLevelDraft = [&]() -> astro::core::Result<void> {
            if (activeProjectRoot.empty())
                return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                    "Open a project before opening a Puzzle level"});
            return loadPuzzleLevelIntoDraft(puzzleLevelIdInput.data());
        };
        auto resetPuzzleLevelDraft = [&] {
            puzzleLevelDraft = defaultPuzzleLevelDefinition();
            puzzleLevelSavedBaseline.reset();
            puzzleLevelIdInput.fill('\0');
            puzzleLevelNameInput.fill('\0');
            std::copy_n(puzzleLevelDraft.id.data(), puzzleLevelDraft.id.size(), puzzleLevelIdInput.data());
            std::copy_n(puzzleLevelDraft.name.data(), puzzleLevelDraft.name.size(), puzzleLevelNameInput.data());
            puzzleLevelMessage.clear();
            activePuzzleLevelId.reset();
        };
#endif
        auto startNewScene = [&] {
            csharpPlayApproval.revoke();
            pendingCSharpApproval.reset();
            const auto id = nextSceneId++;
            scene = Scene(id, "Scene " + std::to_string(id));
            currentScenePath.clear();
            sceneDirty = false;
            selectedEntity.reset();
            inspectorDraftEntity.reset();
            inspectorDraftScene.reset();
            commands.clearHistory();
        };
        auto saveCurrentScene = [&]() -> bool {
            if (activeProjectRoot.empty())
            {
                reportError(tr("Open a project before saving a scene"));
                return false;
            }
            auto target = currentScenePath;
            if (target.empty())
            {
                target = activeProjectRoot / "Scenes" / ("scene_" + std::to_string(scene.id()) + ".astro-scene");
                std::error_code fsError;
                const auto directory = target.parent_path();
                if (!std::filesystem::exists(directory, fsError) && !fsError)
                    std::filesystem::create_directory(directory, fsError);
                if (fsError)
                {
                    reportError(tr("Cannot create Scenes directory: ") + fsError.message());
                    return false;
                }
                const bool alreadyExists = std::filesystem::exists(target, fsError);
                if (fsError || alreadyExists)
                {
                    reportError(fsError ? "Cannot inspect scene destination: " + fsError.message()
                        : "Scene file already exists. Open it before saving changes.");
                    return false;
                }
            }
            auto saved = astro::scene::saveSceneFile(target, scene);
            if (!saved)
            {
                reportError(saved.error().message);
                return false;
            }
            currentScenePath = target;
            sceneDirty = false;
            message = tr("Scene saved");
            return true;
        };
        auto openSceneFile = [&](const std::filesystem::path& file) -> bool {
            if (sceneDirty)
            {
                reportError(tr("Save the current scene before opening another scene"));
                return false;
            }
            auto loaded = astro::scene::loadSceneFile(file);
            if (!loaded)
            {
                reportError(loaded.error().message);
                return false;
            }
            csharpPlayApproval.revoke();
            pendingCSharpApproval.reset();
            scene = std::move(loaded).value();
            currentScenePath = file;
            sceneDirty = false;
            selectedEntity.reset();
            inspectorDraftEntity.reset();
            inspectorDraftScene.reset();
            commands.clearHistory();
            if (scene.id() >= nextSceneId) nextSceneId = scene.id() + 1;
            message = tr("Scene opened");
            return true;
        };
        auto syncCSharpManifestFields = [&] {
            std::snprintf(csharpManifestSdkInput.data(), csharpManifestSdkInput.size(), "%s", csharpManifestDraft.sdkVersion.c_str());
            std::snprintf(csharpManifestAssemblyInput.data(), csharpManifestAssemblyInput.size(), "%s", csharpManifestDraft.assemblyName.c_str());
            csharpManifestTypeInputs.clear();
            for (const auto& binding : csharpManifestDraft.scriptBindings)
            {
                std::array<char, 513> text{};
                std::snprintf(text.data(), text.size(), "%s", binding.managedTypeName.c_str());
                csharpManifestTypeInputs.push_back(text);
            }
            if (!csharpManifestSourcePaths.empty())
            {
                const auto selected = std::find(csharpManifestSourcePaths.begin(), csharpManifestSourcePaths.end(), csharpManifestNewSource);
                if (selected == csharpManifestSourcePaths.end()) csharpManifestNewSource = csharpManifestSourcePaths.front();
            }
        };
        auto refreshCSharpManifestSources = [&](const std::filesystem::path& projectRoot) {
            csharpManifestSourcePaths.clear();
            std::error_code ec;
            const auto scriptsRoot = projectRoot / L"Scripts";
            if (!std::filesystem::is_directory(scriptsRoot, ec) || ec) return;
            for (std::filesystem::recursive_directory_iterator it(scriptsRoot,
                    std::filesystem::directory_options::none, ec), end; !ec && it != end; it.increment(ec))
            {
                if (it->is_symlink(ec) || it->is_directory(ec) && GetFileAttributesW(it->path().c_str()) != INVALID_FILE_ATTRIBUTES
                    && (GetFileAttributesW(it->path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
                if (it->is_regular_file(ec) && it->path().extension() == L".cs")
                    csharpManifestSourcePaths.push_back("Scripts/" + pathUtf8(it->path().lexically_relative(scriptsRoot)));
            }
            std::sort(csharpManifestSourcePaths.begin(), csharpManifestSourcePaths.end());
            if (!csharpManifestSourcePaths.empty()
                && std::find(csharpManifestSourcePaths.begin(), csharpManifestSourcePaths.end(), csharpManifestNewSource)
                    == csharpManifestSourcePaths.end()) csharpManifestNewSource = csharpManifestSourcePaths.front();
        };
        auto loadCSharpManifest = [&](const std::filesystem::path& projectRoot) {
            refreshCSharpManifestSources(projectRoot);
            const auto read = astro::editor::CSharpBuildManifestFile::read(projectRoot);
            csharpManifestExists = false;
            csharpManifestInvalid = false;
            csharpManifestMessage.clear();
            std::error_code ec;
            csharpManifestExists = std::filesystem::exists(
                astro::editor::CSharpBuildManifestFile::manifestPath(projectRoot), ec) && !ec;
            if (read) csharpManifestDraft = read.value();
            else if (read.error().code == astro::core::ErrorCode::NotFound)
            {
                csharpManifestDraft = {};
                csharpManifestDraft.sdkVersion = detectConfiguredCSharpSdk();
                csharpManifestMessage = tr("No C# build manifest yet. Save settings to create one.");
            }
            else
            {
                csharpManifestDraft = {};
                csharpManifestDraft.sdkVersion = detectConfiguredCSharpSdk();
                csharpManifestInvalid = true;
                csharpManifestMessage = read.error().message;
            }
            csharpManifestSaved = csharpManifestDraft;
            syncCSharpManifestFields();
        };
        auto saveCSharpManifest = [&]() -> bool {
            csharpManifestDraft.sdkVersion = csharpManifestSdkInput.data();
            csharpManifestDraft.targetFramework = "net10.0";
            csharpManifestDraft.assemblyName = csharpManifestAssemblyInput.data();
            for (std::size_t i = 0; i < csharpManifestDraft.scriptBindings.size() && i < csharpManifestTypeInputs.size(); ++i)
                csharpManifestDraft.scriptBindings[i].managedTypeName = csharpManifestTypeInputs[i].data();
            const auto saved = astro::editor::CSharpBuildManifestFile::write(activeProjectRoot, csharpManifestDraft);
            if (!saved) { csharpManifestMessage = saved.error().message; return false; }
            csharpManifestExists = true;
            csharpManifestInvalid = false;
            csharpManifestSaved = csharpManifestDraft;
            csharpManifestMessage = tr("C# build settings saved");
            if (assetRegistry) csharpBuildReport = assetRegistry->inspectScriptBuild();
            return true;
        };
        auto openProject = [&](const std::filesystem::path& file) {
            if (csharpBuildRunning) throw std::runtime_error("Wait for the C# build to finish before switching projects");
            if (sceneDirty) throw std::runtime_error("Save the current scene before opening another project");
            if (csharpCodeWorkspace.dirty()) throw std::runtime_error("Save or discard the open C# script before switching projects");
            auto result = astro::editor::readProject(file);
            if (!result) throw std::runtime_error(result.error().message);
            auto projectRoot = file.parent_path();
            if (projectRoot.empty()) projectRoot = std::filesystem::current_path();
            auto registry = astro::assets::AssetRegistry::open(projectRoot);
            if (!registry) throw std::runtime_error(registry.error().message);
            stopPlaySession();
            csharpPlayApproval.revoke();
            pendingCSharpApproval.reset();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
            gameViewPuzzle.reset();
            gameViewPuzzleCampaign.reset();
            puzzleCampaignMessage.clear();
            puzzleCampaignCatalog.clear();
            puzzleCampaignCatalogProjectRoot.clear();
            selectedPuzzleCampaignCatalogId.clear();
            resetPuzzleLevelDraft();
            selectedPuzzleCell.reset();
            pendingPuzzleSwap.reset();
            lastPuzzleMove.reset();
#endif
            activeProject = result.value().name;
            activeProjectRoot = projectRoot;
            loadCSharpManifest(projectRoot);
            csharpCodeWorkspace = astro::editor::CSharpCodeWorkspace{};
            csharpCodeDiagnostics.clear();
            csharpMetadataValidationOutput.clear();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            loadPuzzleProjectSettings(projectRoot);
#endif
            for (const auto& [id, texture] : spriteTextures)
                if (bgfx::isValid(texture)) bgfx::destroy(texture);
            spriteTextures.clear();
            assetRegistry = std::move(registry).value();
            csharpBuildReport = assetRegistry->inspectScriptBuild();
            startNewScene();
            message = tr("Project opened: ") + result.value().name;
        };
        auto createProject = [&](const std::filesystem::path& directory, const std::string& title) {
            if (csharpBuildRunning) throw std::runtime_error("Wait for the C# build to finish before switching projects");
            if (sceneDirty) throw std::runtime_error("Save the current scene before creating another project");
            if (csharpCodeWorkspace.dirty()) throw std::runtime_error("Save or discard the open C# script before switching projects");
            auto result = astro::editor::createProject(directory, title);
            if (!result) throw std::runtime_error(result.error().message);
            auto registry = astro::assets::AssetRegistry::open(directory);
            if (!registry) throw std::runtime_error(registry.error().message);
            stopPlaySession();
            csharpPlayApproval.revoke();
            pendingCSharpApproval.reset();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
            gameViewPuzzle.reset();
            gameViewPuzzleCampaign.reset();
            puzzleCampaignMessage.clear();
            puzzleCampaignCatalog.clear();
            puzzleCampaignCatalogProjectRoot.clear();
            selectedPuzzleCampaignCatalogId.clear();
            resetPuzzleLevelDraft();
            selectedPuzzleCell.reset();
            pendingPuzzleSwap.reset();
            lastPuzzleMove.reset();
#endif
            activeProject = result.value().name;
            activeProjectRoot = directory;
            loadCSharpManifest(directory);
            csharpCodeWorkspace = astro::editor::CSharpCodeWorkspace{};
            csharpCodeDiagnostics.clear();
            csharpMetadataValidationOutput.clear();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            loadPuzzleProjectSettings(directory);
#endif
            for (const auto& [id, texture] : spriteTextures)
                if (bgfx::isValid(texture)) bgfx::destroy(texture);
            spriteTextures.clear();
            assetRegistry = std::move(registry).value();
            csharpBuildReport = assetRegistry->inspectScriptBuild();
            startNewScene();
            message = tr("Project created: ") + result.value().name;
        };
        auto undoSceneEdit = [&] {
            if (!commands.canUndo()) { message = tr("Nothing to undo"); return false; }
            auto result = commands.undo();
            if (!result) { reportError(result.error().message); return false; }
            sceneDirty = true;
            message = tr("Undo completed");
            ++undoShortcuts;
            return true;
        };
        auto redoSceneEdit = [&] {
            if (!commands.canRedo()) { message = tr("Nothing to redo"); return false; }
            auto result = commands.redo();
            if (!result) { reportError(result.error().message); return false; }
            sceneDirty = true;
            message = tr("Redo completed");
            ++redoShortcuts;
            return true;
        };
        auto queueShortcut = [](const SDL_Keycode key) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            event.key.mod = SDL_KMOD_CTRL;
            event.key.down = true;
            if (!SDL_PushEvent(&event)) return false;
            event.type = SDL_EVENT_KEY_UP;
            event.key.down = false;
            return SDL_PushEvent(&event);
        };
        std::filesystem::path smokeScreenshot;
        std::array<astro::assets::AssetId, 3> smokeSpriteAssets{};
        if (smoke || benchmark)
        {
            const auto started = logicDebugger.start(logicGraph);
            const auto evaluated = started ? logicDebugger.runToEnd()
                : astro::core::Result<void>::failure(started.error());
            const auto* finalTrace = logicGraph.nodes().empty() ? nullptr
                : logicDebugger.trace(logicGraph.nodes().back().id);
            if (!evaluated || !finalTrace || !finalTrace->output
                || !std::holds_alternative<bool>(*finalTrace->output) || !std::get<bool>(*finalTrace->output))
                throw std::runtime_error("Visual Logic editor smoke evaluation failed");
            smokeDirectory = std::filesystem::temp_directory_path() /
                ("AstroEditor-" + std::to_string(SDL_GetTicksNS()));
            createProject(smokeDirectory, "Smoke project Ω");
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (smoke)
            {
                const auto defaultSettings = astro::editor::defaultPuzzleSaveProjectSettings(smokeDirectory);
                const auto missingSettings = astro::editor::readPuzzleSaveProjectSettings(smokeDirectory);
                const auto otherProjectSettings = astro::editor::defaultPuzzleSaveProjectSettings(smokeDirectory / "Other");
                if (!missingSettings || missingSettings.value() != defaultSettings
                    || otherProjectSettings.userDataSubdirectory == defaultSettings.userDataSubdirectory)
                    throw std::runtime_error("Puzzle project settings default path is not isolated per project");
                const astro::editor::PuzzleSaveProjectSettings customSettings{
                    "smoke-puzzle-profile", 7, "Saves/Projects/Smoke Ω/Puzzle"};
                if (!astro::editor::writePuzzleSaveProjectSettings(smokeDirectory, customSettings))
                    throw std::runtime_error("Puzzle project settings smoke could not write valid settings");
                const auto restoredSettings = astro::editor::readPuzzleSaveProjectSettings(smokeDirectory);
                auto unsafeSettings = customSettings;
                unsafeSettings.userDataSubdirectory = "../outside";
                if (!restoredSettings || restoredSettings.value() != customSettings
                    || astro::editor::writePuzzleSaveProjectSettings(smokeDirectory, unsafeSettings))
                    throw std::runtime_error("Puzzle project settings smoke accepted invalid path or lost valid data");
                const auto afterRejectedWrite = astro::editor::readPuzzleSaveProjectSettings(smokeDirectory);
                if (!afterRejectedWrite || afterRejectedWrite.value() != customSettings)
                    throw std::runtime_error("Invalid Puzzle path write changed existing project settings");

                const auto userDataRoot = smokeDirectory / "user-data";
                const auto originalCampaignPath = astro::editor::resolvePuzzleCampaignSaveDirectory(
                    userDataRoot, customSettings, "smoke-campaign");
                const auto renamedCampaignPath = astro::editor::resolvePuzzleCampaignSaveDirectory(
                    userDataRoot, customSettings, "renamed-campaign");
                if (!originalCampaignPath || !renamedCampaignPath
                    || originalCampaignPath.value() == renamedCampaignPath.value()
                    || originalCampaignPath.value().filename() != L"smoke-campaign")
                    throw std::runtime_error("Campaign ID change did not select a separate save namespace");
                if (astro::editor::resolvePuzzleCampaignSaveDirectory(userDataRoot, customSettings, "../outside"))
                    throw std::runtime_error("Campaign save path accepted an unsafe campaign ID");

                const auto relocatedProject = smokeDirectory / "Relocated Ω";
                std::error_code relocationError;
                std::filesystem::create_directories(relocatedProject / L".astro", relocationError);
                if (relocationError)
                    throw std::runtime_error("Could not create isolated relocated-project settings folder");
                std::filesystem::copy_file(smokeDirectory / L".astro" / L"puzzle-save.settings",
                    relocatedProject / L".astro" / L"puzzle-save.settings", relocationError);
                if (relocationError)
                    throw std::runtime_error("Could not copy settings into isolated relocation fixture");
                const auto relocatedSettings = astro::editor::readPuzzleSaveProjectSettings(relocatedProject);
                const auto relocatedCampaignPath = relocatedSettings
                    ? astro::editor::resolvePuzzleCampaignSaveDirectory(userDataRoot,
                        relocatedSettings.value(), "smoke-campaign")
                    : astro::core::Result<std::filesystem::path>::failure(relocatedSettings.error());
                if (!relocatedCampaignPath || relocatedCampaignPath.value() != originalCampaignPath.value())
                    throw std::runtime_error("Relocated/copy project settings did not preserve their save namespace");
                std::filesystem::remove_all(relocatedProject, relocationError);
                if (relocationError)
                    throw std::runtime_error("Could not clean the isolated relocation fixture");
            }
#endif
            activeProject.clear();
            openProject(smokeDirectory / "project.astro");
            if (smoke)
            {
                const std::filesystem::path smokeScriptRelative = "Code workspace smoke.cs";
                const auto created = csharpCodeWorkspace.create(activeProjectRoot, smokeScriptRelative);
                if (!created) throw std::runtime_error("C# code workspace smoke could not create UTF-8 source: " + created.error().message);
                csharpCodeWorkspace.setText("// Русский текст Ω\npublic sealed class SmokeScript {}\n");
                if (!csharpCodeWorkspace.dirty() || !csharpCodeWorkspace.save() || csharpCodeWorkspace.dirty())
                    throw std::runtime_error("C# code workspace smoke did not save and clear dirty state");
                astro::editor::CSharpCodeWorkspace reopenedScript;
                if (!reopenedScript.open(activeProjectRoot, smokeScriptRelative)
                    || reopenedScript.text().find("Русский текст Ω") == std::string::npos)
                    throw std::runtime_error("C# code workspace smoke failed UTF-8 reopen");
                csharpCodeWorkspace = std::move(reopenedScript);
                astro::editor::CSharpBuildManifest manifestSmoke;
                manifestSmoke.sdkVersion = detectConfiguredCSharpSdk();
                manifestSmoke.scriptBindings.push_back({"Scripts/Code workspace smoke.cs", "Smoke.Script"});
                const auto manifestSaved = astro::editor::CSharpBuildManifestFile::write(activeProjectRoot, manifestSmoke);
                if (manifestSmoke.sdkVersion.empty() || !manifestSaved)
                    throw std::runtime_error("C# manifest smoke could not create a valid project configuration");
                loadCSharpManifest(activeProjectRoot);
                if (csharpManifestInvalid || !csharpManifestExists || csharpManifestDraft != manifestSmoke)
                    throw std::runtime_error("C# manifest smoke failed to reload the configured SDK and binding");
                const auto encodedScriptPath = pathUtf8(smokeScriptRelative);
                std::snprintf(csharpCodePathInput.data(), csharpCodePathInput.size(), "%s", encodedScriptPath.c_str());
#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
                csharpMetadataValidationOutput = makeSyntheticMetadataReportUiFixture();
                csharpMetadataValidationTypeCount = 256;
                csharpMetadataValidationBatchCount = 4;
#endif
            }
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
            if (smoke)
            {
                auto browserLevel = defaultPuzzleLevelDefinition();
                browserLevel.id = "smoke-browser-level";
                const auto savedBrowserLevel = astro::runtime::PuzzleLevelFile::write(smokeDirectory, browserLevel);
                astro::runtime::PuzzleCampaignDefinition browserCampaign{
                    "smoke-browser", "Campaign Browser Ω", browserLevel.id, {browserLevel.id},
                    {{browserLevel.id, astro::runtime::PuzzleCampaignRuleKind::Default, 0, std::nullopt}}};
                const auto savedBrowserCampaign = astro::runtime::PuzzleCampaignFile::write(smokeDirectory, browserCampaign);
                auto brokenBrowserCampaign = browserCampaign;
                brokenBrowserCampaign.id = "smoke-browser-broken";
                brokenBrowserCampaign.name = "Broken Browser Link";
                brokenBrowserCampaign.startLevelId = "smoke-browser-absent";
                brokenBrowserCampaign.orderedLevelIds = {"smoke-browser-absent"};
                brokenBrowserCampaign.transitions = {{"smoke-browser-absent",
                    astro::runtime::PuzzleCampaignRuleKind::Default, 0, std::nullopt}};
                const auto brokenBrowserText = astro::runtime::serializePuzzleCampaign(brokenBrowserCampaign);
                if (!savedBrowserLevel || !savedBrowserCampaign || !brokenBrowserText)
                    throw std::runtime_error("Puzzle campaign browser smoke fixtures could not be authored");
                const auto brokenBrowserPath = astro::runtime::PuzzleCampaignFile::campaignsDirectory(smokeDirectory)
                    / "smoke-browser-broken.astro-puzzle-campaign";
                { std::ofstream output(brokenBrowserPath, std::ios::binary); output << brokenBrowserText.value(); }
                selectedPuzzleCampaignCatalogId = brokenBrowserCampaign.id;
            }
#endif
            std::filesystem::create_directories(smokeDirectory / "Assets" / "Sprites");
            const std::array<std::array<std::uint8_t, 4>, 3> smokeColors{{
                {{255, 0, 0, 255}}, {{0, 255, 0, 128}}, {{0, 0, 255, 255}}}};
            constexpr std::array<const char*, 3> smokeNames{{"smoke-red.png", "smoke-green-alpha.png", "smoke-blue.png"}};
            for (std::size_t index = 0; index < smokeNames.size(); ++index)
            {
                const auto png = makeSmokePng(smokeColors[index], benchmark ? 16 : 32, benchmark ? 16 : 32);
                const auto source = smokeDirectory / "Assets" / "Sprites" / smokeNames[index];
                { std::ofstream imageFile(source, std::ios::binary);
                  imageFile.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())); }
                auto imported = assetRegistry->importImage(std::filesystem::path("Sprites") / smokeNames[index]);
                if (!imported) throw std::runtime_error("Smoke image import failed: " + imported.error().message);
                smokeSpriteAssets[index] = imported.value().id;
                const auto texture = loadSpriteTexture(assetRegistry->cachedPath(imported.value().id));
                if (!bgfx::isValid(texture)) throw std::runtime_error("Smoke image WIC decode/BGFX upload failed");
                spriteTextures[imported.value().id.value] = texture;
                benchmarkSpriteAssets.push_back(imported.value().id);
            }
            smokeSpriteAsset = smokeSpriteAssets[0];
            if (benchmark)
            {
                const std::array<std::array<std::uint8_t, 4>, 5> palette{{
                    {{92, 128, 68, 255}}, {{126, 101, 74, 255}}, {{138, 145, 154, 255}},
                    {{194, 163, 92, 255}}, {{205, 220, 192, 156}}}};
                constexpr std::array<const char*, 5> names{{
                    "bench-grass.png", "bench-earth.png", "bench-stone.png", "bench-sand.png", "bench-fog-alpha.png"}};
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    const auto png = makeSmokePng(palette[index], 16, 16);
                    const auto source = smokeDirectory / "Assets" / "Sprites" / names[index];
                    { std::ofstream imageFile(source, std::ios::binary);
                      imageFile.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())); }
                    auto imported = assetRegistry->importImage(std::filesystem::path("Sprites") / names[index]);
                    if (!imported) throw std::runtime_error("Benchmark image import failed: " + imported.error().message);
                    const auto texture = loadSpriteTexture(assetRegistry->cachedPath(imported.value().id));
                    if (!bgfx::isValid(texture)) throw std::runtime_error("Benchmark WIC decode/BGFX upload failed");
                    spriteTextures[imported.value().id.value] = texture;
                    benchmarkSpriteAssets.push_back(imported.value().id);
                }
            }
            smokeScreenshot = smokeDirectory / "scene-game-visual.tga";
            const auto occupied = smokeDirectory / "occupied";
            std::filesystem::create_directory(occupied);
            { std::ofstream sentinel(occupied / "keep.txt", std::ios::binary); sentinel << "preserve"; }
            bool rejected = false;
            try { createProject(occupied, "Do not overwrite"); }
            catch (const std::exception&) { rejected = true; }
            std::ifstream sentinel(occupied / "keep.txt", std::ios::binary);
            const std::string sentinelContents((std::istreambuf_iterator<char>(sentinel)), {});
            if (!rejected || sentinelContents != "preserve" || activeProject != "Smoke project Ω")
                throw std::runtime_error("Project collision smoke failed");
            const auto invalidFile = smokeDirectory / "invalid.astro";
            { std::ofstream invalid(invalidFile, std::ios::binary); invalid << "ASTRO_PROJECT 999\n\\\"Invalid\\\"\n"; }
            rejected = false;
            try { openProject(invalidFile); }
            catch (const std::exception&) { rejected = true; }
            if (!rejected || activeProject != "Smoke project Ω")
                throw std::runtime_error("Invalid project must preserve the active project");
            const auto root = scene.createEntity("Smoke Root Ω");
            const auto child = root ? scene.createEntity("Smoke Child", root.value())
                : astro::core::Result<EntityId>::failure({astro::core::ErrorCode::InternalError, "Root creation failed"});
            const auto blueEntity = root ? scene.createEntity("Smoke Blue", root.value())
                : astro::core::Result<EntityId>::failure({astro::core::ErrorCode::InternalError, "Root creation failed"});
            if (!root || !child || !blueEntity || scene.entities().size() != 3 || scene.find(child.value())->parent != root.value())
                throw std::runtime_error("Scene model smoke failed");
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
            if (smoke)
            {
                std::filesystem::create_directories(activeProjectRoot / L"Assets" / L"Models");
                const auto fixture = std::filesystem::path(ASTRO_EDITOR_RENDER3D_FIXTURE_DIR)
                    / L"Assets" / L"Models" / L"triangle.gltf";
                std::filesystem::copy_file(fixture, activeProjectRoot / L"Assets" / L"Models" / L"triangle.gltf",
                    std::filesystem::copy_options::overwrite_existing);
                auto imported = astro::render3d::GltfImporter::importAndRegister(*assetRegistry,
                    std::filesystem::path(std::u8string(u8"Assets/Models/triangle.gltf")));
                if (!imported)
                    throw std::runtime_error("Editor Scene View 3D smoke import failed: " + imported.error().message);
                astro::scene::Transform parentTransform;
                parentTransform.position.x = 0.25;
                parentTransform.scale.x = 2.0;
                astro::scene::Transform childTransform;
                childTransform.position.x = 0.5;
                if (!scene.setTransform(root.value(), parentTransform)
                    || !scene.setTransform(child.value(), childTransform))
                    throw std::runtime_error("Editor Scene View 3D smoke could not configure hierarchy transforms");
                const auto attached = scene.setMeshRenderer(child.value(), imported.value().modelAssetId);
                if (!attached)
                    throw std::runtime_error("Editor Scene View 3D smoke could not attach MeshRenderer: "
                        + attached.error().message);
                const auto secondAttached = scene.setMeshRenderer(blueEntity.value(), imported.value().modelAssetId);
                if (!secondAttached)
                    throw std::runtime_error("Editor Scene View 3D smoke could not attach the second MeshRenderer: "
                        + secondAttached.error().message);
                const auto unresolvedAttached = scene.setMeshRenderer(root.value(), {900000001ull});
                if (!unresolvedAttached)
                    throw std::runtime_error("Editor Scene View 3D smoke could not configure an unresolved AssetId diagnostic");
                sceneTarget.requestFrameAll();
            }
#endif
            selectedEntity = root.value();
            showTimeline = true;
            showSaveSystem = true;
            showCameraPresets = true;
            if (!timeline.seek(0.5) || timeline.sample() != 5.0
                || !cameraRig.setMode(astro::runtime::CameraMode::ThirdPerson))
                throw std::runtime_error("Timeline/camera preset smoke setup failed");
            auto cameraSettings = cameraRig.settings();
            cameraSettings.target = root.value();
            if (!cameraRig.configure(cameraSettings) || !cameraRig.snapToTarget(scene)
                || cameraRig.pose().position.y != 2.0 || cameraRig.pose().position.z != -5.0)
                throw std::runtime_error("Camera preset target/pose smoke failed");
            showUiEditor = true;
            const auto uiTitle = uiDocument.addElement(astro::ui::ElementKind::Text, "Player label");
            if (!uiTitle) throw std::runtime_error("UI editor smoke element creation failed");
            selectedUiElement = uiTitle.value();
            astro::ui::ResponsiveRect uiResponsive;
            uiResponsive.anchorMinX = 0.05;
            uiResponsive.anchorMinY = 0.85;
            uiResponsive.anchorMaxX = 0.95;
            uiResponsive.anchorMaxY = 0.95;
            uiResponsive.offsetMaxX = -8.0;
            uiResponsive.offsetMaxY = -8.0;
            if (!uiDocument.setLayout(uiTitle.value(), uiResponsive))
                throw std::runtime_error("UI responsive layout setup failed");
            const auto uiLayout = uiDocument.resolveLayout(uiTitle.value(), 1280.0, 720.0);
            const auto uiPhoneLayout = uiDocument.resolveLayout(uiTitle.value(), 390.0, 844.0);
            if (!uiLayout || !uiPhoneLayout || uiLayout.value().width == uiPhoneLayout.value().width)
                throw std::runtime_error("UI responsive layout smoke failed");
            if (!uiDocument.setBinding(uiTitle.value(), astro::ui::UiBinding{root.value(),
                astro::ui::BindingSource::EntityName, astro::ui::BindingTarget::Text}, scene)
                || !uiDocument.refreshBindings(scene) || uiDocument.find(uiTitle.value())->text != "Smoke Root Ω")
                throw std::runtime_error("UI scene binding smoke failed");
            const astro::scene::SetScenePropertyPayload editName{root.value(), "name", std::string{"Edited Root Ω"}};
            if (!commands.validate("entity.set_property", 1, editName)
                || !commands.execute<astro::scene::SetScenePropertyPayload, void>("entity.set_property", 1, editName)
                || scene.find(root.value())->name != "Edited Root Ω")
                throw std::runtime_error("Inspector command smoke failed");
            const astro::scene::SetSpriteRendererPayload setSprite{root.value(), smokeSpriteAssets[0], true};
            if (!commands.execute<astro::scene::SetSpriteRendererPayload, void>("entity.set_sprite_renderer", 1, setSprite)
                || !scene.spriteRenderer(root.value()))
                throw std::runtime_error("SpriteRenderer command smoke failed");
            const astro::scene::AddScriptBehaviorPayload addSmokeScript{
                root.value(), {99001}, "Smoke.UnregisteredScript"};
            const auto smokeScript = commands.execute<astro::scene::AddScriptBehaviorPayload, void>(
                "script_behavior.add", 1, addSmokeScript);
            if (!smokeScript || scene.scriptBehaviors(root.value()).size() != 1
                || scene.scriptBehaviors(root.value()).front().enabled)
                throw std::runtime_error("ScriptBehavior authoring smoke failed to add a disabled binding");
            if (!scene.setSpriteRenderer(child.value(), smokeSpriteAssets[1])
                || !scene.setSpriteRenderer(blueEntity.value(), smokeSpriteAssets[2]))
                throw std::runtime_error("Multi-texture visual regression scene setup failed");
            astro::scene::Transform greenTransform;
            greenTransform.position.x = 1.25;
            astro::scene::Transform blueTransform;
            blueTransform.position.x = 2.5;
            if (!scene.setTransform(child.value(), greenTransform) || !scene.setTransform(blueEntity.value(), blueTransform))
                throw std::runtime_error("Visual regression sprite positioning failed");
            if (!saveSystemDocument.createGroup("smoke-actors")
                || !saveSystemDocument.setGroupMember("smoke-actors", root.value(), true)
                || !saveSystemDocument.createProfile("smoke-progress")
                || !saveSystemDocument.setGroupField("smoke-progress", "smoke-actors",
                    std::string(astro::runtime::SaveProperty::entityName), true))
                throw std::runtime_error("Save System profile/group smoke setup failed");
            astro::runtime::CheckpointDefinition smokeCheckpoint;
            smokeCheckpoint.entity = child.value();
            smokeCheckpoint.area = {2.0, 2.0};
            smokeCheckpoint.respawnPosition = {3.0, 4.0, 0.0};
            smokeCheckpoint.mode = astro::runtime::CheckpointMode::SaveSelectedProfile;
            smokeCheckpoint.saveProfileId = "smoke-progress";
            if (!saveSystemDocument.setCheckpoint(smokeCheckpoint))
                throw std::runtime_error("Save System checkpoint smoke setup failed");
            const auto restorePreview = testCheckpointRestore(saveSystemDocument, scene, child.value());
            if (!restorePreview)
                throw std::runtime_error("Save System restore preview failed: " + restorePreview.error().message);
            sceneDirty = true;
            if (!saveCurrentScene() || !queueShortcut(SDLK_Z) || !queueShortcut(SDLK_Y)
                || !queueShortcut(SDLK_S) || !queueShortcut(SDLK_O))
                throw std::runtime_error("Scene shortcut smoke setup failed");
            {
                const auto playStarted = playSession.play(scene,
                    [](Scene&, double) { return astro::core::Result<void>::success(); });
                if (!playStarted || !playSession.runtimeScene())
                    throw std::runtime_error("Game View runtime copy smoke failed");
            }
        }
        if (benchmark && !benchmark3d)
        {
            constexpr int viewportWidth = 960, viewportHeight = 540;
            for (const int spriteCount : {1, 100, 1000, 5000, 10000})
            {
                const int warmup = spriteCount <= 1000 ? 8 : 4;
                const int measured = spriteCount <= 1000 ? 40 : 12;
                Scene benchmarkScene(700 + static_cast<std::uint64_t>(spriteCount),
                    "Renderer benchmark " + std::to_string(spriteCount));
                for (int index = 0; index < spriteCount; ++index)
                {
                    const auto entity = benchmarkScene.createEntity("Sprite " + std::to_string(index));
                    if (!entity || !benchmarkScene.setSpriteRenderer(entity.value(), *smokeSpriteAsset))
                        throw std::runtime_error("Could not construct renderer benchmark scene");
                    astro::scene::Transform transform;
                    transform.position = {double(index % 40) * 1.5, double(index / 40) * 1.5, 0.0};
                    if (!benchmarkScene.setTransform(entity.value(), transform))
                        throw std::runtime_error("Could not position renderer benchmark sprite");
                }
                std::vector<double> scenePrepare, sceneSoftware, sceneCompose, sceneBounds, sceneGeometry, sceneTransient;
                std::vector<double> gamePrepare, gameSoftware, gameCompose, gameBounds, gameGeometry, gameTransient;
                std::vector<double> gpuFrameMs;
                std::uint32_t lastGpuFrame = std::numeric_limits<std::uint32_t>::max();
                for (int frame = 0; frame < warmup + measured; ++frame)
                {
                    bgfx::TextureHandle sceneTexture = BGFX_INVALID_HANDLE, gameTexture = BGFX_INVALID_HANDLE;
                    std::string sceneError, gameError;
                    SceneViewTarget::Timings sceneTiming, gameTiming;
                    if (!sceneTarget.render(benchmarkScene, &*assetRegistry, spriteTextures, viewportWidth,
                            viewportHeight, program, sampler, outputMode, layout, sceneTexture, sceneError, &sceneTiming)
                        || !gameTarget.render(benchmarkScene, &*assetRegistry, spriteTextures, viewportWidth,
                            viewportHeight, program, sampler, outputMode, layout, gameTexture, gameError, &gameTiming))
                        throw std::runtime_error("Renderer benchmark failed: " + (sceneError.empty() ? gameError : sceneError));
                    bgfx::frame();
                    if (frame < warmup) continue;
                    scenePrepare.push_back(sceneTiming.prepareMs);
                    sceneSoftware.push_back(sceneTiming.submitMs);
                    gamePrepare.push_back(gameTiming.prepareMs);
                    gameSoftware.push_back(gameTiming.submitMs);
                    sceneCompose.push_back(sceneTiming.composeMs);
                    sceneBounds.push_back(sceneTiming.boundsMs);
                    sceneGeometry.push_back(sceneTiming.geometryMs);
                    sceneTransient.push_back(sceneTiming.transientCopyMs);
                    gameCompose.push_back(gameTiming.composeMs);
                    gameBounds.push_back(gameTiming.boundsMs);
                    gameGeometry.push_back(gameTiming.geometryMs);
                    gameTransient.push_back(gameTiming.transientCopyMs);
                    const auto* stats = bgfx::getStats();
                    if (stats && stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin
                        && stats->gpuFrameNum != lastGpuFrame)
                    {
                        gpuFrameMs.push_back(double(stats->gpuTimeEnd - stats->gpuTimeBegin)
                            * 1000.0 / double(stats->gpuTimerFreq));
                        lastGpuFrame = stats->gpuFrameNum;
                    }
                }
                const auto percentile = [](std::vector<double> values, const double fraction) {
                    std::sort(values.begin(), values.end());
                    return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
                };
                const auto report = [&](const char* view, const auto& prepare, const auto& submit,
                    const auto& compose, const auto& bounds, const auto& geometry, const auto& transient) {
                    std::vector<double> totals;
                    for (std::size_t index = 0; index < prepare.size(); ++index)
                        totals.push_back(prepare[index] + submit[index]);
                    const double median = percentile(totals, 0.50);
                    std::cout << "[renderer-benchmark] view=" << view << " sprites=" << spriteCount
                        << " viewport=" << viewportWidth << 'x' << viewportHeight
                        << " prepare_p50_ms=" << percentile(prepare, 0.50)
                        << " bgfx_submit_p50_ms=" << percentile(submit, 0.50)
                        << " compose_p50_ms=" << percentile(compose, 0.50)
                        << " camera_bounds_p50_ms=" << percentile(bounds, 0.50)
                        << " vertex_geometry_p50_ms=" << percentile(geometry, 0.50)
                        << " transient_alloc_copy_p50_ms=" << percentile(transient, 0.50)
                        << " total_p50_ms=" << median << " total_p95_ms=" << percentile(totals, 0.95)
                        << " cpu_frame_budget_pct=" << (median / (1000.0 / 60.0) * 100.0) << '\n';
                };
                report("Scene", scenePrepare, sceneSoftware, sceneCompose, sceneBounds, sceneGeometry, sceneTransient);
                report("Game", gamePrepare, gameSoftware, gameCompose, gameBounds, gameGeometry, gameTransient);
                if (!gpuFrameMs.empty())
                    std::cout << "[renderer-benchmark] paired_gpu_frame_p50_ms=" << percentile(gpuFrameMs, 0.50)
                        << " paired_gpu_frame_p95_ms=" << percentile(gpuFrameMs, 0.95)
                        << " gpu_samples=" << gpuFrameMs.size() << '\n';
                else std::cout << "[renderer-benchmark] paired_gpu_frame_ms=unavailable\n";
            }
            const auto makeRepresentativeScene = [&](const std::uint64_t sceneId, const std::string& name,
                const int columns, const int rows, const int chunkWidth, const int chunkHeight,
                const int overlayCount, const bool useHotspots) {
                Scene representative(sceneId, name);
                const int chunkColumns = (columns + chunkWidth - 1) / chunkWidth;
                const int chunkRows = (rows + chunkHeight - 1) / chunkHeight;
                std::vector<EntityId> chunks;
                chunks.reserve(static_cast<std::size_t>(chunkColumns * chunkRows));
                constexpr double cell = 0.18;
                for (int chunkY = 0; chunkY < chunkRows; ++chunkY)
                    for (int chunkX = 0; chunkX < chunkColumns; ++chunkX)
                    {
                        const auto root = representative.createEntity("Chunk " + std::to_string(chunkX)
                            + "," + std::to_string(chunkY));
                        if (!root) throw std::runtime_error("Could not create benchmark scene chunk");
                        astro::scene::Transform rootTransform;
                        rootTransform.position = {chunkX * chunkWidth * cell, chunkY * chunkHeight * cell, 0.0};
                        if (!representative.setTransform(root.value(), rootTransform))
                            throw std::runtime_error("Could not position benchmark scene chunk");
                        chunks.push_back(root.value());
                    }
                for (int y = 0; y < rows; ++y)
                    for (int x = 0; x < columns; ++x)
                    {
                        const int chunkX = x / chunkWidth, chunkY = y / chunkHeight;
                        const auto parent = chunks[static_cast<std::size_t>(chunkY * chunkColumns + chunkX)];
                        const auto entity = representative.createEntity("Tile " + std::to_string(x)
                            + "," + std::to_string(y), parent);
                        const auto paletteIndex = static_cast<std::size_t>((x * 5 + y * 3 + (x / 4))
                            % static_cast<int>(benchmarkSpriteAssets.size()));
                        if (!entity || !representative.setSpriteRenderer(entity.value(), benchmarkSpriteAssets[paletteIndex]))
                            throw std::runtime_error("Could not create benchmark tile sprite");
                        astro::scene::Transform transform;
                        transform.position = {(x % chunkWidth) * cell, (y % chunkHeight) * cell, 0.0};
                        transform.rotationDegrees.z = ((x + 2 * y) % 4) * 90.0;
                        const double scale = 0.9 + double((x + y) % 3) * 0.05;
                        transform.scale = {scale, scale, 1.0};
                        if (!representative.setTransform(entity.value(), transform))
                            throw std::runtime_error("Could not transform benchmark tile sprite");
                    }
                for (int index = 0; index < overlayCount; ++index)
                {
                    const int hotX = useHotspots ? (index % 8) * std::max(1, columns / 8)
                        : (index * 7) % columns;
                    const int hotY = useHotspots ? (index % 6) * std::max(1, rows / 6)
                        : (index * 11) % rows;
                    const int chunkX = hotX / chunkWidth, chunkY = hotY / chunkHeight;
                    const auto parent = chunks[static_cast<std::size_t>(chunkY * chunkColumns + chunkX)];
                    const auto entity = representative.createEntity("Alpha overlay " + std::to_string(index), parent);
                    const auto texture = benchmarkSpriteAssets[index % 2 == 0 ? 1u : 7u];
                    if (!entity || !representative.setSpriteRenderer(entity.value(), texture))
                        throw std::runtime_error("Could not create benchmark alpha overlay");
                    astro::scene::Transform transform;
                    transform.position = {(hotX % chunkWidth) * cell + double(index % 5) * 0.012,
                        (hotY % chunkHeight) * cell + double(index % 7) * 0.009, 0.0};
                    transform.rotationDegrees.z = double(index % 8) * 45.0;
                    transform.scale = {1.15 + double(index % 4) * 0.15,
                        1.15 + double(index % 4) * 0.15, 1.0};
                    if (!representative.setTransform(entity.value(), transform))
                        throw std::runtime_error("Could not transform benchmark alpha overlay");
                }
                return representative;
            };
            const auto countVisibleSprites = [](const Scene& value) {
                return static_cast<std::size_t>(std::count_if(value.entities().begin(), value.entities().end(),
                    [](const Entity& entity) { return entity.spriteRenderer && entity.spriteRenderer->visible; }));
            };
            const auto measureRepresentative = [&](const char* preset, const Scene& representative) {
                constexpr int warmup = 4, measured = 16;
                constexpr int viewportWidth = 960, viewportHeight = 540;
                std::vector<SceneViewTarget::Timings> sceneSamples, gameSamples;
                std::vector<double> gpuSamples;
                std::uint32_t lastGpuFrame = std::numeric_limits<std::uint32_t>::max();
                for (int frame = 0; frame < warmup + measured; ++frame)
                {
                    bgfx::TextureHandle sceneTexture = BGFX_INVALID_HANDLE, gameTexture = BGFX_INVALID_HANDLE;
                    std::string sceneError, gameError;
                    SceneViewTarget::Timings sceneTiming, gameTiming;
                    if (!sceneTarget.render(representative, &*assetRegistry, spriteTextures, viewportWidth,
                            viewportHeight, program, sampler, outputMode, layout, sceneTexture, sceneError, &sceneTiming)
                        || !gameTarget.render(representative, &*assetRegistry, spriteTextures, viewportWidth,
                            viewportHeight, program, sampler, outputMode, layout, gameTexture, gameError, &gameTiming))
                        throw std::runtime_error("Representative benchmark failed: "
                            + (sceneError.empty() ? gameError : sceneError));
                    bgfx::frame();
                    if (frame < warmup) continue;
                    sceneSamples.push_back(sceneTiming);
                    gameSamples.push_back(gameTiming);
                    const auto* stats = bgfx::getStats();
                    if (stats && stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin
                        && stats->gpuFrameNum != lastGpuFrame)
                    {
                        gpuSamples.push_back(double(stats->gpuTimeEnd - stats->gpuTimeBegin)
                            * 1000.0 / double(stats->gpuTimerFreq));
                        lastGpuFrame = stats->gpuFrameNum;
                    }
                }
                const auto percentile = [](std::vector<double> values, const double fraction) {
                    std::sort(values.begin(), values.end());
                    return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
                };
                const auto reportView = [&](const char* view, const auto& samples) {
                    std::vector<double> totals, composition, geometry;
                    for (const auto& timing : samples)
                    {
                        totals.push_back(timing.prepareMs + timing.submitMs);
                        composition.push_back(timing.composeMs);
                        geometry.push_back(timing.geometryMs);
                    }
                    std::cout << "[renderer-scene-benchmark] preset=" << preset << " view=" << view
                        << " sprites=" << countVisibleSprites(representative)
                        << " entities=" << representative.entities().size()
                        << " textures=" << benchmarkSpriteAssets.size()
                        << " cpu_p50_ms=" << percentile(totals, 0.50)
                        << " cpu_p95_ms=" << percentile(totals, 0.95)
                        << " composition_p50_ms=" << percentile(composition, 0.50)
                        << " vertex_geometry_p50_ms=" << percentile(geometry, 0.50) << '\n';
                };
                reportView("Scene", sceneSamples);
                reportView("Game", gameSamples);
                if (!gpuSamples.empty())
                    std::cout << "[renderer-scene-benchmark] preset=" << preset
                        << " paired_gpu_p50_ms=" << percentile(gpuSamples, 0.50)
                        << " paired_gpu_p95_ms=" << percentile(gpuSamples, 0.95)
                        << " samples=" << gpuSamples.size() << '\n';
            };
            auto tileWorld = makeRepresentativeScene(11001, "Chunked tile world", 24, 12, 12, 6, 48, false);
            auto alphaOverdraw = makeRepresentativeScene(11002, "Alpha overdraw", 16, 8, 8, 4, 256, true);
            auto mixedWorld = makeRepresentativeScene(11003, "Mixed world plus effects", 20, 10, 10, 5, 128, false);
            for (const auto& [scenarioName, representative] : std::array<std::pair<const char*, const Scene*>, 3>{{
                    {"chunked-tile-world", &tileWorld}, {"alpha-overdraw", &alphaOverdraw},
                    {"mixed-world-effects", &mixedWorld}}})
            {
                std::uint64_t textureRuns = 0, previousTexture = 0;
                for (const auto& entity : representative->entities())
                    if (entity.spriteRenderer && entity.spriteRenderer->visible
                        && entity.spriteRenderer->texture.value != previousTexture)
                    {
                        ++textureRuns;
                        previousTexture = entity.spriteRenderer->texture.value;
                    }
                std::cout << "[renderer-scene-benchmark] preset=" << scenarioName
                    << " visible_sprites=" << countVisibleSprites(*representative)
                    << " entities=" << representative->entities().size()
                    << " textures=" << benchmarkSpriteAssets.size() << " texture_runs=" << textureRuns
                    << " alpha_overlays=" << (representative == &alphaOverdraw ? 256 : representative == &mixedWorld ? 128 : 48)
                    << " hierarchy=chunk-roots-and-children\n";
                measureRepresentative(scenarioName, *representative);
                benchmarkSceneNames.emplace_back(scenarioName);
                benchmarkScenes.push_back(*representative);
            }
            std::cout << "[renderer-benchmark] full_frame_host_upload_bytes=0"
                << " two_view_transient_vertex_index_bytes_per_frame=" << 2 * 10000 * (4 * sizeof(ImDrawVert) + 6 * sizeof(std::uint16_t))
                << " (10000-sprite case)\n";
        }
        if (benchmark3d)
        {
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
            const auto modelDirectory = activeProjectRoot / L"Assets" / L"Models";
            std::filesystem::create_directories(modelDirectory);
            const auto fixture = std::filesystem::path(ASTRO_EDITOR_RENDER3D_FIXTURE_DIR)
                / L"Assets" / L"Models" / L"triangle.gltf";
            std::filesystem::copy_file(fixture, modelDirectory / L"triangle.gltf");
            const auto modelImportStart = std::chrono::steady_clock::now();
            auto imported = astro::render3d::GltfImporter::importAndRegister(*assetRegistry,
                std::filesystem::path(std::u8string(u8"Assets/Models/triangle.gltf")));
            if (!imported)
                throw std::runtime_error("3D benchmark fixture import failed: " + imported.error().message);
            meshModelImportMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - modelImportStart).count();

            for (const int meshCount : {1, 100, 500, 1000})
            {
                const auto sceneBuildStart = std::chrono::steady_clock::now();
                Scene benchmarkScene(12000 + static_cast<std::uint64_t>(meshCount),
                    "3D mesh benchmark " + std::to_string(meshCount));
                constexpr int meshesPerCluster = 100, clusterColumns = 10, meshesPerRow = 10;
                EntityId cluster{};
                for (int index = 0; index < meshCount; ++index)
                {
                    const int clusterIndex = index / meshesPerCluster;
                    if (index % meshesPerCluster == 0)
                    {
                        const auto createdCluster = benchmarkScene.createEntity(
                            "Cluster " + std::to_string(clusterIndex));
                        if (!createdCluster) throw std::runtime_error("Could not create 3D benchmark cluster");
                        cluster = createdCluster.value();
                        astro::scene::Transform clusterTransform;
                        clusterTransform.position = {double(clusterIndex % clusterColumns) * 24.0,
                            double(clusterIndex / clusterColumns) * 24.0, 0.0};
                        if (!benchmarkScene.setTransform(cluster, clusterTransform))
                            throw std::runtime_error("Could not position 3D benchmark cluster");
                    }
                    const auto entity = benchmarkScene.createEntity(
                        "Mesh " + std::to_string(index), cluster);
                    if (!entity || !benchmarkScene.setMeshRenderer(entity.value(), imported.value().modelAssetId))
                        throw std::runtime_error("Could not configure 3D benchmark MeshRenderer");
                    astro::scene::Transform transform;
                    const int localIndex = index % meshesPerCluster;
                    transform.position = {double(localIndex % meshesPerRow) * 2.2,
                        double(localIndex / meshesPerRow) * 2.2, 0.0};
                    if (!benchmarkScene.setTransform(entity.value(), transform))
                        throw std::runtime_error("Could not position 3D benchmark mesh");
                }
                meshSceneBuildMs.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - sceneBuildStart).count());
                benchmarkSceneNames.emplace_back("mesh-grid-" + std::to_string(meshCount));
                benchmarkScenes.push_back(std::move(benchmarkScene));
            }
#else
            throw std::runtime_error("3D Scene View benchmark requires the optional glTF importer build");
#endif
        }
        int activeBenchmarkScene = -1;
        std::vector<double> fullFrameWallMs, editorCpuMs, viewCpuMs, bgfxFrameCallMs, bgfxCpuFrameMs;
        std::vector<double> bgfxLatencyMs, presentLatencyMs, fullFrameGpuMs, frameDrawCalls;
        std::vector<double> scene3dPrepareMs, scene3dSubmitMs;
        std::uint32_t lastFullFrameGpuNumber = std::numeric_limits<std::uint32_t>::max();
        constexpr int benchmarkWarmupFrames = 24, benchmarkMeasuredFrames = 48;
        if (benchmark && !benchmarkScenes.empty())
        {
            activeBenchmarkScene = 0;
            scene = benchmarkScenes.front();
            selectedEntity = scene.entities().empty() ? std::nullopt : std::optional<EntityId>{scene.entities().front().id};
            sceneDirty = false;
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
            if (benchmark3d) sceneTarget.requestFrameAll();
#endif
            stopPlaySession();
            const auto started = playSession.play(scene,
                [](Scene&, double) { return astro::core::Result<void>::success(); });
            if (!started) throw std::runtime_error("Could not start full-frame benchmark runtime copy");
        }
        while (running && (smoke ? (frames < 140
                || (inspectCopyDialog && puzzleCampaignArchiveConflictCopyPreview.has_value()))
            : benchmark ? activeBenchmarkScene >= 0 && activeBenchmarkScene < static_cast<int>(benchmarkScenes.size())
                && frames < benchmarkWarmupFrames + benchmarkMeasuredFrames : true))
        {
            if (puzzleCampaignTransferRefreshPending)
            {
                puzzleCampaignTransferRefreshPending = false;
                auto transferResult = std::move(gameViewPuzzleCampaignTransferCopyResult);
                auto recoveryResult = std::move(puzzleCampaignTransferRecoveryMessage);
                const auto refreshed = previewPuzzleCampaignTransfer();
                gameViewPuzzleCampaignTransferCopyResult = std::move(transferResult);
                puzzleCampaignTransferRecoveryMessage = std::move(recoveryResult);
                if (!refreshed) puzzleCampaignTransferRecoveryMessage = refreshed.error().message;
            }
            const auto hostFrameStart = std::chrono::steady_clock::now();
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat
                    && !ImGui::GetIO().WantCaptureKeyboard && (event.key.mod & SDL_KMOD_CTRL) != 0)
                {
                    if (event.key.key == SDLK_S)
                    {
                        if (saveCurrentScene()) ++saveShortcuts;
                    }
                    else if (event.key.key == SDLK_O)
                    {
                        sceneDialog = true;
                        ++openShortcuts;
                    }
                    else if (event.key.key == SDLK_Z)
                    {
                        if ((event.key.mod & SDL_KMOD_SHIFT) != 0) redoSceneEdit();
                        else undoSceneEdit();
                    }
                    else if (event.key.key == SDLK_Y) redoSceneEdit();
                }
            }
            int newWidth, newHeight;
            SDL_GetWindowSizeInPixels(window, &newWidth, &newHeight);
            if (newWidth <= 0 || newHeight <= 0) { SDL_Delay(16); continue; }
            if (newWidth != width || newHeight != height)
            {
                width = newWidth; height = newHeight;
                bgfx::SwapChain swapChain;
                swapChain.width = width; swapChain.height = height;
                bgfx::reset(BGFX_RESET_VSYNC, &swapChain);
            }
            if (smoke && (frames == 20 || frames == 45))
                SDL_SetWindowSize(window, frames == 20 ? 1280 : 1100, frames == 20 ? 800 : 720);
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
            if (smoke && frames == 108 && puzzleSmokeReady)
            {
                if (!gameViewPuzzle || puzzleAppliedMoves != 1)
                    throw std::runtime_error("Puzzle Game View did not apply its first fixed-step move");
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                const auto beforeSave = gameViewPuzzle->snapshot();
                const auto persisted = savePuzzleGameViewSlot();
                const auto legalMutation = firstLegalPuzzleSwap(*gameViewPuzzle);
                if (!persisted || !legalMutation
                    || !gameViewPuzzle->swap(legalMutation->first, legalMutation->second)
                    || !loadPuzzleGameViewSlot()
                    || !samePuzzleSnapshot(gameViewPuzzle->snapshot(), beforeSave))
                    throw std::runtime_error("Puzzle Game View save/load smoke did not restore the saved snapshot");
#endif
                if (!playSession.pause()) throw std::runtime_error("Puzzle Game View could not pause for the save smoke");
                const auto beforePausedInput = gameViewPuzzle->snapshot();
                const auto nextMove = firstLegalPuzzleSwap(*gameViewPuzzle);
                if (!nextMove || clickPuzzleCell(nextMove->first, selectedPuzzleCell, pendingPuzzleSwap)
                    || !clickPuzzleCell(nextMove->second, selectedPuzzleCell, pendingPuzzleSwap)
                    || !pendingPuzzleSwap)
                    throw std::runtime_error("Puzzle Game View could not queue the paused smoke move");
                const auto pausedTick = playSession.tick(0.25);
                if (!pausedTick || pausedTick.value().fixedStepCount != 0 || !pendingPuzzleSwap
                    || !samePuzzleSnapshot(gameViewPuzzle->snapshot(), beforePausedInput))
                    throw std::runtime_error("Puzzle Game View advanced while paused");
                const auto stepped = playSession.stepOnce();
                if (!stepped || stepped.value().fixedStepCount != 1 || pendingPuzzleSwap
                    || puzzleAppliedMoves != 2 || gameViewPuzzle->score() <= beforePausedInput.score)
                    throw std::runtime_error("Puzzle Game View single-step did not apply the queued move");
                if (!playSession.resume()) throw std::runtime_error("Puzzle Game View could not resume after Step");
                puzzleSmokeLifecyclePassed = true;
            }
            if (smoke && frames == 120)
            {
                stopPlaySession();
                auto level = defaultPuzzleLevelDefinition();
                level.id = "smoke-level";
                level.name = "Smoke level Ω";
                level.targetScore = 180;
                const auto savedLevel = astro::runtime::PuzzleLevelFile::write(smokeDirectory, level);
                const auto loadedLevel = astro::runtime::PuzzleLevelFile::read(smokeDirectory, level.id);
                if (!savedLevel || !loadedLevel || loadedLevel.value() != level)
                    throw std::runtime_error("Puzzle level authoring format round-trip smoke failed");
                const auto listed = astro::runtime::PuzzleLevelFile::list(smokeDirectory);
                const auto duplicated = astro::runtime::PuzzleLevelFile::duplicate(smokeDirectory,
                    level.id, "smoke-copy", "Smoke copy Ω");
                const auto renamed = astro::runtime::PuzzleLevelFile::rename(smokeDirectory,
                    "smoke-copy", "smoke-renamed");
                const auto oldCopy = astro::runtime::PuzzleLevelFile::read(smokeDirectory, "smoke-copy");
                const auto renamedCopy = astro::runtime::PuzzleLevelFile::read(smokeDirectory, "smoke-renamed");
                const bool catalogContainsSource = listed && std::any_of(listed.value().begin(), listed.value().end(),
                    [&](const auto& entry) { return entry.id == level.id && entry.readable(); });
                if (!catalogContainsSource || !duplicated || !renamed || oldCopy || !renamedCopy
                    || renamedCopy.value().name != "Smoke copy Ω")
                    throw std::runtime_error("Puzzle level catalog duplicate/rename smoke failed");
                const auto refreshed = refreshPuzzleLevelCatalog();
                if (!refreshed) throw std::runtime_error("Puzzle level catalog refresh smoke failed");
                selectedPuzzleCatalogId = level.id;
                const auto opened = loadPuzzleLevelIntoDraft(selectedPuzzleCatalogId);
                if (!opened || puzzleLevelDraft != level)
                    throw std::runtime_error("Puzzle level catalog open smoke failed");
                puzzleLevelCatalogSmokePassed = true;
                const auto started = startPuzzleGame(&puzzleLevelDraft);
                if (!started || !gameViewPuzzle || gameViewPuzzle->tiles() != puzzleLevelDraft.initialTiles
                    || !activePuzzleLevelId || *activePuzzleLevelId != puzzleLevelDraft.id)
                    throw std::runtime_error("Puzzle editor did not launch the authored level definition");
                const auto authoredBeforeSave = gameViewPuzzle->snapshot();
                const auto authoredSaved = savePuzzleGameViewSlot();
                const auto expectedLevelSaveDirectory = smokeDirectory / "puzzle-saves" / "PuzzleLevels" / "smoke-level";
                if (!authoredSaved || !gameViewPuzzleSaveStore
                    || gameViewPuzzleSaveStore->directory() != expectedLevelSaveDirectory)
                    throw std::runtime_error("Authored Puzzle progress did not use its level-specific slot directory");
                const auto authoredMutation = firstLegalPuzzleSwap(*gameViewPuzzle);
                if (!authoredMutation || !gameViewPuzzle->swap(authoredMutation->first, authoredMutation->second))
                    throw std::runtime_error("Authored Puzzle save smoke could not mutate the board");
                const auto mutatedAuthoredSnapshot = gameViewPuzzle->snapshot();
                activePuzzleLevelId = "other-level";
                const auto wrongLevelLoad = loadPuzzleGameViewSlot();
                activePuzzleLevelId = puzzleLevelDraft.id;
                if (wrongLevelLoad || !samePuzzleSnapshot(gameViewPuzzle->snapshot(), mutatedAuthoredSnapshot))
                    throw std::runtime_error("Authored Puzzle slot accepted a different level ID or changed state on rejection");
                const auto authoredLoaded = loadPuzzleGameViewSlot();
                if (!authoredLoaded || !samePuzzleSnapshot(gameViewPuzzle->snapshot(), authoredBeforeSave))
                    throw std::runtime_error("Authored Puzzle progress did not round-trip for its matching level ID");
                puzzleLevelSaveSmokePassed = true;
                puzzleLevelSmokePassed = true;

                auto campaignStart = defaultPuzzleLevelDefinition();
                campaignStart.id = "smoke-campaign-start";
                campaignStart.name = "Campaign Start Ω";
                campaignStart.targetScore = 1;
                auto campaignEnd = campaignStart;
                campaignEnd.id = "smoke-campaign-end";
                campaignEnd.name = "Campaign End Ω";
                const auto savedCampaignStart = astro::runtime::PuzzleLevelFile::write(smokeDirectory, campaignStart);
                const auto savedCampaignEnd = astro::runtime::PuzzleLevelFile::write(smokeDirectory, campaignEnd);
                astro::runtime::PuzzleCampaignDefinition campaign{
                    "smoke-campaign", "Smoke Campaign Ω", campaignStart.id,
                    {campaignStart.id, campaignEnd.id},
                    {{campaignStart.id, astro::runtime::PuzzleCampaignRuleKind::Default, 0, campaignEnd.id},
                     {campaignEnd.id, astro::runtime::PuzzleCampaignRuleKind::Default, 0, std::nullopt}}};
                const auto savedCampaign = astro::runtime::PuzzleCampaignFile::write(smokeDirectory, campaign);
                if (!savedCampaignStart || !savedCampaignEnd || !savedCampaign)
                    throw std::runtime_error("Puzzle Game View campaign smoke fixture could not be authored");
                auto missingLinkCampaign = campaign;
                missingLinkCampaign.id = "smoke-missing-link";
                missingLinkCampaign.name = "Missing Link Smoke";
                missingLinkCampaign.orderedLevelIds[1] = "smoke-absent-level";
                missingLinkCampaign.transitions[0].targetLevelId = "smoke-absent-level";
                missingLinkCampaign.transitions[1].sourceLevelId = "smoke-absent-level";
                const auto missingLinkText = astro::runtime::serializePuzzleCampaign(missingLinkCampaign);
                if (!missingLinkText)
                    throw std::runtime_error("Puzzle campaign smoke could not serialize the broken-link fixture");
                const auto missingLinkPath = astro::runtime::PuzzleCampaignFile::campaignsDirectory(smokeDirectory)
                    / "smoke-missing-link.astro-puzzle-campaign";
                { std::ofstream output(missingLinkPath, std::ios::binary); output << missingLinkText.value(); }
                const auto catalogRefreshed = refreshPuzzleCampaignCatalog();
                const auto brokenCampaignEntry = std::find_if(puzzleCampaignCatalog.begin(), puzzleCampaignCatalog.end(),
                    [](const auto& entry) { return entry.id == "smoke-missing-link"; });
                if (!catalogRefreshed || brokenCampaignEntry == puzzleCampaignCatalog.end()
                    || !brokenCampaignEntry->readable() || brokenCampaignEntry->referencesValid()
                    || brokenCampaignEntry->levelReferences.size() != 2
                    || brokenCampaignEntry->levelReferences[1].available
                    || brokenCampaignEntry->levelReferences[1].levelId != "smoke-absent-level")
                    throw std::runtime_error("Puzzle campaign browser did not diagnose a missing level reference");
                std::copy_n(campaign.id.data(), campaign.id.size(), puzzleCampaignIdInput.data());
                selectedPuzzleCampaignCatalogId = missingLinkCampaign.id;
                const auto campaignStarted = startPuzzleCampaign();
                if (!campaignStarted || !gameViewPuzzleCampaign
                    || gameViewPuzzleCampaign->currentLevel().id != campaignStart.id)
                    throw std::runtime_error("Puzzle Game View could not start the authored campaign");
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                gameViewPuzzleCampaignSaveSlot = 4;
                const auto campaignBeforeSave = gameViewPuzzleCampaign->game().snapshot();
                const auto campaignSaved = savePuzzleCampaignGameViewSlot();
                const auto expectedCampaignSaveDirectory = smokeDirectory / "puzzle-saves"
                    / "PuzzleCampaigns" / campaign.id;
                if (!campaignSaved || !gameViewPuzzleCampaignSaveStore
                    || gameViewPuzzleCampaignSaveStore->directory() != expectedCampaignSaveDirectory)
                    throw std::runtime_error("Campaign progress did not use the project-scoped user save directory");

                const auto campaignCompletingMove = firstCompletingPuzzleSwap(gameViewPuzzleCampaign->game());
                if (!campaignCompletingMove
                    || clickPuzzleCell(campaignCompletingMove->first, selectedPuzzleCell, pendingPuzzleSwap)
                    || !clickPuzzleCell(campaignCompletingMove->second, selectedPuzzleCell, pendingPuzzleSwap)
                    || !playSession.tick(0.05)
                    || gameViewPuzzleCampaign->game().score() <= campaignBeforeSave.score)
                    throw std::runtime_error("Campaign save smoke could not mutate the active campaign through Game View");
                const auto campaignAfterMutation = gameViewPuzzleCampaign->game().snapshot();

                gameViewPuzzleCampaignSaveSlot = 9;
                const auto missingCampaignSlot = loadPuzzleCampaignGameViewSlot();
                if (missingCampaignSlot
                    || !samePuzzleSnapshot(gameViewPuzzleCampaign->game().snapshot(), campaignAfterMutation)
                    || gameViewPuzzleCampaign->currentLevel().id != campaignStart.id)
                    throw std::runtime_error("Missing campaign slot changed the active campaign before validation");

                gameViewPuzzleCampaignSaveSlot = 4;
                const auto campaignRestored = loadPuzzleCampaignGameViewSlot();
                if (!campaignRestored || !gameViewPuzzleCampaign
                    || gameViewPuzzleCampaign->currentLevel().id != campaignStart.id
                    || !samePuzzleSnapshot(gameViewPuzzleCampaign->game().snapshot(), campaignBeforeSave))
                    throw std::runtime_error("Editor Game View campaign progress did not round-trip");

                puzzleSaveDirectoryInput.fill('\0');
                constexpr std::string_view previewSubdirectory = "puzzle-preview-target";
                std::copy(previewSubdirectory.begin(), previewSubdirectory.end(), puzzleSaveDirectoryInput.begin());
                puzzleSaveSettingsDirty = true;
                const auto previewTargetDirectory = smokeDirectory / previewSubdirectory
                    / "PuzzleCampaigns" / campaign.id;
                const auto sourceSlotPath = expectedCampaignSaveDirectory / "slot-4.astro-save";
                std::error_code previewFilesystemError;
                const auto sourceSlotBytesBefore = std::filesystem::file_size(sourceSlotPath, previewFilesystemError);
                if (previewFilesystemError)
                    throw std::runtime_error("Campaign save smoke could not inspect its isolated source slot");
                const auto transferPreview = previewPuzzleCampaignTransfer();
                const auto sourceSlotBytesAfter = std::filesystem::file_size(sourceSlotPath, previewFilesystemError);
                if (!transferPreview || !gameViewPuzzleCampaignTransferPreview
                    || previewFilesystemError || sourceSlotBytesAfter != sourceSlotBytesBefore
                    || std::filesystem::exists(previewTargetDirectory)
                    || gameViewPuzzleCampaignTransferPreview->targetDirectory != previewTargetDirectory
                    || gameViewPuzzleCampaignTransferPreview->slots.size() != 10
                    || gameViewPuzzleCampaignTransferPreview->slots[4].state
                        != astro::runtime::PuzzleCampaignSaveSlotPreviewState::Valid)
                    throw std::runtime_error("Campaign transfer preview changed files or missed the saved source slot");
                puzzleCampaignTransferPreviewSmokePassed = true;
                const auto copiedFixture = confirmPuzzleCampaignTransferCopy(true);
                if (!copiedFixture) throw std::runtime_error(copiedFixture.error().message);
                puzzleCampaignTransferCopySmokePassed = gameViewPuzzleCampaignTransferCopyResult
                    && gameViewPuzzleCampaignTransferCopyResult->state
                        == astro::runtime::PuzzleCampaignSaveTransferCopyState::Copied;
                gameViewPuzzleCampaignSaveSlot = 0;
                puzzleCampaignSaveSmokePassed = true;
#endif
                const auto finalCompletingMove = firstCompletingPuzzleSwap(gameViewPuzzleCampaign->game());
                if (!finalCompletingMove || clickPuzzleCell(finalCompletingMove->first, selectedPuzzleCell, pendingPuzzleSwap)
                    || !clickPuzzleCell(finalCompletingMove->second, selectedPuzzleCell, pendingPuzzleSwap))
                    throw std::runtime_error("Puzzle campaign Game View could not queue a level-completing move");
                const auto firstTick = playSession.tick(0.05);
                if (!firstTick || !lastPuzzleMove || !lastPuzzleMove->levelCompleted)
                    throw std::runtime_error("Puzzle campaign move was not routed through the runtime fixed step");
                if (!gameViewPuzzleCampaign || !lastPuzzleMove || !lastPuzzleMove->levelCompleted)
                    throw std::runtime_error("Puzzle campaign runtime did not report the authored level goal");
                const auto advanced = advancePuzzleCampaign();
                if (!advanced || gameViewPuzzleCampaign->currentLevel().id != "smoke-campaign-end")
                    throw std::runtime_error("Puzzle Game View Continue did not load the authored next level");
                const auto completingMove = firstCompletingPuzzleSwap(gameViewPuzzleCampaign->game());
                if (!completingMove || clickPuzzleCell(completingMove->first, selectedPuzzleCell, pendingPuzzleSwap)
                    || !clickPuzzleCell(completingMove->second, selectedPuzzleCell, pendingPuzzleSwap))
                    throw std::runtime_error("Puzzle campaign Game View could not queue the final level move");
                const auto finalTick = playSession.tick(0.05);
                if (!finalTick || !lastPuzzleMove || !lastPuzzleMove->levelCompleted)
                    throw std::runtime_error("Puzzle campaign final level did not complete through Game View input");
                const auto ended = advancePuzzleCampaign();
                if (!ended || gameViewPuzzleCampaign->state()
                    != astro::runtime::PuzzleCampaignSessionState::Ended
                    || gameViewPuzzleCampaign->currentLevel().id != "smoke-campaign-end")
                    throw std::runtime_error("Puzzle Game View did not retain its final level after END");
                const auto rejectedMove = gameViewPuzzleCampaign->swap({0, 0}, {1, 0});
                if (rejectedMove)
                    throw std::runtime_error("Ended Puzzle campaign accepted gameplay input");
                puzzleCampaignSmokePassed = true;
            }
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (smoke && frames == 120)
            {
                if (!puzzleCampaignTransferCopySmokePassed)
                {
                    std::string detail = "Campaign transfer confirmation smoke did not copy its isolated fixture; dialog="
                        + std::string(puzzleCampaignTransferConfirmationDrawn ? "1" : "0");
                    if (gameViewPuzzleCampaignTransferCopyResult)
                        detail += " state=" + std::to_string(static_cast<int>(gameViewPuzzleCampaignTransferCopyResult->state))
                            + " diagnostic=" + gameViewPuzzleCampaignTransferCopyResult->diagnostic;
                    throw std::runtime_error(detail);
                }
                constexpr std::string_view recoverySubdirectory = "puzzle-recovery-target";
                const auto campaignId = gameViewPuzzleCampaign->campaign().id;
                const auto recoveryDirectory = smokeDirectory / recoverySubdirectory
                    / "PuzzleCampaigns" / campaignId;
                std::filesystem::create_directories(recoveryDirectory);
                const auto journalPath = recoveryDirectory / ".astro-campaign-transfer.pending";
                std::ofstream journal(journalPath, std::ios::binary);
                const std::string manifest = "ASTRO_CAMPAIGN_TRANSFER 1\nCAMPAIGN " + campaignId
                    + "\nCOUNT 1\nITEM 4 P 7 0\nEND\n";
                journal.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
                journal.close();
                if (!journal) throw std::runtime_error("Could not prepare the isolated Editor recovery smoke fixture");
                { std::ofstream partial(recoveryDirectory / "slot-4.astro-save", std::ios::binary); partial << "par"; }
                puzzleSaveDirectoryInput.fill('\0');
                std::copy(recoverySubdirectory.begin(), recoverySubdirectory.end(), puzzleSaveDirectoryInput.begin());
                puzzleSaveSettingsDirty = true;
                const auto recovered = previewPuzzleCampaignTransfer();
                if (!recovered || !gameViewPuzzleCampaignTransferPreview
                    || !gameViewPuzzleCampaignTransferPreview->pendingRecovery
                    || !gameViewPuzzleCampaignTransferPreview->pendingRecovery->journalValid)
                    throw std::runtime_error("Editor did not detect the isolated interrupted-copy journal");
            }
            if (smoke && frames == 128)
            {
                constexpr std::string_view invalidSubdirectory = "puzzle-invalid-journal-target";
                const auto campaignId = gameViewPuzzleCampaign->campaign().id;
                const auto invalidDirectory = smokeDirectory / invalidSubdirectory / "PuzzleCampaigns" / campaignId;
                std::filesystem::create_directories(invalidDirectory);
                const auto journalPath = invalidDirectory / ".astro-campaign-transfer.pending";
                puzzleCampaignInvalidJournalSmokeBytes = "ASTRO_CAMPAIGN_TRANSFER 1\nCAMPAIGN " + campaignId
                    + "\nCOUNT 1\nITEM 4 P 7 0\n";
                { std::ofstream journal(journalPath, std::ios::binary); journal.write(
                    puzzleCampaignInvalidJournalSmokeBytes.data(),
                    static_cast<std::streamsize>(puzzleCampaignInvalidJournalSmokeBytes.size())); }
                constexpr std::string_view sentinel = "keep candidate save untouched";
                { std::ofstream candidate(invalidDirectory / "slot-4.astro-save", std::ios::binary);
                    candidate.write(sentinel.data(), static_cast<std::streamsize>(sentinel.size())); }
                puzzleSaveDirectoryInput.fill('\0');
                std::copy(invalidSubdirectory.begin(), invalidSubdirectory.end(), puzzleSaveDirectoryInput.begin());
                puzzleSaveSettingsDirty = true;
                const auto invalidPreview = previewPuzzleCampaignTransfer();
                std::ifstream journalInput(journalPath, std::ios::binary);
                std::ifstream candidateInput(invalidDirectory / "slot-4.astro-save", std::ios::binary);
                const std::string journalAfter(std::istreambuf_iterator<char>(journalInput), {});
                const std::string candidateAfter(std::istreambuf_iterator<char>(candidateInput), {});
                journalInput.close();
                candidateInput.close();
                if (!invalidPreview || !gameViewPuzzleCampaignTransferPreview
                    || !gameViewPuzzleCampaignTransferPreview->pendingRecovery
                    || gameViewPuzzleCampaignTransferPreview->pendingRecovery->journalState
                        != astro::runtime::PuzzleCampaignSaveTransferRecoveryJournalState::Malformed
                    || !gameViewPuzzleCampaignTransferPreview->pendingRecovery->artifacts.empty()
                    || journalAfter != puzzleCampaignInvalidJournalSmokeBytes || candidateAfter != sentinel)
                    throw std::runtime_error("Editor did not preserve an invalid journal and unclassified candidate save");
            }
#endif
#endif
            if (playSession.state() == astro::runtime::RuntimeState::Playing)
            {
                const auto ticked = playSession.tick(ImGui::GetIO().DeltaTime);
                if (!ticked) reportError(ticked.error().message);
            }
            if (assetRegistry)
                for (const auto& entity : scene.entities())
                    if (entity.spriteRenderer && assetRegistry->find(entity.spriteRenderer->texture)
                        && !spriteTextures.contains(entity.spriteRenderer->texture.value))
                    {
                        const auto cached = assetRegistry->cachedPath(entity.spriteRenderer->texture);
                        std::error_code cacheError;
                        if (std::filesystem::exists(cached, cacheError) && !cacheError)
                        {
                            const auto handle = loadSpriteTexture(cached);
                            if (bgfx::isValid(handle)) spriteTextures.emplace(entity.spriteRenderer->texture.value, handle);
                        }
                    }
            SceneViewTarget::Timings editorSceneTiming, editorGameTiming;
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu(tr("File")))
                {
                    if (ImGui::MenuItem(tr("New scene"), nullptr, false, !activeProjectRoot.empty()))
                    {
                        if (sceneDirty) reportError(tr("Save the current scene before creating another scene"));
                        else { startNewScene(); message = tr("New scene created in memory"); }
                    }
                    if (ImGui::MenuItem(tr("Save scene"), "Ctrl+S", false, !activeProjectRoot.empty()))
                        saveCurrentScene();
                    if (ImGui::MenuItem(tr("Open scene..."), "Ctrl+O", false, !activeProjectRoot.empty()))
                        sceneDialog = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("New project..."))) { dialog = true; create = true; }
                    if (ImGui::MenuItem(tr("Open project..."))) { dialog = true; create = false; }
                    if (ImGui::MenuItem(tr("Close project"), nullptr, false, !activeProject.empty()))
                    {
                        if (csharpBuildRunning) reportError(tr("Wait for the C# build to finish before switching projects"));
                        else if (sceneDirty) reportError(tr("Save the current scene before closing the project"));
                        else if (csharpCodeWorkspace.dirty()) reportError(tr("Save or discard the open C# script before closing the project"));
                        else
                        {
                            stopPlaySession();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
                            gameViewPuzzle.reset();
                            resetPuzzleLevelDraft();
                            selectedPuzzleCell.reset();
                            pendingPuzzleSwap.reset();
                            lastPuzzleMove.reset();
#endif
                            activeProject.clear();
                            activeProjectRoot.clear();
                            currentScenePath.clear();
                            csharpCodeWorkspace = astro::editor::CSharpCodeWorkspace{};
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                            gameViewPuzzleSaveStore.reset();
                            gameViewPuzzleSaveProfile.reset();
                            gameViewPuzzleSaveDirectory.clear();
                            gameViewPuzzleCampaignSaveStore.reset();
                            gameViewPuzzleCampaignSaveProfile.reset();
                            gameViewPuzzleCampaignSaveDirectory.clear();
                            gameViewPuzzleCampaignSaveMessage.clear();
                            puzzleSaveSettingsReady = false;
                            puzzleSaveSettingsDirty = false;
#endif
                            commands.clearHistory();
                        }
                    }
                    if (ImGui::MenuItem(tr("Exit")))
                    {
                        if (csharpBuildRunning) reportError(tr("Wait for the C# build to finish before switching projects"));
                        else if (csharpCodeWorkspace.dirty()) reportError(tr("Save or discard the open C# script before closing the project"));
                        else running = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("Edit")))
                {
                    if (ImGui::MenuItem(tr("Undo"), "Ctrl+Z", false, commands.canUndo())) undoSceneEdit();
                    if (ImGui::MenuItem(tr("Redo"), "Ctrl+Y", false, commands.canRedo())) redoSceneEdit();
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("Settings...")))
                    {
                        pendingEditorLanguage = gEditorLanguage;
                        showEditorSettings = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("View")))
                {
                    ImGui::MenuItem(tr("Project"), nullptr, &showProject);
                    ImGui::MenuItem(tr("C# Code"), nullptr, &showCodeWorkspace);
                    ImGui::MenuItem(tr("Diagnostics"), nullptr, &showDiagnostics);
                    ImGui::MenuItem(tr("Game View"), nullptr, &showGameView);
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
                    ImGui::MenuItem(tr("Puzzle Campaign Browser"), nullptr, &showPuzzleCampaignBrowser);
#endif
                    ImGui::MenuItem(tr("Visual Logic"), nullptr, &showVisualLogic);
                    ImGui::MenuItem(tr("UI Editor"), nullptr, &showUiEditor);
                    ImGui::MenuItem(tr("Timeline"), nullptr, &showTimeline);
                    ImGui::MenuItem(tr("Camera Presets"), nullptr, &showCameraPresets);
                    ImGui::MenuItem(tr("Save System"), nullptr, &showSaveSystem);
                    if (ImGui::MenuItem(tr("Reset layout")))
                    {
                        resetLayout = true;
                        showProject = showDiagnostics = showVisualLogic = true;
                        showUiEditor = true;
                        showTimeline = showCameraPresets = true;
                        showSaveSystem = true;
                        showGameView = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
            const auto dock = ImGui::DockSpaceOverViewport();
            if (resetLayout)
            {
                ImGui::DockBuilderRemoveNode(dock);
                ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
                ImGuiID left, remainder, center, right, logicPane, scenePane;
                ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.23f, &left, &remainder);
                ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Right, 0.25f, &right, &center);
                ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.32f, &logicPane, &scenePane);
                ImGui::DockBuilderDockWindow(tr("Hierarchy"), left);
                ImGui::DockBuilderDockWindow(tr("Scene View"), scenePane);
                ImGui::DockBuilderDockWindow(tr("Visual Logic"), logicPane);
                ImGui::DockBuilderDockWindow(tr("Project"), right);
                ImGui::DockBuilderDockWindow(tr("C# Code"), right);
                ImGui::DockBuilderDockWindow(tr("Diagnostics"), right);
                ImGui::DockBuilderDockWindow(tr("Inspector"), right);
                ImGui::DockBuilderDockWindow(tr("UI Editor"), right);
                ImGui::DockBuilderDockWindow(tr("Timeline"), right);
                ImGui::DockBuilderDockWindow(tr("Camera Presets"), right);
                ImGui::DockBuilderDockWindow(tr("Save System"), right);
                ImGui::DockBuilderFinish(dock);
                resetLayout = false;
            }
            if (showEditorSettings)
            {
                if (ImGui::Begin(tr("Settings"), &showEditorSettings))
                {
                    editorSettingsWindowDrawn = true;
                    if (ImGui::BeginCombo(tr("Language"), tr(astro::editor::languageName(pendingEditorLanguage))))
                    {
                        for (const auto language : {EditorLanguage::English, EditorLanguage::Russian})
                        {
                            const bool selected = pendingEditorLanguage == language;
                            if (ImGui::Selectable(tr(astro::editor::languageName(language)), selected))
                                pendingEditorLanguage = language;
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TextWrapped(tr("Changes apply immediately and are saved for this user."));
                    if (!languageSettingsReadable)
                        ImGui::TextWrapped(tr("Language preference could not be read; using English."));
                    if (languageSettingsPath.empty())
                        ImGui::TextWrapped(tr("User settings path unavailable; language change cannot be saved."));
                    ImGui::BeginDisabled(languageSettingsPath.empty()
                        || (languageSettingsReadable && pendingEditorLanguage == gEditorLanguage));
                    if (ImGui::Button(tr("Apply")))
                    {
                        const auto saved = astro::editor::writeEditorLanguage(languageSettingsPath, pendingEditorLanguage);
                        if (saved)
                        {
                            gEditorLanguage = pendingEditorLanguage;
                            languageSettingsReadable = true;
                            message = tr("Language preference saved");
                        }
                        else
                        {
                            std::array<char, 1024> errorText{};
                            std::snprintf(errorText.data(), errorText.size(),
                                tr("Could not save language preference: %s"), saved.error().message.c_str());
                            reportError(errorText.data());
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button(tr("Cancel")))
                    {
                        pendingEditorLanguage = gEditorLanguage;
                        showEditorSettings = false;
                    }
                }
                ImGui::End();
            }
            if (showProject)
            {
                ImGui::Begin(tr("Project"), &showProject);
                docked = ImGui::IsWindowDocked();
                ImGui::TextUnformatted(activeProject.empty() ? tr("No project open") : activeProject.c_str());
                ImGui::Separator();
                ImGui::TextWrapped(tr("Scene files are stored under Scenes. Import PNG/JPEG source files from Assets/."));
                if (assetRegistry)
                {
                    csharpBuildPanelDrawn = true;
                    ImGui::Separator();
                    ImGui::TextUnformatted(tr("C# scripts"));
                    ImGui::TextDisabled(tr("Metadata inspection is explicit, does not run scripts, and requires a current registered build."));
                    const char* buildStatus = tr("No successful C# build found");
                    switch (csharpBuildReport.freshness)
                    {
                    case astro::assets::ScriptBuildFreshness::Current: buildStatus = tr("C# build: current"); break;
                    case astro::assets::ScriptBuildFreshness::Stale: buildStatus = tr("C# build: stale"); break;
                    case astro::assets::ScriptBuildFreshness::Invalid: buildStatus = tr("C# build: invalid"); break;
                    case astro::assets::ScriptBuildFreshness::Missing: break;
                    }
                    ImGui::TextUnformatted(buildStatus);
                    if (ImGui::Button(tr("Refresh C# status")))
                        csharpBuildReport = assetRegistry->inspectScriptBuild();
                    ImGui::SameLine();
                    const bool canRegisterCSharp = csharpBuildReport.freshness == astro::assets::ScriptBuildFreshness::Current
                        && csharpBuildReport.requiresRegistration && !csharpBuildReport.bindings.empty();
                    ImGui::BeginDisabled(!canRegisterCSharp);
                    if (ImGui::Button(tr("Register current C# build")))
                    {
                        auto registered = assetRegistry->registerScriptsFromBuild();
                        if (!registered) reportError(registered.error().message);
                        csharpBuildReport = assetRegistry->inspectScriptBuild();
                    }
                    ImGui::EndDisabled();
                    for (const auto& binding : csharpBuildReport.bindings)
                    {
                        const auto registration = binding.registered
                            ? tr("registered AssetId")
                            : tr("not registered");
                        if (binding.registered && binding.assetId)
                            ImGui::TextWrapped(tr("%s -> %s (%s %llu)"), binding.sourceRelativePath.c_str(),
                                binding.managedTypeName.c_str(), registration,
                                static_cast<unsigned long long>(binding.assetId->value));
                        else
                            ImGui::TextWrapped(tr("%s -> %s (%s)"), binding.sourceRelativePath.c_str(),
                                binding.managedTypeName.c_str(), registration);
                        if (!binding.diagnostic.empty())
                            ImGui::TextWrapped(tr("%s"), astro::editor::translateEditorDiagnostic(
                                binding.diagnostic, gEditorLanguage).c_str());
                    }
                    for (const auto& diagnostic : csharpBuildReport.diagnostics)
                        ImGui::TextWrapped(tr("%s"), astro::editor::translateEditorDiagnostic(
                            diagnostic, gEditorLanguage).c_str());
#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
                    csharpMetadataValidationPanelDrawn = true;
                    ImGui::SeparatorText(tr("CLR metadata validation"));
                    ImGui::TextDisabled(tr("The selected assembly is copied into a temporary AppContainer worker; no user script code is executed."));
                    if (csharpMetadataValidationRunning)
                        ImGui::BeginDisabled();
                    else
                        ImGui::BeginDisabled(csharpBuildRunning || csharpBuildReport.freshness != astro::assets::ScriptBuildFreshness::Current);
                    if (ImGui::Button(csharpMetadataValidationRunning
                        ? tr("Validating C# metadata...") : tr("Inspect registered C# types")))
                    {
                        csharpBuildReport = assetRegistry->inspectScriptBuild();
                        const auto plan = astro::editor::prepareCSharpMetadataValidation(*assetRegistry, csharpBuildReport);
                        if (!plan)
                        {
                            csharpMetadataValidationOutput = plan.error().message;
                            message = tr("C# metadata validation could not start");
                        }
                        else
                        {
                            auto request = std::move(plan).value();
                            csharpMetadataValidationProjectRoot = request.projectRoot;
                            csharpMetadataValidationOutput.clear();
                            csharpMetadataValidationTypeCount = 0;
                            csharpMetadataValidationBatchCount = 0;
                            csharpMetadataValidationRunning = true;
                            const auto dotnetRoot = pathFromUtf8(ASTRO_EDITOR_DOTNET_ROOT);
                            const auto validatorAssembly = pathFromUtf8(ASTRO_EDITOR_METADATA_VALIDATOR_DLL);
                            csharpMetadataValidationFuture = std::async(std::launch::async,
                                [dotnetRoot, validatorAssembly, request = std::move(request)] {
                                    return astro::editor::runCSharpMetadataValidation(dotnetRoot, validatorAssembly, request);
                                });
                            message = tr("C# metadata validation started");
                        }
                    }
                    ImGui::EndDisabled();
                    if (csharpMetadataValidationRunning && csharpMetadataValidationFuture.valid()
                        && csharpMetadataValidationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    {
                        astro::editor::CSharpMetadataValidationResult result;
                        try { result = csharpMetadataValidationFuture.get(); }
                        catch (const std::exception& error) { result.output = error.what(); }
                        csharpMetadataValidationRunning = false;
                        std::error_code projectRootError;
                        const auto currentProjectRoot = std::filesystem::weakly_canonical(activeProjectRoot, projectRootError);
                        if (!projectRootError && csharpMetadataValidationProjectRoot == currentProjectRoot)
                        {
                            csharpMetadataValidationTypeCount = result.managedTypeCount;
                            csharpMetadataValidationBatchCount = result.workerBatchCount;
                            csharpMetadataValidationOutput = std::move(result.output);
                            message = tr(result.passed ? "C# metadata validation passed" : "C# metadata validation failed");
                        }
                    }
                    if (csharpMetadataValidationRunning)
                        ImGui::TextDisabled(tr("Validating C# metadata..."));
                    if (!csharpMetadataValidationOutput.empty())
                    {
                        if (csharpMetadataValidationTypeCount > 0)
                            ImGui::TextDisabled(tr("Requested %zu registered types in %zu isolated batches; report cap is 128 KiB."),
                                csharpMetadataValidationTypeCount, csharpMetadataValidationBatchCount);
                        ImGui::SeparatorText(tr("Metadata validation result"));
                        ImGui::BeginChild("##csharp-metadata-validation", ImVec2(-1.0f, 110.0f), ImGuiChildFlags_Borders);
                        const auto metadataTextStarted = std::chrono::steady_clock::now();
                        ImGui::TextWrapped(tr("%s"), csharpMetadataValidationOutput.c_str());
#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
                        if (smoke)
                        {
                            const auto drawMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - metadataTextStarted).count();
                            csharpMetadataReportMaxDrawMs = std::max(csharpMetadataReportMaxDrawMs, drawMs);
                            csharpMetadataReportScrollableInSmoke = csharpMetadataReportScrollableInSmoke
                                || ImGui::GetScrollMaxY() > 0.0f;
                        }
#endif
                        ImGui::EndChild();
                    }
#endif
                    ImGui::InputText(tr("Assets-relative image path"), assetPathInput, sizeof(assetPathInput));
                    if (ImGui::Button(tr("Import PNG/JPEG")))
                    {
                        const std::u8string encoded(reinterpret_cast<const char8_t*>(assetPathInput));
                        auto imported = assetRegistry->importImage(std::filesystem::path(encoded));
                        if (!imported) reportError(imported.error().message);
                        else
                        {
                            message = tr("Imported image as AssetId ") + std::to_string(imported.value().id.value);
                            const auto handle = loadSpriteTexture(assetRegistry->cachedPath(imported.value().id));
                            if (!bgfx::isValid(handle)) reportError(tr("Image imported, but WIC could not decode it for preview"));
                            else spriteTextures[imported.value().id.value] = handle;
                        }
                    }
                    for (const auto missing : assetRegistry->missingAssets())
                        ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), tr("Missing source for AssetId %llu"),
                            static_cast<unsigned long long>(missing.value));
                    if (ImGui::CollapsingHeader(tr("Imported images")))
                        for (const auto& [id, ref] : assetRegistry->entries())
                            ImGui::BulletText(tr("%llu — %s (%u x %u)"), static_cast<unsigned long long>(id.value),
                                ref.sourceRelativePath.c_str(), ref.width, ref.height);
                }
                ImGui::End();
            }
            if (showCodeWorkspace)
            {
                csharpCodeWorkspaceDrawn = true;
                if (ImGui::Begin(tr("C# Code"), &showCodeWorkspace))
                {
                    ImGui::TextDisabled(tr("Project scripts are confined to Scripts/*.cs and saved as UTF-8."));
                    if (activeProjectRoot.empty()) ImGui::TextDisabled(tr("Open a project to edit C# scripts."));
                    else
                    {
                        csharpManifestWindowDrawn = true;
                        if (ImGui::CollapsingHeader(tr("C# build settings"), ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            if (csharpManifestInvalid)
                            {
                                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", csharpManifestMessage.c_str());
                                if (ImGui::Button(tr("Replace invalid manifest...")))
                                    ImGui::OpenPopup("##ReplaceInvalidCSharpManifest");
                            }
                            else
                            {
                                ImGui::InputText(tr("Pinned .NET SDK"), csharpManifestSdkInput.data(), csharpManifestSdkInput.size());
                                const auto detectedSdk = detectConfiguredCSharpSdk();
                                ImGui::TextDisabled(tr("Configured local SDK: %s"), detectedSdk.empty() ? tr("not detected") : detectedSdk.c_str());
                                ImGui::TextDisabled(tr("Target framework: %s"), csharpManifestDraft.targetFramework.c_str());
                                ImGui::InputText(tr("C# assembly name"), csharpManifestAssemblyInput.data(), csharpManifestAssemblyInput.size());

                                csharpManifestDraft.sdkVersion = csharpManifestSdkInput.data();
                                csharpManifestDraft.assemblyName = csharpManifestAssemblyInput.data();
                                for (std::size_t i = 0; i < csharpManifestDraft.scriptBindings.size()
                                    && i < csharpManifestTypeInputs.size(); ++i)
                                    csharpManifestDraft.scriptBindings[i].managedTypeName = csharpManifestTypeInputs[i].data();

                                ImGui::SeparatorText(tr("Script type bindings"));
                                for (std::size_t i = 0; i < csharpManifestDraft.scriptBindings.size(); ++i)
                                {
                                    ImGui::PushID(static_cast<int>(i));
                                    ImGui::TextWrapped(tr("%s"), csharpManifestDraft.scriptBindings[i].sourcePath.c_str());
                                    ImGui::InputText(tr("Managed type name"), csharpManifestTypeInputs[i].data(),
                                        csharpManifestTypeInputs[i].size());
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton(tr("Remove binding")))
                                    {
                                        csharpManifestDraft.scriptBindings.erase(csharpManifestDraft.scriptBindings.begin()
                                            + static_cast<std::ptrdiff_t>(i));
                                        csharpManifestTypeInputs.erase(csharpManifestTypeInputs.begin()
                                            + static_cast<std::ptrdiff_t>(i));
                                        ImGui::PopID();
                                        break;
                                    }
                                    ImGui::PopID();
                                }
                                if (ImGui::BeginCombo(tr("Binding source"), csharpManifestNewSource.empty()
                                    ? tr("Select a C# source") : csharpManifestNewSource.c_str()))
                                {
                                    for (const auto& source : csharpManifestSourcePaths)
                                        if (ImGui::Selectable(source.c_str(), csharpManifestNewSource == source))
                                            csharpManifestNewSource = source;
                                    ImGui::EndCombo();
                                }
                                ImGui::InputText(tr("New managed type name"), csharpManifestNewTypeInput.data(),
                                    csharpManifestNewTypeInput.size());
                                ImGui::BeginDisabled(csharpManifestNewSource.empty()
                                    || std::any_of(csharpManifestDraft.scriptBindings.begin(), csharpManifestDraft.scriptBindings.end(),
                                        [&](const auto& binding) { return binding.sourcePath == csharpManifestNewSource; }));
                                if (ImGui::Button(tr("Add binding")))
                                {
                                    csharpManifestDraft.scriptBindings.push_back({csharpManifestNewSource,
                                        csharpManifestNewTypeInput.data()});
                                    csharpManifestTypeInputs.emplace_back();
                                    std::snprintf(csharpManifestTypeInputs.back().data(), csharpManifestTypeInputs.back().size(),
                                        "%s", csharpManifestNewTypeInput.data());
                                    csharpManifestNewTypeInput.fill('\0');
                                }
                                ImGui::EndDisabled();
                                ImGui::SameLine();
                                if (ImGui::Button(tr("Refresh C# source list")))
                                    refreshCSharpManifestSources(activeProjectRoot);
                                const bool manifestDirty = csharpManifestDraft != csharpManifestSaved;
                                ImGui::BeginDisabled(!manifestDirty && csharpManifestExists);
                                if (ImGui::Button(tr("Save C# build settings"))) saveCSharpManifest();
                                ImGui::EndDisabled();
                            }
                            if (!csharpManifestMessage.empty()) ImGui::TextWrapped(tr("%s"), csharpManifestMessage.c_str());
                            if (ImGui::BeginPopupModal("##ReplaceInvalidCSharpManifest", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                            {
                                ImGui::TextWrapped(tr("Replace the invalid C# build manifest with fresh defaults?"));
                                if (ImGui::Button(tr("Replace with defaults")))
                                {
                                    csharpManifestDraft = {};
                                    csharpManifestDraft.sdkVersion = detectConfiguredCSharpSdk();
                                    csharpManifestInvalid = false;
                                    syncCSharpManifestFields();
                                    saveCSharpManifest();
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
                                ImGui::EndPopup();
                            }
                        }
                        ImGui::Separator();
                        ImGui::BeginDisabled(csharpBuildRunning);
                        ImGui::InputText(tr("Open script path under Scripts"), csharpCodePathInput.data(), csharpCodePathInput.size());
                        if (ImGui::Button(tr("Open script")))
                        {
                            const auto relative = pathFromUtf8(csharpCodePathInput.data());
                            if (csharpCodeWorkspace.dirty())
                            {
                                pendingCSharpCodePath = relative;
                                pendingCSharpCodeCreate = false;
                                ImGui::OpenPopup("##UnsavedCSharpChanges");
                            }
                            else
                            {
                                const auto opened = csharpCodeWorkspace.open(activeProjectRoot, relative);
                                csharpCodeMessage = opened ? tr("Script opened") : opened.error().message;
                            }
                        }
                        ImGui::SameLine();
                        ImGui::InputText(tr("New script path under Scripts"), csharpNewFileInput.data(), csharpNewFileInput.size());
                        if (ImGui::Button(tr("Create script")))
                        {
                            const auto relative = pathFromUtf8(csharpNewFileInput.data());
                            if (csharpCodeWorkspace.dirty())
                            {
                                pendingCSharpCodePath = relative;
                                pendingCSharpCodeCreate = true;
                                ImGui::OpenPopup("##UnsavedCSharpChanges");
                            }
                            else
                            {
                                const auto created = csharpCodeWorkspace.create(activeProjectRoot, relative);
                                csharpCodeMessage = created ? tr("Script created") : created.error().message;
                                if (created) std::snprintf(csharpCodePathInput.data(), csharpCodePathInput.size(), "%s", csharpNewFileInput.data());
                            }
                        }
                        ImGui::SeparatorText(tr("Scripts"));
                        const auto scriptsRoot = activeProjectRoot / L"Scripts";
                        std::error_code scriptListError;
                        if (std::filesystem::exists(scriptsRoot, scriptListError) && !scriptListError)
                        {
                            for (std::filesystem::recursive_directory_iterator it(scriptsRoot,
                                    std::filesystem::directory_options::skip_permission_denied, scriptListError), end;
                                it != end && !scriptListError; it.increment(scriptListError))
                            {
                                if (!it->is_regular_file(scriptListError) || scriptListError || it->path().extension() != L".cs") continue;
                                const auto relative = it->path().lexically_relative(scriptsRoot);
                                const auto encoded = pathUtf8(relative);
                                const bool selected = csharpCodeWorkspace.relativeScript() == relative;
                                if (ImGui::Selectable(encoded.c_str(), selected))
                                {
                                    if (csharpCodeWorkspace.dirty())
                                    {
                                        pendingCSharpCodePath = relative;
                                        pendingCSharpCodeCreate = false;
                                        ImGui::OpenPopup("##UnsavedCSharpChanges");
                                    }
                                    else
                                    {
                                        const auto opened = csharpCodeWorkspace.open(activeProjectRoot, relative);
                                        csharpCodeMessage = opened ? tr("Script opened") : opened.error().message;
                                        std::snprintf(csharpCodePathInput.data(), csharpCodePathInput.size(), "%s", encoded.c_str());
                                    }
                                }
                            }
                        }
                        ImGui::EndDisabled();
                        if (!csharpCodeWorkspace.relativeScript().empty())
                        {
                            ImGui::Separator();
                            const auto currentPath = pathUtf8(csharpCodeWorkspace.relativeScript());
                            ImGui::TextUnformatted(currentPath.c_str());
                            if (csharpCodeWorkspace.dirty()) ImGui::SameLine(), ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1), tr("Unsaved"));
                            ImGui::BeginDisabled(csharpBuildRunning || !csharpCodeWorkspace.dirty());
                            if (ImGui::Button(tr("Save script")))
                            {
                                const auto saved = csharpCodeWorkspace.save();
                                csharpCodeMessage = saved ? tr("Script saved") : saved.error().message;
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
#if defined(ASTRO_EDITOR_DOTNET_ROOT)
                            ImGui::BeginDisabled(csharpBuildRunning || csharpMetadataValidationRunning);
                            if (ImGui::Button(csharpBuildRunning ? tr("Building C#...") : tr("Build C# project")))
                            {
                                if (csharpCodeWorkspace.dirty())
                                {
                                    const auto saved = csharpCodeWorkspace.save();
                                    if (!saved) csharpCodeMessage = saved.error().message;
                                }
                                if (!csharpCodeWorkspace.dirty())
                                {
                                    csharpBuildRunning = true;
                                    csharpCodeDiagnostics.clear();
                                    csharpCodeBuildOutput.clear();
                                    csharpBuildFuture = std::async(std::launch::async, [root = activeProjectRoot] {
                                        return buildCSharpProject(root);
                                    });
                                    csharpCodeMessage = tr("C# project build started");
                                }
                            }
                            ImGui::EndDisabled();
#else
                            ImGui::BeginDisabled();
                            ImGui::Button(tr("C# build unavailable in this configuration"));
                            ImGui::EndDisabled();
#endif
                            CodeTextCallbackState codeTextCallback{&csharpCodeWorkspace.mutableText(), csharpCodeJumpLine,
                                csharpCodeJumpLine > 0, false};
                            if (csharpCodeJumpLine > 0) ImGui::SetKeyboardFocusHere();
                            ImGui::BeginDisabled(csharpBuildRunning);
                            if (ImGui::InputTextMultiline("##csharp-source", csharpCodeWorkspace.mutableText().data(),
                                csharpCodeWorkspace.mutableText().size() + 1, ImVec2(-1.0f, 250.0f),
                                ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways,
                                resizeCodeText, &codeTextCallback))
                            {
                                csharpCodeDiagnostics.clear();
                                csharpCodeBuildOutput.clear();
                                csharpCodeMessage = tr("Unsaved");
                            }
                            ImGui::EndDisabled();
                            if (csharpCodeJumpLine > 0 && !codeTextCallback.jumpPending) csharpCodeJumpLine = 0;
                            if (codeTextCallback.sizeLimitHit) csharpCodeMessage = tr("C# editor source size limit is 4 MiB");
                        }
                        if (csharpBuildRunning && csharpBuildFuture.valid()
                            && csharpBuildFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                        {
                            CSharpBuildResult result;
                            try { result = csharpBuildFuture.get(); }
                            catch (const std::exception& error) { result.output = error.what(); }
                            csharpBuildRunning = false;
                            csharpCodeDiagnostics = astro::editor::CSharpCodeWorkspace::parseDiagnostics(result.output);
                            csharpCodeBuildOutput = result.output;
                            if (csharpCodeBuildOutput.size() > 16384)
                                csharpCodeBuildOutput.resize(16384), csharpCodeBuildOutput += "\n[output truncated]";
                            csharpCodeMessage = result.exitCode == 0 ? tr("C# build succeeded") : tr("C# build failed");
                            if (assetRegistry) csharpBuildReport = assetRegistry->inspectScriptBuild();
                        }
                        if (!csharpCodeMessage.empty()) ImGui::TextWrapped(tr("%s"), csharpCodeMessage.c_str());
                        if (!csharpCodeDiagnostics.empty())
                        {
                            ImGui::SeparatorText(tr("Compiler diagnostics"));
                            for (const auto& diagnostic : csharpCodeDiagnostics)
                            {
                                const auto label = diagnostic.sourcePath + "(" + std::to_string(diagnostic.line) + ","
                                    + std::to_string(diagnostic.column) + "): " + diagnostic.severity + " "
                                    + diagnostic.code + ": " + diagnostic.message;
                                if (ImGui::Selectable(label.c_str()))
                                {
                                    auto relative = pathFromUtf8(diagnostic.sourcePath.c_str());
                                    if (relative.begin() != relative.end() && *relative.begin() == "Scripts")
                                        relative = relative.lexically_relative("Scripts");
                                    if (csharpCodeWorkspace.dirty())
                                    {
                                        pendingCSharpCodePath = relative;
                                        pendingCSharpCodeCreate = false;
                                        ImGui::OpenPopup("##UnsavedCSharpChanges");
                                    }
                                    else
                                    {
                                        const auto opened = csharpCodeWorkspace.open(activeProjectRoot, relative);
                                        csharpCodeMessage = opened ? tr("Script opened") : opened.error().message;
                                        if (opened) csharpCodeJumpLine = std::max(1, diagnostic.line);
                                    }
                                }
                            }
                        }
                        if (!csharpCodeBuildOutput.empty())
                        {
                            ImGui::SeparatorText(tr("Build output"));
                            ImGui::BeginChild("##csharp-build-output", ImVec2(-1.0f, 100.0f), ImGuiChildFlags_Borders);
                            ImGui::TextWrapped(tr("%s"), csharpCodeBuildOutput.c_str());
                            ImGui::EndChild();
                        }
                    }
                    if (ImGui::BeginPopupModal("##UnsavedCSharpChanges", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::TextWrapped(tr("Save changes before switching C# scripts?"));
                        if (ImGui::Button(tr("Save and continue")))
                        {
                            const auto saved = csharpCodeWorkspace.save();
                            if (saved && pendingCSharpCodePath)
                            {
                                const auto result = pendingCSharpCodeCreate
                                    ? csharpCodeWorkspace.create(activeProjectRoot, *pendingCSharpCodePath)
                                    : csharpCodeWorkspace.open(activeProjectRoot, *pendingCSharpCodePath);
                                csharpCodeMessage = result ? tr(pendingCSharpCodeCreate ? "Script created" : "Script opened")
                                    : result.error().message;
                                if (result)
                                {
                                    const auto relativeText = pathUtf8(*pendingCSharpCodePath);
                                    std::snprintf(csharpCodePathInput.data(), csharpCodePathInput.size(), "%s", relativeText.c_str());
                                }
                            }
                            else csharpCodeMessage = saved.error().message;
                            pendingCSharpCodePath.reset();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(tr("Discard and continue")))
                        {
                            if (pendingCSharpCodePath)
                            {
                                const auto result = pendingCSharpCodeCreate
                                    ? csharpCodeWorkspace.create(activeProjectRoot, *pendingCSharpCodePath)
                                    : csharpCodeWorkspace.open(activeProjectRoot, *pendingCSharpCodePath);
                                csharpCodeMessage = result ? tr(pendingCSharpCodeCreate ? "Script created" : "Script opened")
                                    : result.error().message;
                                if (result)
                                {
                                    const auto relativeText = pathUtf8(*pendingCSharpCodePath);
                                    std::snprintf(csharpCodePathInput.data(), csharpCodePathInput.size(), "%s", relativeText.c_str());
                                }
                            }
                            pendingCSharpCodePath.reset();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(tr("Cancel"))) { pendingCSharpCodePath.reset(); ImGui::CloseCurrentPopup(); }
                        ImGui::EndPopup();
                    }
                }
                ImGui::End();
            }
            if (ImGui::Begin(tr("Hierarchy")))
            {
                docked = ImGui::IsWindowDocked();
                ImGui::BeginDisabled(activeProjectRoot.empty());
                if (ImGui::Button(tr("+ Entity")))
                {
                    auto created = scene.createEntity("Entity " + std::to_string(nextEntityName++));
                    if (created)
                    {
                        selectedEntity = created.value();
                        sceneDirty = true;
                        commands.clearHistory();
                        message = tr("Entity created");
                    }
                    else reportError(created.error().message);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(activeProjectRoot.empty() || !selectedEntity || !scene.find(*selectedEntity));
                if (ImGui::Button(tr("+ Child")))
                {
                    auto created = scene.createEntity("Entity " + std::to_string(nextEntityName++), selectedEntity);
                    if (created)
                    {
                        selectedEntity = created.value();
                        sceneDirty = true;
                        commands.clearHistory();
                        message = tr("Child entity created");
                    }
                    else reportError(created.error().message);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(activeProjectRoot.empty() || !selectedEntity || !scene.find(*selectedEntity));
                if (ImGui::Button(tr("Delete subtree")))
                {
                    auto erased = scene.erase(*selectedEntity, true);
                    if (erased)
                    {
                        selectedEntity.reset();
                        inspectorDraftEntity.reset();
                        sceneDirty = true;
                        commands.clearHistory();
                        message = tr("Entity subtree deleted");
                    }
                    else reportError(erased.error().message);
                }
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::TextUnformatted(scene.name().c_str());
                ImGui::BeginChild("EntityTree", ImVec2(0, 0), false);
                drawHierarchyBranch(scene, std::nullopt, selectedEntity);
                ImGui::EndChild();
            }
            ImGui::End();
            if (ImGui::Begin(tr("Inspector")))
            {
                docked = docked && ImGui::IsWindowDocked();
                if (activeProjectRoot.empty()) ImGui::TextDisabled(tr("Create or open a project first."));
                else drawInspector(scene, selectedEntity, commands, inspectorDraft, inspectorDraftEntity,
                    inspectorDraftScene, sceneDirty, message, assetRegistry ? &*assetRegistry : nullptr,
                    csharpBuildReport, selectedScriptAsset, scriptAuthoringPanelDrawn, spriteTextures, reportError);
            }
            ImGui::End();
            if (ImGui::Begin(tr("Scene View")))
            {
                docked = docked && ImGui::IsWindowDocked();
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
                if (ImGui::Button(tr("Frame All"))) sceneTarget.requestFrameAll();
                ImGui::SameLine();
                if (ImGui::Button(tr("Reload 3D meshes"))) sceneTarget.invalidate3dCache();
#endif
                if (ImGui::BeginTabBar("SceneViewTabs"))
                {
                    if (ImGui::BeginTabItem(tr("Rendered")))
                    {
                        const auto available = ImGui::GetContentRegionAvail();
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
                        const auto viewportMin = ImGui::GetCursorScreenPos();
                        const ImVec2 viewportMax{viewportMin.x + available.x, viewportMin.y + available.y};
                        if (available.x > 2.0f && available.y > 2.0f
                            && ImGui::IsMouseHoveringRect(viewportMin, viewportMax, true))
                        {
                            const auto& io = ImGui::GetIO();
                            if (io.MouseDown[ImGuiMouseButton_Right])
                                sceneTarget.orbit3d(io.MouseDelta.x, io.MouseDelta.y);
                            if (io.MouseDown[ImGuiMouseButton_Middle])
                                sceneTarget.pan3d(io.MouseDelta.x, io.MouseDelta.y, available.y);
                            if (io.MouseWheel != 0.0f) sceneTarget.zoom3d(io.MouseWheel);
                        }
#endif
                        bgfx::TextureHandle renderedTexture = BGFX_INVALID_HANDLE;
                        std::string renderError;
                        if (assetRegistry && available.x > 2.0f && available.y > 2.0f
                            && sceneTarget.render(scene, &*assetRegistry, spriteTextures,
                                static_cast<int>(available.x), static_cast<int>(available.y),
                                program, sampler, outputMode, layout, renderedTexture, renderError,
                                benchmark ? &editorSceneTiming : nullptr))
                        {
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
                            if (smoke)
                            {
                                const auto& io = ImGui::GetIO();
                                const auto* mainViewport = ImGui::GetMainViewport();
                                if (mainViewport && io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
                                {
                                    sceneViewRectNormalized = {
                                        (viewportMin.x - mainViewport->Pos.x) / io.DisplaySize.x,
                                        (viewportMin.y - mainViewport->Pos.y) / io.DisplaySize.y,
                                        (viewportMax.x - mainViewport->Pos.x) / io.DisplaySize.x,
                                        (viewportMax.y - mainViewport->Pos.y) / io.DisplaySize.y};
                                    sceneViewRectValid = true;
                                }
                            }
#endif
                            ImGui::Image(static_cast<ImTextureID>(renderedTexture.idx + 1), available,
                                framebufferUv0(), framebufferUv1());
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip(tr("Right-drag: orbit; middle-drag: pan; wheel: zoom. Frame All fits visible 3D meshes."));
#endif
                            ++sceneViewFrames;
                        }
                        else ImGui::TextDisabled(tr("%s"), assetRegistry ? renderError.c_str()
                            : "Open a project to render its SpriteRenderer assets.");
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
                        if (!sceneTarget.meshError().empty())
                            ImGui::TextDisabled(tr("3D preview: %s"), sceneTarget.meshError().c_str());
#endif
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem(tr("Hierarchy map")))
                    {
                        drawSceneView(scene, selectedEntity);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
            if (showGameView)
            {
                if (!smoke && !benchmark)
                    ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
                if (ImGui::Begin(tr("Game View"), &showGameView))
                {
                const auto state = playSession.state();
                if (state == astro::runtime::RuntimeState::Faulted)
                {
                    stopPlaySession();
                }
                if (state == astro::runtime::RuntimeState::Stopped || state == astro::runtime::RuntimeState::Faulted)
                {
                    if (ImGui::Button(tr("Play")))
                    {
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
                        gameViewPuzzle.reset();
                        gameViewPuzzleCampaign.reset();
                        activePuzzleLevelId.reset();
                        puzzleCampaignMessage.clear();
                        selectedPuzzleCell.reset();
                        pendingPuzzleSwap.reset();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                        gameViewPuzzleSaveStore.reset();
                        gameViewPuzzleSaveProfile.reset();
                        gameViewPuzzleSaveDirectory.clear();
                        gameViewPuzzleCampaignSaveStore.reset();
                        gameViewPuzzleCampaignSaveProfile.reset();
                        gameViewPuzzleCampaignSaveDirectory.clear();
#endif
#endif
                        csharpPlayApproval.revoke();
                        pendingCSharpApproval.reset();
                        bool hasEnabledScripts = false;
                        for (const auto& entity : scene.entities())
                            for (const auto& behavior : entity.scriptBehaviors)
                                hasEnabledScripts = hasEnabledScripts || behavior.enabled;
                        if (!hasEnabledScripts)
                        {
                            auto started = playSession.play(scene,
                                [](Scene&, double) { return astro::core::Result<void>::success(); });
                            if (!started) reportError(started.error().message);
                        }
                        else
                        {
#if defined(ASTRO_EDITOR_HAS_CSHARP_RUNTIME)
                        if (!assetRegistry)
                            reportError(tr("Open a project and register a current C# build before Play approval."));
                        else
                        {
                            auto captured = astro::editor::CSharpPlayApproval::capture(scene, currentScenePath, *assetRegistry);
                            if (!captured) reportError(captured.error().message);
                            else
                            {
                                pendingCSharpApproval = std::move(captured).value();
                                csharpApprovalDiagnostic.clear();
                                ImGui::OpenPopup(tr("C# Play approval###CSharpPlayApproval"));
                            }
                        }
#else
                        reportError(tr("This Editor build has no C# runtime support."));
#endif
                        }
                    }
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
                    ImGui::SameLine();
                    if (ImGui::Button(tr("Play Puzzle")))
                    {
                        const auto started = startPuzzleGame();
                        if (!started) reportError(started.error().message);
                    }
                    ImGui::InputText(tr("Campaign ID"), puzzleCampaignIdInput.data(), puzzleCampaignIdInput.size());
                    ImGui::SameLine();
                    ImGui::BeginDisabled(activeProjectRoot.empty() || puzzleCampaignIdInput[0] == '\0');
                    if (ImGui::Button(tr("Play Campaign")))
                    {
                        const auto started = startPuzzleCampaign();
                        if (!started) { puzzleCampaignMessage = started.error().message; reportError(started.error().message); }
                    }
                    ImGui::EndDisabled();
                    if (activeProjectRoot.empty()) ImGui::TextDisabled(tr("Open a project to launch an authored campaign."));
                    else ImGui::TextDisabled(tr("Campaign assets: <project>/Puzzle/Campaigns and Puzzle/Levels."));
                    if (!puzzleCampaignMessage.empty()) ImGui::TextWrapped(tr("%s"), puzzleCampaignMessage.c_str());
#endif
                }
                else
                {
                    if (state == astro::runtime::RuntimeState::Playing)
                    {
                        if (ImGui::Button(tr("Pause")))
                        {
                            const auto paused = playSession.pause();
                            if (!paused) reportError(paused.error().message);
                        }
                    }
                    else if (state == astro::runtime::RuntimeState::Paused)
                    {
                        if (ImGui::Button(tr("Resume")))
                        {
                            const auto resumed = playSession.resume();
                            if (!resumed) reportError(resumed.error().message);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(tr("Step")))
                        {
                            const auto stepped = playSession.stepOnce();
                            if (!stepped) reportError(stepped.error().message);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(tr("Stop")))
                    {
                        stopPlaySession();
                        csharpPlayApproval.revoke();
                        pendingCSharpApproval.reset();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
                        gameViewPuzzle.reset();
                        gameViewPuzzleCampaign.reset();
                        activePuzzleLevelId.reset();
                        puzzleCampaignMessage.clear();
                        selectedPuzzleCell.reset();
                        pendingPuzzleSwap.reset();
                        lastPuzzleMove.reset();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                        gameViewPuzzleSaveStore.reset();
                        gameViewPuzzleSaveProfile.reset();
                        gameViewPuzzleSaveDirectory.clear();
                        gameViewPuzzleCampaignSaveStore.reset();
                        gameViewPuzzleCampaignSaveProfile.reset();
                        gameViewPuzzleCampaignSaveDirectory.clear();
                        gameViewPuzzleCampaignSaveMessage.clear();
#endif
#endif
                    }
                    ImGui::SameLine();
                    ImGui::Text(tr("%s  %.2f s"), tr(state == astro::runtime::RuntimeState::Playing ? "Playing" : "Paused"),
                        playSession.simulationTimeSeconds());
                }
                if (ImGui::BeginPopupModal(tr("C# Play approval###CSharpPlayApproval"), nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize))
                {
                    if (pendingCSharpApproval)
                    {
                        const auto& snapshot = *pendingCSharpApproval;
                        const auto projectText = pathUtf8(snapshot.projectRoot);
                        const auto sceneFileText = pathUtf8(snapshot.scenePath);
                        ImGui::TextWrapped(tr("Project: %s"), projectText.c_str());
                        ImGui::TextWrapped(tr("Scene: %s (ID %llu)"), snapshot.sceneName.c_str(),
                            static_cast<unsigned long long>(snapshot.sceneId));
                        if (!sceneFileText.empty()) ImGui::TextWrapped(tr("Scene file: %s"), sceneFileText.c_str());
                        ImGui::TextWrapped(tr("Build fingerprint: %s"), snapshot.buildFingerprint.c_str());
                        ImGui::TextWrapped(tr("SDK: %s"), snapshot.sdkVersion.c_str());
                        ImGui::TextWrapped(tr("Assembly: %s"), snapshot.assemblyRelativePath.c_str());
                        ImGui::TextWrapped(tr("Assembly SHA-256: %s"), snapshot.assemblySha256.c_str());
                        ImGui::TextWrapped(tr("Runtime config: %s"), snapshot.runtimeConfigRelativePath.c_str());
                        ImGui::TextWrapped(tr("Runtime config SHA-256: %s"), snapshot.runtimeConfigSha256.c_str());
                        ImGui::Separator();
                        ImGui::Text(tr("Enabled ScriptBehavior entries: %llu"),
                            static_cast<unsigned long long>(snapshot.scripts.size()));
                        for (const auto& script : snapshot.scripts)
                        {
                            ImGui::TextWrapped(tr("Entity %llu, component %llu, AssetId %llu"),
                                static_cast<unsigned long long>(script.entity),
                                static_cast<unsigned long long>(script.component),
                                static_cast<unsigned long long>(script.asset.value));
                            ImGui::TextWrapped(tr("Source: %s"), script.sourceRelativePath.c_str());
                            ImGui::TextWrapped(tr("Managed type: %s"), script.managedTypeName.c_str());
                            ImGui::TextWrapped(tr("Source SHA-256: %s"), script.sourceSha256.c_str());
                        }
                        ImGui::Separator();
                        ImGui::TextWrapped(tr("C# scripts run in-process with current user privileges; they are not sandboxed."));
                        if (!csharpApprovalDiagnostic.empty())
                            ImGui::TextWrapped(tr("Approval check failed: %s"), csharpApprovalDiagnostic.c_str());
                        if (ImGui::Button(tr("Allow for this Play session")))
                        {
                            auto current = assetRegistry
                                ? astro::editor::CSharpPlayApproval::capture(scene, currentScenePath, *assetRegistry)
                                : astro::core::Result<astro::editor::CSharpPlayApprovalSnapshot>::failure(
                                    {astro::core::ErrorCode::InvalidState, "The project is no longer open"});
                            if (!current)
                            {
                                csharpApprovalDiagnostic = current.error().message;
                                reportError(current.error().message);
                            }
                            else
                            {
                                auto approved = csharpPlayApproval.approve(snapshot, current.value());
                                if (!approved)
                                {
                                    csharpApprovalDiagnostic = approved.error().message;
                                    reportError(approved.error().message);
                                }
                                else
                                {
#if defined(ASTRO_EDITOR_HAS_CSHARP_RUNTIME)
                                    auto recheckApproval = [&](const astro::assets::AssetId) -> astro::core::Result<void> {
                                        if (!assetRegistry)
                                            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                                                "The project is no longer open"});
                                        auto latest = astro::editor::CSharpPlayApproval::capture(
                                            scene, currentScenePath, *assetRegistry);
                                        if (!latest)
                                        {
                                            csharpPlayApproval.revoke();
                                            return astro::core::Result<void>::failure(latest.error());
                                        }
                                        if (!csharpPlayApproval.isApprovedFor(latest.value()))
                                        {
                                            csharpPlayApproval.revoke();
                                            return astro::core::Result<void>::failure({astro::core::ErrorCode::ValidationError,
                                                "The approved C# script/build snapshot changed before host load"});
                                        }
                                        return astro::core::Result<void>::success();
                                    };
                                    auto loadedScripts = astro::runtime::CSharpScriptRuntime::load(
                                        std::filesystem::path(ASTRO_EDITOR_DOTNET_ROOT), *assetRegistry,
                                        scene, recheckApproval);
                                    if (!loadedScripts)
                                    {
                                        csharpPlayApproval.revoke();
                                        csharpApprovalDiagnostic = loadedScripts.error().message;
                                        reportError(loadedScripts.error().message);
                                    }
                                    else
                                    {
                                        csharpScriptRuntime = std::move(loadedScripts).value();
                                        auto* const scripts = csharpScriptRuntime.get();
                                        auto started = playSession.playWithLifecycle(scene,
                                            [scripts](Scene& runtime) { return scripts->onCreate(runtime); },
                                            [scripts](Scene& runtime, const double delta) {
                                                return scripts->fixedUpdate(runtime, delta);
                                            },
                                            [scripts](Scene& runtime) { return scripts->onDestroy(runtime); });
                                        if (!started)
                                        {
                                            csharpPlayApproval.revoke();
                                            csharpApprovalDiagnostic = started.error().message;
                                            reportError(started.error().message);
                                            csharpScriptRuntime.reset();
                                        }
                                        else csharpApprovalDiagnostic.clear();
                                    }
#endif
                                    if (playSession.state() == astro::runtime::RuntimeState::Playing)
                                    {
                                        pendingCSharpApproval.reset();
                                        ImGui::CloseCurrentPopup();
                                    }
                                }
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(tr("Cancel")))
                        {
                            csharpPlayApproval.revoke();
                            pendingCSharpApproval.reset();
                            csharpApprovalDiagnostic.clear();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    else ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                const auto* gameScene = playSession.runtimeScene();
                const auto& displayedScene = gameScene ? *gameScene : scene;
                if (csharpPlayApproval.approved() && playSession.state() != astro::runtime::RuntimeState::Stopped)
                    ImGui::TextWrapped(tr("Approved C# callbacks are active in this Play session."));
                const auto available = ImGui::GetContentRegionAvail();
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
                if (gameViewPuzzleCampaign)
                {
                    const auto& campaign = *gameViewPuzzleCampaign;
                    ImGui::Text(tr("Campaign: %s   Level: %s   Score: %llu / %llu"),
                        campaign.campaign().name.c_str(), campaign.currentLevel().id.c_str(),
                        static_cast<unsigned long long>(campaign.game().score()),
                        static_cast<unsigned long long>(campaign.currentLevel().targetScore));
                    if (!puzzleCampaignMessage.empty()) ImGui::TextWrapped(tr("%s"), puzzleCampaignMessage.c_str());
                    if (campaign.state() == astro::runtime::PuzzleCampaignSessionState::Ended)
                    {
                        ImGui::TextDisabled(tr("Campaign ended. The final board remains visible until restart or Stop."));
                        if (ImGui::Button(tr("Restart campaign")))
                        {
                            const auto restarted = startPuzzleCampaign();
                            if (!restarted) { puzzleCampaignMessage = restarted.error().message; reportError(restarted.error().message); }
                        }
                    }
                    else if (lastPuzzleMove && lastPuzzleMove->levelCompleted)
                    {
                        ImGui::TextDisabled(tr("Level goal reached. Continue when ready; the authored transition rule will be applied."));
                        if (ImGui::Button(tr("Continue campaign")))
                        {
                            const auto advanced = advancePuzzleCampaign();
                            if (!advanced) { puzzleCampaignMessage = advanced.error().message; reportError(advanced.error().message); }
                        }
                    }
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                    drawPuzzleSaveSettingsPanel();
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputInt(tr("Campaign save slot"), &gameViewPuzzleCampaignSaveSlot);
                    gameViewPuzzleCampaignSaveSlot = std::clamp(gameViewPuzzleCampaignSaveSlot, 0, 9);
                    ImGui::BeginDisabled(activeProjectRoot.empty() || !puzzleSaveSettingsReady
                        || puzzleSaveSettingsDirty || !gameViewPuzzleCampaign);
                    if (ImGui::Button(tr("Save progress")))
                    {
                        const auto saved = savePuzzleCampaignGameViewSlot();
                        if (!saved)
                        {
                            gameViewPuzzleCampaignSaveMessage = saved.error().message;
                            reportError(saved.error().message);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(tr("Load progress")))
                    {
                        const auto loaded = loadPuzzleCampaignGameViewSlot();
                        if (!loaded)
                        {
                            gameViewPuzzleCampaignSaveMessage = loaded.error().message;
                            reportError(loaded.error().message);
                        }
                    }
                    ImGui::EndDisabled();
                    if (gameViewPuzzleCampaignSaveStore)
                    {
                        const auto directory = gameViewPuzzleCampaignSaveStore->directory().u8string();
                        ImGui::TextDisabled(tr("Campaign save folder: %s"),
                            reinterpret_cast<const char*>(directory.c_str()));
                    }
                    if (!gameViewPuzzleCampaignSaveMessage.empty())
                        ImGui::TextWrapped(tr("%s"), gameViewPuzzleCampaignSaveMessage.c_str());
                    ImGui::TextDisabled(tr("Campaign progress is stored in user data and changes only when you save or load a slot."));
#else
                    ImGui::TextDisabled(tr("Campaign progress saves are unavailable in this Editor build."));
#endif
                    if (drawPuzzleGameView(campaign.game(), selectedPuzzleCell, pendingPuzzleSwap, lastPuzzleMove))
                        ++puzzleBoardRenderedFrames;
                    ++gameViewFrames;
                    ++puzzleBoardFrames;
                    ++puzzleCampaignBoardFrames;
                }
                else if (gameViewPuzzle)
                {
                    if (smoke) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    if (ImGui::CollapsingHeader(tr("Puzzle Level Authoring")))
                    {
                        puzzleLevelAuthoringDrawn = true;
                        puzzleLevelCatalogDrawn = true;
                        if (activeProjectRoot != puzzleLevelCatalogProjectRoot)
                        {
                            const auto refreshed = refreshPuzzleLevelCatalog();
                            if (!refreshed) puzzleLevelMessage = refreshed.error().message;
                        }
                        if (puzzleLevelIdInput[0] == '\0')
                        {
                            std::copy_n(puzzleLevelDraft.id.data(), puzzleLevelDraft.id.size(), puzzleLevelIdInput.data());
                            std::copy_n(puzzleLevelDraft.name.data(), puzzleLevelDraft.name.size(), puzzleLevelNameInput.data());
                        }
                        ImGui::TextUnformatted(tr("Project level catalog"));
                        if (ImGui::SmallButton(tr("Refresh catalog")))
                        {
                            const auto refreshed = refreshPuzzleLevelCatalog();
                            if (!refreshed) { puzzleLevelMessage = refreshed.error().message; reportError(refreshed.error().message); }
                        }
                        if (ImGui::BeginChild("PuzzleLevelCatalog", ImVec2(0.0f, 105.0f), true))
                        {
                            for (const auto& entry : puzzleLevelCatalog)
                            {
                                if (!entry.readable())
                                {
                                    const auto diagnostic = astro::editor::translateEditorDiagnostic(entry.diagnostic, gEditorLanguage);
                                    ImGui::TextDisabled(tr("%s — invalid: %s"), entry.id.c_str(), diagnostic.c_str());
                                    continue;
                                }
                                const auto label = entry.id + " — " + *entry.name;
                                if (ImGui::Selectable(label.c_str(), selectedPuzzleCatalogId == entry.id))
                                    selectedPuzzleCatalogId = entry.id;
                            }
                            if (puzzleLevelCatalog.empty()) ImGui::TextDisabled(tr("No saved levels yet."));
                        }
                        ImGui::EndChild();
                        ImGui::BeginDisabled(activeProjectRoot.empty() || selectedPuzzleCatalogId.empty());
                        if (ImGui::Button(tr("Open selected")))
                        {
                            puzzleLevelDraft.id = puzzleLevelIdInput.data();
                            puzzleLevelDraft.name = puzzleLevelNameInput.data();
                            if (puzzleLevelDraftIsDirty())
                            {
                                pendingPuzzleLevelOpen = selectedPuzzleCatalogId;
                                pendingPuzzleLevelReset = false;
                                ImGui::OpenPopup(tr("Discard unsaved Puzzle level changes?"));
                            }
                            else
                            {
                                const auto opened = loadPuzzleLevelIntoDraft(selectedPuzzleCatalogId);
                                if (!opened) { puzzleLevelMessage = opened.error().message; reportError(opened.error().message); }
                            }
                        }
                        ImGui::EndDisabled();
                        ImGui::SetNextItemWidth(150.0f);
                        ImGui::InputText(tr("New level ID"), puzzleLevelNewIdInput.data(), puzzleLevelNewIdInput.size());
                        ImGui::SetNextItemWidth(190.0f);
                        ImGui::InputText(tr("Duplicate name"), puzzleLevelDuplicateNameInput.data(),
                            puzzleLevelDuplicateNameInput.size());
                        ImGui::BeginDisabled(activeProjectRoot.empty() || selectedPuzzleCatalogId.empty());
                        if (ImGui::Button(tr("Duplicate selected")))
                        {
                            const auto duplicated = astro::runtime::PuzzleLevelFile::duplicate(activeProjectRoot,
                                selectedPuzzleCatalogId, puzzleLevelNewIdInput.data(), puzzleLevelDuplicateNameInput.data());
                            if (!duplicated) { puzzleLevelMessage = duplicated.error().message; reportError(duplicated.error().message); }
                            else
                            {
                                selectedPuzzleCatalogId = puzzleLevelNewIdInput.data();
                                const auto refreshed = refreshPuzzleLevelCatalog();
                                puzzleLevelMessage = refreshed
                                    ? "Duplicated level as " + selectedPuzzleCatalogId : refreshed.error().message;
                                if (!refreshed) reportError(refreshed.error().message);
                            }
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        const bool canRename = puzzleLevelSavedBaseline && !puzzleLevelDraftIsDirty()
                            && !activeProjectRoot.empty() && puzzleLevelNewIdInput[0] != '\0'
                            && std::string_view(puzzleLevelNewIdInput.data()) != puzzleLevelSavedBaseline->id;
                        ImGui::BeginDisabled(!canRename);
                        if (ImGui::Button(tr("Rename current level")))
                        {
                            ImGui::OpenPopup(tr("Confirm Puzzle level ID rename"));
                        }
                        ImGui::EndDisabled();
                        if (ImGui::BeginPopupModal(tr("Discard unsaved Puzzle level changes?"), nullptr,
                            ImGuiWindowFlags_AlwaysAutoResize))
                        {
                            ImGui::TextWrapped(tr("Open another level or start a new draft and discard the current unsaved edits?"));
                            if (ImGui::Button(tr("Discard and continue")))
                            {
                                if (pendingPuzzleLevelOpen)
                                {
                                    const auto opened = loadPuzzleLevelIntoDraft(*pendingPuzzleLevelOpen);
                                    if (!opened) { puzzleLevelMessage = opened.error().message; reportError(opened.error().message); }
                                }
                                else if (pendingPuzzleLevelReset) resetPuzzleLevelDraft();
                                pendingPuzzleLevelOpen.reset();
                                pendingPuzzleLevelReset = false;
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(tr("Cancel")))
                            {
                                pendingPuzzleLevelOpen.reset();
                                pendingPuzzleLevelReset = false;
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                        if (ImGui::BeginPopupModal(tr("Confirm Puzzle level ID rename"), nullptr,
                            ImGuiWindowFlags_AlwaysAutoResize))
                        {
                            ImGui::TextWrapped(tr("Rename %s to %s? This removes the old level file after publishing the new ID. Referencing or unreadable campaign files block the rename; update and save campaign links first. Player progress stays under the old ID and is not moved."),
                                puzzleLevelSavedBaseline ? puzzleLevelSavedBaseline->id.c_str() : "",
                                puzzleLevelNewIdInput.data());
                            if (ImGui::Button(tr("Rename")))
                            {
                                if (puzzleLevelSavedBaseline)
                                {
                                    const auto oldId = puzzleLevelSavedBaseline->id;
                                    const auto renamed = astro::runtime::PuzzleLevelFile::rename(activeProjectRoot,
                                        oldId, puzzleLevelNewIdInput.data());
                                    if (!renamed) { puzzleLevelMessage = renamed.error().message; reportError(renamed.error().message); }
                                    else
                                    {
                                        puzzleLevelDraft.id = puzzleLevelNewIdInput.data();
                                        puzzleLevelSavedBaseline = puzzleLevelDraft;
                                        puzzleLevelIdInput.fill('\0');
                                        std::copy_n(puzzleLevelDraft.id.data(), puzzleLevelDraft.id.size(),
                                            puzzleLevelIdInput.data());
                                        selectedPuzzleCatalogId = puzzleLevelDraft.id;
                                        if (activePuzzleLevelId == oldId)
                                        {
                                            activePuzzleLevelId = puzzleLevelDraft.id;
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                                            gameViewPuzzleSaveStore.reset();
                                            gameViewPuzzleSaveProfile.reset();
                                            gameViewPuzzleSaveDirectory.clear();
#endif
                                        }
                                        const auto refreshed = refreshPuzzleLevelCatalog();
                                        puzzleLevelMessage = refreshed ? "Renamed level to " + puzzleLevelDraft.id
                                            : refreshed.error().message;
                                        if (!refreshed) reportError(refreshed.error().message);
                                    }
                                }
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(tr("Cancel rename")))
                            {
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::InputText(tr("Level ID"), puzzleLevelIdInput.data(), puzzleLevelIdInput.size());
                        ImGui::InputText(tr("Level name"), puzzleLevelNameInput.data(), puzzleLevelNameInput.size());
                        puzzleLevelDraft.id = puzzleLevelIdInput.data();
                        puzzleLevelDraft.name = puzzleLevelNameInput.data();
                        int widthInput = static_cast<int>(puzzleLevelDraft.width);
                        int heightInput = static_cast<int>(puzzleLevelDraft.height);
                        int kindsInput = static_cast<int>(puzzleLevelDraft.tileKinds);
                        int seedInput = static_cast<int>(puzzleLevelDraft.seed);
                        int targetInput = static_cast<int>(std::min<std::uint64_t>(puzzleLevelDraft.targetScore,
                            static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
                        bool resizeBoard = ImGui::InputInt(tr("Width"), &widthInput) | ImGui::InputInt(tr("Height"), &heightInput);
                        widthInput = std::clamp(widthInput, 3, 16);
                        heightInput = std::clamp(heightInput, 3, 16);
                        if (resizeBoard && (static_cast<std::size_t>(widthInput) != puzzleLevelDraft.width
                            || static_cast<std::size_t>(heightInput) != puzzleLevelDraft.height))
                        {
                            std::vector<std::uint8_t> resized(static_cast<std::size_t>(widthInput * heightInput), 0);
                            for (std::size_t row = 0; row < std::min<std::size_t>(heightInput, puzzleLevelDraft.height); ++row)
                                for (std::size_t column = 0; column < std::min<std::size_t>(widthInput, puzzleLevelDraft.width); ++column)
                                    resized[row * static_cast<std::size_t>(widthInput) + column]
                                        = puzzleLevelDraft.initialTiles[row * puzzleLevelDraft.width + column];
                            puzzleLevelDraft.width = static_cast<std::size_t>(widthInput);
                            puzzleLevelDraft.height = static_cast<std::size_t>(heightInput);
                            puzzleLevelDraft.initialTiles = std::move(resized);
                        }
                        if (ImGui::InputInt(tr("Tile kinds"), &kindsInput))
                        {
                            puzzleLevelDraft.tileKinds = static_cast<std::size_t>(std::clamp(kindsInput, 3, 8));
                            for (auto& tile : puzzleLevelDraft.initialTiles)
                                tile = static_cast<std::uint8_t>(tile % puzzleLevelDraft.tileKinds);
                        }
                        if (ImGui::InputInt(tr("Seed"), &seedInput))
                            puzzleLevelDraft.seed = static_cast<std::uint32_t>(std::max(seedInput, 0));
                        if (ImGui::InputInt(tr("Target score"), &targetInput))
                            puzzleLevelDraft.targetScore = static_cast<std::uint64_t>(std::max(targetInput, 1));
                        ImGui::TextDisabled(tr("Click a cell to cycle its tile ID. Grid edits affect the draft only."));
                        if (ImGui::BeginTable("PuzzleLevelBoard", static_cast<int>(puzzleLevelDraft.width),
                            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
                        {
                            for (std::size_t row = 0; row < puzzleLevelDraft.height; ++row)
                            {
                                ImGui::TableNextRow();
                                for (std::size_t column = 0; column < puzzleLevelDraft.width; ++column)
                                {
                                    ImGui::TableSetColumnIndex(static_cast<int>(column));
                                    const auto cell = row * puzzleLevelDraft.width + column;
                                    ImGui::PushID(static_cast<int>(cell));
                                    const auto label = std::to_string(puzzleLevelDraft.initialTiles[cell]);
                                    if (ImGui::SmallButton(label.c_str()))
                                        puzzleLevelDraft.initialTiles[cell] = static_cast<std::uint8_t>(
                                            (puzzleLevelDraft.initialTiles[cell] + 1) % puzzleLevelDraft.tileKinds);
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndTable();
                        }
                        if (ImGui::Button(tr("New level")))
                        {
                            puzzleLevelDraft.id = puzzleLevelIdInput.data();
                            puzzleLevelDraft.name = puzzleLevelNameInput.data();
                            if (puzzleLevelDraftIsDirty())
                            {
                                pendingPuzzleLevelOpen.reset();
                                pendingPuzzleLevelReset = true;
                                ImGui::OpenPopup(tr("Discard unsaved Puzzle level changes?"));
                            }
                            else resetPuzzleLevelDraft();
                        }
                        ImGui::SameLine();
                        ImGui::BeginDisabled(activeProjectRoot.empty());
                        if (ImGui::Button(tr("Load level")))
                        {
                            puzzleLevelDraft.id = puzzleLevelIdInput.data();
                            puzzleLevelDraft.name = puzzleLevelNameInput.data();
                            if (puzzleLevelDraftIsDirty())
                            {
                                pendingPuzzleLevelOpen = puzzleLevelIdInput.data();
                                pendingPuzzleLevelReset = false;
                                ImGui::OpenPopup(tr("Discard unsaved Puzzle level changes?"));
                            }
                            else
                            {
                                const auto loaded = readPuzzleLevelDraft();
                                if (!loaded) { puzzleLevelMessage = loaded.error().message; reportError(loaded.error().message); }
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(tr("Save level")))
                        {
                            const auto saved = writePuzzleLevelDraft();
                            if (!saved) { puzzleLevelMessage = saved.error().message; reportError(saved.error().message); }
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button(tr("Play authored level")))
                        {
                            const auto started = startPuzzleGame(&puzzleLevelDraft);
                            if (!started) { puzzleLevelMessage = started.error().message; reportError(started.error().message); }
                        }
                        if (!puzzleLevelMessage.empty()) ImGui::TextWrapped(tr("%s"), puzzleLevelMessage.c_str());
                        ImGui::TextDisabled(tr("Files: <project>/Puzzle/Levels/<ID>.astro-puzzle-level (format v1)."));
                        if (activePuzzleLevelId)
                            ImGui::TextDisabled(tr("Running authored level '%s'; progress slots are isolated by level ID."),
                                activePuzzleLevelId->c_str());
                    }
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
                    drawPuzzleSaveSettingsPanel();
                    ImGui::SetNextItemWidth(90.0f);
                    ImGui::InputInt(tr("Puzzle slot"), &gameViewPuzzleSaveSlot);
                    gameViewPuzzleSaveSlot = std::clamp(gameViewPuzzleSaveSlot, 0, 9);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(activeProjectRoot.empty() || !puzzleSaveSettingsReady || puzzleSaveSettingsDirty);
                    if (ImGui::Button(tr("Save progress")))
                    {
                        const auto saved = savePuzzleGameViewSlot();
                        if (!saved) { gameViewPuzzleSaveMessage = saved.error().message; reportError(saved.error().message); }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(tr("Load progress")))
                    {
                        const auto loaded = loadPuzzleGameViewSlot();
                        if (!loaded) { gameViewPuzzleSaveMessage = loaded.error().message; reportError(loaded.error().message); }
                    }
                    ImGui::EndDisabled();
                    if (gameViewPuzzleSaveStore)
                    {
                        const auto directory = gameViewPuzzleSaveStore->directory().u8string();
                        ImGui::TextDisabled(tr("Save folder: %s"), reinterpret_cast<const char*>(directory.c_str()));
                    }
                    if (!gameViewPuzzleSaveMessage.empty()) ImGui::TextWrapped(tr("%s"), gameViewPuzzleSaveMessage.c_str());
#endif
                    if (drawPuzzleGameView(*gameViewPuzzle, selectedPuzzleCell, pendingPuzzleSwap, lastPuzzleMove))
                        ++puzzleBoardRenderedFrames;
                    ++gameViewFrames;
                    ++puzzleBoardFrames;
                }
                else
#endif
                {
                bgfx::TextureHandle renderedTexture = BGFX_INVALID_HANDLE;
                std::string renderError;
                if (assetRegistry && available.x > 2.0f && available.y > 2.0f
                    && gameTarget.render(displayedScene, &*assetRegistry, spriteTextures,
                        static_cast<int>(available.x), static_cast<int>(available.y),
                        program, sampler, outputMode, layout, renderedTexture, renderError,
                        benchmark ? &editorGameTiming : nullptr))
                {
                    ImGui::Image(static_cast<ImTextureID>(renderedTexture.idx + 1), available,
                        framebufferUv0(), framebufferUv1());
                    ++gameViewFrames;
                }
                else ImGui::TextDisabled(tr("%s"), assetRegistry ? renderError.c_str()
                    : "Open a project to preview the game scene.");
                }
                }
                ImGui::End();
            }
            if (showVisualLogic)
                drawVisualLogic(logicGraph, logicDebugger, reportError, visualLogicWindowDrawn);
            if (showUiEditor)
            {
                const auto bindings = uiDocument.refreshBindings(scene);
                if (!bindings && !uiBindingErrorReported)
                {
                    reportError(bindings.error().message);
                    uiBindingErrorReported = true;
                }
                if (bindings) uiBindingErrorReported = false;
                drawUiEditor(uiDocument, scene, selectedUiElement, uiPreviewPreset, uiTextDraft,
                    uiTextDraftElement, reportError, uiEditorWindowDrawn);
            }
            if (showTimeline)
                drawTimelineEditor(timeline, ImGui::GetIO().DeltaTime, reportError, timelineWindowDrawn);
            if (showSaveSystem)
                drawSaveSystemEditor(saveSystemDocument, scene, selectedEntity, reportError, saveSystemWindowDrawn);
            if (showCameraPresets)
                drawCameraPresets(cameraRig, scene, selectedEntity, ImGui::GetIO().DeltaTime,
                    reportError, cameraWindowDrawn);
            if (showDiagnostics)
            {
                ImGui::Begin(tr("Diagnostics"), &showDiagnostics);
                ImGui::TextWrapped(tr("%s"), message.c_str());
                ImGui::Text(tr("Framebuffer: %d x %d"), width, height);
                ImGui::Text(tr("Renderer: %s"), bgfx::getRendererName(bgfx::getRendererType()));
                ImGui::Text(tr("Scene: %s%s"), scene.name().c_str(), sceneDirty ? " * unsaved" : "");
                if (ImGui::CollapsingHeader(tr("Recent diagnostics")))
                {
                    if (diagnostics.empty()) ImGui::TextDisabled(tr("No errors recorded"));
                    const auto start = diagnostics.size() > 8 ? diagnostics.size() - 8 : 0;
                    for (std::size_t index = start; index < diagnostics.size(); ++index)
                        ImGui::BulletText(tr("%s"), diagnostics[index].c_str());
                }
                const auto audit = commands.auditTrail();
                int shownErrors = 0;
                for (auto it = audit.rbegin(); it != audit.rend() && shownErrors < 4; ++it)
                {
                    if (!it->error) continue;
                    const auto diagnostic = astro::editor::translateEditorDiagnostic(it->error->message, gEditorLanguage);
                    ImGui::TextWrapped(tr("%s: %s"), it->commandId.c_str(), diagnostic.c_str());
                    ++shownErrors;
                }
                ImGui::End();
            }
            if (dialog)
            {
                ImGui::Begin(tr(create ? "New project" : "Open project"), &dialog);
                ImGui::InputText(tr(create ? "New directory (UTF-8)" : "project.astro path (UTF-8)"), path, sizeof(path));
                if (create) ImGui::InputText(tr("Project name"), name, sizeof(name));
                if (ImGui::Button(tr(create ? "Create" : "Open")))
                {
                    try
                    {
                        if (create) createProject(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path))), name);
                        else openProject(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path))));
                        dialog = false;
                    }
                    catch (const std::exception& error) { reportError(error.what()); }
                }
                ImGui::TextWrapped(tr("%s"), message.c_str());
                ImGui::End();
            }
            if (sceneDialog)
            {
                ImGui::Begin(tr("Open Scene"), &sceneDialog);
                ImGui::InputText(tr("Scene file path (UTF-8)"), scenePathInput, sizeof(scenePathInput));
                if (ImGui::Button(tr("Open scene")))
                {
                    try
                    {
                        const auto sceneFile = std::filesystem::path(
                            std::u8string(reinterpret_cast<const char8_t*>(scenePathInput)));
                        if (openSceneFile(sceneFile)) sceneDialog = false;
                    }
                    catch (const std::exception& error) { reportError(error.what()); }
                }
                ImGui::TextWrapped(tr("%s"), message.c_str());
                ImGui::End();
            }
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
            drawPuzzleCampaignBrowser();
#endif
            ImGui::Render();
            draw(ImGui::GetDrawData(), width, height);
            const auto editorCpuEnd = std::chrono::steady_clock::now();
            if (smoke && frames == 89)
            {
                const auto captureBase = smokeDirectory / "scene-game-visual";
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, captureBase.string().c_str());
            }
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
            if (smoke && frames == 100)
            {
                stopPlaySession();
                const auto playStarted = startPuzzleGame();
                if (!playStarted || !playSession.runtimeScene() || !gameViewPuzzle)
                    throw std::runtime_error("Puzzle Game View runtime start smoke failed");
                const auto firstMove = firstLegalPuzzleSwap(*gameViewPuzzle);
                if (!firstMove || clickPuzzleCell(firstMove->first, selectedPuzzleCell, pendingPuzzleSwap)
                    || !clickPuzzleCell(firstMove->second, selectedPuzzleCell, pendingPuzzleSwap)
                    || !pendingPuzzleSwap)
                    throw std::runtime_error("Puzzle Game View could not queue a legal tile swap");
                puzzleSmokeReady = true;
            }
#endif
            const auto bgfxFrameCallStart = std::chrono::steady_clock::now();
            bgfx::frame();
            const auto hostFrameEnd = std::chrono::steady_clock::now();
            const auto* frameStats = bgfx::getStats();
            if (benchmark && frames >= benchmarkWarmupFrames)
            {
                const auto milliseconds = [](const auto duration) {
                    return std::chrono::duration<double, std::milli>(duration).count();
                };
                fullFrameWallMs.push_back(milliseconds(hostFrameEnd - hostFrameStart));
                editorCpuMs.push_back(milliseconds(editorCpuEnd - hostFrameStart));
                bgfxFrameCallMs.push_back(milliseconds(hostFrameEnd - bgfxFrameCallStart));
                frameDrawCalls.push_back(frameStats ? double(frameStats->numDraw) : 0.0);
                viewCpuMs.push_back(editorSceneTiming.prepareMs + editorSceneTiming.submitMs
                    + editorGameTiming.prepareMs + editorGameTiming.submitMs);
                if (benchmark3d)
                {
                    scene3dPrepareMs.push_back(editorSceneTiming.meshPrepareMs);
                    scene3dSubmitMs.push_back(editorSceneTiming.meshSubmitMs);
                }
                if (frameStats && frameStats->cpuTimerFreq > 0)
                {
                    if (frameStats->cpuTimeFrame > 0)
                        bgfxCpuFrameMs.push_back(double(frameStats->cpuTimeFrame) * 1000.0
                            / double(frameStats->cpuTimerFreq));
                    if (frameStats->latencyTotal > 0)
                        bgfxLatencyMs.push_back(double(frameStats->latencyTotal) * 1000.0
                            / double(frameStats->cpuTimerFreq));
                    if (frameStats->latencyPresent > 0)
                        presentLatencyMs.push_back(double(frameStats->latencyPresent) * 1000.0
                            / double(frameStats->cpuTimerFreq));
                }
                if (frameStats && frameStats->gpuTimerFreq > 0
                    && frameStats->gpuTimeEnd > frameStats->gpuTimeBegin
                    && frameStats->gpuFrameNum != lastFullFrameGpuNumber)
                {
                    fullFrameGpuMs.push_back(double(frameStats->gpuTimeEnd - frameStats->gpuTimeBegin)
                        * 1000.0 / double(frameStats->gpuTimerFreq));
                    lastFullFrameGpuNumber = frameStats->gpuFrameNum;
                }
            }
            ++frames;
            if (benchmark && frames == benchmarkWarmupFrames + benchmarkMeasuredFrames)
            {
                const auto percentile = [](std::vector<double> values, const double fraction) {
                    if (values.empty()) return 0.0;
                    std::sort(values.begin(), values.end());
                    return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
                };
                const auto report = [&](const char* label, const std::vector<double>& values) {
                    if (values.empty()) std::cout << label << "=unavailable";
                    else std::cout << label << "_p50_ms=" << percentile(values, 0.50)
                        << ' ' << label << "_p95_ms=" << percentile(values, 0.95);
                };
                const auto spriteCount = std::count_if(scene.entities().begin(), scene.entities().end(),
                    [](const Entity& entity) { return entity.spriteRenderer && entity.spriteRenderer->visible; });
                std::cout << "[editor-frame-benchmark] scene=" << benchmarkSceneNames[activeBenchmarkScene]
                    << " sprites=" << spriteCount << " entities=" << scene.entities().size()
                    << " textures=" << benchmarkSpriteAssets.size() << " measured_frames=" << fullFrameWallMs.size() << ' ';
                report("app_frame_wall", fullFrameWallMs); std::cout << ' ';
                report("editor_cpu_before_bgfx_frame", editorCpuMs); std::cout << ' ';
                report("scene_game_view_cpu_sum", viewCpuMs); std::cout << ' ';
                std::vector<double> otherEditorCpuMs;
                for (std::size_t sample = 0; sample < editorCpuMs.size(); ++sample)
                    otherEditorCpuMs.push_back(std::max(0.0, editorCpuMs[sample] - viewCpuMs[sample]));
                report("other_editor_cpu", otherEditorCpuMs); std::cout << ' ';
                report("bgfx_frame_call_cpu", bgfxFrameCallMs); std::cout << ' ';
                report("bgfx_cpu_frame", bgfxCpuFrameMs); std::cout << ' ';
                report("bgfx_latency_total", bgfxLatencyMs); std::cout << ' ';
                report("bgfx_present_latency", presentLatencyMs); std::cout << ' ';
                report("full_frame_gpu", fullFrameGpuMs); std::cout << ' ';
                std::cout << "draw_calls_p50=" << percentile(frameDrawCalls, 0.50) << '\n';
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
                if (benchmark3d)
                {
                    const auto meshCount = static_cast<std::size_t>(std::count_if(
                        scene.entities().begin(), scene.entities().end(), [](const Entity& entity) {
                            return entity.meshRenderer && entity.meshRenderer->visible;
                        }));
                    if (meshCount == 0 || sceneTarget.frameMeshSubmissions() != meshCount
                        || sceneTarget.frameMeshDiagnostics() != 0 || scene3dPrepareMs.size() != benchmarkMeasuredFrames
                        || scene3dSubmitMs.size() != benchmarkMeasuredFrames
                        || fullFrameWallMs.size() != benchmarkMeasuredFrames
                        || activeBenchmarkScene >= static_cast<int>(meshSceneBuildMs.size()))
                        throw std::runtime_error("3D benchmark did not render every mesh or collect all timing samples");

                    std::vector<double> scene3dCpuMs;
                    scene3dCpuMs.reserve(scene3dPrepareMs.size());
                    for (std::size_t index = 0; index < scene3dPrepareMs.size(); ++index)
                        scene3dCpuMs.push_back(scene3dPrepareMs[index] + scene3dSubmitMs[index]);

                    PROCESS_MEMORY_COUNTERS_EX memory{};
                    memory.cb = sizeof(memory);
                    const bool memoryAvailable = GetProcessMemoryInfo(GetCurrentProcess(),
                        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
                    std::array<wchar_t, 32768> executablePath{};
                    const auto executablePathLength = GetModuleFileNameW(nullptr, executablePath.data(),
                        static_cast<DWORD>(executablePath.size()));
                    std::error_code executableSizeError;
                    const auto executableBytes = executablePathLength > 0
                            && executablePathLength < executablePath.size()
                        ? std::filesystem::file_size(std::filesystem::path(executablePath.data()), executableSizeError)
                        : std::uintmax_t{};
                    const auto wallP50 = percentile(fullFrameWallMs, 0.50);
                    std::cout << "[editor-3d-frame-benchmark] scene=" << benchmarkSceneNames[activeBenchmarkScene]
                        << " meshes=" << meshCount << " entities=" << scene.entities().size()
                        << " scene_target=" << sceneTarget.framebufferWidth() << 'x'
                        << sceneTarget.framebufferHeight() << " measured_frames=" << fullFrameWallMs.size() << ' ';
                    report("mesh_prepare", scene3dPrepareMs); std::cout << ' ';
                    report("mesh_submit", scene3dSubmitMs); std::cout << ' ';
                    report("scene3d_cpu", scene3dCpuMs); std::cout << ' ';
                    report("app_frame_wall", fullFrameWallMs); std::cout << ' ';
                    report("editor_cpu_before_bgfx_frame", editorCpuMs); std::cout << ' ';
                    report("bgfx_frame_call_cpu", bgfxFrameCallMs); std::cout << ' ';
                    report("bgfx_full_frame_gpu_timer", fullFrameGpuMs); std::cout << ' ';
                    std::cout << "frame_rate_from_wall_p50=" << (wallP50 > 0.0 ? 1000.0 / wallP50 : 0.0)
                        << " mesh_asset_import_ms=" << meshModelImportMs
                        << " scene_build_ms=" << meshSceneBuildMs[activeBenchmarkScene]
                        << " process_working_set_mb=";
                    if (memoryAvailable)
                        std::cout << double(memory.WorkingSetSize) / (1024.0 * 1024.0);
                    else std::cout << "unavailable";
                    std::cout << " process_private_mb=";
                    if (memoryAvailable)
                        std::cout << double(memory.PrivateUsage) / (1024.0 * 1024.0);
                    else std::cout << "unavailable";
                    std::cout << " editor_executable_bytes=";
                    if (executableSizeError) std::cout << "unavailable";
                    else std::cout << executableBytes;
                    std::cout << " draw_calls_p50=" << percentile(frameDrawCalls, 0.50)
                        << " mesh_draws_last_frame=" << sceneTarget.frameMeshSubmissions() << '\n';
                }
#endif

                ++activeBenchmarkScene;
                if (activeBenchmarkScene < static_cast<int>(benchmarkScenes.size()))
                {
                    scene = benchmarkScenes[activeBenchmarkScene];
                    selectedEntity = scene.entities().empty() ? std::nullopt
                        : std::optional<EntityId>{scene.entities().front().id};
                    sceneDirty = false;
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
                    if (benchmark3d) sceneTarget.requestFrameAll();
#endif
                    stopPlaySession();
                    const auto started = playSession.play(scene,
                        [](Scene&, double) { return astro::core::Result<void>::success(); });
                    if (!started) throw std::runtime_error("Could not restart full-frame benchmark runtime copy");
                    frames = 0;
                    fullFrameWallMs.clear(); editorCpuMs.clear(); viewCpuMs.clear(); bgfxFrameCallMs.clear();
                    bgfxCpuFrameMs.clear(); bgfxLatencyMs.clear(); presentLatencyMs.clear();
                    fullFrameGpuMs.clear(); frameDrawCalls.clear();
                    scene3dPrepareMs.clear(); scene3dSubmitMs.clear();
                    lastFullFrameGpuNumber = std::numeric_limits<std::uint32_t>::max();
                }
            }
        }
        const bool rendered = bgfx::getStats()->numDraw > 0;
        if (smoke)
        {
            if (!editorSettingsWindowDrawn || std::string_view(tr("Language")) != "Язык")
                throw std::runtime_error("Russian editor settings smoke failed");
            if (!scriptAuthoringPanelDrawn)
                throw std::runtime_error("ScriptBehavior Inspector authoring panel was not drawn");
#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
            if (!csharpMetadataValidationPanelDrawn)
                throw std::runtime_error("C# metadata validation panel smoke failed");
            if (csharpMetadataValidationOutput.size() != 128 * 1024 || !csharpMetadataReportScrollableInSmoke
                || csharpMetadataReportMaxDrawMs > 250.0)
                throw std::runtime_error("128 KiB C# metadata report UX smoke failed to scroll or exceeded its draw-time budget");
#endif
            const auto screenshotFile = smokeDirectory / "scene-game-visual.tga";
            for (int attempt = 0; attempt < 100 && !std::filesystem::exists(screenshotFile); ++attempt)
                SDL_Delay(10);
            if (!smokeScreenshotContainsSpriteColors(screenshotFile))
                throw std::runtime_error("Scene/Game visual regression failed: screenshot lacks sprite colors");
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
            sceneViewWhitePixels = sceneViewRectValid
                ? smokeScreenshotSceneViewWhitePixels(screenshotFile, sceneViewRectNormalized) : 0;
            if (sceneViewWhitePixels < 32)
                throw std::runtime_error("Editor Scene View visual smoke found too few white mesh pixels in the rendered viewport: "
                    + std::to_string(sceneViewWhitePixels));
#endif
            if (!playSession.runtimeScene() || playSession.runtimeScene() == &scene)
                throw std::runtime_error("Game View must render an isolated runtime Scene copy");
#if defined(ASTRO_EDITOR_HAS_PUZZLE_RUNTIME)
            if (!puzzleCampaignCatalogDrawn || !puzzleCampaignAuthoringDrawn
                || !puzzleCampaignLinkDiagnosticsDrawn || !puzzleCampaignMissingLinkDrawn)
                throw std::runtime_error(std::string("Puzzle campaign browser smoke failed: catalog=")
                    + (puzzleCampaignCatalogDrawn ? "1" : "0") + " details="
                    + (puzzleCampaignLinkDiagnosticsDrawn ? "1" : "0") + " authoring="
                    + (puzzleCampaignAuthoringDrawn ? "1" : "0") + " missing="
                    + (puzzleCampaignMissingLinkDrawn ? "1" : "0"));
            if (!puzzleSmokeReady || !puzzleSmokeLifecyclePassed || puzzleAppliedMoves < 2
                || puzzleBoardFrames == 0 || (!gameViewPuzzle && !gameViewPuzzleCampaign)
                || !puzzleCampaignSmokePassed || puzzleCampaignBoardFrames == 0
                || !puzzleSaveSettingsWindowDrawn || !puzzleSaveSettingsReady
                || !puzzleLevelAuthoringDrawn || !puzzleLevelCatalogDrawn || !puzzleLevelCatalogSmokePassed
                || !puzzleLevelSmokePassed || !puzzleLevelSaveSmokePassed
                || scene.find(1)->name != "Edited Root Ω")
                throw std::runtime_error("Puzzle Game View smoke failed to render, step gameplay or preserve the authoring Scene");
#if defined(ASTRO_EDITOR_HAS_PUZZLE_SAVE)
            if (!puzzleCampaignSaveSmokePassed || !puzzleCampaignTransferPreviewSmokePassed
                || !puzzleCampaignTransferPreviewDrawn || !puzzleCampaignTransferConfirmationDrawn
                || !puzzleCampaignTransferCopySmokePassed || !puzzleCampaignTransferRecoveryDrawn
                || !puzzleCampaignTransferRecoverySmokePassed || !puzzleCampaignTransferPartialPolicySmokePassed
                || !puzzleCampaignTransferInvalidJournalSmokePassed
                || !puzzleCampaignTransferArchivePromptDrawn || !puzzleCampaignTransferArchiveSmokePassed
                || !puzzleCampaignArchiveConflictSmokePassed
                || !puzzleCampaignArchiveConflictCopyPromptDrawn
                || !puzzleCampaignArchiveConflictCopySmokePassed)
                throw std::runtime_error("Puzzle campaign save/load/transfer confirmation/recovery Game View smoke failed: archive prompt="
                    + std::to_string(puzzleCampaignTransferArchivePromptDrawn) + ", archive copy="
                    + std::to_string(puzzleCampaignTransferArchiveSmokePassed) + ", conflict inspection="
                    + std::to_string(puzzleCampaignArchiveConflictSmokePassed) + ", copy prompt="
                    + std::to_string(puzzleCampaignArchiveConflictCopyPromptDrawn) + ", copy="
                    + std::to_string(puzzleCampaignArchiveConflictCopySmokePassed));
#endif
#endif
            stopPlaySession();
            sceneDialog = false;
            if (undoShortcuts != 1 || redoShortcuts != 1 || saveShortcuts != 1 || openShortcuts != 1
                || !openSceneFile(currentScenePath) || sceneDirty || !scene.find(1)
                || scene.find(1)->name != "Edited Root Ω"
                || !scene.spriteRenderer(1) || !smokeSpriteAsset || scene.spriteRenderer(1)->texture != *smokeSpriteAsset
                || scene.scriptBehaviors(1).size() != 1
                || scene.scriptBehaviors(1).front().managedTypeName != "Smoke.UnregisteredScript"
                || scene.scriptBehaviors(1).front().enabled)
                throw std::runtime_error("Scene persistence/shortcut smoke failed");
            if (!scene.prefabInstances().empty()) throw std::runtime_error("Empty prefab metadata smoke failed");
            const auto loadedPath = currentScenePath;
            const auto invalidScenePath = smokeDirectory / "invalid.astro-scene";
            { std::ofstream invalidScene(invalidScenePath, std::ios::binary); invalidScene << "ASTRO_SCENE 999\n"; }
            if (openSceneFile(invalidScenePath) || !scene.find(1) || scene.find(1)->name != "Edited Root Ω"
                || currentScenePath != loadedPath || diagnostics.empty())
                throw std::runtime_error("Invalid scene open must preserve the active scene and report diagnostics");
        }
        std::cout << "[editor] frames=" << frames << " docked=" << docked << " rendered=" << rendered
                  << " sceneViewFrames=" << sceneViewFrames << " gameViewFrames=" << gameViewFrames
                  << " undo=" << undoShortcuts << " redo=" << redoShortcuts << " save=" << saveShortcuts << '\n';
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
        if (smoke)
            std::cout << "[editor] scene3dSubmissions=" << sceneTarget.meshSubmissions()
                      << " worldOrigin=" << sceneTarget.lastMeshOrigin().x << ','
                      << sceneTarget.lastMeshOrigin().y << ',' << sceneTarget.lastMeshOrigin().z
                      << " frameAll=" << sceneTarget.frameAllApplied()
                      << " cameraDistance=" << sceneTarget.cameraDistance()
                      << " cameraFocusX=" << sceneTarget.cameraFocus().x
                      << " lastFrameMeshSubmissions=" << sceneTarget.frameMeshSubmissions()
                      << " lastFrameMeshDiagnostics=" << sceneTarget.frameMeshDiagnostics()
                      << " missingAssetDiagnostic="
                      << (sceneTarget.meshError().find("AssetId is not registered") != std::string::npos)
                      << " sceneViewWhitePixels=" << sceneViewWhitePixels << '\n';
#endif
#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
        if (smoke)
            std::cout << "[editor] syntheticMetadataReportBytes=" << csharpMetadataValidationOutput.size()
                      << " scrollable=" << csharpMetadataReportScrollableInSmoke
                      << " maxTextDrawMs=" << csharpMetadataReportMaxDrawMs << '\n';
#endif
        // Smoke/benchmark own this temporary project and remove only its paths.
        if (smoke || benchmark)
        {
            std::filesystem::remove(smokeDirectory / "project.astro");
            std::filesystem::remove(smokeDirectory / "invalid.astro");
            std::filesystem::remove(smokeDirectory / "invalid.astro-scene");
            std::filesystem::remove(smokeDirectory / "occupied" / "keep.txt");
            std::filesystem::remove(smokeDirectory / "occupied");
            std::error_code ignored;
            std::filesystem::remove(currentScenePath, ignored);
            std::filesystem::remove_all(smokeDirectory / "Assets", ignored);
            std::filesystem::remove_all(smokeDirectory / "Scripts", ignored);
            std::filesystem::remove_all(smokeDirectory / "Library", ignored);
            std::filesystem::remove_all(smokeDirectory / ".astro", ignored);
            std::filesystem::remove_all(smokeDirectory / "Scenes", ignored);
            std::filesystem::remove_all(smokeDirectory / "puzzle-saves", ignored);
            std::filesystem::remove_all(smokeDirectory / "Puzzle", ignored);
            std::filesystem::remove_all(smokeDirectory / "puzzle-preview-target", ignored);
            std::filesystem::remove_all(smokeDirectory / "puzzle-recovery-target", ignored);
            std::filesystem::remove_all(smokeDirectory / "puzzle-invalid-journal-target", ignored);
            std::filesystem::remove(smokeDirectory / "project.astro");
            std::filesystem::remove(smokeScreenshot, ignored);
            std::filesystem::remove(smokeDirectory);
        }
        return smoke && !((inspectCopyDialog ? frames >= 140 : frames == 140)
            && docked && rendered && sceneViewFrames > 0 && gameViewFrames > 0
            && activeProject == "Smoke project Ω"
            && scene.entities().size() == 3 && scene.find(1)->name == "Edited Root Ω"
#if defined(ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW)
            && sceneTarget.meshSubmissions() > 0
            && sceneTarget.frameMeshSubmissions() == 2
            && sceneTarget.frameMeshDiagnostics() == 1
            && sceneTarget.meshError().find("AssetId is not registered") != std::string::npos
            && std::abs(sceneTarget.lastMeshOrigin().x - 5.25f) < 0.001f
            && sceneTarget.frameAllApplied() && sceneTarget.cameraDistance() < 1000.0f
            && std::abs(sceneTarget.cameraFocus().x - 4.0f) < 0.01f
            && sceneViewWhitePixels >= 32
#endif
            && undoShortcuts == 1 && redoShortcuts == 1 && saveShortcuts == 1 && openShortcuts == 1
            && visualLogicWindowDrawn && logicDebugger.state() == astro::logic::DebugState::Completed
            && uiEditorWindowDrawn && uiDocument.elements().size() == 1
            && uiDocument.find(1)->text == "Edited Root Ω"
            && scriptAuthoringPanelDrawn && scene.scriptBehaviors(1).size() == 1
            && timelineWindowDrawn && timeline.keyframes().size() == 3 && timeline.sample() == 5.0
            && cameraWindowDrawn && cameraRig.settings().mode == astro::runtime::CameraMode::ThirdPerson
            && saveSystemWindowDrawn && saveSystemDocument.profiles().contains("smoke-progress")
            && saveSystemDocument.groups().contains("smoke-actors")
            && saveSystemDocument.checkpoints().contains(2) && csharpBuildPanelDrawn && csharpCodeWorkspaceDrawn
            && csharpManifestWindowDrawn && csharpManifestExists && csharpManifestDraft.targetFramework == "net10.0"
#if defined(ASTRO_EDITOR_METADATA_VALIDATOR_DLL) && defined(ASTRO_EDITOR_DOTNET_ROOT)
            && csharpMetadataValidationPanelDrawn
#endif
            && csharpBuildReport.freshness == astro::assets::ScriptBuildFreshness::Missing) ? 1 : 0;
    }
};
}
int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--smoke"
        && std::string_view(argv[1]) != "--benchmark"
        && std::string_view(argv[1]) != "--benchmark-3d"
        && std::string_view(argv[1]) != "--smoke-inspect-copy-dialog")) return 2;
    const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) { std::cerr << "COM initialization failed\n"; return 1; }
    struct ComLifetime { ~ComLifetime() { CoUninitialize(); } } comLifetime;
    try
    {
        Editor editor;
        editor.init();
        const bool inspectCopyDialog = argc == 2
            && std::string_view(argv[1]) == "--smoke-inspect-copy-dialog";
        const bool smoke = argc == 2 && (std::string_view(argv[1]) == "--smoke" || inspectCopyDialog);
        const bool benchmark3d = argc == 2 && std::string_view(argv[1]) == "--benchmark-3d";
        const bool benchmark = argc == 2
            && (std::string_view(argv[1]) == "--benchmark" || benchmark3d);
        return editor.run(smoke, benchmark, benchmark3d, inspectCopyDialog);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
