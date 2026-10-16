# PrePort status

Branch `PrePort`: everything that can be done before the engine moves, nothing Godot-specific.
Plan and survey: `docs/PREPORT.md`. Branch layers: `docs/BRANCHES.md`. Decisions are made by the
decision steward (`fs2-decision-steward` skill, `decisions.md`); the D-numbers below are its entries.

Reference configuration (D-023): 1024x768, GR_1024, medium skill. All 46 recordings in
`recordings/autotest` are in it: AI pilot, 4x, re-made 2026-10-16 with the logic and drawing split (D-027).

## Stage gates (D-026)
| Gate | State |
|---|---|
| 1. Logic runs headless, every suite mission plays to its end under the AI pilot | Not met. `-nodraw` skips drawing but still clears and flips, and the window, sound and input are live. Campaign recordings stop at 120 s of mission time, so most do not reach their end. |
| 2. No OS reads, drawing dependencies or window-size reads in logic | In progress. The split is merged (D-027) and `-nodraw` matches on all 46. Left, from the 2026-10-16 survey: the squad message menu, including orders and reinforcements, still runs in `HUD_render_2d` (piece 4, deferred by Tom 2026-10-15); training messages are dequeued and voiced in `HUD_render_2d`; `nebl_update_all` reads `gr_screen.max_w/h`; `neb2_render_setup` pre-renders the nebula background in the logic phase, outside the drawing wrapper (random numbers not yet checked). Done: the lock start position comes from the game view (2026-10-16). Done: the flip's mouse reads (flying deltas, cursor position) are made by the renderer-independent `gr_flip_frame` (`2d.cpp`), in the same order, so a backend's flip reads nothing (2026-10-16). `Viewer_obj` needs no move: `game_render_frame_setup` already runs in the logic phase, also under `-nodraw`. The HUD message queue advances while drawing, but no gameplay reads it. |
| 3. Engine interface (render, audio, input, platform) documented and the only path | Inventory drafted in `docs/ENGINE_INTERFACE.md`. The leaks around the seams are not fixed yet. |
| 4. PrePort recording suite green in the reference configuration | Green 2026-10-16: 46/46 replay, sm1-01 builds identical, `-nodraw` 46/46. |

## Current batch
Done 2026-10-16: baseline on DESKTOP-CLAUDE; CRLF mission files (D-028, on `RecordPlayback`, merged up); `LogicDrawSplit` merged with its recordings re-made (D-027).

## Next batches (proposed order)
1. Training messages: they are dequeued and voiced in `HUD_render_2d`, so a training mission desyncs under `-nodraw`. None of the 46 recordings has one, so first write an Autotest mission with training messages and record it, then split `message_training_display` into an update in `HUD_update_2d` and the drawing.
2. Check whether `neb2_pre_render` and `stars_draw_background` in `game_render_frame_setup` draw gameplay random numbers.
3. `nebl_update_all`: the flash distance from the game view instead of `gr_screen.max_w/h`.
4. Squad message menu (piece 4): Tom deferred it, so lifting the deferral is his call (steward, D-027 follow-up). Last, and listed as a carried exception at "PrePort done?" unless gate 2 cannot be met without it.

## Open items
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
