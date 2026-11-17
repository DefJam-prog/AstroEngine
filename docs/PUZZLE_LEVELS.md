# Puzzle Level Authoring and Catalog (06.18–06.20)

This is a first level-authoring slice for the built-in Puzzle preview, not the complete T01 template.

## Create and play a level

1. Open a project and click **Play Puzzle** in Game View.
2. Expand **Puzzle Level Authoring**. The catalog lists saved levels; unreadable files stay visible with a diagnostic.
3. Select a row and click **Open selected**. If the current draft has unsaved edits, confirm discard first. **Refresh catalog** rescans project files.
4. Use **New level** for a blank draft. Set its ID/name, board width and height (3–16 in this editor), tile palette size (3–8), seed and target score.
5. Click a cell in the grid to cycle its tile ID. Cell changes affect the in-memory draft until saved. **Save level** creates a new file without overwriting an existing ID, or updates the already-open level with the same ID. Use **Rename current level** to change an existing ID.
6. To duplicate a saved catalog item, select it, enter **New level ID** and **Duplicate name**, then click **Duplicate selected**. The copy is added as a separate file; the source stays unchanged.
7. Click **Play authored level** to start the current draft in Game View.

Renaming opens a confirmation dialog because it removes the old level file after publishing the new file. A collision or invalid ID leaves the source in place. Before publishing, the runtime API checks every campaign: rename is refused if a readable campaign references the old ID, if a campaign file is unreadable, or if the catalog cannot be checked. Update and save affected campaigns first, rename the level, then add the new ID to the campaigns. The Editor reports which campaign blocks the rename. The progress-save namespace is based on the level ID: rename does not move or delete slots under the old ID; the renamed level starts with an empty namespace unless the user later manages those files manually.

Level ID accepts 1–64 ASCII letters, digits, `-` and `_`; the name accepts valid UTF-8 up to 256 bytes. The runtime format supports boards through 64×64 and 3–32 tile kinds; the current GUI intentionally exposes a smaller board/palette for usable editing.

## File and runtime behavior

The versioned UTF-8 text format is documented in [FILE_FORMATS.md](FILE_FORMATS.md#puzzle-level-v1-0618). It stores the level ID/name, dimensions, tile kinds, seed, score target and exact initial tile IDs. `PuzzleGame::create(PuzzleLevelDefinition)` validates the definition and starts with that exact board; the existing seeded `create(width, height, ...)` API remains available unchanged.

Files are project content, separate from Puzzle player-progress slots. For an authored level, Save/Load is enabled with the explicit custom string field `puzzle.level.id`; the field is checked against the active level before state is published. Slots are written below the project-specific SDL user-data directory at `PuzzleLevels/<ID>`, so different authored levels use distinct slot namespaces. The built-in sample keeps its previous profile/path and has no authored-level identity. The SaveStore file format is unchanged. The catalog can list, open, duplicate, and rename files; campaign authoring and runtime progression are documented in [PUZZLE_CAMPAIGNS.md](PUZZLE_CAMPAIGNS.md). Player progress for campaigns is not saved. The authoring workflow does not guarantee that a board has a valid move. Unknown level versions and malformed data return diagnostics without publishing a partial level.

The Editor smoke creates a test-owned project level, lists/opens it, duplicates and renames a second level, launches the source board, saves progress to its level-specific namespace, verifies a different level ID cannot load that slot or mutate the board, then restores the matching slot. All files stay inside its temporary directory. See [06.20 report](reports/06.20-puzzle-level-catalog.md).
