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

## Puzzle content in Game UI (05.7)

The SDL demo now supplies optional host-owned gameplay content through `ImGuiGameUiAdapter::setGameplayContentRenderer`. The demo uses that hook to draw an 8×8 seeded `PuzzleGame` board beneath the HUD. Click/tap two neighboring tiles to request a swap; accepted matches update the score and level HUD. Start resets the board; existing Pause, Resume, Settings, and Main menu transitions still use `GameUiModel` and `RuntimeSession`. The sample also keeps synthesized SDL cues enabled in an interactive run and uses the selected English/Russian UI language for the board instruction.

Build and launch the demo as above. Its deterministic gameplay smoke can be run with:

```powershell
& '.\build\stack-spike\samples\game_ui_demo\astro_game_ui_demo.exe' --smoke-puzzle
ctest --test-dir build/stack-spike --output-on-failure -R runtime.game_ui.puzzle.smoke
```

The smoke drives one deterministic accepted move through the same selection/move handler, renders the Gameplay screen, checks HUD score, and exercises pause/resume/menu while confirming the authoring Scene remains intact. It does not synthesize physical mouse input or compare a puzzle-board pixel golden.

## Authored Puzzle campaign launch (05.8)

Pass a project root and campaign ID to launch authored project data in the same SDL runtime window:

```powershell
& '.\build\04.8-debug-clean\samples\game_ui_demo\astro_game_ui_demo.exe' `
  --project-root '.\samples\ПазлКампания' --campaign starter
```

The campaign must live under `Puzzle/Campaigns/<ID>.astro-puzzle-campaign` and reference valid files under `Puzzle/Levels/`. The host loads the campaign on startup through `PuzzleCampaignSession`. Tile swaps are submitted to the RuntimeSession fixed-step callback. After an authored score target is reached, **Continue campaign** resolves one transition. `END` leaves the final board and score visible. The Windows runtime package includes the checked-in sample under `game-data/puzzle-campaign`; run `launch_puzzle_campaign.cmd` to start it from any working directory. Save progress and Load progress use manually selected slots under SDL user data. For the format and example source, see [Puzzle Campaigns](PUZZLE_CAMPAIGNS.md) and [samples/ПазлКампания](../samples/ПазлКампания/README.md).

```powershell
cmake -S . -B build/stack-spike -DASTRO_BUILD_GAME_UI_DEMO=ON -DBUILD_TESTING=ON
cmake --build build/stack-spike --target astro_game_ui_demo --parallel 4
& '.\build\stack-spike\samples\game_ui_demo\astro_game_ui_demo.exe'
ctest --test-dir build/stack-spike --output-on-failure
```

## Limits

- Campaign progress can be saved manually in the standalone SDL host using ten selected slots; this does not persist HUD settings or general Scene state. The demo's audio remains synthesized cues rather than asset playback. English/Russian UI localization and the language preference exist; gamepad navigation and generic typed Command API bindings for Game UI operations are not implemented.
- The UI adapter is an optional Windows SDL3/ImGui target using the project's existing local dependency cache. Other platforms need their own adapter.
- `requestQuit` emits an event; the host application decides whether to exit. EventBus notifications are synchronous; observer failures are reported after the state change and do not roll it back.
- `runtime.game_ui.visual.smoke` checks the menu/game/pause/settings flow produces ImGui draw vertices and compares the main-menu screenshot to `tests/visual/game_ui_main_menu.bmp`. The pixel threshold allows mean absolute channel error up to 1.5/255 and up to 1% of pixels may differ by more than 12/255. No user usability study was performed.
