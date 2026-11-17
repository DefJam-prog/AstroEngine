# Sample Puzzle campaign project

This directory contains a small authored project-content example. It is read-only when used by the runtime demo.

- `Puzzle/Campaigns/starter.astro-puzzle-campaign` orders `intro` and `finale`.
- Completing `intro` with at least 30 points selects `finale`; the DEFAULT edge also targets `finale`.
- Completing `finale` follows `DEFAULT END` and leaves the final board visible.
- Both authored 3×3 boards have a deterministic accepted move for the runtime smoke.

From the repository root, run the optional Windows SDL Game UI demo with:

```powershell
& '.\build\04.8-debug-clean\samples\game_ui_demo\astro_game_ui_demo.exe' `
  --project-root '.\samples\ПазлКампания' --campaign starter
```

The window opens on its menu. Choose **Start game**, make neighboring tile selections, then choose **Continue campaign** after a level reaches its target. The standalone Game UI provides manual campaign progress slots; saves stay in SDL user data and do not modify this sample project. Autosave and progress migration are not implemented.
