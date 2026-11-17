AstroEngine Game UI runtime prototype — Windows x64

Run astro_game_ui_demo.exe to open the current Game UI prototype.
The window demonstrates menu, gameplay HUD, pause, settings, and a seeded 8x8
Puzzle board. Select two neighboring tiles to make a move; accepted matches
update the score. This board is a gameplay demo and does not load authored
campaign levels by default. To launch an authored campaign, pass the project
directory and campaign ID, for example:

astro_game_ui_demo.exe --project-root "C:\path\to\project" --campaign starter

The ZIP includes a playable two-level example at game-data/puzzle-campaign.
Run launch_puzzle_campaign.cmd to start it; the launcher uses its own directory,
so the current working directory can be anywhere. The matching authored project
is also available in the repository at samples/ПазлКампания.
When an authored level reaches its target, choose Continue campaign. The END
transition retains the final board and score until you return to the menu. Save
progress and Load progress use the selected slot in the SDL per-user data folder.
During a campaign, select a slot from 0 to 9 and choose Save progress or Load
progress to manage campaign progress manually. Saves are stored in the SDL
per-user data directory, separated by project path and campaign ID. The profile
stores the campaign ID, active level ID, END state, and Puzzle board snapshot;
it does not modify the authored project. Loading rejects incompatible profile
versions or changed authored level rules. Saves are manual: there is no
autosave or migration between save schema versions.
English and Russian can be selected in Settings; the choice is stored in the
SDL per-user preference directory, outside the project and game saves.
The audio cues are synthesized at runtime and can be unavailable on systems
without a usable audio device; the prototype reports this and continues.

This package is a runtime UI/gameplay prototype, not the completed 2D puzzle
game. It includes one authored sample campaign and supports manual campaign
progress slots. General SaveProfile authoring, checkpoints, autosave, and
arbitrary project export are not connected to this demo executable.

Third-party license texts are in the licenses directory.
