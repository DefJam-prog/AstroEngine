#include "astro/runtime/ImGuiGameUiAdapter.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
class SdlLifetime final
{
public:
    SdlLifetime()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    }
    ~SdlLifetime() { SDL_Quit(); }
    SdlLifetime(const SdlLifetime&) = delete;
    SdlLifetime& operator=(const SdlLifetime&) = delete;
};

class Demo final
{
public:
    Demo(const bool smoke, const bool updateGolden, std::string goldenPath,
        SDL_Window* window, SDL_Renderer* renderer)
        : smoke_(smoke), updateGolden_(updateGolden), goldenPath_(std::move(goldenPath)),
          window_(window), renderer_(renderer), scene_(1, "Game UI demo"),
          adapter_(ui_, session_, scene_, [this](astro::scene::Scene&, const double) {
              ++score_;
              return ui_.setHudText("hud.score", std::to_string(score_));
          }, events_)
    {
    }

    int run()
    {
        const auto player = scene_.createEntity("Demo player");
        if (!player) throw std::runtime_error(player.error().message);
        if (!ui_.addHudField("hud.score", "Score", "0")
            || !ui_.addHudField("hud.level", "Level", "1")
            || !ui_.addSetting({"ui.scale", "UI scale", 1.0, 0.75, 1.5, {}})
            || !ui_.addSetting({"display.fullscreen", "Fullscreen", false, 0.0, 1.0, {}}))
            throw std::runtime_error("Failed to initialize Game UI model");
        const auto subscription = events_.subscribe<astro::runtime::GameUiEvent>([this](const auto& event) {
            if (event.kind == astro::runtime::GameUiEventKind::SettingChanged
                && event.settingId == "display.fullscreen")
            {
                if (!SDL_SetWindowFullscreen(window_, std::get<bool>(event.settingValue)))
                    throw std::runtime_error(SDL_GetError());
            }
        });
        if (!subscription) throw std::runtime_error(subscription.error().message);
        const auto initialized = adapter_.initialize(window_, renderer_);
        if (!initialized) throw std::runtime_error(initialized.error().message);
        if (smoke_) return runSmoke();

        bool running = true;
        auto previousTime = SDL_GetTicksNS();
        while (running && !adapter_.quitRequested())
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                adapter_.processEvent(event);
                if (event.type == SDL_EVENT_QUIT) running = false;
            }
            const auto now = SDL_GetTicksNS();
            const auto elapsed = static_cast<double>(now - previousTime) / 1'000'000'000.0;
            previousTime = now;
            if (session_.state() == astro::runtime::RuntimeState::Playing
                || session_.state() == astro::runtime::RuntimeState::Paused)
            {
                const auto frame = session_.tick(elapsed);
                if (!frame) std::cerr << "Runtime tick: " << frame.error().message << '\n';
            }
            const auto rendered = adapter_.render();
            if (!rendered) std::cerr << "Game UI render: " << rendered.error().message << '\n';
            SDL_Delay(8);
        }
        (void)events_.unsubscribe(subscription.value());
        return 0;
    }

private:
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

    static std::vector<std::uint8_t> rgbaPixels(SDL_Surface* source)
    {
        Surface converted(SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!converted) throw std::runtime_error(SDL_GetError());
        if (converted->w <= 0 || converted->h <= 0 || converted->w > 4096 || converted->h > 4096)
            throw std::runtime_error("Visual reference has invalid dimensions");
        if (SDL_MUSTLOCK(converted.get()) && !SDL_LockSurface(converted.get()))
            throw std::runtime_error(SDL_GetError());
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(converted->w)
            * static_cast<std::size_t>(converted->h) * 4);
        const auto* sourceBytes = static_cast<const std::uint8_t*>(converted->pixels);
        const auto rowBytes = static_cast<std::size_t>(converted->w) * 4;
        for (int row = 0; row < converted->h; ++row)
            std::copy_n(sourceBytes + static_cast<std::size_t>(row) * converted->pitch,
                rowBytes, pixels.data() + static_cast<std::size_t>(row) * rowBytes);
        if (SDL_MUSTLOCK(converted.get())) SDL_UnlockSurface(converted.get());
        return pixels;
    }

    void compareOrWriteGolden()
    {
        Surface rendered(SDL_RenderReadPixels(renderer_, nullptr), SDL_DestroySurface);
        if (!rendered) throw std::runtime_error(std::string("Could not capture Game UI frame: ") + SDL_GetError());
        if (updateGolden_)
        {
            if (!SDL_SaveBMP(rendered.get(), goldenPath_.c_str()))
                throw std::runtime_error(std::string("Could not write visual reference: ") + SDL_GetError());
            return;
        }
        Surface golden(SDL_LoadBMP(goldenPath_.c_str()), SDL_DestroySurface);
        if (!golden) throw std::runtime_error(std::string("Could not read visual reference: ") + SDL_GetError());
        if (rendered->w != golden->w || rendered->h != golden->h)
            throw std::runtime_error("Game UI visual reference dimensions changed");
        const auto actual = rgbaPixels(rendered.get());
        const auto reference = rgbaPixels(golden.get());
        std::size_t pixelsOverChannelTolerance = 0;
        std::uint64_t totalAbsoluteError = 0;
        const auto pixelCount = actual.size() / 4;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            bool differs = false;
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const auto a = static_cast<int>(actual[pixel * 4 + channel]);
                const auto b = static_cast<int>(reference[pixel * 4 + channel]);
                const auto error = static_cast<std::uint64_t>(std::abs(a - b));
                totalAbsoluteError += error;
                if (error > 12) differs = true;
            }
            if (differs) ++pixelsOverChannelTolerance;
        }
        const auto channelCount = actual.size();
        const auto meanAbsoluteError = static_cast<double>(totalAbsoluteError) / static_cast<double>(channelCount);
        const auto changedRatio = static_cast<double>(pixelsOverChannelTolerance) / static_cast<double>(pixelCount);
        if (meanAbsoluteError > 1.5 || changedRatio > 0.01)
            throw std::runtime_error("Game UI main-menu screenshot exceeded tolerance: mean MAE="
                + std::to_string(meanAbsoluteError) + ", changed pixels=" + std::to_string(changedRatio));
    }

    int runSmoke()
    {
        std::size_t renderedScreens = 0;
        const auto render = [&]() {
            const auto result = adapter_.render();
            if (!result || adapter_.lastRenderedVertexCount() == 0)
                throw std::runtime_error(result ? "Game UI produced no render vertices" : result.error().message);
            ++renderedScreens;
        };
        render(); // main menu
        if (!goldenPath_.empty()) compareOrWriteGolden();
        const auto started = ui_.startGame(session_, scene_,
            [this](astro::scene::Scene&, const double) {
                ++score_;
                return ui_.setHudText("hud.score", std::to_string(score_));
            }, events_);
        if (!started) throw std::runtime_error(started.error().message);
        render(); // HUD during gameplay
        const auto paused = ui_.pause(session_, events_);
        if (!paused) throw std::runtime_error(paused.error().message);
        render(); // pause menu
        const auto opened = ui_.openSettings(events_);
        if (!opened) throw std::runtime_error(opened.error().message);
        const auto changed = ui_.setSetting("ui.scale", 1.2, events_);
        if (!changed) throw std::runtime_error(changed.error().message);
        render(); // settings panel
        const auto closed = ui_.closeSettings(events_);
        const auto resumed = ui_.resume(session_, events_);
        if (!closed || !resumed) throw std::runtime_error("Failed to resume after Settings");
        render();
        const auto menu = ui_.returnToMenu(session_, events_);
        if (!menu) throw std::runtime_error(menu.error().message);
        render();
        return renderedScreens == 6 && ui_.screen() == astro::runtime::GameUiScreen::MainMenu
            && session_.state() == astro::runtime::RuntimeState::Stopped ? 0 : 1;
    }

    bool smoke_;
    bool updateGolden_;
    std::string goldenPath_;
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    astro::runtime::GameUiModel ui_;
    astro::runtime::RuntimeSession session_;
    astro::core::EventBus events_;
    astro::scene::Scene scene_;
    int score_ = 0;
    astro::runtime::ImGuiGameUiAdapter adapter_;
};
}

int main(int argc, char** argv)
{
    const bool smoke = argc >= 2 && std::string_view(argv[1]) == "--smoke";
    bool updateGolden = false;
    std::string goldenPath;
    if (argc == 3 && smoke) goldenPath = argv[2];
    else if (argc == 4 && smoke && std::string_view(argv[2]) == "--update-golden")
    {
        updateGolden = true;
        goldenPath = argv[3];
    }
    else if (argc != 1 && !(argc == 2 && smoke))
    {
        std::cerr << "Usage: astro_game_ui_demo [--smoke [golden.bmp | --update-golden path]]\n";
        return 2;
    }
    try
    {
        SdlLifetime sdl;
        const auto flags = SDL_WINDOW_RESIZABLE | (smoke ? SDL_WINDOW_HIDDEN : 0);
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("AstroEngine — Game UI prototype", 960, 640, flags), SDL_DestroyWindow);
        if (!window) throw std::runtime_error(SDL_GetError());
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            SDL_CreateRenderer(window.get(), smoke ? "software" : nullptr), SDL_DestroyRenderer);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        Demo demo(smoke, updateGolden, std::move(goldenPath), window.get(), renderer.get());
        return demo.run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[error] " << error.what() << '\n';
        return 1;
    }
}
