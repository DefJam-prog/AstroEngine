# Puzzle Campaign Format Proposal (06.21)

**Status: v1 parser, structural validator, serializer, project-file read/write, one-edge resolver, Puzzle runtime session, Editor Game View host, campaign browser/authoring UI, safe level-rename guard, standalone SDL Game UI host, and explicit manual campaign progress slots are implemented in limited form (06.22–06.27, 06.31–06.33, 05.8–05.9).** This document defines the v1 contract for ordering authored Puzzle levels and selecting the next level when a host reports completion.

## File location and example

Campaign files are project content at `Puzzle/Campaigns/<campaign-id>.astro-puzzle-campaign`. A campaign references level IDs stored as `Puzzle/Levels/<level-id>.astro-puzzle-level`; it does not embed or copy level data.

```text
ASTRO_PUZZLE_CAMPAIGN 1
ID "story"
NAME "Основная история"
START "intro"
LEVELS 4
LEVEL 0 "intro"
LEVEL 1 "challenge"
LEVEL 2 "bonus"
LEVEL 3 "finale"
TRANSITIONS 6
TRANSITION "intro" DEFAULT "challenge"
TRANSITION "challenge" SCORE_AT_LEAST 900 "bonus"
TRANSITION "challenge" SCORE_AT_LEAST 500 "finale"
TRANSITION "challenge" DEFAULT "finale"
TRANSITION "bonus" DEFAULT END
TRANSITION "finale" DEFAULT END
END
```

`TRANSITIONS` counts the following `TRANSITION` lines. `LEVEL` indices are contiguous and define catalog/display order. `START` identifies the entry level; it need not be index 0. The order does not implicitly create transitions.

## Proposed v1 grammar

```text
ASTRO_PUZZLE_CAMPAIGN 1
ID "<campaign-id>"
NAME "<utf8-name>"
START "<level-id>"
LEVELS <1..256>
LEVEL <zero-based-index> "<level-id>"
TRANSITIONS <1..4096>
TRANSITION "<source-level-id>" SCORE_AT_LEAST <u64-score> "<target-level-id>"
TRANSITION "<source-level-id>" DEFAULT "<target-level-id>" | END
END
```

There must be exactly `LEVELS` level records. The `|` above describes alternatives in the grammar; a concrete default line contains either a quoted target ID or the unquoted token `END`.

## Validation and resolution rules

- UTF-8 text, strict v1 header, no trailing records, maximum file size 128 KiB. Unknown versions or condition tokens are errors; readers must not discard unknown records silently.
- Quoted strings use `\"` for a quote and `\\` for a backslash. Other escape sequences and raw control bytes are rejected.
- Campaign and level IDs use the same safe 1–64 ASCII characters as authored level IDs: letters, digits, `-`, `_`. IDs are unique within each corresponding list. Names are 1–256 valid UTF-8 bytes.
- Level count is 1–256; transitions count is 1–4096. Every `LEVEL` index occurs exactly once from `0` through `count-1`. The `START` ID and every transition source/target must be listed in this campaign.
- A level ID may appear only once in the ordered level list. The referenced level file must exist and its internal ID must match the reference before the campaign can start.
- Every listed level has exactly one `DEFAULT` transition, and that record is last among rules for its source. `DEFAULT END` marks a terminal level. `DEFAULT` to another ID selects the fallback destination.
- A level may have zero or more `SCORE_AT_LEAST` rules before its default. Thresholds for one source are positive `u64` values in strictly descending order. At completion, evaluate them in file order using that level's final Puzzle score; take the first matching target, otherwise the default. The host requests one transition per completed level; resolving a transition does not itself start or recursively complete another level.
- All listed levels must be reachable from `START` when considering every declared target edge. Cycles are allowed for deliberate replay loops; they are followed only after separate host completion requests.
- v1 conditions are intentionally limited to score thresholds. Stars, inventory, arbitrary flags, scripted predicates, checkpoints, and cross-campaign links are not defined because the current Puzzle runtime does not provide those contracts.
- IDs are stable references. `PuzzleLevelFile::rename` checks the campaign catalog and refuses the rename if any readable campaign references the source ID. It also refuses when any campaign is unreadable or the catalog cannot be checked. The editor message identifies campaign IDs. Update/save campaign links first, then rename; afterward add the new ID and save the campaign. There is no implicit multi-file rewrite or progress-slot migration.

## Implemented API and checks (06.22–06.23)

`astro/runtime/PuzzleCampaign.hpp` exposes `PuzzleCampaignDefinition`, `validatePuzzleCampaign`, `parsePuzzleCampaign`, `serializePuzzleCampaign`, `PuzzleCampaignFile::{read,write}`, and `resolvePuzzleCampaignTransition`. Parsing and serialization are independent of project files. File read/write additionally require each referenced ID to resolve to a valid authored `PuzzleLevelFile`; paths are constrained to the project root, campaign symlinks are rejected, and writes publish a bounded UTF-8 document through a sibling temporary file and atomic replacement. Names reject control bytes because the line-oriented grammar has no multiline escape syntax.

Example:

```cpp
auto campaign = astro::runtime::PuzzleCampaignFile::read(projectRoot, "story");
if (!campaign) {
    // Surface campaign.error().message in project diagnostics.
}
```

`runtime.puzzle.campaign.integration` covers UTF-8 parse/serialize round-trip, ordered IDs/rules, grammar and semantic rejection cases, cycles, level-reference resolution, filename/document ID matching, traversal rejection, and replacement of a malformed document. The same test target checks runtime threshold/default/END selection and one-edge semantics. It does not test Editor controls or automatic progression/loading.

## Runtime transition resolution (06.23)

`resolvePuzzleCampaignTransition(campaign, completedLevelId, finalScore)` validates the campaign and resolves one rule for the completed source level. It evaluates score rules in declared descending order, selects the first satisfied threshold, and returns the DEFAULT destination otherwise. A successful empty `std::optional` means terminal `END`; an invalid campaign or completed level outside the campaign returns an error. The resolver does not load the returned level, mutate the campaign, inspect player state, or recursively follow another edge. The host requests a new resolution only after the next level is later completed.

The integration test checks the higher and lower matching thresholds, DEFAULT fallback, END, unknown source rejection, and that a returned destination is not advanced in the same call.

## Runtime campaign session (06.24)

`PuzzleCampaignSession::start(projectRoot, campaignId)` reads and validates the campaign, loads its START level, and creates a playable `PuzzleGame`. `swap` forwards gameplay input and remembers when the authored level's `targetScore` has been reached. `completeCurrentLevel` is accepted only after that completion event; it resolves the active Puzzle score, loads and initializes the selected authored level, and then publishes it as active. If reading or initializing the next level fails, the current completed level and game remain available so the host can diagnose the error or retry after repairing the asset.

When a completion selects `END`, the session enters `Ended`, retains the last level/game/score for a results screen, and rejects further swaps or completion requests. Restart is explicit: a new session starts at START. The session does not write progress saves, load engine Scenes, or automatically restart. Integration tests exercise real project level files, threshold and DEFAULT loading, failed-load preservation/retry, END, and explicit restart.

## Read-only campaign browser and reference diagnostics (06.26)

`PuzzleCampaignFile::list(projectRoot)` scans only direct files in the project's campaign directory, bounds enumeration to 4096 matching files, rejects directory/file symlinks, and returns entries sorted by ID. A malformed document remains listed with a diagnostic and no parsed definition. A structurally valid document remains available for inspection even when one or more level files are missing, malformed, or mismatched; each ordered level has a `PuzzleCampaignLevelReference` with `available` and its specific diagnostic. `referencesValid()` is true only if the campaign parsed and every level file validates. Strict `PuzzleCampaignFile::read` and campaign start still reject broken references.

The Windows Editor exposes this data through **View → Puzzle Campaign Browser**. It displays campaign name/ID, START, ordered levels, transition rules, and per-level status. Selecting a structurally readable campaign copies its ID into the Game View launch field. The authoring area below the catalog creates/loads a draft, edits ID/name, adds valid project levels, changes order and START, configures per-level score threshold branches and DEFAULT/END targets, and saves through a confirmation dialog. Dirty drafts require discard confirmation before reset/load. Saving under a different ID creates a copy and retains the old campaign; saving to an existing ID replaces it after confirmation. The strict project writer validates structure and referenced level files before publication. `editor.smoke` verifies the authoring view is drawn; integration tests cover the campaign file API.

## Campaign authoring and level rename handling (06.27)

Campaign authoring uses the same v1 model and strict `PuzzleCampaignFile::write` path as runtime loading. It does not mutate a level asset or automatically rewrite other campaigns. To rename a referenced level safely: edit each affected campaign so it no longer points to the old ID (redirect incoming edges/remove the level as appropriate), save and validate those campaigns, rename the level, then edit/save campaigns again to add the new ID and links. If any campaign file is malformed or the catalog scan fails, rename is conservatively blocked because the editor cannot prove the ID is unused. The level rename API enforces this rule even if called outside the Editor.

## Persistence and migration policy

This is a project content file, not player progress. Implemented writes use a sibling temporary and atomic replacement, validate path containment under the project root, and preserve the previous published file if the new temporary cannot be written or replaced. v1 has no migration defined. Future versions require explicit migration rules and backup/diagnostic behavior before implementation.

## Game View host (06.25)

When the optional Puzzle runtime is available, the Windows Editor Game View accepts a campaign ID for the open project and starts `PuzzleCampaignSession` from `Puzzle/Campaigns/<ID>.astro-puzzle-campaign`. The active session's board is displayed in Game View; adjacent tile input is queued and applied by the host's fixed-step `RuntimeSession` callback. Once the level target is reached, the user selects **Continue campaign** to resolve and load one transition. `END` keeps the final board and score visible; **Restart campaign** starts a fresh session at START. **Stop** clears the session. Since 06.31, the Editor also exposes ten manual campaign progress slots (0–9) in Game View; see [Save System](SAVE_SYSTEM.md#editor-game-view-puzzle-campaign-progress-0631). Campaign documents remain read-only in the Editor; use the format/file API to author them.

The editor smoke writes two levels and a campaign to its isolated temporary project, exercises the shared Game View start/continue handlers and runtime fixed-step input path, verifies next-level loading and END, and checks rendered campaign frames. It is an internal programmatic smoke, not a physical ImGui click automation.

## Standalone SDL Game UI host (05.8)

The optional `astro_game_ui_demo` accepts `--project-root <path> --campaign <id>`. It validates and loads the authored campaign through `PuzzleCampaignSession`, displays its active level in the existing Game UI, and routes accepted tile swaps through the RuntimeSession fixed-step callback. When the authored target is reached, the UI offers **Continue campaign**; one click resolves exactly one threshold/default edge and loads that authored level. `END` retains the final board and score. A malformed campaign or unavailable level fails startup with a diagnostic; a failed next-level load preserves the completed level so Continue can be retried after repairing the project content.

The repository example at `samples/ПазлКампания` contains the `starter` campaign. The standalone Game UI provides manual progress save/load in one of ten slots under SDL user data at `PuzzleCampaigns/<FNV-1a canonical project path key>/<campaign-id>`. Its profile explicitly includes only campaign ID, active level ID, ended flag and Puzzle snapshot; it does not write project content or select Scene properties. Pending Continue and terminal END states survive a round trip, and current authored level rules are checked before state restoration. The Editor uses its own explicit per-project user-data path as documented in 06.31. There is no autosave, migration, or physical mouse automation. The Windows SDL demo smoke launches the checked-in read-only sample through a Unicode path and covers midlevel/pending-completion/END persistence.

## Remaining acceptance tests

Migration behavior (none is defined for v1), symlink/path edge cases and atomic-write failure injection remain future tests. The level integration test covers referenced rename refusal, unreadable-campaign refusal, source preservation and rename after links are removed. Editor smoke checks campaign save/load handlers; manual Windows UI verification is recorded in [report 06.32](reports/06.32-editor-campaign-save-ui.md), while automated physical ImGui click testing remains absent. Campaign progress persistence reuses SaveStore and has its own integration test. Runtime session and Game View host load Puzzle level files only; engine Scene loading remains separate.
