# Gameplay recorder

This build of FreeSpace 2 can record one mission played by a person and replay it exactly. It writes a per-frame trace of the gameplay state, so a later build can be checked for any change in how the game plays.

## Recording

```
FS2vs2022d.exe -window -record recordings\NAME.fs2rec -trace recordings\NAME.trace
```

1. Start the game, pick the pilot and the mission (campaign ready room, or the tech room simulator), and play it.
2. The recording covers the mission only. It starts when the player enters the mission, and the mission load and the time spent in the briefing and loadout screens do not count.
3. It ends when the mission ends: debriefing, a quit back to the main hall, a restart from the death popup, or closing the game. One process records one mission. Quit and relaunch to record another.
4. `NAME.fs2rec.record.log` reports `RECORD OK` and counts every recorded value by type.

To make a recording with nobody playing, use the autotest (`docs/AUTOTEST.md`):
```
FS2vs2022d.exe -window -autotest -at_mission sm1-03 -record recordings\NAME.fs2rec -trace recordings\NAME.trace
```

Put a file name in double quotes if it contains a space.

### Recording every mission you play

```
FS2vs2022d.exe -window -autorecord DIRECTORY
```

- Each mission is recorded to `DIRECTORY\MISSION.fs2rec`, with `MISSION.trace` and `MISSION.fs2rec.record.log` next to it. With no directory, it is `Recordings` in the game directory.
- A recording replaces any earlier recording of the same mission, but only if the mission ends properly: debriefing, a quit back to the main hall, or a restart from the death popup. If the game crashes or is closed during the mission, the earlier recording is kept.
- Only the first mission since the game started can be replayed, because some state carries over from one mission to the next. So, back in the main hall after a mission, a message says so and the game restarts itself with the same command line.
- A mission started without going back to the main hall, for example by restarting from the death popup, is not recorded, and a message says so.

## Replaying

```
FS2vs2022d.exe -window -replay recordings\NAME.fs2rec -trace replays\NAME.trace
TraceDiff recordings\NAME.trace replays\NAME.trace
```

- Replay starts the game and loads the recorded pilot and campaign save from the recording into `data\players\replay`, so the player's own pilot is never touched. It then goes through the normal start path: main hall, ready room or simulator, briefing, and commit with the recorded loadout. After that it plays the mission from the recorded values, and quits when the recording ends.
- `NAME.fs2rec.replay.log` reports `REPLAY OK`, `DESYNC ...` or `ERROR ...`.
  - A **desync** means this run asked the OS for something the recording does not have at that point, so the game had already done something different. The process exits with code 3.
  - An **error** is a setup mismatch: a different mission file, skill level, screen size or sound settings. The process exits with code 4.
- Add `-fulltrace` to write the full state every frame instead of every 60 frames. Use it to pin down the exact frame and field once TraceDiff has found a divergence.

## What is recorded

Every value the game takes from the OS during the mission, as one ordered stream. The game reads them in the same order on replay.

| Source | Hook |
|---|---|
| Clock (main thread only) | `timer_get_fixed_seconds`, `timer_get_milliseconds`, `timer_get_microseconds` (`Io/Timer.cpp`) |
| Keyboard events | `key_mark` (`Io/Key.cpp`) |
| Focus events | `key_lost_focus`, `key_got_focus`, `os_foreground` |
| Mouse | `mouse_mark_button`, cursor position (`Io/Mouse.cpp`) |
| Joystick | `joy_down`, `joy_down_count`, `joy_up_count`, `joy_down_time`, `joystick_read_raw_axis`, `joy_get_pos` (`Io/Joy.cpp`), `control_get_axes_readings` (`ControlConfig/ControlsConfig.cpp`) |
| Sound device state | `snd_play*` (whether a channel was free), `snd_is_playing`, `snd_time_remaining`, `snd_get_pitch`, `snd_num_playing` (`Sound/Sound.cpp`) |
| Music stream state | `audiostream_is_playing`, `audiostream_is_paused`, `audiostream_done_reading`, `audiostream_get_bytes_committed` (`Sound/AudioStr.cpp`) |
| Window message boundary | end of `os_poll` (`OsApi/OsApi.cpp`), so recorded events are injected at the same point |

The stream starts at `GS_EVENT_ENTER_GAME`. The mission load, briefing and loadout screens run live and are not replayed: the load's loading-screen flips depend on what the menus already cached, so they differ between runs. So these are put back at `GS_EVENT_ENTER_GAME`:

- **The random number stream.** It is re-seeded with `srand(level seed)` in both record and replay, because the briefing consumes `rand()` for as long as the player stays in it. This is the only change to how the game plays while recording.
- **Time state** that the briefing advanced: `timestamp_ticker`, `Last_time`, `Last_delta_time` and `Missiontime`. These come from the recording header.
- **The loadout.** `Player_loadout` comes from the header. Replay commits it through the normal briefing path.
- **The view position.** `View_position` is left by whatever the menus last drew in 3D, so it depends on the path the player took through them. Until the player enters the mission nothing updates it, and engine sounds start for the ships within hearing range of it. It comes from the header (version 3); older recordings replay without it and may desync on the first frames.

The stream ends in `game_enter_state` when the game enters the main menu or the debriefing, before that state's init runs. The main hall's init moves the cursor, and that reads the window focus only when the cursor is not already in place, so it can differ between builds. Recordings made before this change end one step later and may not replay.

`Semirand[]`, which is filled from the clock at startup, and the detail settings come from the header at mission load.

A hooked function called from inside another hooked function is not recorded on its own. For example, the clock read inside `joy_down_time` is covered by `joy_down_time`'s own value.

## File format (`.fs2rec`, version 3)

Version 1 files, which have no autotest settings, and version 2 files, which have no view position, still replay.

All values are little-endian.

1. `replay_header` (`code/Replay/Replay.cpp`): magic `FS2REC`, version, build, mission name and checksum, game mode, campaign, callsign, level seed, `Semirand`, skill, detail, screen size, sound settings, loadout size, time state at mission entry, the autotest settings (version 2), the pilot and campaign file sizes, and the view position at mission entry (version 3).
2. `loadout_data`, as raw bytes.
3. The pilot file (`.plr`) and the campaign save file (`.csg`), as they were on disk when the mission loaded.
4. The stream. Each entry is a tag byte (`RT_*` in `Replay.h`) and then its values:
   - a single int is a zigzag varint of the difference from the previous value with the same tag;
   - several ints are a varint count followed by zigzag varints;
   - a float is 4 raw bytes.

   `RT_INPUT_DEVICE` holds the state of an input device the original game did not have (such as a control pad), as a list of ints whose meaning belongs to that device's code (`replay_input_device`). A build without the device writes no such entries, and a build with it treats a missing entry as "not connected", so recordings replay across both and the format needs no change for each new device.
## Trace format

See `code/Replay/TraceFormat.h`. Each frame has a header with the frame number, mission time, frame time, `timestamp_ticker`, `Rand_count` and a 64-bit hash of the full state. The full state is written every 60 frames, or every frame with `-fulltrace`.

The state covers:

- the player's `control_info` and `button_info`;
- mission events and goals;
- for every object, keyed by signature:
  - flags, position, orientation, velocity, rotational velocity, hull and shields;
  - for ships: flags, class, weapon energy, afterburner fuel, selected banks, secondary ammo, AI mode, submode, target and goal, and the hits of each subsystem;
  - for weapons: class, life left, target and flags.

## TraceDiff

```
TraceDiff A.trace B.trace                  exact: the first frame whose state differs, and which fields
TraceDiff A.trace B.trace -tol tol.txt     within tolerances; integer values (events, goals, AI modes, objects existing) stay exact
TraceDiff A.trace B.trace -curve           CSV: the largest difference of each kind at every full frame
```

The tolerance file has one `kind value` per line. The kinds are `pos`, `orient`, `vel`, `rotvel`, `health`, `energy` and `control`. See `docs/tolerances.txt`. The exit code is 0 for a match, 1 for a divergence and 2 for a usage or file error.

## Known limits

- Red alert missions are not supported yet; replay stops with an error.
- If the window loses focus during recording, `Mouse_hidden` is cleared and that is not recorded. This only matters when flying with the mouse.
- Replay must use the same screen size, detail settings and sound settings as the recording. Rendering consumes the same `rand()` stream as gameplay.
