#include "astro/render2d/Canvas2D.hpp"
#include "astro/render2d/SceneSprites.hpp"
#include "astro/assets/AssetRegistry.hpp"
#include "astro/runtime/InputMapping.hpp"
#include "astro/runtime/RuntimeSession.hpp"
#include "astro/runtime/SDLCanvas2DRenderer.hpp"
#include "astro/runtime/SDLImageTextureCache.hpp"
#include "astro/runtime/SDLInputAdapter.hpp"
#include "astro/scene/Scene.hpp"
#include <SDL3/SDL.h>

#include <chrono>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

    SceneSetup()
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        sampleProject = std::filesystem::temp_directory_path()
            / ("AstroEngine 2D Sample " + std::to_string(unique));
        std::error_code error;
        if (!std::filesystem::create_directory(sampleProject, error) || error)
            throw std::runtime_error("Cannot create a unique temporary sample project: " + error.message());
        try
        {
            auto opened = assets::AssetRegistry::open(sampleProject);
            if (!opened) throw std::runtime_error(opened.error().message);
            assets.emplace(std::move(opened).value());
            const auto pngPath = sampleProject / "Assets" / "sample_sprite.png";
            writeSampleImage(pngPath);
            auto imported = assets->importImage("sample_sprite.png");
            if (!imported) throw std::runtime_error(imported.error().message);
        const auto created = authoring.createEntity("Player");
        if (!created) throw std::runtime_error(created.error().message);
        player = created.value();
            auto transform = authoring.transform(player)->value;
            transform.scale = {2.0, 2.0, 1.0};
            if (!authoring.setTransform(player, transform)) throw std::runtime_error("Cannot set sample sprite Transform");
            if (!authoring.setSpriteRenderer(player, imported.value().id))
                throw std::runtime_error("Cannot attach sample SpriteRenderer");
        }
        catch (...)
        {
            std::filesystem::remove_all(sampleProject, error);
            throw;
        }
    }

    ~SceneSetup()
    {
        std::error_code error;
        std::filesystem::remove_all(sampleProject, error);
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
    render2d::Canvas2D& canvas)
{
    canvas.clear();
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

int run(const bool smoke)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } } sdlLifetime;
    const auto flags = SDL_WINDOW_RESIZABLE | (smoke ? SDL_WINDOW_HIDDEN : 0);
    SdlWindow window(SDL_CreateWindow("AstroEngine — 2D Play Preview", 960, 640, flags), SDL_DestroyWindow);
    if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SdlRenderer renderer(SDL_CreateRenderer(window.get(), smoke ? "software" : nullptr), SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());

    SceneSetup setup;
    runtime::InputMapping input;
    bindControls(input);
    runtime::SDLInputAdapter inputAdapter(input);
    runtime::RuntimeSession play;
    const auto start = [&]() {
        return play.play(setup.authoring, [&input, player = setup.player](scene::Scene& runtimeScene, const double dt) {
            auto transform = runtimeScene.transform(player)->value;
            constexpr double speed = 4.0;
            transform.position.x += static_cast<double>(input.state("move_x").value) * speed * dt;
            transform.position.y += static_cast<double>(input.state("move_y").value) * speed * dt;
            return runtimeScene.setTransform(player, transform);
        });
    };
    const auto started = start();
    if (!started) throw std::runtime_error(started.error().message);
    runtime::SDLCanvas2DRenderer draw(renderer.get());
    runtime::SDLImageTextureCache textureCache(renderer.get(), *setup.assets);
    render2d::Canvas2D canvas;
    const render2d::Camera2D camera{{0.0, 0.0}, 48.0, 1.0};
    bool running = true;
    std::size_t frames = 0;

    const auto drawFrame = [&]() {
        const auto* state = play.runtimeScene();
        if (!state) throw std::runtime_error("Play session has no runtime Scene");
        const auto prepared = makeFrame(*state, *setup.assets, canvas);
        if (!prepared) throw std::runtime_error(prepared.error().message);
        int width = 0, height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer.get(), &width, &height))
            throw std::runtime_error(std::string("SDL_GetCurrentRenderOutputSize failed: ") + SDL_GetError());
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
        const auto* runtimeTransform = play.runtimeScene()->transform(setup.player);
        if (!runtimeTransform || runtimeTransform->value.position.x <= 0.0
            || setup.authoring.transform(setup.player)->value.position.x != 0.0)
            throw std::runtime_error("Play movement failed or changed the authoring Scene");
        if (!play.pause()) throw std::runtime_error("Play pause failed");
        const auto beforePausedTick = play.runtimeScene()->transform(setup.player)->value.position.x;
        const auto pausedTick = play.tick(1.0 / 60.0);
        if (!pausedTick || pausedTick.value().fixedStepCount != 0
            || play.runtimeScene()->transform(setup.player)->value.position.x != beforePausedTick)
            throw std::runtime_error("Paused Play tick advanced the simulation");
        const auto pausedX = play.runtimeScene()->transform(setup.player)->value.position.x;
        if (!play.stepOnce() || play.runtimeScene()->transform(setup.player)->value.position.x <= pausedX)
            throw std::runtime_error("Single-step Play did not advance the runtime Scene");
        drawFrame();
        SdlSurface pixels(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
        if (!pixels) throw std::runtime_error(std::string("SDL_RenderReadPixels failed: ") + SDL_GetError());
        const int sampleX = pixels->w / 2 + 2;
        const int sampleY = pixels->h / 2;
        std::uint8_t red = 0, green = 0, blue = 0, alpha = 0;
        if (!SDL_ReadSurfacePixel(pixels.get(), sampleX, sampleY, &red, &green, &blue, &alpha)
            || red < 200 || green < 140 || blue > 120 || alpha < 240)
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

int main(const int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--smoke")) return 2;
    try { return run(argc == 2); }
    catch (const std::exception& error)
    {
        std::cerr << "[2d-play:error] " << error.what() << '\n';
        return 1;
    }
}
