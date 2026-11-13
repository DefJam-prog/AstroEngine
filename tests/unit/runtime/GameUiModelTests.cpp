#include "astro/runtime/GameUiModel.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

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
    runtime::GameUiModel ui;
    core::EventBus events;
    std::vector<runtime::GameUiEvent> observed;
    const auto subscription = events.subscribe<runtime::GameUiEvent>([&](const auto& event) {
        observed.push_back(event);
    });

    check(ui.screen() == runtime::GameUiScreen::MainMenu && !ui.paused(), "begin in Main Menu");
    const auto menuActions = ui.availableActions();
    check(menuActions.size() == 3 && menuActions[0] == runtime::GameUiAction::StartGame
        && menuActions[1] == runtime::GameUiAction::OpenSettings
        && menuActions[2] == runtime::GameUiAction::Quit, "expose main menu actions in stable order");

    check(static_cast<bool>(ui.addSetting({"audio.master", "Master volume", 0.8, 0.0, 1.0, {}})),
        "register a bounded numeric setting");
    check(static_cast<bool>(ui.addSetting({"display.fullscreen", "Fullscreen", false, 0.0, 1.0, {}})),
        "register a boolean setting");
    check(static_cast<bool>(ui.addSetting({"language", "Language", std::string{"en"}, 0.0, 1.0, {"en", "ru"}})),
        "register a choice setting");
    check(!ui.addSetting({"audio.master", "Duplicate", 0.2, 0.0, 1.0, {}}), "reject duplicate setting IDs");
    check(!ui.addSetting({"invalid id", "Bad ID", false, 0.0, 1.0, {}}), "reject unsafe setting IDs");
    check(!ui.addSetting({"bad.range", "Bad range", 0.5, 1.0, 0.0, {}}), "reject invalid setting range");
    check(!ui.addSetting({"bad.default", "Bad default", std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0, {}}),
        "reject non-finite numeric setting default");
    check(!ui.addSetting({"bad.choices", "Bad choices", std::string{"x"}, 0.0, 1.0, {"x", "x"}}),
        "reject duplicate choice options");

    check(static_cast<bool>(ui.addHudField("hud.score", "Очки", "0")), "register UTF-8 HUD field");
    check(!ui.addHudField("bad id", "Invalid", ""), "reject invalid HUD ID");
    check(!ui.addHudField("bad.utf8", std::string("\xC0\xAF", 2), ""), "reject invalid UTF-8 label");
    check(static_cast<bool>(ui.setHudText("hud.score", "42"))
        && ui.hudFields().at("hud.score").text == "42", "update HUD text");
    check(!ui.setHudText("missing", "1"), "reject update for unknown HUD field");

    check(!ui.setSetting("audio.master", 0.5, events), "settings cannot change outside Settings screen");
    check(static_cast<bool>(ui.openSettings(events)) && ui.screen() == runtime::GameUiScreen::Settings,
        "open Settings from Main Menu");
    const auto settingsActions = ui.availableActions();
    check(settingsActions.size() == 1 && settingsActions[0] == runtime::GameUiAction::Back,
        "expose Back action from Settings");
    check(!ui.setSetting("audio.master", std::numeric_limits<double>::infinity(), events),
        "reject non-finite setting value");
    check(!ui.setSetting("audio.master", true, events), "reject setting type mismatch");
    check(!ui.setSetting("audio.master", 1.5, events), "reject setting value outside range");
    check(!ui.setSetting("language", std::string{"de"}, events), "reject unlisted choice");
    check(static_cast<bool>(ui.setSetting("audio.master", 0.4, events))
        && std::get<double>(ui.settings().at("audio.master").value) == 0.4,
        "accept and store valid setting value");
    check(observed.size() == 2 && observed[0].sequence == 1
        && observed[0].kind == runtime::GameUiEventKind::SettingsOpened
        && observed[1].sequence == 2 && observed[1].kind == runtime::GameUiEventKind::SettingChanged
        && observed[1].settingId == "audio.master" && observed[1].screen == runtime::GameUiScreen::Settings,
        "publish validated setting change event");
    check(static_cast<bool>(ui.closeSettings(events)) && ui.screen() == runtime::GameUiScreen::MainMenu,
        "return to originating screen after Settings");
    check(static_cast<bool>(ui.requestQuit(events)) && observed.back().kind == runtime::GameUiEventKind::QuitRequested,
        "publish quit request without changing model state");
    check(static_cast<bool>(ui.removeHudField("hud.score")) && ui.hudFields().empty(), "remove HUD field");
    if (subscription) check(static_cast<bool>(events.unsubscribe(subscription.value())), "unsubscribe game UI observer");

    if (failures == 0) std::cout << "All Game UI model unit tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
