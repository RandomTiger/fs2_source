# PrePort status

Branch `PrePort`: everything that can be done before the engine moves, nothing Godot-specific.
Plan and survey: `docs/PREPORT.md`. Branch layers: `docs/BRANCHES.md`. Decisions are made by the
decision steward (`fs2-decision-steward` skill, `decisions.md`); the D-numbers below are its entries.

Reference configuration (D-023): 1024x768, GR_1024, medium skill. All 48 recordings in
`recordings/autotest` are in it: AI pilot, 4x, recording format version 4, re-made 2026-10-16 when sound stopped feeding gameplay (D-034, D-035).

**PrePort done 2026-10-17 (D-049).** It was open again for generic fixes while the 64-bit build was proved (D-058) and closed when `PrePort64` was proven (D-067). Every layer but `master` is open again for the work that belongs on it (D-20261017-223742). The 64-bit build is on `PrePort64` (D-057, D-058); other port work on `GodotPrep`, branched at `1b9092d`. Carried: the squad message menu and the modal loops (D-041). Nothing Godot-specific until Tom confirms the engine (D-026).

## Stage gates (D-026)
| Gate | State |
|---|---|
| 1. Logic runs headless: every custom test mission plays to its end, every campaign recording for its recorded length (D-042) | Met 2026-10-16, pending the steward's reading of "end" for the custom missions. `-headless` runs with a message-only window that is never shown, no graphics device (`-nographics`, the null renderer `Graphics/GrDummy.cpp`, plus `-nodraw`) and no sound device (`-nosounddevice`: sounds load their length, nothing plays); keys, mouse and joystick come from the recording. All 48 recordings replay headless and match in Release and Debug (`tools/nodraw_check.ps1 -Headless`). Of the 14 custom missions, Autotest, AutotestChain and AutotestPlayer end themselves; the other 11 run their timeline to the time limit their notes set. Campaign recordings stay at 120 s (D-042). |
| 2. No OS reads, drawing dependencies or window-size reads in logic | Ready to argue as met (2026-10-17), with the squad menu carried. The split is merged (D-027) and `-nodraw` matches on all 46. Left, from the 2026-10-16 survey: the squad message menu, including orders and reinforcements, still runs in `HUD_render_2d` (piece 4, deferred by Tom 2026-10-15). Closed 2026-10-17 (D-048): `nebl_update_all` counts flash points in the game view; the `gr_screen.max_w/h` reads are in `nebl_generate_section`, drawing only, and its values are reset before logic uses them. Done: the nebula background pre-render runs in the drawing step, since only the renderer reads its pixels (2026-10-16). Found by the 2026-10-16 gap survey, all closed (D-048): `fireball_get_lod` sizes in the game view and reads the animation's own size from its file header; the talking-head message length is D-040; collision skips faces by the model's own invisible-texture flag, not a texture handle. Accepted by Tom: the detail level sets the debris count, as a recorded setting. Done: the lock start position comes from the game view (2026-10-16). Done: the flip's mouse reads (flying deltas, cursor position) are made by the renderer-independent `gr_flip_frame` (`2d.cpp`), in the same order, so a backend's flip reads nothing (2026-10-16). `Viewer_obj` needs no move: `game_render_frame_setup` already runs in the logic phase, also under `-nodraw`. The HUD message queue advances while drawing, but no gameplay reads it. Done: training messages are queued, voiced and timed in the logic phase (2026-10-16). Done: sound no longer feeds gameplay. Message voices run on their own timer (D-034), and sound-device answers, the music-start clock read and the flip's cursor read are no longer recorded (D-035). The camera and supernova shake stay on the gameplay random stream: they run in the logic step, and the host never moves the camera (D-036). The trace records the eye every frame (trace version 2). |
| 3. Engine interface (render, audio, input, platform) documented and the only path | Documented in `docs/ENGINE_INTERFACE.md`. Fixed 2026-10-16/17: no direct D3D calls, no colour globals outside BmpMan (D-044), no Win32 or `windows.h` in game code (Windows host D-045, platform backends D-047, own `MulDiv`), file reads classified (D-046), directory listing through OsApi. 10 `gr_screen.mode` lines remain, none in logic: 7 in BmpMan inside the texture seam, 2 in the demo-only title screen (not built), 1 null-renderer log line. Ready to argue as met. |
| 4. PrePort recording suite green in the reference configuration (D-042: `tools/suite_check.ps1` quick before each commit, `-Full` daily) | Last full run: 2026-10-21 16:20 on `875c5c4`, every check green: replay 87/87 (the 48 FS2 recordings and the 36 in `recordings/fs1` against FS1 data, Release and Debug identical, plus the demo set), -nodraw, -headless Release and Debug 48/48, sound model 87/87, menus 2/2, tools and warnings clean; `recordings/fs1` is recording format 4 from PrePort up (D-20261021-125812). Before that: 2026-10-17 00:48 on `cd47225`, 48/48 in every check (replay Release and Debug, -nodraw, sound model, -headless Release and Debug). Green 2026-10-16: 48/48, Release and Debug identical; `-nodraw` 48/48; no-sound check (`tools/sound_model_check.ps1`) 48/48 against the real device; no Assert, Int3 or Warning in any recording. |

## Current batch
Done 2026-10-16: `-headless` (no window shown, no graphics or sound device) and `tools/suite_check.ps1`; `-nosounddevice`, the sound system with no device; `-nographics`, the null renderer, with all 48 recordings replaying headless; under `-nodraw` the frame no longer clears the screen, sets clip regions or draws the frame rate and time left (the view percent clamp is kept); an animation keeps its timing when no bitmap slot is free, so an unvoiced message is no longer cut short (D-040; the head still follows the pilot's HUD config, which the recorded pilot file carries); the death explosion's art version, and so its length, sized in the game view (`fireball_get_lod`, `game_view_bitmap_width`); `Framecount` advanced in the logic step, not inside the frame rate display; collision's invisible faces from a model flag set from the texture names, not from texture handles; the game's own copy of the CRT random generator for the gameplay stream; asteroid rotation and the nebula poof update no longer depend on the renderer (D-039); the nebula pre-render in the drawing step; the eye in the trace (trace version 2, D-036); the nebula lightning flash distance from the game view's screen size; `nodraw_check.ps1` replays the checked-in recordings (`-Fresh` records fresh runs, for the "PrePort done?" check); baseline on DESKTOP-CLAUDE; CRLF mission files (D-028); `LogicDrawSplit` merged (D-027); lock start position and flip mouse reads; training messages in the logic phase; review 42 (D-032); `replay_check` output names (D-033); the no-sound check and the leak and training test missions (D-031); sound out of gameplay and recording format version 4 (D-034, D-035).

## Next batches (proposed order)
Tom 2026-10-16: work that needs no re-recording first.
1. Done 2026-10-17: engine interface leaks (`docs/ENGINE_INTERFACE.md`). PrePort done (D-049); the next work is the GodotPrep plan below.

Carried, by Tom's call:
- Popups and other modal loops block a host frame. Tom 2026-10-16: decide later, at GodotPrep (D-041; the steward recommends running the game on a coroutine).
- Squad message menu (piece 4): deferred by Tom; listed as a carried exception at "PrePort done?" unless gate 2 cannot be met without it.

## Project goals (D-063)
- **Demo recording suite.** Recordings over the demo missions, shipped in the repo with their own reference entry, so the suite runs without a retail install (public CI). The demo data is the only data set the project can redistribute. Demo behaviour is its own logic, not a subset of the retail suite; where the two differ, retail is the oracle and the demo keeps what the shipped demo did. First step: a compile-time `FS2_DEMO` build playing the demo data. Tom to be involved.
- **One exe for demo and retail.** The game detects which data set it has and switches to demo mode at run time, so both share a binary and a recording format. Sized in `docs/DEMO_CODE.md` on `PrePortCleanup`: about 85 `FS2_DEMO` blocks in about 45 files, mostly menus and mission UI, plus a dozen pool limits that become run-time caps; about eight batches. Scheduled after the `PrePortCleanup` order (D-062), and proved by the demo suite and the retail suite both replaying unchanged. Until then no cleanup change touches demo-conditional code.

## PrePortCleanup (D-059 to D-062, D-066 to D-070)
No change may alter a recording; each cut passes the x86 quick suite and x64 headless 48/48, Release and Debug.
- G2 done: branches of five never-defined macros (754 lines).
- G3 done: 39 `#if 0` blocks cut; 3 multiplayer blocks restored (D-069), since multiplayer stays out of the cut (D-060, D-014).
- G4 decided by Tom (D-068): FRED, the tools and the software renderer (`GrSoft` and its scanline and blit files) are kept. They are not game code and are excluded from D-060 and from D-062 item 1, which means one renderer in the game build.
- Tools build and run (D-068): with MFC installed, FRED2, Pofview and Nebedit build, and with the start-up heap fix (#11) they run: FRED opens and saves missions, Pofview opens models, Nebedit edits. Their software views are wireframe only, as in the released source, whose software texture mapper never linked (#13, D-20261018-022937). The VS2022 standard (every project builds and runs, steward with Tom) is checked by `tools/tools_check.ps1` (on `RecordPlayback`, 2026-10-18): all nine tool projects build in Release, the command-line tools do their basic job, FRED opens and saves a mission, Pofview opens a model given on its command line (#12 fixed on `VS2022`) and Nebedit opens. Its first run found #22 (Fonttool never set up its heap, review 49). Next: it joins the quick suite.
- G5 (Glide remnants, D-070): the renderer selection and the launcher-name mapping wait for the demo and build-variant work (`docs/DEMO_CODE.md`).
- G5 done (D-070): two Glide sites; G6 done (D-071): the Aureal A3D remnants. EAX and DirectSound3D are a live setting and go to the sound backend collapse (D-062 item 2).
- G2b done (D-072): `#ifndef HARDWARE_ONLY` branches, 1068 lines. They compile in neither the game nor the tools; the proof for FRED is that macro argument until the tools build check runs.
- G8 closed with no cut (D-074): 8-bit and palette paths carry the fonts and HUD gauges; 16-bit is a user setting. Pass two (G4 to G8) is complete; next is D-061 (memory wrapper trim, Debug fill, then array guards).
- G7 kept (D-073): `Demo/Demo.cpp` is Volition's own `.fsd` demo recorder, reachable in Debug, superseded by RecordPlayback and unrelated to the data-set demo mode (D-063).
- Not cut: demo-conditional code (D-063), legacy-format readers (FS1 compatibility, D-064), multiplayer, and the GR_640 interface art mode. GR_640 is a kept UI art mode: FS1 has no 1024 art, so UI art mode and in-game render resolution become separate settings. No cut or collapse may assume 1024-only art, and the renderer collapse brief must say how GR_640 UI art survives (steward with Tom, 2026-10-17). FS1 itself stays unscheduled; its table converters and mission file-name handling go in data loading, not game logic. Checked: no cleanup cut so far removed GR_640 code.

Tom's decisions, not scheduled:
- Textured tool views before FRED moves to Godot: revive the 8-bit software texture path for the tools (reopens part of G2b) or write a 32-bit one; costed on issue #13 (D-20261018-022937).
- Decided: 16-bit colour stays (D-20261017-231123); the renderer collapse keeps it and the GR_640 UI art.
- Decided: both sound backends stay until an audio assessment method exists, scripted listening situations per API (D-20261017-231124).
- On hold (Tom, 2026-10-18): removing the `.fsd` demo recorder (`Demo.cpp`, `Demo.h` and 29 `GM_DEMO_*` sites; D-073 (b)). Tom wants to know what Volition used it for first; backlog issue #70.

## Queue (2026-10-20)
Earlier order (Tom, 2026-10-18) done: the tools check in the suite, the FS2 demo brief, the audio assessment brief (listening session waits for Tom), the warnings branch (zero warnings, merged up and tagged `Warnings-done`).
1. **One exe for demo and retail** on `FS2Demo` (D-063): batch 4, the data set profile table, in progress (D-20261020-053500 to 053503); then batches 7, 8, 6, 5. FS1 rows of the profile table are for the FS1 work once batch 4 is in `Enhanced`.
1a. **FS1 tests in the gates** (Tom, 2026-10-21): a condition of the FS1Compat merge into `Enhanced`. From that merge on, every layer's gate (Enhanced up to GodotPrep, x64 included) also runs the FS1 retail and FS1 demo tests, so FS1 is checked on every carry-up, not only on the FS1 branches.
2. **OgvMovies into `Enhanced`** (D-20261020-063403), after batch 4: steward review brief first (Xiph licences in `lib/xiph/THIRD_PARTY.md`; the libs as their own projects, headers external, never edited, excluded from `warning_check` by name; x64 builds; movies skipped under `-nodraw`, `-headless` and replay; no recording moves), then merged up with the gates, tagged `OgvMovies-done` and the branch name deleted.
2a. **Build speed** (Tom, 2026-10-20; report `fs2_docs/CPP_INCLUDES_BUILD_TIME.md` on DESKTOP-CLAUDE): parallel compile (`/MP`) and the Debug browse database off (the debug PDB stays), on `VS2022` (project files only), steward first, carried up with the gates; the x64 Debug minimal-rebuild setting goes with `/MP`; check the reported x64 warning count against the zero-warnings gate. Precompiled headers: not wanted (Tom). Include and forward-declaration cleanup: no general sweep; targeted at GodotPrep if wanted (the Ship.h/ai.h/Weapon.h cycle, HUDtarget.h, Player.h).
3. **PrePortCleanup:** CPU detection and the slow paths removed, timer reads through QueryPerformanceCounter; then the rest of D-062 (renderers, sound, input, low memory, Win32, Debug/Release) and the build-variant cuts (batch 9).
4. **Mission loading analysis** (Tom, 2026-10-19; replaces the suns survey).
4a. **Credits names on `Enhanced`** (Tom, 2026-10-21): a short block shown before the original credits, naming RandomTiger and Claude (Anthropic). Added in code after `credits.tbl` is read, never by editing the data; same on every data set. Steward first (layer below PrePort); credits are not in any recording, so no trace should move.
5. **Latest:** after the next full-48 pass on `Enhanced`, propose moving `Latest` there; Tom's call (D-20261020-063401).
6. **Branch rule** (D-20261020-063405): a finished feature branch, fully merged and its layer green and pushed, gets an annotated `<Name>-done` tag naming the layer and merge commit; the tag is pushed and checked on the remote, then the branch name is deleted. Unfinished work gets `<Name>-checkpoint-<YYYY-MM-DD>` only. Layer freeze tags keep `<Layer>-done` (D-059); new ones are `<Layer>-frozen-<date>`. To write into `docs/BRANCHES.md` with the tag list (D-20261020-063400).
7. **Local tidy-up**, low priority (D-20261020-063404): worktrees and local names of the tagged branches, `git worktree remove` without `--force`, `git branch -d` only, when no agent is in them.
8. **In progress (2026-10-21): carry-up of the FS2Demo, FS1Compat and Credits merge** (Tom: "Yes, full plan"). `Enhanced` and `PrePort` pushed, every check green with the FS1 retail and FS1 demo tests; `PrePortCleanup` gate run, then `PrePort64` and `GodotPrep`. Found on the way: `replay_check` reads the retail folder's `autotest.log` for the demo set's Debug guard check (#83), fix on `PrePortCleanup`.
9. **Volition's `.fsd` demo recorder removed on `PrePortCleanup`** (Tom, 2026-10-21: "see if there is anything interesting to be learned from it and then delete it at the cleanup level. We can replace it if we need to."). What it teaches is in #70; then `code/Demo` and its `GM_DEMO_*` sites go, quick suite identical.
10. **FS1Demo out of FS1Compat** (rule 4 of D-20261021-173031), after item 8: (a) on `FS1Compat`, `git revert -m 1 f2363f5` as one commit listing the split, then the FS1 retail fixes the demo work found re-applied as ordinary commits naming their FS1Demo source, retail part only; gate FS1 retail suite identical; (b) on `FS1Demo`, before its next merge either way: merge `FS1Compat` down, then revert the revert as one commit titled as such; (c) `FS1Compat` into `Enhanced` carrying the revert, full suite on all four data sets, FS2 retail and FS2 demo untouched; the FS1 demo set on the retail row until (d) is a known state; `Latest` moves only on the full suite; (d) `FS1Demo` into `Enhanced` direct when green (D-20261021-171829). Steward first for each commit below PrePort.
11. **PadControl under VoiceControl** (D-20261021-154901): the WIP pad merge (D-20261021-104700) goes now without voice; `VoiceControl` off `RecordPlayback`, into `Enhanced` per green batch, inert without the speech DLL; `PadControl` takes it merged down at voice's next green point and uses its squad-menu block and frame hook; the first pad merge carrying voice code is a brief. Pad gate: quick suite identical with no pad and no DLL, plus the pad run when a pad is present.
12. **Docs with the next real change:** `BRANCHES.md` gets the branch content rule (D-20261021-173031), "FS1Compat: retail FS1 only; demo-only changes live on FS1Demo", the pad/voice parentage and the tag rule (D-20261021 tag entry); the FS1 doc notes the revert and the revert of the revert; `FS2_DEMO.md` section 1 may quote the one-exe rules (D-20261021-154902); `PAD_CONTROL.md` the voice parentage.

Done 2026-10-20: finished branches tagged and their names deleted (32BitRendering, CosmeticRandom, DebrisOffscreenFix, LogicDrawSplit, ShivanShipGlow, SoundOwnTimer, Warnings; D-20261020-063400); `PadControl-done` renamed `PadControl-checkpoint-2026-10-13`, the pad work is not finished (D-20261020-063402); the per-commit suite cut to the test missions, past breakers and rotating guests, with the menu tour (D-20261019-235230 to 235232).

## Proposed before the port (Tom, 2026-10-18), not yet ordered
- **Build warnings to zero.** Release game build: 2,420 distinct warnings. 2,313 are C4996 (the CRT's "unsafe" string functions); one project setting, `_CRT_SECURE_NO_WARNINGS`, silences them without touching code. About 100 are real: C4456-C4459 (a local hides another, 92), C4505 (unused static functions, 4), C4731 (inline assembly changes the frame pointer, 3), MSB8028 (shared intermediate folders, 2). Tools and Debug not yet counted. Tom 2026-10-18: a feature branch off `RecordPlayback` (not `VS2022`), so every change is checked against the recordings, and no re-recording is allowed on it: a warning fix that changes a trace is wrong. Merged into `Enhanced` when green. Each fix in original code goes through fix-or-keep; the order goes to the steward.
- **Loading optimisations.** `stars_page_in` pages in every starfield bitmap and every sun and glow for every level, not just the ones the mission uses. First a survey of what level loading pages in and how long it takes, then the fixes; nothing that changes what a mission plays.

## Open items

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
- Done 2026-10-17 on `PrePort64` (D-057, D-067): 48/48 x64 headless identical to x86, Release and Debug, and the headless restart check. Direct3D on x64 is closed as impossible, not deferred: 64-bit Windows has no Direct3D 7 or earlier, so x64 runs headless until Godot draws. Was: x64: the timer's inline assembly, pointer-to-int casts, `bitmap.data`, `time_t` in the VP index, x86-only DirectX import libraries, `AnimPlay.cpp` frame pointer allocation.
- Out-of-bounds reads that affect gameplay fixed before x64 (`AiBig.cpp:870`).
- `#pragma optimize("", off)` blocks reviewed for another compiler.

Later (GodotPrep or after)
- Coordinates: Godot is right-handed; mirror one axis and flip winding at the boundary.
- Cutscenes: there is no movie player; new work.
- Real 3D, one system at a time, after a backend drawing the existing projected triangles.
