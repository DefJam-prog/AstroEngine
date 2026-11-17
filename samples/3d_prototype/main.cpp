#include "astro/render3d/Camera3D.hpp"
#include "astro/render3d/MeshData.hpp"
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
#include "astro/assets/AssetRegistry.hpp"
#include "astro/render3d/GltfImporter.hpp"
#include "astro/render3d/SceneMeshResolver.hpp"
#include "astro/scene/SceneFile.hpp"
#endif

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
struct Vertex final { float x, y, z; std::uint8_t r, g, b, a; };
static_assert(sizeof(Vertex) == 16);

class SmokeCallback final : public bgfx::CallbackI
{
    std::atomic<bool> completed_{false};
    std::atomic<bool> valid_{false};
    std::atomic<std::uint32_t> foregroundPixels_{0};
    std::atomic<std::uint32_t> colorBuckets_{0};
    std::atomic<std::uint32_t> minX_{0}, minY_{0}, maxX_{0}, maxY_{0};
    std::uint32_t requiredColorBuckets_ = 4;

public:
    void setRequiredColorBuckets(const std::uint32_t count) noexcept { requiredColorBuckets_ = count; }
    void fatal(const char* file, std::uint16_t line, bgfx::Fatal::Enum, const char* message) override
    {
        std::fprintf(stderr, "bgfx fatal at %s:%u: %s\n", file ? file : "?", line, message ? message : "?");
    }
    void traceVargs(const char*, std::uint16_t, const char* format, std::va_list args) override
    {
        std::vfprintf(stderr, format, args);
    }
    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override { return 0; }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override { return false; }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, std::uint32_t) override {}

    void screenShot(const char*, const std::uint32_t width, const std::uint32_t height,
        const std::uint32_t pitch, bgfx::TextureFormat::Enum, const void* data,
        const std::uint32_t size, bool) override
    {
        if (!data || width < 64 || height < 64 || pitch < width * 4u
            || static_cast<std::uint64_t>(pitch) * height > size)
        {
            completed_.store(true, std::memory_order_release);
            return;
        }

        const auto* pixels = static_cast<const std::uint8_t*>(data);
        std::array<bool, 512> buckets{};
        std::uint32_t count = 0;
        std::uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const auto* row = pixels + static_cast<std::size_t>(pitch) * y;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const auto* pixel = row + static_cast<std::size_t>(x) * 4;
                // bgfx screenshot callbacks return BGRA. The view clears to RGBA 0x20,0x28,0x30.
                if (std::abs(static_cast<int>(pixel[0]) - 0x30) <= 1
                    && std::abs(static_cast<int>(pixel[1]) - 0x28) <= 1
                    && std::abs(static_cast<int>(pixel[2]) - 0x20) <= 1)
                    continue;
                if (pixel[3] < 200) continue;
                ++count;
                minX = std::min(minX, x); minY = std::min(minY, y);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y);
                const std::size_t bucket = (pixel[2] >> 5) * 64u + (pixel[1] >> 5) * 8u + (pixel[0] >> 5);
                buckets[bucket] = true;
            }
        }
        const auto colors = static_cast<std::uint32_t>(std::count(buckets.begin(), buckets.end(), true));
        foregroundPixels_.store(count, std::memory_order_relaxed);
        colorBuckets_.store(colors, std::memory_order_relaxed);
        minX_.store(minX, std::memory_order_relaxed); minY_.store(minY, std::memory_order_relaxed);
        maxX_.store(maxX, std::memory_order_relaxed); maxY_.store(maxY, std::memory_order_relaxed);
        const bool boundsValid = count > 500 && colors >= requiredColorBuckets_ && maxX > minX + 12 && maxY > minY + 12
            && minX > width / 10 && maxX < width - width / 10
            && minY > height / 10 && maxY < height - height / 10;
        valid_.store(boundsValid, std::memory_order_release);
        completed_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool completed() const noexcept { return completed_.load(std::memory_order_acquire); }
    [[nodiscard]] bool valid() const noexcept { return valid_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint32_t foregroundPixels() const noexcept { return foregroundPixels_.load(); }
    [[nodiscard]] std::uint32_t colorBuckets() const noexcept { return colorBuckets_.load(); }
    [[nodiscard]] std::array<std::uint32_t, 4> bounds() const noexcept
    {
        return {minX_.load(), minY_.load(), maxX_.load(), maxY_.load()};
    }
};

#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
astro::render3d::ResolvedSceneMesh loadSceneMesh(const std::filesystem::path& projectRoot)
{
    using namespace astro;
    auto registry = assets::AssetRegistry::open(projectRoot);
    if (!registry) throw std::runtime_error("AssetRegistry open failed: " + registry.error().message);
    auto imported = render3d::GltfImporter::importAndRegister(registry.value(),
        std::filesystem::path("Assets/Models/triangle.gltf"));
    if (!imported) throw std::runtime_error("Scene smoke model import failed: " + imported.error().message);

    auto document = scene::loadSceneFile(projectRoot / "Scenes" / "RendererSmoke.astro-scene");
    if (!document) throw std::runtime_error("Scene smoke load failed: " + document.error().message);
    const scene::MeshRendererComponent* component = nullptr;
    for (const auto& entity : document.value().entities())
    {
        if (entity.meshRenderer)
        {
            if (component) throw std::runtime_error("Scene smoke fixture must contain exactly one MeshRenderer");
            component = &*entity.meshRenderer;
        }
    }
    if (!component || !component->visible)
        throw std::runtime_error("Scene smoke fixture must contain one visible MeshRenderer");
    if (component->model != imported.value().modelAssetId)
        throw std::runtime_error("Scene smoke MeshRenderer AssetId does not match the imported model");

    auto resolved = render3d::resolveMeshRenderer(registry.value(), *component);
    if (!resolved) throw std::runtime_error("Scene smoke model resolution failed: " + resolved.error().message);
    return std::move(resolved).value();
}
#endif

std::vector<std::uint8_t> readShader(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Unable to open bgfx shader: " + path.string());
    std::vector<std::uint8_t> data(std::istreambuf_iterator<char>(file), {});
    if (data.empty() || data.size() > 4 * 1024 * 1024) throw std::runtime_error("Invalid bgfx shader size");
    return data;
}

#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
std::filesystem::path pathFromUtf8(const std::string_view value)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
#endif

class Prototype final
{
    SDL_Window* window_ = nullptr;
    bool sdl_ = false, bgfx_ = false;
    SmokeCallback callback_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices_ = BGFX_INVALID_HANDLE;

    void cleanup() noexcept
    {
        if (bgfx_)
        {
            if (bgfx::isValid(indices_)) bgfx::destroy(indices_);
            if (bgfx::isValid(vertices_)) bgfx::destroy(vertices_);
            if (bgfx::isValid(program_)) bgfx::destroy(program_);
            bgfx::shutdown();
            bgfx_ = false;
        }
        if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
        if (sdl_) { SDL_Quit(); sdl_ = false; }
    }

public:
    ~Prototype() { cleanup(); }

    int run(bool smoke
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        , const astro::render3d::ResolvedSceneMesh* sceneMesh = nullptr
#endif
    )
    {
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        const bool hasSceneMesh = sceneMesh != nullptr;
#else
        constexpr bool hasSceneMesh = false;
#endif
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        sdl_ = true;
        constexpr std::uint32_t initialWidth = 960, initialHeight = 640;
        window_ = SDL_CreateWindow(hasSceneMesh ? "AstroEngine - Scene Mesh preview" : "AstroEngine - 3D prototype",
            initialWidth, initialHeight, SDL_WINDOW_RESIZABLE);
        if (!window_) throw std::runtime_error(SDL_GetError());

        bgfx::Init init;
        init.type = bgfx::RendererType::Direct3D11;
        init.fallback = false;
        init.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        init.swapChain.width = initialWidth;
        init.swapChain.height = initialHeight;
        init.reset = BGFX_RESET_VSYNC;
        init.callback = &callback_;
        if (!init.swapChain.nwh || !bgfx::init(init)) throw std::runtime_error("bgfx D3D11 initialization failed");
        bgfx_ = true;

        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
        const auto proceduralMesh = astro::render3d::MeshData::makeColoredCube();
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        const auto& mesh = sceneMesh ? sceneMesh->geometry : proceduralMesh;
        if (sceneMesh) callback_.setRequiredColorBuckets(1);
#else
        const auto& mesh = proceduralMesh;
#endif
        constexpr std::size_t MaxSmokeVertices = 65536;
        constexpr std::size_t MaxSmokeIndices = 196608;
        if (mesh.vertices().size() > MaxSmokeVertices || mesh.triangleIndices().size() > MaxSmokeIndices)
            throw std::runtime_error("3D smoke geometry exceeds the 16-bit preview budget");
        std::vector<Vertex> vertexData;
        vertexData.reserve(mesh.vertices().size());
        for (const auto& vertex : mesh.vertices())
            vertexData.push_back({vertex.position.x, vertex.position.y, vertex.position.z,
                vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a});
        std::vector<std::uint16_t> indexData;
        indexData.reserve(mesh.triangleIndices().size());
        for (const auto index : mesh.triangleIndices()) indexData.push_back(static_cast<std::uint16_t>(index));
        vertices_ = bgfx::createVertexBuffer(bgfx::copy(vertexData.data(), static_cast<std::uint32_t>(vertexData.size() * sizeof(Vertex))), layout);
        indices_ = bgfx::createIndexBuffer(bgfx::copy(indexData.data(), static_cast<std::uint32_t>(indexData.size() * sizeof(std::uint16_t))));
        const std::filesystem::path shaderDir(ASTRO_3D_SHADER_DIR);
        auto vertexShaderBytes = readShader(shaderDir / "vs_cubes.bin");
        auto fragmentShaderBytes = readShader(shaderDir / "fs_cubes.bin");
        const auto vertexShader = bgfx::createShader(bgfx::copy(vertexShaderBytes.data(), static_cast<std::uint32_t>(vertexShaderBytes.size())));
        const auto fragmentShader = bgfx::createShader(bgfx::copy(fragmentShaderBytes.data(), static_cast<std::uint32_t>(fragmentShaderBytes.size())));
        program_ = bgfx::createProgram(vertexShader, fragmentShader, true);
        if (!bgfx::isValid(vertices_) || !bgfx::isValid(indices_) || !bgfx::isValid(program_))
            throw std::runtime_error("bgfx failed to create the procedural cube resources");

        astro::render3d::Camera3D camera;
        bool running = true;
        std::uint32_t frames = 0;
        int previousWidth = static_cast<int>(initialWidth), previousHeight = static_cast<int>(initialHeight);
        std::vector<double> frameTimesMs;
        bool captureRequested = false;
        while (running && (!smoke || (frames < 120 && !callback_.completed())))
        {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_QUIT) running = false;
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window_, &width, &height);
            if (width <= 0 || height <= 0) { SDL_Delay(16); continue; }
            if (width != previousWidth || height != previousHeight)
            {
                bgfx::SwapChain swapChain;
                swapChain.width = static_cast<std::uint32_t>(width);
                swapChain.height = static_cast<std::uint32_t>(height);
                bgfx::reset(BGFX_RESET_VSYNC, &swapChain);
                previousWidth = width;
                previousHeight = height;
            }
            bgfx::setViewRect(0, 0, 0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
            bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202830ff, 1.0f, 0);
            bgfx::touch(0);

            const auto view = camera.viewMatrix();
            const auto projection = camera.projectionMatrix(static_cast<float>(width) / static_cast<float>(height));
            if (!view || !projection) throw std::runtime_error("3D camera validation failed");
            bgfx::setViewTransform(0, view.value().values.data(), projection.value().values.data());
            float model[16];
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            if (sceneMesh) bx::mtxIdentity(model);
            else
#endif
                bx::mtxRotateXY(model, 0.35f + static_cast<float>(frames) * 0.012f,
                    0.55f + static_cast<float>(frames) * 0.008f);
            bgfx::setTransform(model);
            bgfx::setVertexBuffer(0, vertices_);
            bgfx::setIndexBuffer(indices_);
            auto state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            if (!sceneMesh) state |= BGFX_STATE_CULL_CCW;
#else
            state |= BGFX_STATE_CULL_CCW;
#endif
            bgfx::setState(state);
            bgfx::submit(0, program_);
            if (smoke && frames == 30)
            {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "astroengine-3d-smoke-memory-capture");
                captureRequested = true;
            }
            bgfx::frame();
            if (smoke && frames > 5 && frames < 30)
            {
                const auto* stats = bgfx::getStats();
                if (stats && stats->cpuTimerFreq > 0 && stats->cpuTimeFrame >= 0)
                    frameTimesMs.push_back(1000.0 * static_cast<double>(stats->cpuTimeFrame)
                        / static_cast<double>(stats->cpuTimerFreq));
            }
            ++frames;
        }
        const bool submitted = bgfx::getStats() && bgfx::getStats()->numDraw > 0;
        std::sort(frameTimesMs.begin(), frameTimesMs.end());
        const auto percentile = [&frameTimesMs](double fraction) {
            if (frameTimesMs.empty()) return 0.0;
            const auto index = static_cast<std::size_t>(fraction * static_cast<double>(frameTimesMs.size() - 1));
            return frameTimesMs[index];
        };
        const auto bounds = callback_.bounds();
        std::cout << std::fixed << std::setprecision(3) << "[info] renderer=" << bgfx::getRendererName(bgfx::getRendererType())
            << " frames=" << frames << " draw_calls=" << (bgfx::getStats() ? bgfx::getStats()->numDraw : 0)
            << " frame_ms_p50=" << percentile(0.50) << " frame_ms_p95=" << percentile(0.95)
            << " pixel_capture=" << callback_.completed() << " foreground_pixels=" << callback_.foregroundPixels()
            << " color_buckets=" << callback_.colorBuckets() << " bounds=" << bounds[0] << ',' << bounds[1]
            << '-' << bounds[2] << ',' << bounds[3] << '\n';
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        if (sceneMesh)
            std::cout << "[info] scene_mesh=1 model_asset=" << sceneMesh->modelAssetId.value
                << " mesh_index=" << sceneMesh->meshIndex << " primitive_index=" << sceneMesh->primitiveIndex
                << " visible=" << sceneMesh->visible << '\n';
#endif
        cleanup();
        return smoke && (!captureRequested || !callback_.completed() || !callback_.valid() || !submitted
            || frameTimesMs.size() < 10) ? 1 : 0;
    }
};
}

int main(int argc, char** argv)
{
    bool smoke = false;
    std::optional<std::filesystem::path> sceneProject;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") smoke = true;
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
    else if (argc == 4 && std::string_view(argv[1]) == "--scene-smoke"
        && std::string_view(argv[2]) == "--project-root")
    {
        smoke = true;
        sceneProject = pathFromUtf8(argv[3]);
    }
#endif
    else if (argc != 1)
    {
        std::cerr << "Usage: astro_render3d_bgfx_smoke [--smoke]"
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            << " | --scene-smoke --project-root <temporary-fixture-project>"
#endif
            << '\n';
        return 2;
    }
    try
    {
        Prototype prototype;
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        if (sceneProject)
        {
            const auto resolved = loadSceneMesh(*sceneProject);
            return prototype.run(smoke, &resolved);
        }
#endif
        return prototype.run(smoke
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            , nullptr
#endif
        );
    }
    catch (const std::exception& error) { std::cerr << "[error] " << error.what() << '\n'; return 1; }
}
