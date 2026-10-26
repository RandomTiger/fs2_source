# PrePort32 status

Branch `PrePort32` (formerly `PrePort`): port preparation that needs no engine, rendered and 32-bit; code removal (cleanup) lives here, one removal per commit.

**Port stack** (Tom, D-20261021-200950, as corrected by D-20261022-173525): `Enhanced` -> `PrePort32` -> `PrePort64` -> `Godot` -> `GodotEnhanced` (later). Carry-up goes `Enhanced` -> `PrePort32` -> `PrePort64`, once per `Latest` move. `PrePortCleanup` and `GodotPrep` are closed; their open items belong to `PrePort32` (cleanup) or `PrePort64` (GodotPrep's plan).
Plan and survey: `docs/PREPORT.md`. Branch layers: `docs/BRANCHES.md`. Decisions are made by the
decision steward (`fs2-decision-steward` skill, `decisions.md`); the D-numbers below are its entries.

Reference configuration (D-023): 1024x768, GR_1024, medium skill. All 48 recordings in
`recordings/autotest` are in it: AI pilot, 4x, recording format version 4, re-made 2026-10-16 when sound stopped feeding gameplay (D-034, D-035).

This file holds the open queue only. Finished items, the stage gates as met and the PrePortCleanup record are in `docs/status/DONE.md` (Tom, 2026-10-23). Stage gates 1 to 3 (D-026) are met or ready to argue as met; gate 4 (suite green) is re-run per commit (quick) and per layer change (full).

## Carried, by Tom's call
- Popups and other modal loops block a host frame. Resolved for the port by D-20261022-065224: the game runs as a coroutine on Godot's main thread; the fiber test is a port task in its turn (D-041).
- Squad message menu (piece 4): deferred by Tom; a carried exception unless gate 2 cannot be met without it.

## Project goals (D-063)
- **Demo recording suite.** Recordings over the demo missions, shipped in the repo with their own reference entry, so the suite runs without a retail install (public CI). The demo data is the only data set the project can redistribute. Demo behaviour is its own logic, not a subset of the retail suite; where the two differ, retail is the oracle and the demo keeps what the shipped demo did. Tom to be involved.
- **One exe for demo and retail.** The game detects which data set it has and switches to demo mode at run time, so both share a binary and a recording format. Sized in `docs/DEMO_CODE.md`: about 85 `FS2_DEMO` blocks in about 45 files, mostly menus and mission UI, plus a dozen pool limits that become run-time caps; about eight batches, proved by the demo suite and the retail suite both replaying unchanged. Until then no cleanup change touches demo-conditional code.

## Cleanup rules (D-059 to D-063, D-068)
No change may alter a recording; each cut passes the x86 quick suite and x64 headless 48/48, Release and Debug.
- Not cut: demo-conditional code (D-063), legacy-format readers (FS1 compatibility, D-064), multiplayer, FRED, the tools and the software renderer (D-068), and the GR_640 interface art mode. GR_640 is a kept UI art mode: FS1 has no 1024 art, so UI art mode and in-game render resolution become separate settings. No cut or collapse may assume 1024-only art, and the renderer collapse brief must say how GR_640 UI art survives (steward with Tom, 2026-10-17).
- Decided: 16-bit colour stays (D-20261017-231123); the renderer collapse keeps it and the GR_640 UI art.
- Done 2026-10-26 (#101, D-20261026-112237): headless starts in the data set's UI art mode, profile field `headless_gr_mode` (GR_640 on both FS1 rows, GR_1024 on both FS2 rows); recordings\fs1 and the FS1 demo now replay and run `-headless` (`-Headless` switch on `replay_check`, `fs1demo_run`, `fs1demo_gate`). No FS2 trace moved.
- Decided: both sound backends stay until an audio assessment method exists, scripted listening situations per API (D-20261017-231124). The first blind listening session ran 2026-10-23 (#81).
- Backlog, Tom's call, not scheduled: textured tool views before FRED moves to Godot, revive the 8-bit software texture path for the tools (reopens part of G2b) or write a 32-bit one; costed on issue #13 and #26 (D-20261018-022937). Tom 2026-10-23: stays on the backlog.

## Queue
1. **One exe for demo and retail** on `FS2Demo` (D-063): batches 7, 8, 6, 5 (`docs/FS2_DEMO.md` section 5; finished batches in `docs/fs2demo/`). Owed first: the cutscene bit renumbered to 12 with 8 to 11 reserved (D-20261022-161946). When they land, a demo-capable exe for Tom.
1b. **Recordings from below `PrePort32`** (standing rule, D-20261022-212629): every recording made below `PrePort32` that arrives in a carry-up is re-recorded on `PrePort32` in format 4, in that carry-up, as a logged deviation (conditions in `docs/REPLAY.md`). It is then carried up unchanged. Each carry-up report lists the files. First use: 2026-10-23, `sm3-10`, plus the FS1 missions `md-09`, `sm2-06a`, `sm2-10a`, `sm3-05a` and `sm3-09a` (813e812, 5e52424). Trace version 2 (the eye) stays port-only, `PrePort32` and above; recordings from below are re-traced at carry-up (D-20261022-211522, Tom).
3. **Cleanup on `PrePort32`:** CPU detection and the slow paths removed, timer reads through QueryPerformanceCounter; then the rest of D-062 (renderers, sound, input, low memory, Win32, Debug/Release) and the build-variant cuts (batch 9).
7. **Local tidy-up**, low priority (D-20261020-063404): worktrees and local names of the tagged branches, `git worktree remove` without `--force`, `git branch -d` only, when no agent is in them.
10. **FS1Demo out of FS1Compat** (rule 4 of D-20261021-173031), not done as of 2026-10-23 (no revert on `FS1Compat`; `FS1Compat` has since merged into `Enhanced` at `d9e6bd4`): (a) on `FS1Compat`, `git revert -m 1 f2363f5` as one commit listing the split, then the FS1 retail fixes the demo work found re-applied as ordinary commits naming their FS1Demo source, retail part only; gate FS1 retail suite identical; (b) on `FS1Demo`, before its next merge either way: merge `FS1Compat` down, then revert the revert as one commit titled as such; (c) `FS1Compat` into `Enhanced` carrying the revert, full suite on all four data sets, FS2 retail and FS2 demo untouched; (d) `FS1Demo` into `Enhanced` direct when green (D-20261021-171829). Steward first for each commit below PrePort.
11. **PadControl and VoiceControl** are Tom's own agent, not the manager's: `VoiceControl` off `RecordPlayback`, into `Enhanced` when done (D-20261021-105201); pad gate: quick suite identical with no pad and no DLL, plus the pad run when a pad is present.
12. **Docs with the next real change:** `PAD_CONTROL.md` the voice parentage; the FS1 doc notes the revert and the revert of the revert (item 10); `FS2_DEMO.md` section 1 may quote the one-exe rules (D-20261021-154902).
13. **Godot** (D-20261021-195110, D-20261022-143925): cut from `PrePort64`; the first brief ports record/playback; gate v1 is every recording set `PrePort64` requires through `godot --headless`, Release and Debug, TraceDiff 0 against native x64, plus restart; the steward reviews the gate before any gameplay port brief.

## Open items
- #74: one-off Release heap crash, watched; ProcDump capture armed via `FS2_PROCDUMP`.
- Carried up 2026-10-24 (Enhanced 7aec997, merge 08b6f4a): FS2Demo batch 5 profile field `object_slots` (FS2 demo 300, every other row MAX_OBJECTS) orders the object free list only, never a cap (D-20261023-220843); `SPDemo-01`/`SPDemo-02` re-recorded in format 4 under 1b (4c3b0d4, #98).
- Issue labels still naming `PrePort`/`PrePortCleanup` (#75, #83); no `branch: PrePort32` label exists yet.

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
- x64 done 2026-10-17 on `PrePort64` (D-057, D-067; `docs/status/DONE.md`).
- Out-of-bounds reads that affect gameplay fixed before x64 (`AiBig.cpp:870`).
- `#pragma optimize("", off)` blocks reviewed for another compiler.

Later (Godot or after)
- Coordinates: Godot is right-handed; mirror one axis and flip winding at the boundary.
- Cutscenes: `.ogv` playback exists (`OgvMovies-done`); the port needs its own player.
- Real 3D, one system at a time, after a backend drawing the existing projected triangles.

## PrePort64 only
This section is this layer's own; everything above is carried from `PrePort32` unchanged.
- **Engine: Godot, confirmed 2026-10-17 (D-065).** Tom weighed thinner hosts (SDL3, sokol, bgfx, raylib) and lighter engines. Godot stays because the aim after the port is to expand the game graphically and in features and to bring FRED and the other tools in, which wants an editor and a renderer with headroom. The C++ logic stays engine-agnostic: nothing on this layer may assume Godot, and the headless C++ build stays the proof runner. FRED in the Godot editor is a goal after the port, not scheduled.
- **Gate:** `x64_check` (x64 headless Release and Debug of every recording) after each merge; last 2026-10-23 on `b01db11` (#70 merge), quick gate green and x64 r64/d64 49/49. Last full run: 2026-10-21 21:35 on `b01f524`, every check green, including the 36 FS1 recordings against FS1 data and the FS1 demo under the #84 exception.
- **GodotPrep items done 2026-10-17** (from the port-needs list above): `WinMain` split into `game_host_init`, `game_host_frame` and `game_host_shutdown`, the game keeps its own clock (D-050, 1); `cfile` takes an explicit root from the host (D-050, 2); a read-only object snapshot after each simulation step, `Object/ObjectSnapshot.h`, nothing reads it yet (D-050, 3); platform hooks as one table the host fills in, `OsApi/HostHooks.h`: time, events and foreground, mouse, joystick, the recorder's wraps unchanged, sound and files with their own interfaces (D-051, D-052, 4); restartable in one process, `tools/restart_check.ps1` in the suite, with the carried state found and reset: the clock pause count, the alternate random stream the AI seeds its first enemy-choice timer from, the trace's rand counter, the autotest run state, the recorder, and subsystem and bitmap guards left set by shutdown (D-055, 5); settings from `fs2.ini` at the host root, the language and the low-memory mode in the recording header (version 5), keys documented in `docs/ENGINE_INTERFACE.md` (D-056, 6). The joystick polling thread question (D-052) was settled by frame polling (D-20261022-143926). Out-of-bounds reads that affect gameplay: `AiBig.cpp:870` (19ea4a6) and `ShipHit.cpp:917` (bf00ba1) fixed on `OriginalSourceBugFixes`, 2026-10-13.
- **Investigation open: nodraw exit 5 on PrePort64** (D-20261020-132256, D-20261020-133957). AutotestNebMedium, x86 Release `-nodraw`, quick suite during the FS1Compat carry: exit 5 (STUCK) at frame 1680 of 5203, traces identical up to there, 2026-10-20, host busy (other sessions building). Rate 1 in 13 on that exe (the suite run, 2 reruns, 10 serialised reruns on a quiet host, all others exit 0 MATCH; nodraw quick set rerun 23/23). Dump: none; the D-053 dump code is on `GodotPrep` only and no check script below it keeps `autotest.log`. While open, a second exit 5 in any check at any layer stops work; first question: does PrePortCleanup's pre-merge exe stall on the same mission. Closes after the D-053 tooling reaches every layer from `RecordPlayback` (D-20261020-133957 ii).
