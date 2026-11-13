# Runtime Game UI 05.2

`GameUiModel` keeps runtime menu state, available actions, HUD text fields and validated setting values independent of a graphics backend. `RuntimeSession` controls Play/Pause/Resume/Stop; the model changes its screen only when the corresponding runtime operation succeeds.

```cpp
astro::runtime::GameUiModel ui;
astro::runtime::RuntimeSession session;
astro::core::EventBus events;

ui.addHudField("hud.score", "Score", "0");
ui.addSetting({"ui.scale", "UI scale", 1.0, 0.75, 1.5, {}});

ui.startGame(session, authoringScene,
    [](astro::scene::Scene&, double fixedDelta) {
        (void)fixedDelta;
        return astro::core::Result<void>::success();
    }, events);
ui.pause(session, events);  // also pauses RuntimeSession
ui.openSettings(events);
ui.setSetting("ui.scale", 1.1, events);
ui.closeSettings(events);  // returns to the paused screen
ui.resume(session, events);
```

## Visible SDL3/ImGui adapter

An optional `ImGuiGameUiAdapter` renders four responsive screens in an SDL3 window:

- Main Menu: Start game, Settings, Quit.
- Gameplay: HUD values and Pause.
- Paused: HUD values, Resume, Settings, Main menu.
- Settings: boolean controls, bounded numeric sliders and choice lists.

The optional demo is built separately from the editor. It exercises the model and renderer together. Its fullscreen and UI-scale settings have effects in the demo; other projects subscribe to `GameUiEvent` and apply settings through their own platform/audio modules. The default headless runtime does not link SDL3 or ImGui.

```powershell
cmake -S . -B build/stack-spike -DASTRO_BUILD_GAME_UI_DEMO=ON -DBUILD_TESTING=ON
cmake --build build/stack-spike --target astro_game_ui_demo --parallel 4
& '.\build\stack-spike\samples\game_ui_demo\astro_game_ui_demo.exe'
ctest --test-dir build/stack-spike --output-on-failure
```

## Limits

- Settings and HUD values are in-memory and are not serialized or bound to game components. There is no audio subsystem, settings persistence, localization, gamepad navigation, or generic typed Command API for these operations yet.
- The UI adapter is an optional Windows SDL3/ImGui target using the project's existing local dependency cache. Other platforms need their own adapter.
- `requestQuit` emits an event; the host application decides whether to exit. EventBus notifications are synchronous; observer failures are reported after the state change and do not roll it back.
- `runtime.game_ui.visual.smoke` checks that six menu/game/pause/settings frames produce ImGui draw vertices and compares the main-menu screenshot to `tests/visual/game_ui_main_menu.bmp`. The pixel threshold allows mean absolute channel error up to 1.5/255 and up to 1% of pixels may differ by more than 12/255. No user usability study was performed.
