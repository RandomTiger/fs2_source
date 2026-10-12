# Control pad support (prototype)

Branch `PadControl`, from `VS2022` at 94ea4fa. Exes: `FS2pad_d.exe` / `FS2pad_r.exe`.

## Goals
- Fly a mission from a pad. The pad does not have to cover every control; the keyboard
  stays live for the rest.
- As much code as possible in new files. Original files only get small, marked hooks.
- Built to port to Godot: the mapping logic knows nothing about Windows or FS2.
- Test pad: wired Xbox 360. Target: any XInput pad first, others later.

## Architecture
Three layers, each in its own files under `code/Pad/`:

| Layer | Files | Knows about | Godot port |
|---|---|---|---|
| Backend | `PadXInput.cpp/.h` | XInput only | Replaced by Godot `Input` |
| Core | `PadCore.cpp/.h` | A normalised pad state + time | Ported as is (plain C++, no FS2 types) |
| Glue | `PadGame.cpp/.h` | FS2 actions, `control_info`, `button_info` | Rewritten against the port's game code |

**Backend** fills a normalised state every frame: sticks -1..1, triggers 0..1, and
buttons named by position (South/East/West/North, LB/RB, LS/RS, Start/Back, D-pad), the
same layout SDL and Godot use. So a PlayStation or Switch pad maps to the same names
later, with no change to the core.

XInput first: built into Windows (`xinput9_1_0.dll`, no new dependency), covers 360/One/
Series pads and most third-party pads in XInput mode. A DirectInput or SDL backend for
other pads can come later behind the same interface.

**Core** turns the state into actions through a data table:
`(input, gesture, layer) -> action`.
- Gestures: press, release, hold, double tap, double tap and hold.
- Layers: the "interaction mode state". A held modifier (e.g. RB) or a game state (e.g.
  the squad message menu is open) switches which table rows are live.
- Analog: radial deadzone, response curve, sensitivity, invert, per stick or axis.
- Output: analog values (yaw, pitch, roll, throttle rate) plus actions this frame,
  each either held (continuous) or fired once (trigger).

**Glue** feeds the output into the game the same way keys and joystick do, so the rest
of FS2 (and the recorder, later) sees no difference:
- one-shot actions go through `button_info_set(&Player->bi, id)`;
- held actions (fire, afterburner) answer `check_control(id)`;
- analog adds into `ci->heading / pitch / bank` and `forward_cruise_percent`, the same
  path the joystick relative-throttle axis uses.

## Hooks into original code (all marked `// Claude:`)
1. `code.vcxproj` / `.filters`: add the new files; link `xinput9_1_0.lib`.
2. Game start/shutdown: `pad_init()` / `pad_close()`.
3. `game_process_keys()` (KeyControl.cpp): `pad_frame()` after `button_info_clear`.
4. `check_control()` (ControlsConfig.cpp): return 1 if the pad holds that action.
5. `read_keyboard_controls()` (PlayerControl.cpp): add the pad's analog values.

Read every frame. The original joystick code reads only 10 times a second ("like we did
in Descent"); that is too coarse for a thumbstick.

## Core layout (from Tom)
| Input | Gesture | Action |
|---|---|---|
| Left stick X / Y | analog | yaw / pitch |
| Right stick X | analog | roll |
| Right trigger | hold | fire primary |
| D-pad up/down/left/right | press | shield to front/rear/left/right (`SHIELD_XFER_*`) |
| Left trigger | analog hold | raise throttle, rate scaled by pressure |
| Left trigger | double tap and hold | afterburner |
| Left shoulder | press | zero throttle |
| Left shoulder | double tap | match target speed |
| Special button | press | pause |

Gesture overlap: the first press of a double tap also does its single action (LB stops,
then match speed takes over; LT raises throttle briefly, then afterburner). Waiting for
the double-tap window instead would add ~250 ms lag to every stop. Proposed: no delay.

## What fits and what doesn't
Tier 1, needed to play: on the base layer or a one-button layer.
- Flight: yaw, pitch, roll, throttle up, stop, afterburner, match speed.
- Weapons: fire primary, fire secondary, cycle primary, cycle secondary, countermeasure.
- Targeting: next hostile, target in reticle, closest attacking me, next subsystem.
- Shields: four quadrants.
- Squad message menu, with pad navigation of the menu while it is open (a layer).
- Pause. Jump out at mission end (`END_MISSION`).

Tier 2, useful: behind a modifier layer.
- ETS up/down for weapons/shields/engines, equalise.
- Target next/prev friendly, escort, next bomb, newest ship, target's target, stop targeting.
- Throttle down (no slot yet), missile link (`CYCLE_NUM_MISSLES`), chase view, padlock
  look (right stick Y is free), show goals, radar range.

Tier 3, keyboard only.
- Multiplayer messaging and net keys, time compression, HUD contrast, external camera,
  view slew, navmap, damage popup, escort add/clear, cargo and turret targeting,
  self-destruct, debug keys.
- Front-end menus. The pad works in a mission only.

## Settings
Prototype: a text file `pad.cfg` in the game folder, read at start-up. Not the pilot
file, so the pilot format does not change. Keys:
invert pitch, invert yaw, invert roll; sensitivity and response curve per stick;
deadzone per stick; trigger threshold; double-tap window (ms); the action table itself.
An in-game options screen is out of scope for the prototype.

## Known clash
DirectInput also sees the 360 pad as a joystick (both triggers on one Z axis). If it is
selected as FS2's joystick, its buttons and axes double up with the pad module. The pad
module should skip legacy joystick reading for the pad it owns, or the joystick option
is set to none for testing.
