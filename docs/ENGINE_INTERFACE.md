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

## Leaks: where game code goes around the seams
| Kind | Count | Main places |
|---|---|---|
| Branches on `gr_screen.mode` or `D3D_enabled` outside Graphics | ~65 lines, 16 files | `ModelInterp.cpp` 20, `BmpMan.cpp` 12, `Neb.cpp` 9, `FreeSpace.cpp` 5 |
| Direct calls into the D3D backend | 7 | `d3d_zbias` (`ModelInterp.cpp`), `d3d_start_frame`/`d3d_stop_frame` (`FreeSpace.cpp`), D3D preload (`BmpMan.cpp`) |
| Colour-format globals (`Gr_*`, `D3D_32bit_textures`) outside Graphics | 84 | `BmpMan.cpp` 75 |
| Win32 calls in game code | ~55 | `FreeSpace.cpp` 35 (`MessageBox`, `Sleep`, registry, current directory, CD drive scan, `CreateProcess`); `WinDebug.cpp` (assert dialogs); `PadXInput.cpp` |
| `fopen` around cfile | ~6 game-side sites | `FreeSpace.cpp`, `SystemVars.cpp`, `Scoring.cpp`, `ModelRead.cpp` |
| `windows.h` in game code | 4 files | `FreeSpace.cpp`, `WinDebug.cpp`, `Math/Fix.cpp`, `PadXInput.cpp` |

Missing seams to add: a message box, current directory and directory listing, and renderer capability queries to replace the `gr_screen.mode` and `D3D_enabled` tests. `os_sleep` exists, but `FreeSpace.cpp` calls `Sleep` directly.

Logic-side leaks (gameplay depending on drawing or window size) are tracked in `STATUS.md` under gate 2.

## Size of a backend
About 200 functions, or about 120 if cfile stays in the game. Render is about 65 to 70 plus about 25 for textures, audio 56, input 47, platform about 28. Clearing the leaks first means replacing about 65 renderer branches and 84 colour-global reads with a few capability queries.
