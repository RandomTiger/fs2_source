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
| Branches on `gr_screen.mode` outside Graphics | 20 lines, 8 files | `BmpMan.cpp` 8, `Neb.cpp` 3. The dead Glide and DirectDraw tests were removed, and the 30 `D3D_enabled` tests now ask the renderer `gr_screen.hardware`, which Direct3D and the null renderer set (2026-10-16) |
| Direct calls into the D3D backend | 0 | Fixed 2026-10-16: z bias, flush, frame start and stop, and the level-load texture preload (with the ship glow build) are renderer table entries (`gr_zbias`, `gr_flush`, `gr_start_frame`, `gr_stop_frame`, `gr_preload_init`, `gr_preload`, `gr_preload_done`) |
| Colour-format globals (`Gr_*`, `D3D_32bit_textures`) outside Graphics | 84 | `BmpMan.cpp` 75 |
| Win32 calls in game code | ~55 | `FreeSpace.cpp` 35 (`MessageBox`, `Sleep`, registry, current directory, CD drive scan, `CreateProcess`); `WinDebug.cpp` (assert dialogs); `PadXInput.cpp` |
| `fopen` around cfile | ~6 game-side sites | `FreeSpace.cpp`, `SystemVars.cpp`, `Scoring.cpp`, `ModelRead.cpp` |
| `windows.h` in game code | 4 files | `FreeSpace.cpp`, `WinDebug.cpp`, `Math/Fix.cpp`, `PadXInput.cpp` |

Missing seams to add: a message box, current directory and directory listing, and renderer capability queries to replace the `gr_screen.mode` and `D3D_enabled` tests. `os_sleep` exists, but `FreeSpace.cpp` calls `Sleep` directly.

Logic-side leaks (gameplay depending on drawing or window size) are tracked in `STATUS.md` under gate 2.

## Size of a backend
About 200 functions, or about 120 if cfile stays in the game. Render is about 65 to 70 plus about 25 for textures, audio 56, input 47, platform about 28. Clearing the leaks first means replacing about 65 renderer branches and 84 colour-global reads with a few capability queries.
