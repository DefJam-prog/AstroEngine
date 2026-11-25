#include "astro/render3d/Camera3D.hpp"
#include "astro/render3d/BaseColorTexture.hpp"
#include "astro/render3d/MeshData.hpp"
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
#include "BgfxSceneRenderer.hpp"
#include "astro/scene/Scene.hpp"
#include "astro/render3d/SceneRenderPacket.hpp"
#include "astro/runtime/RuntimeSession.hpp"
#endif
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
#include "astro/assets/AssetRegistry.hpp"
#include "astro/render3d/GltfImporter.hpp"
#include "astro/render3d/SceneMeshResolver.hpp"
#include "astro/scene/SceneFile.hpp"
#include "astro/runtime/WicImageDecoder.hpp"
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
#include <cmath>
#include <cstring>
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
struct Vertex final { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
static_assert(sizeof(Vertex) == 24);

class SmokeCallback final : public bgfx::CallbackI
{
    std::atomic<bool> completed_{false};
    std::atomic<bool> valid_{false};
    std::atomic<std::uint32_t> foregroundPixels_{0};
    std::atomic<std::uint32_t> colorBuckets_{0};
    std::atomic<std::uint32_t> materialPixels_{0};
    std::atomic<std::uint32_t> textureColorFamilies_{0};
    std::atomic<std::uint32_t> factorColorPixels_{0};
    std::atomic<std::uint32_t> factorMeanR_{0}, factorMeanG_{0}, factorMeanB_{0};
    std::atomic<bool> screenshotSaved_{false};
    std::atomic<std::uint32_t> minX_{0}, minY_{0}, maxX_{0}, maxY_{0};
    std::uint32_t requiredColorBuckets_ = 4;
    bool inspectFactorColors_ = false;

public:
    void setRequiredColorBuckets(const std::uint32_t count) noexcept { requiredColorBuckets_ = count; }
    void inspectFactorColors(const bool enabled) noexcept { inspectFactorColors_ = enabled; }
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

    void screenShot(const char* filePath, const std::uint32_t width, const std::uint32_t height,
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
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        if (filePath && *filePath && std::filesystem::path(filePath).extension() == ".tga"
            && width <= 65535 && height <= 65535)
        {
            const auto utf8Path = std::u8string(reinterpret_cast<const char8_t*>(filePath), std::strlen(filePath));
            std::ofstream image(std::filesystem::path(utf8Path), std::ios::binary | std::ios::trunc);
            if (image)
            {
                const std::array<std::uint8_t, 18> header{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    static_cast<std::uint8_t>(width), static_cast<std::uint8_t>(width >> 8),
                    static_cast<std::uint8_t>(height), static_cast<std::uint8_t>(height >> 8), 32, 0x28};
                image.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
                for (std::uint32_t y = 0; y < height && image; ++y)
                    image.write(reinterpret_cast<const char*>(pixels + static_cast<std::size_t>(pitch) * y),
                        static_cast<std::streamsize>(static_cast<std::size_t>(width) * 4));
                screenshotSaved_.store(static_cast<bool>(image), std::memory_order_release);
            }
        }
#endif
        std::array<bool, 512> buckets{};
        std::uint32_t count = 0;
        std::uint32_t materialPixels = 0;
        bool red = false, green = false, blue = false, yellow = false;
        std::uint64_t factorSumR = 0, factorSumG = 0, factorSumB = 0;
        std::uint32_t factorMatches = 0;
        const auto* background = pixels;
        std::uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const auto* row = pixels + static_cast<std::size_t>(pitch) * y;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const auto* pixel = row + static_cast<std::size_t>(x) * 4;
                // Compare against the captured corner so sRGB backbuffer clear conversion is handled too.
                if (std::abs(static_cast<int>(pixel[0]) - background[0]) <= 1
                    && std::abs(static_cast<int>(pixel[1]) - background[1]) <= 1
                    && std::abs(static_cast<int>(pixel[2]) - background[2]) <= 1)
                    continue;
                if (pixel[3] < 200) continue;
                // BGFX screenshot bytes are BGRA. The material smoke expects red to dominate.
                if (pixel[2] > pixel[1] * 2u && pixel[2] > pixel[0] * 2u) ++materialPixels;
                const auto r = pixel[2], g = pixel[1], b = pixel[0];
                red = red || (r > 140 && r > g * 2u && r > b * 2u);
                green = green || (g > 140 && g > r * 2u && g > b * 2u);
                blue = blue || (b > 140 && b > r * 2u && b > g * 2u);
                yellow = yellow || (r > 140 && g > 140 && r > b * 2u && g > b * 2u);
                if (inspectFactorColors_)
                {
                    if (r >= 90 && r <= 96 && g >= 90 && g <= 96 && b >= 90 && b <= 96)
                    {
                        ++factorMatches;
                        factorSumR += r; factorSumG += g; factorSumB += b;
                    }
                }
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
        materialPixels_.store(materialPixels, std::memory_order_relaxed);
        textureColorFamilies_.store(static_cast<std::uint32_t>(red) + static_cast<std::uint32_t>(green)
            + static_cast<std::uint32_t>(blue) + static_cast<std::uint32_t>(yellow), std::memory_order_relaxed);
        factorColorPixels_.store(factorMatches, std::memory_order_relaxed);
        factorMeanR_.store(factorMatches ? static_cast<std::uint32_t>(factorSumR / factorMatches) : 0,
            std::memory_order_relaxed);
        factorMeanG_.store(factorMatches ? static_cast<std::uint32_t>(factorSumG / factorMatches) : 0,
            std::memory_order_relaxed);
        factorMeanB_.store(factorMatches ? static_cast<std::uint32_t>(factorSumB / factorMatches) : 0,
            std::memory_order_relaxed);
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
    [[nodiscard]] std::uint32_t materialPixels() const noexcept { return materialPixels_.load(); }
    [[nodiscard]] std::uint32_t textureColorFamilies() const noexcept { return textureColorFamilies_.load(); }
    [[nodiscard]] std::uint32_t factorColorPixels() const noexcept { return factorColorPixels_.load(); }
    [[nodiscard]] std::array<std::uint32_t, 3> factorMeanRgb() const noexcept
    {
        return {factorMeanR_.load(), factorMeanG_.load(), factorMeanB_.load()};
    }
    [[nodiscard]] bool screenshotSaved() const noexcept { return screenshotSaved_.load(std::memory_order_acquire); }
    [[nodiscard]] std::array<std::uint32_t, 4> bounds() const noexcept
    {
        return {minX_.load(), minY_.load(), maxX_.load(), maxY_.load()};
    }
};

#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
struct SceneSmokeInput final
{
    astro::render3d::ResolvedSceneMesh mesh;
    astro::assets::MaterialAssetData material;
    astro::assets::AssetId textureAssetId{};
    astro::runtime::WicDecodedImage texture;
    bool factorSmoke = false;
};

SceneSmokeInput loadSceneMesh(const std::filesystem::path& projectRoot, const bool textureSmoke,
    const bool factorSmoke)
{
    using namespace astro;
    auto registry = assets::AssetRegistry::open(projectRoot);
    if (!registry) throw std::runtime_error("AssetRegistry open failed: " + registry.error().message);
    auto imported = render3d::GltfImporter::importAndRegister(registry.value(),
        std::filesystem::path("Assets/Models/triangle.gltf"));
    if (!imported) throw std::runtime_error("Scene smoke model import failed: " + imported.error().message);
    std::optional<assets::AssetReference> textureReference;
    if (textureSmoke)
    {
        auto image = registry.value().importImage(factorSmoke
            ? std::filesystem::path("Textures/mid-gray.png")
            : std::filesystem::path("Textures/uv-checker.png"));
        if (!image) throw std::runtime_error("Texture smoke image registration failed: " + image.error().message);
        textureReference = std::move(image).value();
    }
    auto materialReference = registry.value().registerMaterial(factorSmoke
        ? std::filesystem::path("Materials/texture-factor-smoke.astro-material")
        : textureSmoke ? std::filesystem::path("Materials/texture-smoke.astro-material")
        : std::filesystem::path("Materials/smoke.astro-material"));
    if (!materialReference)
        throw std::runtime_error("Scene smoke material registration failed: " + materialReference.error().message);

    const auto sceneName = factorSmoke ? "RendererTextureFactorSmoke.astro-scene"
        : textureSmoke ? "RendererTextureSmoke.astro-scene" : "RendererSmoke.astro-scene";
    auto document = scene::loadSceneFile(projectRoot / "Scenes" / sceneName);
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
    if (component->materialOverride != materialReference.value().id)
        throw std::runtime_error("Scene smoke MeshRenderer does not reference its registered material override");

    auto resolved = render3d::resolveMeshRenderer(registry.value(), *component);
    if (!resolved) throw std::runtime_error("Scene smoke model resolution failed: " + resolved.error().message);
    auto material = registry.value().resolveMaterial(component->materialOverride);
    if (!material) throw std::runtime_error("Scene smoke material resolution failed: " + material.error().message);
    SceneSmokeInput result{std::move(resolved).value(), material.value().material};
    if (textureSmoke)
    {
        if (!textureReference || !result.material.baseColorTexture
            || result.material.baseColorTexture->imageAssetId != textureReference->id
            || result.material.baseColorTexture->texCoord != 0 || !result.mesh.geometry.hasTexCoords())
            throw std::runtime_error("Texture material role does not resolve to the imported UV0 channel");
        auto decoded = runtime::decodeWicImage(registry.value().cachedPath(textureReference->id));
        if (!decoded) throw std::runtime_error("Texture smoke WIC decode failed: " + decoded.error().message);
        result.textureAssetId = textureReference->id;
        result.texture = std::move(decoded).value();
        result.factorSmoke = factorSmoke;
        if (result.texture.pixels.size() != std::uint64_t(result.texture.width) * result.texture.height * 4)
            throw std::runtime_error("Texture smoke decoded pixel payload is inconsistent");
        if (factorSmoke)
        {
            auto prepared = render3d::prepareLinearBaseColorBgra8(result.texture.pixels,
                result.material.baseColorFactor);
            if (!prepared) throw std::runtime_error("Base-color linear preparation failed: " + prepared.error().message);
            result.texture.pixels = std::move(prepared).value();
        }
    }
    return result;
}
#endif

#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
struct RuntimePacketSmokeScene final
{
    astro::scene::Scene authoringScene{7001, "Runtime BGFX packet smoke"};
    astro::render3d::SceneMeshResolver resolver;
    astro::scene::EntityId movingEntity = 0;
};

RuntimePacketSmokeScene makeRuntimePacketSmokeScene()
{
    using namespace astro;
    RuntimePacketSmokeScene fixture;
    auto& document = fixture.authoringScene;
    const auto parent = document.createEntity("Transformed parent");
    const auto left = document.createEntity("Left mesh", parent ? std::optional(parent.value()) : std::nullopt);
    const auto right = document.createEntity("Right mesh", parent ? std::optional(parent.value()) : std::nullopt);
    const auto camera = document.createEntity("Active camera");
    if (!parent || !left || !right || !camera) throw std::runtime_error("Unable to create packet smoke entities");
    scene::Transform parentTransform;
    parentTransform.position = {-1.2, 0.0, 0.0};
    parentTransform.rotationDegrees.y = 8.0;
    parentTransform.scale = {0.75, 0.75, 0.75};
    scene::Transform rightTransform;
    rightTransform.position.x = 3.2;
    scene::Transform cameraTransform;
    cameraTransform.position.z = 8.0;
    if (!document.setTransform(parent.value(), parentTransform)
        || !document.setTransform(right.value(), rightTransform)
        || !document.setTransform(camera.value(), cameraTransform)
        || !document.setCamera3D(camera.value(), 60.0, 0.1, 100.0)
        || !document.setCamera3DActive(camera.value(), true)
        || !document.setMeshRenderer(left.value(), assets::AssetId{701})
        || !document.setMeshRenderer(right.value(), assets::AssetId{702}))
        throw std::runtime_error("Unable to configure packet smoke scene");

    auto geometry = std::make_shared<const render3d::MeshData>(render3d::MeshData::makeColoredCube());
    fixture.resolver = [geometry](const scene::MeshRendererComponent& component) {
        assets::MaterialAssetData material;
        material.baseColorFactor = component.model.value == 701
            ? std::array<float, 4>{0.95f, 0.12f, 0.08f, 1.0f}
            : std::array<float, 4>{0.08f, 0.95f, 0.08f, 1.0f};
        return core::Result<render3d::SceneMeshSource>::success({geometry, material});
    };
    auto packet = render3d::buildSceneRenderPacket(document, nullptr, fixture.resolver);
    if (!packet) throw std::runtime_error("Scene render packet failed: " + packet.error().message);
    if (packet.value().items.size() != 2 || !packet.value().diagnostics.empty())
        throw std::runtime_error("Packet smoke scene did not resolve exactly two clean render items");
    fixture.movingEntity = right.value();
    return fixture;
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
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle textureSampler_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices_ = BGFX_INVALID_HANDLE;
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
    std::unique_ptr<astro::runtime::BgfxSceneRenderer> sceneRenderer_;
#endif

    void cleanup() noexcept
    {
        if (bgfx_)
        {
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
            sceneRenderer_.reset();
#endif
            if (bgfx::isValid(indices_)) bgfx::destroy(indices_);
            if (bgfx::isValid(vertices_)) bgfx::destroy(vertices_);
            if (bgfx::isValid(texture_)) bgfx::destroy(texture_);
            if (bgfx::isValid(textureSampler_)) bgfx::destroy(textureSampler_);
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
        , const SceneSmokeInput* sceneInput = nullptr, bool materialSmoke = false, bool textureSmoke = false,
          bool factorSmoke = false
#endif
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        , astro::runtime::RuntimeSession* runtimeSession = nullptr,
          const astro::scene::Scene* authoringScene = nullptr,
          const astro::render3d::SceneMeshResolver* runtimeResolver = nullptr,
          astro::scene::EntityId trackedEntity = 0,
          astro::scene::Transform authoringTrackedTransform = {}
#endif
    )
    {
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        const bool hasSceneMesh = sceneInput != nullptr;
        if (factorSmoke) callback_.inspectFactorColors(true);
#else
        constexpr bool hasSceneMesh = false;
        constexpr bool textureSmoke = false;
        constexpr bool factorSmoke = false;
#endif
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        const bool hasRuntimePacket = runtimeSession != nullptr && authoringScene != nullptr && runtimeResolver != nullptr;
#else
        constexpr bool hasRuntimePacket = false;
#endif
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        sdl_ = true;
        constexpr std::uint32_t initialWidth = 960, initialHeight = 640;
        window_ = SDL_CreateWindow(hasRuntimePacket ? "AstroEngine - Runtime Scene packet"
                : hasSceneMesh ? "AstroEngine - Scene Mesh preview" : "AstroEngine - 3D prototype",
            initialWidth, initialHeight, SDL_WINDOW_RESIZABLE);
        if (!window_) throw std::runtime_error(SDL_GetError());

        bgfx::Init init;
        init.type = bgfx::RendererType::Direct3D11;
        init.fallback = false;
        init.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        init.swapChain.width = initialWidth;
        init.swapChain.height = initialHeight;
        const std::uint32_t resetFlags = BGFX_RESET_VSYNC
            | (factorSmoke ? BGFX_RESET_SRGB_BACKBUFFER : 0u);
        init.reset = resetFlags;
        init.callback = &callback_;
        if (!init.swapChain.nwh || !bgfx::init(init)) throw std::runtime_error("bgfx D3D11 initialization failed");
        bgfx_ = true;

        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
        const auto proceduralMesh = astro::render3d::MeshData::makeColoredCube();
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        const auto& mesh = sceneInput ? sceneInput->mesh.geometry : proceduralMesh;
        if (sceneInput) callback_.setRequiredColorBuckets(1);
#else
        const auto& mesh = proceduralMesh;
#endif
        constexpr std::size_t MaxSmokeVertices = 65536;
        constexpr std::size_t MaxSmokeIndices = 196608;
        if (!hasRuntimePacket && (mesh.vertices().size() > MaxSmokeVertices || mesh.triangleIndices().size() > MaxSmokeIndices))
            throw std::runtime_error("3D smoke geometry exceeds the 16-bit preview budget");
        std::vector<Vertex> vertexData;
        if (!hasRuntimePacket)
        {
        vertexData.reserve(mesh.vertices().size());
        for (const auto& vertex : mesh.vertices())
        {
            auto color = vertex.color;
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            if (sceneInput)
            {
                const auto tint = [&sceneInput](const std::size_t channel) {
                    return static_cast<std::uint8_t>(std::clamp(
                        std::lround(sceneInput->material.baseColorFactor[channel] * 255.0f), 0l, 255l));
                };
                color.r = static_cast<std::uint8_t>((static_cast<unsigned>(color.r) * tint(0) + 127u) / 255u);
                color.g = static_cast<std::uint8_t>((static_cast<unsigned>(color.g) * tint(1) + 127u) / 255u);
                color.b = static_cast<std::uint8_t>((static_cast<unsigned>(color.b) * tint(2) + 127u) / 255u);
                color.a = static_cast<std::uint8_t>((static_cast<unsigned>(color.a) * tint(3) + 127u) / 255u);
            }
#endif
            vertexData.push_back({vertex.position.x, vertex.position.y, vertex.position.z,
                color.r, color.g, color.b, color.a, vertex.texCoord0.x, vertex.texCoord0.y});
        }
        std::vector<std::uint16_t> indexData;
        indexData.reserve(mesh.triangleIndices().size());
        for (const auto index : mesh.triangleIndices()) indexData.push_back(static_cast<std::uint16_t>(index));
        vertices_ = bgfx::createVertexBuffer(bgfx::copy(vertexData.data(), static_cast<std::uint32_t>(vertexData.size() * sizeof(Vertex))), layout);
        indices_ = bgfx::createIndexBuffer(bgfx::copy(indexData.data(), static_cast<std::uint32_t>(indexData.size() * sizeof(std::uint16_t))));
        }
        const std::filesystem::path shaderDir(ASTRO_3D_SHADER_DIR);
        auto vertexShaderBytes = readShader(shaderDir / (textureSmoke ? "vs_stencil_texture.bin" : "vs_cubes.bin"));
        auto fragmentShaderBytes = readShader(shaderDir / (textureSmoke ? "fs_stencil_texture.bin" : "fs_cubes.bin"));
        const auto vertexShader = bgfx::createShader(bgfx::copy(vertexShaderBytes.data(), static_cast<std::uint32_t>(vertexShaderBytes.size())));
        const auto fragmentShader = bgfx::createShader(bgfx::copy(fragmentShaderBytes.data(), static_cast<std::uint32_t>(fragmentShaderBytes.size())));
        program_ = bgfx::createProgram(vertexShader, fragmentShader, true);
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        if (textureSmoke)
        {
            textureSampler_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
            texture_ = bgfx::createTexture2D(static_cast<std::uint16_t>(sceneInput->texture.width),
                static_cast<std::uint16_t>(sceneInput->texture.height), false, 1, bgfx::TextureFormat::BGRA8,
                BGFX_TEXTURE_NONE, bgfx::copy(sceneInput->texture.pixels.data(),
                    static_cast<std::uint32_t>(sceneInput->texture.pixels.size())));
        }
#endif
        if ((!hasRuntimePacket && (!bgfx::isValid(vertices_) || !bgfx::isValid(indices_))) || !bgfx::isValid(program_))
            throw std::runtime_error("bgfx failed to create the procedural cube resources");
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        if (hasRuntimePacket)
            sceneRenderer_ = std::make_unique<astro::runtime::BgfxSceneRenderer>(program_);
#endif
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        if (textureSmoke && (!bgfx::isValid(texture_) || !bgfx::isValid(textureSampler_)))
            throw std::runtime_error("bgfx failed to create the base-color texture resources");
#endif

        astro::render3d::Camera3D camera;
        bool running = true;
        std::uint32_t frames = 0;
        int previousWidth = static_cast<int>(initialWidth), previousHeight = static_cast<int>(initialHeight);
        std::vector<double> frameTimesMs;
        bool captureRequested = false;
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        std::uint32_t packetItemsSubmitted = 0;
        std::uint32_t playFixedSteps = 0;
        bool runtimePacketValid = true;
        bool authoringSceneUnchanged = true;
        std::optional<astro::render3d::SceneRenderPacket> runtimePacket;
        std::string runtimeScreenshotPath;
        if (hasRuntimePacket)
        {
            if (const char* path = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "ASTRO_RUNTIME_PACKET_SCREENSHOT"))
                runtimeScreenshotPath = path;
        }
#endif
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
                bgfx::reset(resetFlags, &swapChain);
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
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
            if (hasRuntimePacket)
            {
                if (!runtimeSession || runtimeSession->state() != astro::runtime::RuntimeState::Playing)
                    throw std::runtime_error("Runtime Play session stopped while the BGFX window was active");
                const auto ticked = runtimeSession->tick(1.0 / 60.0);
                if (!ticked) throw std::runtime_error("Runtime Play tick failed: " + ticked.error().message);
                playFixedSteps += ticked.value().fixedStepCount;
                const auto* runtimeScene = runtimeSession->runtimeScene();
                if (!runtimeScene || runtimeScene == authoringScene)
                    throw std::runtime_error("Play must render its disposable runtime Scene copy");
                const auto* authoringTransform = authoringScene->transform(trackedEntity);
                authoringSceneUnchanged = authoringTransform
                    && authoringTransform->value == authoringTrackedTransform;
                auto packet = astro::render3d::buildSceneRenderPacket(*runtimeScene, nullptr, *runtimeResolver);
                if (!packet) throw std::runtime_error("Runtime Scene packet build failed: " + packet.error().message);
                runtimePacket = std::move(packet).value();
                const auto rendered = sceneRenderer_->render(*runtimePacket, 0,
                    static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
                if (!rendered) throw std::runtime_error("Runtime packet render failed: " + rendered.error().message);
                packetItemsSubmitted = rendered.value();
                runtimePacketValid = packetItemsSubmitted == runtimePacket->items.size()
                    && runtimePacket->items.size() == 2 && runtimePacket->diagnostics.empty()
                    && sceneRenderer_->cachedMeshCount() == 2 && authoringSceneUnchanged;
            }
            else
#endif
            {
            float model[16];
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            if (sceneInput) bx::mtxIdentity(model);
            else
#endif
                bx::mtxRotateXY(model, 0.35f + static_cast<float>(frames) * 0.012f,
                    0.55f + static_cast<float>(frames) * 0.008f);
            bgfx::setTransform(model);
            bgfx::setVertexBuffer(0, vertices_);
            bgfx::setIndexBuffer(indices_);
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            if (textureSmoke)
                bgfx::setTexture(0, textureSampler_, texture_, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
                    | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
#endif
            auto state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            if (!sceneInput) state |= BGFX_STATE_CULL_CCW;
#else
            state |= BGFX_STATE_CULL_CCW;
#endif
            bgfx::setState(state);
            bgfx::submit(0, program_);
            }
            if (smoke && frames == 30)
            {
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, hasRuntimePacket && !runtimeScreenshotPath.empty()
                    ? runtimeScreenshotPath.c_str() : "astroengine-3d-smoke-memory-capture");
#else
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "astroengine-3d-smoke-memory-capture");
#endif
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
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        if (hasRuntimePacket)
            std::cout << "[info] runtime_packet_items=" << runtimePacket->items.size()
                << " runtime_packet_diagnostics=" << runtimePacket->diagnostics.size()
                << " runtime_items_submitted=" << packetItemsSubmitted
                << " runtime_gpu_mesh_cache=" << sceneRenderer_->cachedMeshCount()
                << " runtime_packet_valid=" << runtimePacketValid
                << " runtime_session_playing=" << (runtimeSession->state() == astro::runtime::RuntimeState::Playing)
                << " runtime_scene_is_copy=" << (runtimeSession->runtimeScene() != authoringScene)
                << " runtime_fixed_steps=" << playFixedSteps
                << " authoring_scene_unchanged=" << authoringSceneUnchanged
                << " runtime_screenshot_saved=" << callback_.screenshotSaved() << '\n';
#endif
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
        if (textureSmoke)
            std::cout << "[info] texture_smoke=1 texture_asset=" << sceneInput->textureAssetId.value
                << " texture_uv=0 texture_size=" << sceneInput->texture.width << 'x' << sceneInput->texture.height
                << " sampled_color_families=" << callback_.textureColorFamilies() << '\n';
        if (factorSmoke)
        {
            const auto mean = callback_.factorMeanRgb();
            std::cout << "[info] factor_smoke=1 srgb_source=1 base_color_factor="
                << sceneInput->material.baseColorFactor[0] << ',' << sceneInput->material.baseColorFactor[1] << ','
                << sceneInput->material.baseColorFactor[2] << ',' << sceneInput->material.baseColorFactor[3]
                << " output_srgb=1 expected_srgb=93 tolerance=3 matched_pixels="
                << callback_.factorColorPixels() << " mean_rgb=" << mean[0] << ',' << mean[1] << ',' << mean[2] << '\n';
        }
        if (sceneInput)
            std::cout << "[info] scene_mesh=1 model_asset=" << sceneInput->mesh.modelAssetId.value
                << " mesh_index=" << sceneInput->mesh.meshIndex << " primitive_index=" << sceneInput->mesh.primitiveIndex
                << " material_override=" << sceneInput->mesh.materialOverride.value
                << " base_color=" << sceneInput->material.baseColorFactor[0] << ','
                << sceneInput->material.baseColorFactor[1] << ',' << sceneInput->material.baseColorFactor[2] << ','
                << sceneInput->material.baseColorFactor[3] << " material_pixels=" << callback_.materialPixels()
                << " visible=" << sceneInput->mesh.visible << '\n';
#endif
 #ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        if (runtimeSession)
        {
            const auto stopped = runtimeSession->stopChecked();
            if (!stopped) throw std::runtime_error("Runtime Play stop failed: " + stopped.error().message);
        }
 #endif
        cleanup();
        return smoke && (!captureRequested || !callback_.completed() || !callback_.valid() || !submitted
            || frameTimesMs.size() < 10
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            || (materialSmoke && callback_.materialPixels() < 500)
            || (textureSmoke && !factorSmoke && callback_.textureColorFamilies() < 2)
            || (factorSmoke && callback_.factorColorPixels() < 500)
#endif
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
            || !runtimePacketValid || (hasRuntimePacket && (!callback_.screenshotSaved() || playFixedSteps == 0
                || !authoringSceneUnchanged || runtimeSession->state() != astro::runtime::RuntimeState::Stopped))
#endif
            ) ? 1 : 0;
    }
};
}

int main(int argc, char** argv)
{
    bool smoke = false;
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
    bool runtimePacketSmoke = false;
#endif
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
    bool materialSmoke = false;
    bool textureSmoke = false;
    bool factorSmoke = false;
#endif
    std::optional<std::filesystem::path> sceneProject;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") smoke = true;
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
    else if (argc == 2 && std::string_view(argv[1]) == "--runtime-packet-smoke")
    {
        smoke = true;
        runtimePacketSmoke = true;
    }
#endif
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
    else if (argc == 4 && (std::string_view(argv[1]) == "--scene-smoke"
            || std::string_view(argv[1]) == "--material-smoke"
            || std::string_view(argv[1]) == "--texture-smoke"
            || std::string_view(argv[1]) == "--base-color-factor-smoke")
        && std::string_view(argv[2]) == "--project-root")
    {
        smoke = true;
        materialSmoke = std::string_view(argv[1]) == "--material-smoke";
        textureSmoke = std::string_view(argv[1]) == "--texture-smoke";
        factorSmoke = std::string_view(argv[1]) == "--base-color-factor-smoke";
        sceneProject = pathFromUtf8(argv[3]);
    }
#endif
    else if (argc != 1)
    {
        std::cerr << "Usage: astro_render3d_bgfx_smoke [--smoke]"
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
            << " | --runtime-packet-smoke"
#endif
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            << " | --scene-smoke|--material-smoke|--texture-smoke|--base-color-factor-smoke --project-root <temporary-fixture-project>"
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
            const auto resolved = loadSceneMesh(*sceneProject, textureSmoke || factorSmoke, factorSmoke);
            return prototype.run(smoke, &resolved, materialSmoke, textureSmoke || factorSmoke, factorSmoke
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
                , nullptr
#endif
            );
        }
#endif
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
        if (runtimePacketSmoke)
        {
            auto fixture = makeRuntimePacketSmokeScene();
            const auto* authoringTransform = fixture.authoringScene.transform(fixture.movingEntity);
            if (!authoringTransform) throw std::runtime_error("Runtime smoke tracked entity has no authoring Transform");
            const auto initialTransform = authoringTransform->value;
            astro::runtime::RuntimeSession session;
            auto started = session.play(fixture.authoringScene,
                [trackedEntity = fixture.movingEntity](astro::scene::Scene& runtimeScene, const double deltaSeconds) {
                    const auto* current = runtimeScene.transform(trackedEntity);
                    if (!current) return astro::core::Result<void>::failure(
                        {astro::core::ErrorCode::NotFound, "Tracked runtime entity lost its Transform"});
                    auto next = current->value;
                    next.position.y += deltaSeconds * 0.1;
                    return runtimeScene.setTransform(trackedEntity, next);
                });
            if (!started) throw std::runtime_error("Runtime Play start failed: " + started.error().message);
            return prototype.run(smoke
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
                , nullptr, false, false, false
#endif
                , &session, &fixture.authoringScene, &fixture.resolver, fixture.movingEntity, initialTransform);
        }
#endif
        return prototype.run(smoke
#ifdef ASTRO_RENDER3D_HAS_SCENE_SMOKE
            , nullptr, false, false, false
#endif
#ifdef ASTRO_RENDER3D_HAS_RUNTIME_PACKET
            , nullptr, nullptr, nullptr, 0, {}
#endif
        );
    }
    catch (const std::exception& error) { std::cerr << "[error] " << error.what() << '\n'; return 1; }
}
