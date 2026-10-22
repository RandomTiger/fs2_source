# Control pad support (prototype)

Branch `PadControl`, from `VS2022` at 94ea4fa. Exes: `FS2pad_d.exe` / `FS2pad_r.exe`.

Parent: `VoiceControl` (Tom, 2026-10-22). The pad needs voice for squad orders, so PadControl takes
updates only from `VoiceControl`, merged down, and merges into `Enhanced` itself once the voice
commits it carries are already there (D-20261021-154901). The squad menu currently has two sets of
entry points, pad's `hud_squadmsg_pad_*` and voice's `hud_squadmsg_voice_*`; moving pad onto
voice's is issue #89.

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
6. `joy_init()` (Joy.cpp): legacy joystick off while an XInput pad is connected.
7. HUD render (HUD.cpp): `pad_hud_render()` draws the open pad tree options.
8. Squad menu (HUDsquadmsg.cpp): a block of `hud_squadmsg_pad_*` functions (open, close,
   move, select, back) and a ">" on the highlighted row.

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
| Left shoulder | hold | lower throttle (same rate as the left trigger) |
| Left shoulder | double tap | zero throttle (hard stop) |
| Right shoulder | hold | fire secondary |
| Special button | press | pause |

Match target speed has no slot yet (was LB double tap); it goes in the pad tree.

Gesture overlap: the first press of a double tap also does its single action (LB slows,
then the second tap stops; LT raises throttle briefly, then afterburner). Waiting for
the double-tap window instead would add ~250 ms lag to every stop. Proposed: no delay.

## Pad tree (from Tom)
Players won't remember dozens of combinations, so the less-used actions live in a
tree driven by a held face button and the D-pad.

- Hold a face button: its category opens. The D-pad's four directions show four options.
- An option is a leaf (does the action) or a branch (shows four more options).
- On every level below the top, one direction (Left) is reserved for back. The top
  level has all four. Releasing the face button closes the tree at any depth.
- With no face button held, the D-pad is shields as normal.
- Sticks and triggers keep working while the tree is open, so the player keeps flying.
- Muscle memory: the action fires on the D-pad press, and the HUD only draws the
  options after a short delay (~200 ms). A practised player clicks through without
  waiting and never sees it pop up.
- Tap vs hold: a quick tap of the same face button can still do one direct action
  (e.g. tap = target next hostile, hold = targeting tree).

Categories:
| Category | Notes |
|---|---|
| Commands | Drives the existing squad message menu. That menu has more than four items per page, so here Up/Down move through it, Right selects, Left goes back. It reuses the game's own menu and HUD. |
| Targeting | Hostile, subsystem, cargo, turret, friendly branches. |
| Weapons | Cycle primary, cycle secondary, missile link, etc. |
| (4th face button) | Free: a direct action or a fourth category (ETS / power). |

Which face button opens which category is just a table entry in `pad.cfg`. Default
(arbitrary): West = Targeting, North = Weapons, East = Commands, South = free.

The tree is data (`PadCore`), so it ports to Godot as is. Only the HUD drawing
(`PadHud.cpp`, new file) is FS2-specific.

## What fits and what doesn't
Tier 1, needed to play: on the base layer or a one-button layer.
- Flight: yaw, pitch, roll, throttle up, stop, afterburner, match speed.
- Weapons: fire primary, fire secondary, cycle primary, cycle secondary, countermeasure.
- Targeting: next hostile, target in reticle, closest attacking me, next subsystem.
- Shields: four quadrants.
- Squad message menu, with pad navigation of the menu while it is open (a layer).
- Pause. Jump out at mission end (`END_MISSION`).

Tier 2, useful: in the pad tree.
- ETS up/down for weapons/shields/engines, equalise.
- Target next/prev friendly, escort, next bomb, newest ship, target's target, stop targeting.
- Subsystem targeting: prev subsystem, subsystem in reticle, stop targeting subsystem.
- Cargo: next/prev uninspected cargo. Turrets: next/prev live turret.
- Throttle down (no slot yet), missile link (`CYCLE_NUM_MISSLES`), chase view, padlock
  look (right stick Y is free), show goals, radar range.

Tier 3, keyboard only.
- Multiplayer messaging and net keys, time compression, HUD contrast, external camera,
  view slew, navmap, damage popup, escort add/clear, self-destruct, debug keys.
- Front-end menus. The pad works in a mission only.

## Settings
Prototype: an optional text file `pad.cfg` in the game folder, read the first time the
pad is used. Not the pilot file, so the pilot format does not change. One
`name = value` per line, `#` for comments. Names and defaults:

```
invert_pitch = 0         # 0: pull back = nose up
invert_yaw = 0
invert_roll = 0
left_sensitivity = 1.0
right_sensitivity = 1.0
left_deadzone = 0.15     # radial, 0..1
right_deadzone = 0.15
curve = 2.0              # 1 = linear, higher = finer near centre
trigger_deadzone = 0.05
trigger_threshold = 0.3  # trigger counts as a button above this
double_tap_ms = 250
throttle_rate = 100      # percent per second at full left trigger
```
The binding table is in code (`PadGame.cpp`) for now. An in-game options screen is out
of scope for the prototype.

## Known clash
The legacy joystick code also sees the 360 pad, and picks it even with the joystick
option set to none; its hat is bound to the padlock views. So `joy_init()` turns the
legacy joystick off when an XInput pad is connected at startup. Side effect: a real
flight stick plugged in alongside the pad is ignored too.
