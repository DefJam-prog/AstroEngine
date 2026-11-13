#include "astro/runtime/GameUiModel.hpp"

#include <cstdlib>
#include <iostream>
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
    scene::Scene authoringScene{701, "Game UI integration"};
    const auto playerResult = authoringScene.createEntity("Player");
    check(static_cast<bool>(playerResult), "create authoring scene player");
    if (!playerResult) return EXIT_FAILURE;
    const auto player = playerResult.value();

    runtime::GameUiModel ui;
    runtime::RuntimeSession session;
    core::EventBus events;
    std::vector<runtime::GameUiEvent> observed;
    const auto subscription = events.subscribe<runtime::GameUiEvent>([&](const auto& event) {
        observed.push_back(event);
    });
    check(static_cast<bool>(subscription), "subscribe to Game UI events");
    check(static_cast<bool>(ui.addSetting({"audio.master", "Master volume", 1.0, 0.0, 1.0, {}})),
        "register settings value");
    check(static_cast<bool>(ui.addHudField("hud.score", "Score", "0")), "register score HUD field");
    check(!ui.pause(session, events), "reject pause before a game starts");

    std::uint32_t fixedTicks = 0;
    const auto started = ui.startGame(session, authoringScene,
        [&](scene::Scene& playScene, const double) {
            ++fixedTicks;
            auto transform = playScene.transform(player)->value;
            transform.position.x += 1.0;
            return playScene.setTransform(player, transform);
        }, events);
    check(static_cast<bool>(started) && ui.screen() == runtime::GameUiScreen::Gameplay
        && session.state() == runtime::RuntimeState::Playing, "start game from main menu");
    check(!ui.startGame(session, authoringScene, {}, events), "reject duplicate runtime start");
    check(static_cast<bool>(ui.setHudText("hud.score", "1")), "publish runtime HUD data");
    const auto firstFrame = session.tick(1.0 / 60.0);
    check(firstFrame && fixedTicks == 1 && session.runtimeScene()
        && session.runtimeScene()->transform(player)->value.position.x == 1.0,
        "runtime executes one fixed update against the play Scene copy");

    check(static_cast<bool>(ui.pause(session, events)) && ui.paused()
        && session.state() == runtime::RuntimeState::Paused, "pause UI and RuntimeSession together");
    const auto pausedFrame = session.tick(0.1);
    check(pausedFrame && fixedTicks == 1 && session.runtimeScene()->transform(player)->value.position.x == 1.0,
        "paused RuntimeSession suppresses fixed updates");
    check(static_cast<bool>(ui.openSettings(events)) && ui.screen() == runtime::GameUiScreen::Settings,
        "open settings while paused");
    check(static_cast<bool>(ui.setSetting("audio.master", 0.5, events)), "change runtime setting");
    check(static_cast<bool>(ui.closeSettings(events)) && ui.paused()
        && session.state() == runtime::RuntimeState::Paused, "return from settings to paused gameplay");
    check(static_cast<bool>(ui.resume(session, events)) && ui.screen() == runtime::GameUiScreen::Gameplay
        && session.state() == runtime::RuntimeState::Playing, "resume game and runtime together");
    const auto resumedFrame = session.tick(1.0 / 60.0);
    check(resumedFrame && fixedTicks == 2 && session.runtimeScene()->transform(player)->value.position.x == 2.0,
        "runtime continues fixed updates after resume");
    check(static_cast<bool>(ui.returnToMenu(session, events))
        && ui.screen() == runtime::GameUiScreen::MainMenu && session.state() == runtime::RuntimeState::Stopped,
        "stop runtime and return to main menu");
    check(static_cast<bool>(ui.requestQuit(events)), "forward quit request to application host");

    check(observed.size() == 8 && observed[0].kind == runtime::GameUiEventKind::StartRequested
        && observed[1].kind == runtime::GameUiEventKind::PauseChanged
        && observed[2].kind == runtime::GameUiEventKind::SettingsOpened
        && observed[3].kind == runtime::GameUiEventKind::SettingChanged
        && observed[4].kind == runtime::GameUiEventKind::SettingsClosed
        && observed[5].kind == runtime::GameUiEventKind::PauseChanged
        && observed[6].kind == runtime::GameUiEventKind::ReturnedToMenu
        && observed[7].kind == runtime::GameUiEventKind::QuitRequested,
        "publish complete ordered menu, pause, settings, and quit event flow");
    check(authoringScene.transform(player)->value.position.x == 0.0,
        "RuntimeSession UI flow does not modify authoring scene");
    check(ui.hudFields().at("hud.score").text == "1"
        && std::get<double>(ui.settings().at("audio.master").value) == 0.5,
        "retain HUD and settings model values through screen transitions");
    if (subscription) check(static_cast<bool>(events.unsubscribe(subscription.value())), "unsubscribe game UI observer");

    if (failures == 0) std::cout << "All Game UI integration tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
