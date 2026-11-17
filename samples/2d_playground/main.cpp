#include "astro/render2d/Canvas2D.hpp"
#include "astro/render2d/SceneSprites.hpp"
#include "astro/assets/AssetRegistry.hpp"
#include "astro/runtime/InputMapping.hpp"
#include "astro/runtime/RuntimeSession.hpp"
#include "astro/runtime/SDLCanvas2DRenderer.hpp"
#include "astro/runtime/SDLImageTextureCache.hpp"
#include "astro/runtime/SDLInputAdapter.hpp"
#include "astro/scene/Scene.hpp"
#include "astro/scene/SceneFile.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace astro;
using SdlWindow = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using SdlRenderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
using SdlSurface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

struct SceneSetup
{
    scene::Scene authoring{1, "2D Playground"};
    scene::EntityId player = 0;
    std::filesystem::path sampleProject;
    std::optional<assets::AssetRegistry> assets;
    bool temporaryProject = false;

    SceneSetup()
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        sampleProject = std::filesystem::temp_directory_path()
            / ("AstroEngine 2D Sample " + std::to_string(unique));
        std::error_code error;
        if (!std::filesystem::create_directory(sampleProject, error) || error)
            throw std::runtime_error("Cannot create a unique temporary sample project: " + error.message());
        temporaryProject = true;
        try
        {
            auto opened = assets::AssetRegistry::open(sampleProject);
            if (!opened) throw std::runtime_error(opened.error().message);
            assets.emplace(std::move(opened).value());
            const auto pngPath = sampleProject / "Assets" / "sample_sprite.png";
            writeSampleImage(pngPath);
            auto imported = assets->importImage("sample_sprite.png");
            if (!imported) throw std::runtime_error(imported.error().message);
            const auto group = authoring.createEntity("Sprite Group");
            if (!group) throw std::runtime_error(group.error().message);
            auto groupTransform = authoring.transform(group.value())->value;
            groupTransform.rotationDegrees.z = 15.0;
            groupTransform.scale = {1.5, 1.5, 1.0};
            if (!authoring.setTransform(group.value(), groupTransform)) throw std::runtime_error("Cannot set sample group Transform");
            const auto created = authoring.createEntity("Player", group.value());
            if (!created) throw std::runtime_error(created.error().message);
            player = created.value();
            auto transform = authoring.transform(player)->value;
            transform.scale = {2.0, 2.0, 1.0};
            if (!authoring.setTransform(player, transform)) throw std::runtime_error("Cannot set sample sprite Transform");
            if (!authoring.setSpriteRenderer(player, imported.value().id))
                throw std::runtime_error("Cannot attach sample SpriteRenderer");
            std::filesystem::create_directories(sampleProject / "Scenes", error);
            if (error) throw std::runtime_error("Cannot create temporary sample Scenes directory: " + error.message());
            const auto saved = scene::saveSceneFile(sampleProject / "Scenes" / "Preview.scene", authoring);
            if (!saved) throw std::runtime_error(saved.error().message);
            openProject(sampleProject, "Scenes/Preview.scene", true);
        }
        catch (...)
        {
            std::filesystem::remove_all(sampleProject, error);
            throw;
        }
    }

    SceneSetup(const std::filesystem::path& project, const std::filesystem::path& sceneFile)
    {
        openProject(project, sceneFile, false);
    }

    ~SceneSetup()
    {
        std::error_code error;
        if (temporaryProject && !sampleProject.empty()) std::filesystem::remove_all(sampleProject, error);
    }

    void openProject(const std::filesystem::path& project, const std::filesystem::path& sceneFile,
        const bool controlNamedPlayer)
    {
        std::error_code error;
        const auto root = std::filesystem::canonical(project, error);
        if (error || !std::filesystem::is_directory(root, error))
            throw std::runtime_error("Project root must be an existing directory");
        const auto requireDirectory = [&](const std::filesystem::path& path) {
            const auto status = std::filesystem::symlink_status(path, error);
            if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
                throw std::runtime_error("Project preview requires existing non-symlink Assets, Library/Assets, and .astro directories");
        };
        requireDirectory(root / "Assets");
        requireDirectory(root / "Library" / "Assets");
        requireDirectory(root / ".astro");
        const auto indexPath = root / ".astro" / "assets.index";
        const auto indexStatus = std::filesystem::symlink_status(indexPath, error);
        if (error || std::filesystem::is_symlink(indexStatus) || !std::filesystem::is_regular_file(indexStatus))
            throw std::runtime_error("Project preview requires an existing regular .astro/assets.index");
        if (sceneFile.empty() || sceneFile.is_absolute())
            throw std::runtime_error("Scene file must be a non-empty relative path inside the project");
        const auto normalizedScene = sceneFile.lexically_normal();
        for (const auto& part : normalizedScene)
            if (part == "..") throw std::runtime_error("Scene file path cannot traverse outside the project");
        const auto scenePath = std::filesystem::canonical(root / normalizedScene, error);
        if (error || !std::filesystem::is_regular_file(scenePath, error))
            throw std::runtime_error("Scene path must identify an existing project file");
        const auto relative = scenePath.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("Scene file resolves outside the project root");
        auto registry = assets::AssetRegistry::open(root);
        if (!registry) throw std::runtime_error(registry.error().message);
        auto loaded = scene::loadSceneFile(scenePath);
        if (!loaded) throw std::runtime_error(loaded.error().message);

        sampleProject = root;
        assets.emplace(std::move(registry).value());
        authoring = std::move(loaded).value();
        player = 0;
        if (controlNamedPlayer)
            for (const auto& entity : authoring.entities())
                if (entity.name == "Player") { player = entity.id; break; }
    }

    static void writeSampleImage(const std::filesystem::path& path)
    {
        auto crc = [](const std::uint8_t* bytes, const std::size_t count) {
            std::uint32_t value = 0xFFFFFFFFu;
            for (std::size_t i = 0; i < count; ++i)
            {
                value ^= bytes[i];
                for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
            }
            return ~value;
        };
        auto appendBe32 = [](std::vector<std::uint8_t>& out, const std::uint32_t value) {
            out.push_back(static_cast<std::uint8_t>(value >> 24));
            out.push_back(static_cast<std::uint8_t>(value >> 16));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
            out.push_back(static_cast<std::uint8_t>(value));
        };
        std::vector<std::uint8_t> png{137,80,78,71,13,10,26,10};
        auto addChunk = [&](const std::array<char, 4> type, const std::vector<std::uint8_t>& data) {
            appendBe32(png, static_cast<std::uint32_t>(data.size()));
            const auto start = png.size();
            for (const auto ch : type) png.push_back(static_cast<std::uint8_t>(ch));
            png.insert(png.end(), data.begin(), data.end());
            appendBe32(png, crc(png.data() + start, png.size() - start));
        };
        std::vector<std::uint8_t> ihdr;
        appendBe32(ihdr, 32); appendBe32(ihdr, 32);
        ihdr.insert(ihdr.end(), {8,6,0,0,0});
        addChunk({'I','H','D','R'}, ihdr);
        std::vector<std::uint8_t> raw;
        for (int y = 0; y < 32; ++y)
        {
            raw.push_back(0);
            for (int x = 0; x < 32; ++x) raw.insert(raw.end(), {245,195,72,255});
        }
        std::vector<std::uint8_t> zlib{0x78,0x01,0x01};
        const auto length = static_cast<std::uint16_t>(raw.size());
        zlib.push_back(static_cast<std::uint8_t>(length)); zlib.push_back(static_cast<std::uint8_t>(length >> 8));
        const auto inverse = static_cast<std::uint16_t>(~length);
        zlib.push_back(static_cast<std::uint8_t>(inverse)); zlib.push_back(static_cast<std::uint8_t>(inverse >> 8));
        zlib.insert(zlib.end(), raw.begin(), raw.end());
        std::uint32_t a = 1, b = 0;
        for (const auto byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
        appendBe32(zlib, (b << 16) | a);
        addChunk({'I','D','A','T'}, zlib);
        addChunk({'I','E','N','D'}, {});
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!output) throw std::runtime_error("Cannot write temporary sample sprite");
    }
};

void bind(runtime::InputMapping& input, const std::string& action, const SDL_Scancode key, const float scale)
{
    const auto result = input.bind(action, {{runtime::InputDevice::KeyboardKey, key}, scale, 0.0f});
    if (!result) throw std::runtime_error(result.error().message);
}

void bindControls(runtime::InputMapping& input)
{
    bind(input, "move_x", SDL_SCANCODE_A, -1.0f);
    bind(input, "move_x", SDL_SCANCODE_D, 1.0f);
    bind(input, "move_x", SDL_SCANCODE_LEFT, -1.0f);
    bind(input, "move_x", SDL_SCANCODE_RIGHT, 1.0f);
    bind(input, "move_y", SDL_SCANCODE_S, -1.0f);
    bind(input, "move_y", SDL_SCANCODE_W, 1.0f);
    bind(input, "move_y", SDL_SCANCODE_DOWN, -1.0f);
    bind(input, "move_y", SDL_SCANCODE_UP, 1.0f);
}

core::Result<void> makeFrame(const scene::Scene& scene, const assets::AssetRegistry& assetRegistry,
    render2d::Canvas2D& canvas, const bool demoGeometry)
{
    canvas.clear();
    if (!demoGeometry) return render2d::appendSceneSprites(scene, assetRegistry, canvas, {100.0, 10});
    auto add = [&](const render2d::Vec2 center, const render2d::Vec2 size,
        const render2d::Color color, const std::int32_t layer) {
        return canvas.addRectangle({center, size, color, layer});
    };
    auto added = add({0.0, -2.4}, {14.0, 0.7}, {72, 155, 105, 255}, 0);
    if (!added) return added;
    added = add({-3.5, -0.6}, {3.0, 0.35}, {85, 115, 180, 255}, 1);
    if (!added) return added;
    added = add({2.7, 0.5}, {2.2, 0.35}, {85, 115, 180, 255}, 1);
    if (!added) return added;
    added = add({5.5, 1.5}, {0.6, 0.6}, {214, 86, 78, 255}, 5);
    if (!added) return added;
    return render2d::appendSceneSprites(scene, assetRegistry, canvas, {100.0, 10});
}

int run(const bool smoke, const std::optional<std::filesystem::path>& project,
    const std::optional<std::filesystem::path>& sceneFile)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } } sdlLifetime;
    const auto flags = SDL_WINDOW_RESIZABLE | (smoke ? SDL_WINDOW_HIDDEN : 0);
    SdlWindow window(SDL_CreateWindow("AstroEngine — 2D Play Preview", 960, 640, flags), SDL_DestroyWindow);
    if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SdlRenderer renderer(SDL_CreateRenderer(window.get(), smoke ? "software" : nullptr), SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());

    auto setup = project
        ? std::make_unique<SceneSetup>(*project, *sceneFile)
        : std::make_unique<SceneSetup>();
    runtime::InputMapping input;
    bindControls(input);
    runtime::SDLInputAdapter inputAdapter(input);
    runtime::RuntimeSession play;
    const auto start = [&]() {
        return play.play(setup->authoring, [&input, player = setup->player](scene::Scene& runtimeScene, const double dt) {
            if (player == 0) return core::Result<void>::success();
            const auto* currentTransform = runtimeScene.transform(player);
            if (!currentTransform)
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Selected Play sprite Transform disappeared"});
            auto transform = currentTransform->value;
            constexpr double speed = 4.0;
            transform.position.x += static_cast<double>(input.state("move_x").value) * speed * dt;
            transform.position.y += static_cast<double>(input.state("move_y").value) * speed * dt;
            return runtimeScene.setTransform(player, transform);
        });
    };
    const auto started = start();
    if (!started) throw std::runtime_error(started.error().message);
    runtime::SDLCanvas2DRenderer draw(renderer.get());
    runtime::SDLImageTextureCache textureCache(renderer.get(), *setup->assets);
    render2d::Canvas2D canvas;
    const render2d::Camera2D demoCamera{{0.0, 0.0}, 48.0, 1.0};
    bool running = true;
    std::size_t frames = 0;

    const auto drawFrame = [&]() {
        const auto* state = play.runtimeScene();
        if (!state) throw std::runtime_error("Play session has no runtime Scene");
        const auto prepared = makeFrame(*state, *setup->assets, canvas, !project.has_value());
        if (!prepared) throw std::runtime_error(prepared.error().message);
        int width = 0, height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer.get(), &width, &height))
            throw std::runtime_error(std::string("SDL_GetCurrentRenderOutputSize failed: ") + SDL_GetError());
        auto camera = demoCamera;
        if (project && !canvas.sprites().empty() && width > 0 && height > 0)
        {
            double minimumX = std::numeric_limits<double>::infinity();
            double minimumY = std::numeric_limits<double>::infinity();
            double maximumX = -std::numeric_limits<double>::infinity();
            double maximumY = -std::numeric_limits<double>::infinity();
            for (const auto& sprite : canvas.sprites())
                for (const auto corner : sprite.worldCorners)
                {
                    minimumX = std::min(minimumX, corner.x);
                    minimumY = std::min(minimumY, corner.y);
                    maximumX = std::max(maximumX, corner.x);
                    maximumY = std::max(maximumY, corner.y);
                }
            camera.center = {(minimumX + maximumX) * 0.5, (minimumY + maximumY) * 0.5};
            const auto worldWidth = std::max(maximumX - minimumX, 1e-9);
            const auto worldHeight = std::max(maximumY - minimumY, 1e-9);
            camera.pixelsPerUnit = std::min(static_cast<double>(width) / worldWidth,
                static_cast<double>(height) / worldHeight) * 0.85;
            camera.pixelsPerUnit = std::clamp(camera.pixelsPerUnit, 1e-8, 10000.0);
        }
        std::optional<core::Error> textureError;
        const runtime::SDLTextureResolver resolveTexture = [&textureCache, &textureError](const assets::AssetId id) {
            auto resolved = textureCache.texture(id);
            if (!resolved) { textureError = resolved.error(); return static_cast<SDL_Texture*>(nullptr); }
            return resolved.value();
        };
        const auto rendered = draw.render(canvas, camera,
            {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)}, resolveTexture);
        if (!rendered) throw std::runtime_error(rendered.error().message);
        if (textureError) throw std::runtime_error(textureError->message);
    };

    if (smoke)
    {
        input.beginFrame();
        auto pressed = input.setDigital({runtime::InputDevice::KeyboardKey, SDL_SCANCODE_D}, true);
        if (!pressed) throw std::runtime_error(pressed.error().message);
        const auto tick = play.tick(1.0 / 60.0);
        if (!tick || tick.value().fixedStepCount != 1) throw std::runtime_error("Fixed Play tick did not run exactly once");
        if (setup->player != 0)
        {
            const auto* runtimeTransform = play.runtimeScene()->transform(setup->player);
            if (!runtimeTransform || runtimeTransform->value.position.x <= 0.0
                || setup->authoring.transform(setup->player)->value.position.x != 0.0)
                throw std::runtime_error("Play movement failed or changed the authoring Scene");
        }
        if (!play.pause()) throw std::runtime_error("Play pause failed");
        const auto beforePausedTick = setup->player == 0 ? 0.0
            : play.runtimeScene()->transform(setup->player)->value.position.x;
        const auto pausedTick = play.tick(1.0 / 60.0);
        const auto afterPausedTick = setup->player == 0 ? 0.0
            : play.runtimeScene()->transform(setup->player)->value.position.x;
        if (!pausedTick || pausedTick.value().fixedStepCount != 0 || afterPausedTick != beforePausedTick)
            throw std::runtime_error("Paused Play tick advanced the simulation");
        const auto pausedX = setup->player == 0 ? 0.0
            : play.runtimeScene()->transform(setup->player)->value.position.x;
        if (!play.stepOnce())
            throw std::runtime_error("Single-step Play did not advance the runtime Scene");
        if (setup->player != 0
            && play.runtimeScene()->transform(setup->player)->value.position.x <= pausedX)
            throw std::runtime_error("Single-step Play did not advance the selected Player");
        drawFrame();
        SdlSurface pixels(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
        if (!pixels) throw std::runtime_error(std::string("SDL_RenderReadPixels failed: ") + SDL_GetError());
        const int sampleX = pixels->w / 2 + 2;
        const int sampleY = pixels->h / 2;
        std::uint8_t red = 0, green = 0, blue = 0, alpha = 0;
        const bool read = SDL_ReadSurfacePixel(pixels.get(), sampleX, sampleY, &red, &green, &blue, &alpha);
        const bool expectedSample = setup->player == 0
            ? (read && red > 150 && green > 70 && blue < 100 && alpha > 240)
            : (read && red > 200 && green > 140 && blue < 120 && alpha > 240);
        if (!expectedSample)
            throw std::runtime_error("2D render smoke did not find the player color at the projected center");
        return 0;
    }

    auto previous = std::chrono::steady_clock::now();
    while (running)
    {
        input.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            const auto handled = inputAdapter.processEvent(event);
            if (!handled) std::cerr << "[input] " << handled.error().message << '\n';
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
                else if (event.key.scancode == SDL_SCANCODE_P)
                {
                    const auto result = play.state() == runtime::RuntimeState::Playing ? play.pause() : play.resume();
                    if (!result) std::cerr << "[play] " << result.error().message << '\n';
                }
                else if (event.key.scancode == SDL_SCANCODE_R)
                {
                    play.stop();
                    const auto result = start();
                    if (!result) std::cerr << "[play] " << result.error().message << '\n';
                }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (play.state() == runtime::RuntimeState::Playing)
        {
            const auto advanced = play.tick(elapsed);
            if (!advanced) throw std::runtime_error(advanced.error().message);
        }
        drawFrame();
        ++frames;
    }
    play.stop();
    std::cout << "[2d-play] frames=" << frames << " scene=runtime-copy\n";
    return 0;
}
}

int wmain(const int argc, wchar_t** argv)
{
    bool smoke = false;
    std::optional<std::filesystem::path> project;
    std::optional<std::filesystem::path> sceneFile;
    for (int index = 1; index < argc; ++index)
    {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--smoke") smoke = true;
        else if (argument == L"--project" && index + 1 < argc)
        {
            if (project) return 2;
            project = std::filesystem::path(argv[++index]);
        }
        else if (argument == L"--scene" && index + 1 < argc)
        {
            if (sceneFile) return 2;
            sceneFile = std::filesystem::path(argv[++index]);
        }
        else if (argument == L"--help")
        {
            std::wcout << L"Usage: astro_2d_playground [--project <directory> --scene <relative-scene-file>] [--smoke]\n";
            return 0;
        }
        else return 2;
    }
    if (project.has_value() != sceneFile.has_value()) return 2;
    try { return run(smoke, project, sceneFile); }
    catch (const std::exception& error)
    {
        std::cerr << "[2d-play:error] " << error.what() << '\n';
        return 1;
    }
}
