# PrePort status

Branch `PrePort`: everything that can be done before the engine moves, nothing Godot-specific.
Plan and survey: `docs/PREPORT.md`. Branch layers: `docs/BRANCHES.md`. Decisions are made by the
decision steward (`fs2-decision-steward` skill, `decisions.md`); the D-numbers below are its entries.

Reference configuration (D-023): 1024x768, GR_1024, medium skill. All 46 recordings in
`recordings/autotest` are in it (header checked 2026-10-15: skill 2, 1024x768, AI pilot, 4x).

## Stage gates (D-026)
| Gate | State |
|---|---|
| 1. Logic runs headless, every suite mission plays to its end under the AI pilot | Not met. `-nodraw` skips drawing but still clears and flips; window, sound and input are live. Campaign recordings stop at 120 s of mission time, so most do not reach their end. |
| 2. No OS reads, drawing dependencies or window-size reads in logic | In progress on `LogicDrawSplit` (pieces 1-3 and 5 done there). Left: piece 4 (squad orders), lock start position from `gr_screen.clip_*`, mouse read in `gr_d3d_flip`, `next_hit_spark` using `Viewer_obj`. |
| 3. Engine interface (render, audio, input, platform) documented and the only path | Not started. |
| 4. PrePort recording suite green in the reference configuration | Baseline being re-established on DESKTOP-CLAUDE. |

## Current batch
1. Baseline: build PrePort and replay all 46 recordings on DESKTOP-CLAUDE.
2. Measure `LogicDrawSplit` against PrePort's recordings: how many fail, where, and why.
3. Decision request to the steward on merging `LogicDrawSplit` (it invalidates recordings).
4. Docs: this file, PrePort and GodotPrep in `docs/BRANCHES.md`.

## Open items
- Merge `LogicDrawSplit` into `PrePort` once green (steward's log, D-008).
- SM1-07 hand-played recording: Tom's to re-record (D-011); in `TODO.md`.

## What the port needs from the C++ side
Both the PrePort backlog and the GodotPrep plan. Background for each is in `docs/PREPORT.md`.

Logic and presentation
- Logic and drawing separated, drawing only reads state (`LogicDrawSplit`).
- Logic uses the GR_1024 game view for any screen-space maths, never the window (D-006, D-022).
- A read-only snapshot of objects after each simulation step (number, signature, model, transform) for the host to draw from.
- Fonts: real metrics without a renderer (`split_str` and HUD message wrapping measure in pixels).
- Every `gr_screen.mode` switch outside the `gf_*` table (`gr_bitmap`, `gr_activate`, `neb2_*`, `BM_SELECT_*`) handles a new backend.

Host and platform
- `WinMain` split into init, one frame, shutdown; the game keeps its own clock.
- No blocking loops: popups and other modal loops run per frame.
- Restartable in one process: all game state reset in one place, no `atexit` teardown.
- Platform hooks as function tables the host fills in: time, input, sound, files, window; the same hooks the recorder uses, so recordings can referee the port.
- `cfile` takes an explicit root instead of the current directory.
- Sound behind an interface that does not need a window handle.
- No `windows.h` in any header the host binding includes.
- Settings from an ini, not the registry (D-016).

Build and determinism
- x64: the timer's inline assembly, pointer-to-int casts, `bitmap.data`, `time_t` in the VP index, x86-only DirectX import libraries, `AnimPlay.cpp` frame pointer allocation.
- Out-of-bounds reads that affect gameplay fixed before x64 (`AiBig.cpp:870`).
- `#pragma optimize("", off)` blocks reviewed for another compiler.

Later (GodotPrep or after)
- Coordinates: Godot is right-handed; mirror one axis and flip winding at the boundary.
- Cutscenes: there is no movie player; new work.
- Real 3D, one system at a time, after a backend drawing the existing projected triangles.
