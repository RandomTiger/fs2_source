# PrePort status

Branch `PrePort`: everything that can be done before the engine moves, nothing Godot-specific.
Plan and survey: `docs/PREPORT.md`. Branch layers: `docs/BRANCHES.md`. Decisions are made by the
decision steward (`fs2-decision-steward` skill, `decisions.md`); the D-numbers below are its entries.

Reference configuration (D-023): 1024x768, GR_1024, medium skill. All 48 recordings in
`recordings/autotest` are in it: AI pilot, 4x, recording format version 4, re-made 2026-10-16 when sound stopped feeding gameplay (D-034, D-035).

## Stage gates (D-026)
| Gate | State |
|---|---|
| 1. Logic runs headless, every suite mission plays to its end under the AI pilot | Not met. `-nodraw` skips drawing but still clears and flips, and the window, sound and input are live. Campaign recordings stop at 120 s of mission time, so most do not reach their end. |
| 2. No OS reads, drawing dependencies or window-size reads in logic | In progress. The split is merged (D-027) and `-nodraw` matches on all 46. Left, from the 2026-10-16 survey: the squad message menu, including orders and reinforcements, still runs in `HUD_render_2d` (piece 4, deferred by Tom 2026-10-15); `nebl_update_all` reads `gr_screen.max_w/h`. Done: the nebula background pre-render runs in the drawing step, since only the renderer reads its pixels (2026-10-16). Found by the 2026-10-16 gap survey, to do: the death explosion's art version, whose length sets when a ship breaks up and an asteroid explodes, is picked from the screen clip and bitmap size (`fireball_get_lod`); a message lasts until its talking-head animation ends, which depends on the HUD gauge setting and a free bitmap slot (`MissionMessage.cpp`); collision skips faces by texture handle (`ModelCollide.cpp:647`). Accepted by Tom: the detail level sets the debris count, as a recorded setting. Done: the lock start position comes from the game view (2026-10-16). Done: the flip's mouse reads (flying deltas, cursor position) are made by the renderer-independent `gr_flip_frame` (`2d.cpp`), in the same order, so a backend's flip reads nothing (2026-10-16). `Viewer_obj` needs no move: `game_render_frame_setup` already runs in the logic phase, also under `-nodraw`. The HUD message queue advances while drawing, but no gameplay reads it. Done: training messages are queued, voiced and timed in the logic phase (2026-10-16). Done: sound no longer feeds gameplay. Message voices run on their own timer (D-034), and sound-device answers, the music-start clock read and the flip's cursor read are no longer recorded (D-035). The camera and supernova shake stay on the gameplay random stream: they run in the logic step, and the host never moves the camera (D-036). The trace records the eye every frame (trace version 2). |
| 3. Engine interface (render, audio, input, platform) documented and the only path | Inventory drafted in `docs/ENGINE_INTERFACE.md`. The leaks around the seams are not fixed yet. |
| 4. PrePort recording suite green in the reference configuration | Green 2026-10-16: 48/48, Release and Debug identical; `-nodraw` 48/48; no-sound check (`tools/sound_model_check.ps1`) 48/48 against the real device; no Assert, Int3 or Warning in any recording. |

## Current batch
Done 2026-10-16: an animation keeps its timing when no bitmap slot is free, so an unvoiced message is no longer cut short (D-040; the head still follows the pilot's HUD config, which the recorded pilot file carries); the death explosion's art version, and so its length, sized in the game view (`fireball_get_lod`, `game_view_bitmap_width`); `Framecount` advanced in the logic step, not inside the frame rate display; collision's invisible faces from a model flag set from the texture names, not from texture handles; the game's own copy of the CRT random generator for the gameplay stream; asteroid rotation and the nebula poof update no longer depend on the renderer (D-039); the nebula pre-render in the drawing step; the eye in the trace (trace version 2, D-036); the nebula lightning flash distance from the game view's screen size; `nodraw_check.ps1` replays the checked-in recordings (`-Fresh` records fresh runs, for the "PrePort done?" check); baseline on DESKTOP-CLAUDE; CRLF mission files (D-028); `LogicDrawSplit` merged (D-027); lock start position and flip mouse reads; training messages in the logic phase; review 42 (D-032); `replay_check` output names (D-033); the no-sound check and the leak and training test missions (D-031); sound out of gameplay and recording format version 4 (D-034, D-035).

## Next batches (proposed order)
Tom 2026-10-16: work that needs no re-recording first.
1. The screen calls left in the logic phase of `game_frame` (`gr_clear`, `gr_reset_clip`, `game_set_view_clip`): no logic reads the clip any more, so they can move into drawing (gate 1).
2. Popups that do not block the frame (fs2ue only made every popup cancel at once); the training message's unbound-key popup goes to the steward.
3. Squad message menu (piece 4): Tom deferred it; lifting the deferral is his call. Last, and listed as a carried exception at "PrePort done?" unless gate 2 cannot be met without it.

## Open items
- SM1-07 hand-played recording: Tom's to re-record (D-011); in `TODO.md`.

## What the port needs from the C++ side
Both the PrePort backlog and the GodotPrep plan. Background for each is in `docs/PREPORT.md`.

Logic and presentation
- Sound data (at least each sound's length) loaded without a sound device: with `-nosound` no sound loads, so message voices never start and messages last only their animation.
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
- The host runs the C++ menu flow (D-038). Only if the port replaces the menus or runs a headless campaign: extract apply loadout (`MissionShipChoice.cpp:1952`), restore loadout (`common_select_init`), finish mission (`MissionDebrief.cpp:2203-2222`) and advance campaign (`:1635`).

Build and determinism
- x64: the timer's inline assembly, pointer-to-int casts, `bitmap.data`, `time_t` in the VP index, x86-only DirectX import libraries, `AnimPlay.cpp` frame pointer allocation.
- Out-of-bounds reads that affect gameplay fixed before x64 (`AiBig.cpp:870`).
- `#pragma optimize("", off)` blocks reviewed for another compiler.

Later (GodotPrep or after)
- Coordinates: Godot is right-handed; mirror one axis and flip winding at the boundary.
- Cutscenes: there is no movie player; new work.
- Real 3D, one system at a time, after a backend drawing the existing projected triangles.
