# Autotest

The game can play missions with nobody at the controls. This has two uses:
- **Build machine.** Load every campaign mission, play it for a while, and report crashes, errors, asserts and hangs through a log and the exit code.
- **Recording tests.** Make a gameplay recording (`docs/REPLAY.md`) without a person, so recordings can be made and replayed as often as needed.

The code is `code/Replay/Autotest.cpp`. With no `-autotest` flag, and no autotest recording being replayed, it does nothing.

## Usage
Run with the working directory set to the game directory.
```
FS2vs2022d.exe -window -autotest [-at_mission FILE] [-at_time SECONDS] [-at_speed N] [-at_pilot CALLSIGN]
```
| Flag | Meaning | Default |
|---|---|---|
| `-at_mission` | The one mission to play, for example `sm1-03` or `sm1-03.fs2`. | Every single-player campaign mission, in campaign order |
| `-at_time` | How long to play each mission, in seconds of **mission time**. | 120 |
| `-at_speed` | Time compression, 1 to 8. | 4 |
| `-at_pilot` | The pilot to use. | The most recently saved single-player pilot |

The `-at_` flags do nothing without `-autotest`. An invalid value stops the run with exit code 2.

## What happens
1. The pilot is loaded with no pilot screen.
2. Each mission starts the way the tech room simulator starts it: command briefing, briefing, then commit with the default loadout.
3. The mission plays at the chosen time compression. The player ship is invulnerable and nobody flies it.
4. When the time limit is reached, the mission ends the way it does when the player quits. A mission that ends itself goes through the debriefing and back to the main hall.
5. After the last mission the game quits.

## Test missions
Missions written for autotest are in `data/missions` (`Autotest*.fs2`); copy them to the game's `data\missions` folder to run them.
- A mission whose `$Notes:` contain `autotest: mission controls vulnerability` sets the player's vulnerability itself, through the `invulnerable` flag and the `ship-vulnerable` and `ship-invulnerable` sexps. Autotest then stops forcing the player invulnerable. A mission can use this to let the player die and so end itself, for example in a supernova.
- When the player dies, autotest answers the death popup with "quit mission", as the time limit does.
- The log lists each mission event and goal as it becomes true, false or complete, with the mission time, so a test mission can be checked to have run to its end.

While an autotest runs:
- Every popup is answered with its first choice and not shown. The one exception is the training popup for an action with no control bound, whose first choice opens the control config screen: the autotest aborts the mission instead.
- The game keeps running when its window is not in front.
- `Assert`, `Warning` and `Int3` are logged and the game carries on. Each place is logged three times, then only counted; the counts are listed at the end of the log. `Error` is logged and the game exits with code 1. In Release builds `Assert`, `Warning` and `Int3` are compiled out.
- A crash is logged and the game exits with the exception code (for example `0xC0000005`), with no Windows crash dialog.
- If the command briefing, briefing or red alert screen has not moved on after 30 seconds, the mission is logged as failed and the run goes on to the next mission from the main hall.
- A watchdog thread stops the game with exit code 5 if no frame runs for 120 seconds (start-up included), if a menu screen lasts 120 seconds, or if a mission runs four times longer in real time than its time limit, plus 120 seconds.

## Results
`autotest.log` in the game directory, written as the run goes. It ends with `PASS` or `FAIL`.

| Exit code | Meaning |
|---|---|
| 0 | Pass |
| 1 | `Error()` |
| 2 | A mission did not start, an `Assert` or `Int3` was hit, or a flag was invalid |
| 5 | The watchdog found the game stuck |
| `0xC...` | A crash, with its exception code |
| 3, 4 | A replay desync or setup error (`docs/REPLAY.md`) |

## Recordings
```
FS2vs2022d.exe -window -autotest -at_mission sm1-03 -record recordings\NAME.fs2rec -trace recordings\NAME.trace
FS2vs2022d.exe -window -replay recordings\NAME.fs2rec -trace replays\NAME.trace
tools\bin\TraceDiff.exe recordings\NAME.trace replays\NAME.trace
```
- With `-record`, the game plays one mission and quits. Without `-at_mission`, that is the first campaign mission.
- The recording stores the autotest settings. A replay plays the mission under the same rules (time compression, invulnerability, popups, the time limit) and ends on the same frame, whatever its own command line says.
- The time limit is in mission time, which a replay reproduces exactly. A real-time limit would end the replay on a different frame.
- `-autotest` on a replay adds only the log, the watchdog and the no-dialog handling. The replay still drives the menus itself.
- If an `Assert` or `Int3` fired while recording, replay with `-autotest` too, or the replay stops at the same `Assert` dialog.
- Red alert missions can be run, but replay does not support them yet, so the log warns when one is recorded.

## Not covered
- Nobody flies the player ship, so player weapons, collisions and the player's own damage are not tested. AI ships fight as normal.
- Missions start from the simulator, not the campaign, so campaign state (red alert carry-over, campaign variables) is not tested.
- Popups are always answered with their first choice. The run does not try the other choices.
