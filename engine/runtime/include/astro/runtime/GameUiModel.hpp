#pragma once

#include "astro/core/EventBus.hpp"
#include "astro/runtime/RuntimeSession.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace astro::runtime
{
enum class GameUiScreen : std::uint8_t { MainMenu, Gameplay, Paused, Settings };
enum class GameUiAction : std::uint8_t { StartGame, Pause, Resume, OpenSettings, Back, ReturnToMenu, Quit };
enum class GameUiEventKind : std::uint8_t
{
    StartRequested, PauseChanged, SettingsOpened, SettingsClosed, ReturnedToMenu, QuitRequested, SettingChanged
};

using GameSettingValue = std::variant<bool, double, std::string>;

struct GameSettingDefinition final
{
    std::string id;
    std::string label;
    GameSettingValue initialValue;
    double minimum = 0.0;
    double maximum = 1.0;
    std::vector<std::string> choices;
};

struct GameSetting final
{
    std::string id;
    std::string label;
    GameSettingValue value;
    double minimum = 0.0;
    double maximum = 1.0;
    std::vector<std::string> choices;
};

struct HudField final
{
    std::string id;
    std::string label;
    std::string text;
};

struct GameUiEvent final
{
    std::uint64_t sequence = 0;
    GameUiEventKind kind = GameUiEventKind::StartRequested;
    GameUiScreen screen = GameUiScreen::MainMenu;
    std::string settingId;
    GameSettingValue settingValue = false;
};

// Backend-neutral runtime UI state and controls. Rendering is supplied by a future adapter.
class GameUiModel final
{
public:
    [[nodiscard]] GameUiScreen screen() const noexcept { return screen_; }
    [[nodiscard]] bool paused() const noexcept { return screen_ == GameUiScreen::Paused; }
    [[nodiscard]] std::vector<GameUiAction> availableActions() const;
    [[nodiscard]] const std::map<std::string, GameSetting>& settings() const noexcept { return settings_; }
    [[nodiscard]] const std::map<std::string, HudField>& hudFields() const noexcept { return hudFields_; }

    core::Result<void> addSetting(GameSettingDefinition definition);
    core::Result<void> setSetting(std::string_view id, GameSettingValue value, core::EventBus& events);
    core::Result<void> addHudField(std::string id, std::string label, std::string initialText = {});
    core::Result<void> setHudText(std::string_view id, std::string text);
    core::Result<void> removeHudField(std::string_view id);

    core::Result<void> startGame(RuntimeSession& session, const scene::Scene& authoringScene,
        FixedStep fixedStep, core::EventBus& events);
    core::Result<void> pause(RuntimeSession& session, core::EventBus& events);
    core::Result<void> resume(RuntimeSession& session, core::EventBus& events);
    core::Result<void> openSettings(core::EventBus& events);
    core::Result<void> closeSettings(core::EventBus& events);
    core::Result<void> returnToMenu(RuntimeSession& session, core::EventBus& events);
    core::Result<void> requestQuit(core::EventBus& events);

private:
    core::Result<void> publish(core::EventBus& events, GameUiEventKind kind,
        std::string settingId = {}, GameSettingValue settingValue = false);
    GameUiScreen screen_ = GameUiScreen::MainMenu;
    GameUiScreen settingsReturnScreen_ = GameUiScreen::MainMenu;
    std::map<std::string, GameSetting> settings_;
    std::map<std::string, HudField> hudFields_;
    std::uint64_t nextEventSequence_ = 1;
};
}
