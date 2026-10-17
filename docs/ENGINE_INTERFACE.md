# Engine interface

What a host engine (Godot through GDExtension, or another C++ engine) must provide for the game to run, and where game code still goes around it. PrePort gate 3 (D-026): this set is documented and is the only way logic reaches presentation. **Status: inventory only (2026-10-16).** The seams exist, but the leaks below still go around them.

Counts are for game code. `Network`, `Inetfile`, FRED and the tools are left out. They come from a read of `LogicDrawSplit` at 8ce44a4.

## Seams
| Group | Header | What it covers | Functions |
|---|---|---|---|
| Render | `Graphics/2d.h` (`gr_screen.gf_*`) | Frame (flip, start and stop frame, clear), state (clip, colour, font, bitmap, shader, fog, cull, z-buffer), drawing (bitmap, string, line, circle, rect, gradient, `tmapper`), screen operations (fades, flash, save and restore) | 56 pointers |
| | `2d.h`, functions not in the table | `gr_init`, `gr_close`, `gr_init_font`, `gr_bitmap`, `gr_bitmap_ex`, cursor bitmap, `gr_pline_special` | ~10 |
| | `Bmpman/BmpMan.h`, `Palman/PalMan.h`, `Graphics/Font.h` | Texture upload and preload, palette, fonts | ~25 |
| Audio | `Sound/Sound.h` | `snd_*`: load, play (2D, 3D, looping), stop, volume, pan and pitch, queries, listener, environment | 37 |
| | `Sound/AudioStr.h` | `audiostream_*`: music and voice streams | 19 |
| Input | `Io/Key.h`, `Io/Mouse.h`, `Io/Joy.h` | Key, mouse and joystick events and state | 18 + 13 + 16 |
| | `Pad/PadGame.h` | XInput pad (Enhanced feature) | 7 |
| Platform | `Io/Timer.h` | Clock (`timer_get_*`); `timestamp*` is game logic built on it | 11 |
| | `OsApi/OsApi.h` | Window, poll, foreground, sleep | 10 |
| | `OsApi/OsRegistry.h` | Settings (`os_config_*`, registry or ini, D-016) | 8 |
| | `CFile/cfile.h` | Files and VP archives | ~80 (can stay in the game over a file-system adapter) |

Optional or not carried: CD audio (`RBAudio.h`), force feedback (`Joy_ff.h`), multiplayer voice (`rtvoice.h`, D-014).

## The recorder's hooks are the platform boundary
The recorder already hooks about 28 seam functions. Each is a value the game takes from outside: 3 clock reads, `os_poll` and `os_foreground`, key, mouse and focus events, the mouse position, 6 joystick reads, pad state, 3 sound starts (whether a channel was free), 4 sound queries and 4 music-stream queries (`Replay/Replay.h`, `RT_*`). If the host implements exactly these, the recordings can keep refereeing after the engine moves (`docs/PREPORT.md`, "Recordings after the port"). Everything else in the seams is output only, so the host may implement it however it likes.

## The gameplay random stream belongs to the game
`rand()` and `srand()` in game code are the game's own copy of the MSVC CRT generator (`fs2_rand`, `GlobalIncs/SystemVars.cpp`), so the host's threads and libraries cannot shift it. The host never calls the game's `rand`; the gameplay stream is owned by logic (D-039).

## Animation timing belongs to the game
Animations (talking heads, HUD and menu anims) keep their timing in the logic step. Bitmap creation failing (`bm_create` returning -1) leaves an animation's timing unchanged; that frame just has nothing to draw (D-040). The host only supplies pixels.

## The camera stays in the game (decision D-036)
The view setup is part of the game's logic step, not the host's. `game_render_frame_setup` decides the camera every frame, with or without drawing. Its shakes, `apply_hud_shake` (afterburner, engine wash, shudder) and `supernova_apply_shake`, draw from the gameplay random stream, so moving the camera anywhere else shifts every later gameplay random number. The host receives the finished eye from `game_view_set` and draws from it. It never computes shake or moves the camera itself. A host camera feature (VR, free camera, replay camera) has to go through the C++ view setup. The trace records the eye every frame (trace version 2), so a host that broke this rule would show as a TraceDiff `eye` field at the first frame.

## The host runs the C++ menus (decision D-038)
The menus are game flow, not presentation. The host drives the C++ menu state machine through the render and input seams and draws what it asks for. Some gameplay happens only inside menu screens: the loadout is applied on the ship-select commit (`MissionShipChoice.cpp:1952`, which can open a popup at `:1977`), the saved loadout is restored when the briefing opens (`common_select_init` from `brief_init`), and goals, scoring and the next mission are settled when the debriefing opens (`MissionDebrief.cpp:2203-2222`), with the campaign advancing on its accept button (`:1635`). These stay where they are. The menus' presentation needs are part of this interface: in particular `bm_load` must return valid handles for menu art, since a missing briefing mask is fatal (`MissionBrief.cpp:1185`), as in the original. If the port ever replaces the menus with its own UI, or needs a headless campaign past one mission, those four steps are extracted into plain functions the menus call at the same points.

## Leaks: where game code goes around the seams
| Kind | Count | Main places |
|---|---|---|
| Branches on `gr_screen.mode` outside Graphics | 10 lines, 3 files | `BmpMan.cpp` 7 (texture colour format, inside the texture seam, D-044), the demo title screen in `FreeSpace.cpp` 2 (`FS2_DEMO` and `OEM_BUILD` only, not built), and the null-renderer log line in `Autotest.cpp`. None in logic. `game_init` picks the renderer with `gr_init`, which is host start-up. The dead Glide and DirectDraw tests were removed, and the other tests now ask the renderer `gr_screen.hardware`, which Direct3D and the null renderer set (2026-10-16) |
| Direct calls into the D3D backend | 0 | Fixed 2026-10-16: z bias, flush, frame start and stop, and the level-load texture preload (with the ship glow build) are renderer table entries (`gr_zbias`, `gr_flush`, `gr_start_frame`, `gr_stop_frame`, `gr_preload_init`, `gr_preload`, `gr_preload_done`) |
| Colour-format globals (`Gr_*`, `D3D_32bit_textures`) outside Graphics and BmpMan | 0 | Fixed 2026-10-16 (D-044): anim frames are unpacked at the renderer's `gr_screen.texture_bpp`, the debug VRAM display asks `gr_texture_memory`, and the Debug-only D3D settings overlay on the main hall was dropped. The 75 in `BmpMan.cpp` are inside the texture seam (below) |
| Win32 calls in game code | 0 | `WinDebug.cpp` and `PadXInput.cpp` are platform backends, not leaks (D-047, below). `FreeSpace.cpp` has none (2026-10-16): message boxes and sleeps go through `os_message_box` and `os_sleep`, the timer resolution through `os_timer_precise`, and the Windows host functions moved to `FreeSpaceWin32.cpp` (D-045, below) |
| `fopen` around cfile | 0 in the interface (D-046) | Debug dumps, not interface; a port compiles them out: `ShipTextures.txt`/`ShipTimings.txt` (`FreeSpace.cpp`), the monitor file (`SystemVars.cpp`), `dump` (`WinDebug.cpp`), `ui.out` (`WINDOW.cpp`), the PCX writer (`pcxutils.cpp`), the Scoring debug file. Start-up reads before cfile is up use standard C on paths relative to the exe directory, which the host provides with the files: `data\cmdline.cfg` (`cmdline.cpp`), `pad.cfg` (`PadGame.cpp`), `version_compare` (`version.cpp`). The recorder, the log and the launcher updater (`FreeSpaceWin32.cpp`) are host side |
| `windows.h` in game code | 0 | Only the platform backends include it (D-047). `FreeSpace.cpp` dropped it, and `Math/Fix.cpp` computes `fixdiv` and `fixmuldiv` with its own copy of `MulDiv`, checked equal on 50 million cases (2026-10-16) |

No missing seams. The current directory is read with the C runtime's `_getcwd`. Added: `os_find_first`, `os_find_next` and `os_find_close` (`OsApi.h`, directory listing; the campaign list uses them, and CFile, the file seam itself, keeps its own), `os_message_box` (`OsApi.h`), and the renderer capability queries `gr_screen.hardware` and `gr_screen.texture_bpp`.

Logic-side leaks (gameplay depending on drawing or window size) are tracked in `STATUS.md` under gate 2.

## The Windows host (decision D-045)
`FREESPACE2/FreeSpaceWin32.cpp` is the Windows host: `WinMain` and `WinMainSub`, launcher update and launch, and CD detection. Another host leaves the file out and supplies in its place:
- the entry point: it calls `game_host_init(cmdline, root_dir)` once and, if that returns 1, `game_host_frame()` until it returns 0, then `game_host_shutdown()` for the exit code (`FreeSpace.h`, D-050). One `game_host_frame` is one pass of the game sequence, the recorder's frame boundary; popups and other modal loops still run inside it (D-041);
- the root directory, passed to `game_host_init`; cfile searches it first, no longer reading it back from the current directory (D-050). The host also makes it the current directory before `game_host_init`, for the start-up reads before cfile is up (D-046); `cfile_init` changes to it as well. `Freespace_total_ram` no longer reaches game logic: the bitmap mode is pinned to high memory, the reference (D-046); only the LowMem setting selects the retail low-memory mode;
- `game_run_launcher` (no video card configured), `game_launch_launcher_on_exit` and `game_maybe_update_launcher`, or nothing;
- the CD hooks (`init_cdrom`, `find_freespace_cd`, `game_do_cd_check`, `game_do_cd_check_specific`, `game_do_cd_mission_check`, `game_cd_changed`, `set_cdrom_path`), which can report no CD needed.
The version string (`get_version_string`) stays in the game; it uses no Windows calls.

## Debug services and the pad are platform backends (decision D-047)
`GlobalIncs/WinDebug.cpp` and `Pad/PadXInput.cpp` are Windows implementations behind headers that carry no Windows types; another host replaces them, as it replaces OsApi.
- `pstypes.h`: `Assert`, `Error` and `Warning`, and the `vm_malloc`, `vm_strdup` and `vm_free` allocator. `Error` terminates. Under autotest none of them shows a dialog: each is logged through `autotest_report`, `Assert` and `Warning` carry on, and `Error` exits with code 1. Otherwise the Windows backend shows a dialog, copies the text to the clipboard and can dump the call stack; another host may only log.
- `PadXInput.h`: the pad backend, or a null one that reports no pad. `PadGame.cpp` and the pad core hold the logic.

## The object snapshot (decision D-050)
After each simulation step `game_frame` copies every object in use, in used-list order, with `object_snapshot_take` (`Object/ObjectSnapshot.h`): object number, signature, type, instance, flags, model number (-1 if none) and transform. A host draws from `object_snapshot_count` and `object_snapshot_get`. Taking it reads game state and changes none of it; nothing in the game reads the copy.

## What gameplay may read from BmpMan and models (decision D-048)
- `bm_get_info` for an animation: `w`, `h`, `nframes` and `fps` are the file's own values, set once by `bm_load_animation` from the anim header (`fps` is halved only in the low-memory mode, which only the LowMem setting selects, D-046). The explosion's art version and length come from them (`fireball_get_lod`), so a host's texture pipeline must not change them. Texture resizing belongs to the renderer's upload.
- Collision reads no texture handle. It skips faces by `texture_invisible`, set from the model's texture names when the model is read (`ModelRead.cpp`). `Mc->hit_bitmap` is written for callers but no gameplay reads it.

## BmpMan is on the renderer's side (decision D-044)
A leak is logic reading presentation state, or reaching presentation by a path other than this interface. BmpMan's pixel conversion (`bm_lock`, the `BM_SELECT_*` pointers, the `Gr_current_*`, `Gr_t_*`, `Gr_ta_*` and `Bm_pixel_format` globals) is presentation: it turns image files into the renderer's texture format, and nothing on the logic side reads it. Its boundary is `BmpMan.h`. A host either reuses BmpMan and sets its format globals from its own texture format, or replaces it behind `BmpMan.h`. Either way nothing in BmpMan names a Godot type.

## Size of a backend
About 200 functions, or about 120 if cfile stays in the game. Render is about 65 to 70 plus about 25 for textures, audio 56, input 47, platform about 28. The renderer branches and colour-global reads outside the seams are now capability queries (`gr_screen.hardware`, `gr_screen.texture_bpp`, `gr_texture_memory`).
