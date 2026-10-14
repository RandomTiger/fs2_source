# PrePort

`PrePort` takes the stable, tested `Enhanced` base and prepares it for the Godot port. Everything here is still the C++ game on Windows, so the recordings referee every change (`tools\replay_check.ps1`, all recordings passing in Release and Debug).

This file records what the code survey of 2026-10-13 found and what was decided, so later sessions do not have to survey the code again. Line numbers are as of that date.

## Decisions
- **Single player only.** The Godot port does not carry multiplayer. `code\Network` (about 61,000 lines) and `Inetfile` stay untouched until the port, and are not ported. They are not stripped before it: about 495 multiplayer checks sit in 59 game files outside Network, and removing them costs a lot for no gain.
- **Real 3D after the port.** The game transforms and projects every vertex on the CPU and hands D3D screen-space vertices. Rewriting that as real 3D in D3D7 would be thrown away at the port, and the recordings do not referee visuals. The port goes in two steps: first a Godot backend that draws the existing projected triangles, so the game runs and plays the same; then real 3D one system at a time, starting with models.
- **Separate the simulation from drawing.** This is the main PrePort work (see "Rendering feeds gameplay" below).
- **CosmeticRandom goes into `RecordPlayback`**, as an agreed exception to "merges only flow up". It fixes nothing a player sees, so it does not belong on `OriginalSourceBugFixes`; on the recording layer it makes the recordings independent of rendering. All recordings are re-recorded once for it.
- **Headless mode** is a feature branch off `PrePort`, because it depends on the simulation/drawing split.
- **The x64 build** is done on `PrePort`.
- **No encoding conversion.** Only 7 source files (9 lines) contain Latin-1 bytes, and 6 of those lines are string literals that must keep their bytes (the French and German "no 3D card" strings compared with the registry, French joystick button names). Byte-safe editing is needed only for those files.
- **Removed before the port:** the Glide, OpenGL and DirectDraw backends. **Kept for now:** the software renderer, because FRED and the tools use it.

## Rendering feeds gameplay
The simulation is only correct when the D3D render code runs each frame. Before the renderer can be replaced, each of these has to move into the simulation or stop affecting it.

- **Shared random stream.** `myrand()` (`GlobalIncs\SystemVars.cpp:253`) and everything built on it (`frand`, `frand_range`, `vm_vec_rand_vec_quick`, `timestamp_rand`) is also used by render code: particles, model arcs, starfield, nebula and nebula lightning, radar and target box static, EMP jitter, beam flicker, shield hits, fireball orientation, ship sparks, supernova, and the camera shake in `apply_hud_shake` (`FREESPACE2\FreeSpace.cpp:3178-3217`). `docs\REPLAY.md` says the same. `origin/CosmeticRandom` (commits 4a21929, 62cf067) moves 137 call sites onto a separate cosmetic stream and lists every site in `docs/random_call_sites.md`. It deliberately keeps nebula lightning (it applies EMP), fireball type and timers, message choice and all debris on the gameplay stream.
- **Missile lock is computed while drawing the HUD, in screen pixels.** `HUD_render_3d` (`Hud\HUD.cpp:1225`) leads to `hud_update_lock_indicator` (`Hud\HUDtarget.cpp:3614`), which projects the target (`Hud\HUDlock.cpp:1232-1237`) and moves the lock indicator in pixels until it sets `Player_ai->current_target_is_locked` (`HUDlock.cpp:893-988`). Lock timing depends on resolution and field of view, and it only runs when the HUD is drawn.
- **Cargo scanning runs while drawing.** `hud_cargo_scan_update` (`HUDtarget.cpp:3586`) calls `player_inspect_cargo` and `ship_do_cargo_revealed` (`Playerman\PlayerControl.cpp:1697, 1796`).
- **`OF_WAS_RENDERED`** is set in `obj_render_all` (`Object\ObjectSort.cpp:250, 308`) from the view cone. Gameplay reads it to skip same-team ship collisions (`Object\CollideShipShip.cpp:666`), to pick which fireballs are freed when slots run out (`Fireball\FireBalls.cpp:742`, `Object\Object.cpp:583`), and for shield hit effects (`Ship\Shield.cpp:566`, `Ship\ShipFX.cpp:2267`).
- **The eye position and orientation** are set only by `g3_set_view_matrix` (`Render\3dSetup.cpp:209-215`) while drawing, and include the random shake. Gameplay reads them for asteroid wrapping (`Asteroid\Asteroid.cpp:1070-1085`), whether debris is created (`Debris\Debris.cpp:670-676`), target in reticle (`HUDtarget.cpp:2454-2569`), subsystem facing checks (`HUDtarget.cpp:1788, 2337`, `HUDlock.cpp:611`), player warnings (`Ship\Ship.cpp:8250`) and 3D sound positions.
- **Also:** `Player->subsys_in_view` is set while drawing brackets (`Hud\HUDbrackets.cpp:603`); `next_hit_spark` uses `Viewer_obj` (`Ship.cpp:4096`).
- **Frame order** (`game_frame`, `FreeSpace.cpp:4022`): controls, then `game_simulation_frame` (the trace is written at the end of it, `:3888`), then `game_render_frame_setup` and `game_render_frame`, which set `Viewer_obj`, the camera shake and the eye, and compute lock and cargo scan. The next simulation frame uses those results.

The aim is to move the view setup, lock, cargo scan and view-cone test into the simulation step without changing gameplay, and prove it with the recordings.

## Graphics
- Renderer choice: `gr_init` (`Graphics\2d.cpp:821`), switch at `2d.cpp:919-956`, modes in `Graphics\2d.h:568-572`. The game picks the mode only from the launcher's `Videocard` registry string (`FreeSpace.cpp:2111-2219`). `HARDWARE_ONLY` is always defined (`GlobalIncs\PsTypes.h:237`), so the game itself runs only D3D or Glide.
- `gr_screen` holds about 50 `gf_*` function pointers (`2d.h:397-552`), filled by GrD3D, GrGlide, GrSoft, GrDirectDraw and GrOpenGL. **OpenGL is an empty stub** (no `gl*` calls). DirectDraw and OpenGL start with `Int3()`.
- `Freespace2.vcxproj` compiles only `FreeSpace.cpp` and `LevelPaging.cpp` and links `code.lib` (`code\code.vcxproj`). `Scaler.cpp`, `TmapScanline.cpp` and `TmapScanTiled*.cpp` are not compiled.
- D3D still uses: `Font.cpp` (fonts), `PalMan` (`palette_flush`/`palette_update` from `2d.cpp:482, 644, 652`), and `Tmapper.h` for the `vertex` struct and `TMAP_FLAG_*`.
- Glide branches outside Graphics, to clean up when Glide goes: `FreeSpace.cpp:562, 1020, 1038, 2171-2181`, `Nebula\Neb.cpp:80, 408-416, 1123, 1492, 1610`, `Weapon\Weapons.cpp:3279`, `Asteroid.cpp:2234`, `Ship.cpp:8905`, `Model\ModelInterp.cpp:3300`, `Render\3dMath.cpp:277`, `Render\3dLaser.cpp:288, 460`, `MenuUI\Credits.cpp:727-729`, `MenuUI\MainHallMenu.cpp:1271`, `Bmpman\BmpMan.cpp:2372-2420`.
- **3D pipeline:** `g3_rotate_vertex` and `g3_project_vertex` (`Render\3dMath.cpp:157, 259`) transform and project on the CPU; `g3_draw_poly` (`Render\3ddraw.cpp:309`) clips and calls `gr_tmapper` with screen-space vertices. Models walk the POF BSP tree on the CPU, light each vertex on the CPU and draw each polygon with `g3_draw_poly` (`ModelInterp.cpp:546-931`). D3D gets `D3DTLVERTEX` (`GrD3DRender.cpp:695-998`) and never sees a world, view or projection matrix. About 250 `g3_draw_*`, 150 `g3_rotate_*vertex` and 75 `g3_project_vertex` call sites; the most are in Model, Render, FRED2, Ship, Hud and Weapon.

## FRED and the tools
- FRED uses the software renderer: `gr_init(GR_640, GR_SOFTWARE, 8, ...)` (`FRED2\Management.cpp:513`, `FRED2\FREDView.cpp:1591`), drawing into a DIB section (`GrSoft.cpp:566`) blitted to the MFC view. It loads an 8-bit palette and `BmpMan` remaps bitmaps for it (`bm_swizzle_8bit_for_fred`).
- FRED links the same `code.lib` as the game and shares mission parsing, sexp, ship, weapon and AI tables, models, nebula, starfield, briefing, campaign and cfile. Mission saving is FRED's own (`FRED2\MissionSave.cpp`). Known mismatch: Release defines `FRED` but `code.lib` is built without it (`TODO.md`).
- Pofview, Nebedit, Fonttool and `-standalone` also use the software renderer.
- The software renderer is full of inline assembly, so FRED stays a 32-bit build until it moves to Godot. A Godot FRED (an editor plugin reusing mission parsing, sexp and models through the same C++ library) stays possible as long as the game library does not depend on MFC or drawing.

## Platform layer
- **OS, window, timer:** `OsApi\OsApi.h`; `os_get_window()` returns the window as a `uint` (`OsApi.h:57`). The window procedure feeds mouse buttons into Io (`OsApi.cpp:398`). The timer is `Io\Timer.h`; `Timer.cpp:155-222` divides the performance counter in inline assembly.
- **Input:** `Io\Key.h`, `Mouse.h`, `Joy.h`; DirectInput 3 and the Immersion force-feedback library; `code\Pad` is XInput. Clean seam: input enters through `key_mark` and `mouse_mark_button`.
- **Sound:** `Sound\Sound.h`, `AudioStr.h`, `Gamesnd\GameSnd.h`; DirectSound with EAX and A3D, ACM decoding. Clean seam: nothing above Sound calls DirectSound.
- **Files:** `CFile\cfile.h`; 4 files use Win32 directory calls. VP entries are read as a raw struct (`CfileSystem.cpp:515`).
- **Video:** there is no movie player. Every `movie_play` call is commented out. Cutscenes are new work in Godot.
- **Game state** is fixed-size global arrays (`Objects`, `Ships`, `Wings`, `Weapons`, `Ai_info`, `Players`), one game per process.
- **64-bit problems:** `bitmap.data` is a `uint` holding a pointer (`GlobalIncs\PsTypes.h:321`, 23 casts in `BmpMan.cpp`); 44 pointer-to-int casts in 21 files, 28 of them in Graphics; the timer's inline assembly (x64 MSVC has no inline assembly). `typedef long fix` is fine on Windows x64. Floating-point results on x64 may not match x86 exactly; test against TraceDiff's tolerances.

## Headless mode in fs2_source_ue
- Flag `-nographics` (commits cf8d0b7, 050c1d1): a null renderer `GR_DUMMY` (`code/Graphics/GrDummy.cpp`, 167 lines) plus about 100 lines of gates in `2d.cpp`, `2d.h`, `FreeSpace.cpp`, `BmpMan.cpp`, `Neb.cpp`, `HUDmessage.cpp`, `cmdline.cpp` and `SystemVars`. No Unreal code. A window is still created; sound and timing are unchanged.
- Its commits are mixed with its own `-autotest` and `-at_*` code, which clashes with this repo's flags of the same names. Take only the `No_graphics` / `GR_DUMMY` parts.
- **As written it would not reproduce our traces:** `gr_d3d_flip` (`GrD3D.cpp:1757, 1767`) makes recorded mouse reads that the dummy flip skips, so replay desyncs; it forces 640x480, which replay rejects; it skips `HUD_printf`. After the simulation/drawing split, headless means "do not draw", and its test is that headless traces match windowed traces.

## Recordings after the port
`docs\BRANCHES.md` says recordings stop working at `Godot`. That may not be so. The recordings replay the game's reads of the clock, keys, mouse, joystick and sound device. If the Godot platform layer implements the same functions the recorder hooks (`timer_get_*`, `key_mark`, mouse, joystick and sound results), the recordings could referee the port itself. Untested; worth designing the platform layer around those hooks on PrePort.

## Plan
1. Done: CosmeticRandom into `RecordPlayback` with a second pass over the call sites (`docs/random_call_sites.md`, which also lists all the gameplay that happens inside render code); autotest missions re-recorded; hand-played recordings removed; merged up to `Enhanced` (tag `latest-2026-10-13e`) and `PrePort`.
2. Done (6397158): Glide, OpenGL and DirectDraw backends removed; non-Direct3D requests are forced to Direct3D. Not yet checked that FRED still builds: MFC is not installed on DESKTOP-MAIN.
3. Move the view setup, lock, cargo scan and view-cone test into the simulation step, with no gameplay change. Pieces 1-2 are on `PrePort` (093683c); the rest is in progress on the branch `LogicDrawSplit` (folder `fs2_LogicDrawSplit`), whose `docs/LOGIC_DRAW_SPLIT.md` has the status and what is left. A new logic phase runs where rendering starts today and does the gameplay parts in the same order; drawing only reads state. One commit per piece, replay check after each, stop on any divergence. Decided 2026-10-13:
   - HUD-gated gameplay (lock, cargo scan, squad orders, wingman warnings) keeps its conditions (HUD shown, cockpit view) as game state, but its logic moves out of the drawing code so it runs headless.
   - Missile lock and the view-cone test use a fixed **game view**: the art resolution (640x480 or 1024x768), 4:3 and the original field of view, separate from the display. Identical today; later display resolutions and widescreen cannot change lock timing or collision culling.
   - Capital-ship break-up debris (`half_ship_render_ship_and_debris`) always spawns, not only when the split ship is on screen, with the debris limits raised to allow for it. This is an **intended change to the game logic**: it goes on its own branch off `PrePort`, with its own document and an entry in a log of intended gameplay changes, and the affected missions are re-recorded.
   - Test: a `-nodraw` flag skips drawing the mission (3D scene, target box model, HUD) but still clears and flips. Step 3 is complete when every recording replays identically with `-nodraw`.
   Pieces: (1) camera: viewer, shake, eye position and orientation; (2) view-cone test in the game view; (3) missile lock and cargo scan; (4) squad orders, reinforcements, wingman warnings; (5) nebula lightning EMP and half-ship debris.
4. Headless (feature branch off `PrePort`).
5. x64 build: the timer's assembly, pointer casts, then replay on x64.
6. The platform layer around the recorder's hooks.

## Not before the port
Real 3D, stripping Network, FRED work, the `/analyze` bug list in `TODO.md` (that belongs on `OriginalSourceBugFixes`), and tidying every Glide branch beyond what removing Glide needs.

## Lessons from the Unreal ports
Two earlier attempts, both on GitHub and both dead references (read for lessons, never fixed):
- `RandomTiger/fs2ue` (2017 to 2021, Tom by hand). It ran inside Unreal, in the editor and packaged: booted straight into a mission, simulated headless, drew textured ship hulls moving with the simulation, played DirectSound, and could Play/Stop repeatedly. It never had HUD, menus, 2D, effects, keyboard or mouse input, a player camera or correct submodels. Its goal was different: an Unreal actor and Blueprint per ship, with Unreal owning them. Godot is planned as a wrapper for platform and device calls, with the C++ simulation in charge.
- `RandomTiger/fs2_source_ue` (local clone `U:\Projects\GitHub\fs2_source_ue`). No Unreal code was ever written; it got a VS2022 build, a bug audit (`CODE_ISSUES.md`, with false positives), `-autotest` and a `-nographics` null renderer.

"Confirmed" means the point was checked in code; "claimed" means only a commit message or doc says it.

### What it takes to run the game inside a host engine (from fs2ue, confirmed)
- **Split `WinMain` into init, one frame, and shutdown.** fs2ue's `FREESPACE_Init(cmdline)`, `FREESPACE_Update(dt)`, `FREESPACE_Shutdown()` (`code/FREESPACE2/FreeSpace.cpp:5754-6000`, commit c755e296). One update ran `os_poll`, the pad and mouse update, then `gameseq_process_events()`. It ignored the host's frame time and kept the game's own clock, which is what the recorder needs too.
- **No blocking loops.** Popups run their own loop calling `os_poll`/`os_sleep`, which hangs a host that owns the frame; fs2ue made them return -1 at once (`Popup/Popup.cpp:832`). Every modal loop has to become per-frame. `os_sleep` was compiled out because it killed the host frame rate (c2a00743).
- **The game must be restartable in one process.** The biggest single fix (b630630e, 019b7652, 080be685): remove the `atexit()` registrations (`gr_close`, `bm_close`, `model_free_all`), add deinit functions that reset the `*_inited` statics (ship, weapon, HUD energy/reticle/targeting), null freed pointers (`Lcl_ext_filename`, `Ship_info.subsystems`), reset `Num_fonts`, `bm_next_handle`, the countermeasure globals and `ds_initialized`, and clear `Polygon_models`. This applies to a GDExtension reload and to running replays back to back in one process. Better: all game state reset in one place.
- **Files depend on the current directory.** fs2ue swapped to a hard-coded game folder around init (5a404451). The platform layer should give cfile an explicit root.
- **DirectSound needs a window handle.** fs2ue compiled out window creation and returned the host's window from `os_get_window()` (0c97e936); sound only worked after that.
- **The null renderer had to set `D3D_enabled = 1`** (ed86629e), because game code branches on it even when nothing is drawn.
- **Windows headers clash with engine headers** (`UpdateResource`, `GetObject`, `GetClassName`, min/max). Keep `windows.h` out of anything the engine binding includes.
- **Original-code bug:** `parse_objects` reads an uninitialised `p_object` (`Mission/MissionParse.cpp`); fs2ue cleared it. Candidate for `OriginalSourceBugFixes`, after checking.

### Structure to avoid (fs2ue)
About 240 `#if FS2_UE` blocks in 94 files; Unreal types (`FVector`, `TArray`, `AShip*`) inside core game headers (`PsTypes.h`, `Ship.h`); game code calling the engine (`SpawnActor` from `Ship.cpp`); rendering faked through the dummy `gr_*` table and a global; teardown fixed by scattered resets; hard-coded paths. The cleaner shape: a game library with no engine headers; init / step / shutdown; after each simulation step, a read-only snapshot of objects (number, signature, model, transform); platform hooks (time, input, sound, files, window) as function tables the host fills in, the same hooks the recorder uses.

### Mirroring simulation objects for drawing (later real-3D stage)
- fs2ue created one actor per ship in `ship_create` and pushed its transform in `ship_process_post`, after physics (`Ship.cpp:3609`); that point is right. But nothing removed the actor on `ship_delete`, and the raw pointer stayed in the reused `Ships[]` slot. Key nodes on object number plus signature, create and free them from object creation and deletion, and share one mesh per model.
- **Meshes from the model interpreter:** fs2ue ran `model_interp_sub` once with lighting and culling off, and captured the vertices in the dummy `gr_tmapper` (`Model/modelcache.cpp`). It needed back-face culling, clipping and BSP ordering disabled. Bugs: every chunk copied the whole vertex array, submodels were baked with no offset or rotation, and it picked `detail[n_detail_levels]` (out of range; `detail[0]` is the most detailed). Reading POF files directly is cleaner.
- **Coordinates:** FS2 is left-handed, Y up, Z forward. Unreal is left-handed, so fs2ue only swapped axes (`FVector(z, x, y)`) and reversed triangle winding (4611dea6, 79d1c56f). **Godot is right-handed** (Y up, -Z forward): one axis has to be mirrored and the winding flipped. fs2ue applied no unit scale.

### Determinism (fs2_source_ue, confirmed in this tree)
- **Out-of-bounds reads that affect gameplay**, repeatable only while memory layout stays the same, so they would split recordings on x64 or Godot: `Ship/ShipHit.cpp:917` reads `Weapon_info[-1].subsystem_factor` when a ship's death shockwave lands within 10 m of a subsystem; `Ship/AiBig.cpp:870` indexes `Weapon_info` with a weapon slot number (up to 350, against 200 entries). Fix on `OriginalSourceBugFixes` before step 5.
- **Removing an Assert can change gameplay.** In Release, `vm_matrix_interpolate` with a zero rotation axis normalises it to (1,0,0) and turns about 57 degrees (`Math/VecMat.cpp:597-611`). fs2_source_ue's "fix" (9edca07) snapped instead, which changes behaviour. Every such fix needs the recordings.
- **`#pragma optimize("", off)`** in AiCode, AiBig, ShipHit, CollideShipShip, EventMusic and MissionBriefCommon, never turned back on in AiCode (`AiCode.cpp:449`). Another compiler ignores it, so floating-point code generation changes.
- fs2_source_ue tested by feel (training missions as unit tests), so behaviour-changing fixes got in unnoticed. The recordings exist to stop that.

### Headless (step 4)
- fs2_source_ue's `-nographics` also skips `joy_init()` (cf8d0b7), another recorded read headless would miss.
- `GR_DUMMY` makes `bm_set_components` a no-op, so headless bitmaps have no pixels.
- Headless still needs real font metrics: `split_str` and HUD message wrapping measure text in pixels.
- Not every renderer call goes through `gf_*`: `gr_bitmap`, `gr_bitmap_ex`, `gr_activate`, `gr_force_windowed`, `neb2_level_init`, `neb2_get_fog_values` and the `BM_SELECT_*` formats switch on `gr_screen.mode` and `Int3` on an unknown mode. A new backend needs a case in each.
- Do headless after the simulation/drawing split, not before: fs2_source_ue's headless mode changed behaviour.

### x64 (step 5)
- `_USE_32BIT_TIME_T` (`GlobalIncs/PsTypes.h:172`) is set for every file and does not compile on x64 (UCRT `#error`). Without it `time_t` is 8 bytes, which breaks the raw read of the VP index (`VP_FILE.write_time`, `CFile/CfileSystem.cpp:479, 515`). fs2ue used a 4-byte `fs2_time_t` for the file struct (09e7d89f). `Scoring.h:259-260` also uses `time_t`.
- The DirectX import libraries in `code/DirectX` (`vDdraw`, `vDinput`, `vDsound`, `vDxguid`) are x86 only.
- `Anim/AnimPlay.cpp:1129` allocates the frame pointer array as `(height+1) * 4` bytes, half what x64 needs.
- fs2ue's x64 timer used 64-bit C maths but truncated to milliseconds, which may move replay traces; it also disabled `Warning()` on Win64 and forced one HRESULT to `S_OK` (b2187b2b). Don't repeat either.
- On x64 the registry key is under `WOW6432Node\Volition`.

### Building FRED and the tools
fs2_source_ue's `Directory.Build.props` pinned `VCToolsVersion` to the toolset that has MFC (MSBuild picked one without it, error MSB8041), and turned off SAFESEH for the old DirectX libraries. `Enable3dControls()` asserts at run time in MFC 14 (a852378).

### Other
- Keep the flight model, `model_collide`/Fvi and the collision pair scheduler as they are rather than use engine physics (fs2_source_ue's analysis, claimed). Original bugs there to keep faithfully: debris/weapon pairs skipped the wrong way round (`Object/ObjCollide.cpp:510`), shockwave face area on the wrong axis (`Physics/Physics.cpp:1162`), `physics_predict_vel` never writing its output (`:798`), the pair pool failing silently at 8000 (`ObjCollide.cpp:238, 535`).
- `targa_copy_data` (`TgaUtils/TgaUtils.cpp:102`) never writes its output; don't reuse it for texture loading.
- fs2_source_ue's editing corrupted the French registry string in `FreeSpace.cpp` (ad144a9), the failure the Latin-1 rule exists for.
- Its CI cost about 15 commits: FS2 exits with code 255, the exe must start in the game folder, and stale processes lock the workspace.
