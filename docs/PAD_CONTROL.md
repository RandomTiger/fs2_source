# Control pad support (prototype)

Branch `PadControl`, from `VS2022` at 94ea4fa. Exes: `FS2_PadControl_d.exe` / `FS2_PadControl_r.exe`.

Parent: `VoiceControl` (Tom, 2026-10-22). The pad needs voice for squad orders, so PadControl takes
updates only from `VoiceControl`, merged down, and merges into `Enhanced` itself once the voice
commits it carries are already there (D-20261021-154901). The squad menu's pad block
(`hud_squadmsg_pad_*`: highlighted row, move, back) and voice block (`hud_squadmsg_voice_*`:
open at a target, press a row by its order) open the menu and press a row through the same two
helpers, `hud_squadmsg_menu_open()` and `hud_squadmsg_menu_press_row()` (issue #89). Row counting
stays separate: the pad counts the visible page in every menu mode, voice the number-key rows of a
command menu.

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
- Gestures: press, release, hold, double tap, double tap and hold, hold past a tap, long hold.
- Layers: the "interaction mode state". A held modifier (North) switches which table rows are
  live. A press belongs to the layer it went down in: RT pressed with North does not start
  firing when North comes up, it needs a new press. RT held before North stops firing under
  North and carries on after.
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
   `hud_show_ets()` (HUDets.cpp): `pad_ets_recolor()` draws the system North + D-pad changes.
   HUDtarget.cpp: `hud_pad_*` tell South what a press would pick without changing it.
8. Squad menu (HUDsquadmsg.cpp): a block of `hud_squadmsg_pad_*` functions (open, close,
   move, select, back) and a ">" on the highlighted row.
9. `game_poll()` (FreeSpace.cpp): `pad_menu_poll()` first thing, the menu layer.
10. `UI_WINDOW::process` / `destroy` (WINDOW.cpp), `snazzy_menu_do()` (SnazzyUI.cpp): tell the layer what is under the cursor.

Read every frame. The original joystick code reads only 10 times a second ("like we did
in Descent"); that is too coarse for a thumbstick.

## Flight layout (from Tom, #151)
| Input | Gesture | Action |
|---|---|---|
| Left stick X / Y | analog | yaw / pitch |
| Right stick X | analog | roll |
| Right trigger | hold | fire primary |
| Right shoulder | hold | fire secondary |
| D-pad up/down/left/right | press | shield to front/rear/left/right (`SHIELD_XFER_*`) |
| Left trigger | analog hold | raise throttle, rate scaled by pressure |
| Left trigger | double tap and hold | afterburner |
| Left shoulder | double tap | zero throttle (hard stop); the second tap must be short too |
| Left shoulder | hold | speed mode: right stick stops rolling, its Y raises (up) or lowers (down) speed, analog |
| Right stick click | press | countermeasure |
| Left shoulder + right stick click | press | auto match target speed on/off |
| South (360: A) | press | target the ship in the reticle; if it is the target, the subsystem in the reticle; if that is selected, no subsystem |
| West (360: X) | hold | voice push-to-talk, as the `PushToTalk` key (`docs/VOICE_CONTROL.md`); works while the squad menu is up |
| Special button | press | pause |
| East (360: B) | | nothing in flight |

Speed mode starts once LB has been down longer than `double_tap_ms`, so a tap or a double tap
does not stop the roll. Stick centred leaves speed matching on; moving it ends matching, as the
left trigger does.

South always means the top object under the reticle, the one a fresh press of the game's control
takes. The game's cycling through what is under the reticle on repeat presses does not apply.

With North (360: Y) held, axes and the left trigger carry on, and buttons do this or nothing:

| With North | Gesture | Action |
|---|---|---|
| Right shoulder | press | one step of: bank 1 single, bank 1 double, bank 2 single, bank 2 double. One bank: single, double |
| Right trigger | press | next primary (`CYCLE_NEXT_PRIMARY`) |
| Left shoulder | hold 1 second | jump out (`END_MISSION`, as Alt-J); an early release cancels, once per hold |
| D-pad left / right | press | energy system to change: shields (not on a ship without them), weapons, engines |
| D-pad up / down | press | raise / lower it (`INCREASE_*`, `DECREASE_*`) |

While North is held the selected system's part of the ETS gauge is white. The selection starts on
shields each mission.

Not on the pad since #151, keyboard only: the squad menu, next or previous hostile, attacking me,
next or previous subsystem, cargo, friendly, turret, and the one-shot match speed.

## On the menus (from Tom, #147)

| Pad | Menus |
|---|---|
| Left stick | Adds to the mouse cursor; a real mouse works alongside it |
| South (360: A) | Left click. With no gadget or region under the cursor, also Enter |
| East (360: B) | Esc |
| LB, RB, D-pad left, right | Left and right arrow |
| D-pad up, down | Up and down arrow |

It covers every state but flight and the two death views, so also pause and the in-mission screens,
and popups whatever is under them, the death popup too. All of it goes through the game's own UI, in
`code/Pad/PadMenu.*` (no game types) and `PadMenuGame.*` (glue): `key_mark` and `mouse_mark_button`,
so a screen does what its arrow keys and hotkeys do, and a screen with none does nothing. One press,
one key: no repeat while held.

A button only counts if it goes down on a menu. It then does not do its flight action until it has
been released once, so South that closes a popup does not target (D-20261104-065940). A button
held from flight into a menu keeps its flight action and does nothing on the menu. Sticks and
triggers are not held back like this.

A recording keeps the events like a person's. A replay runs none of this and gets them from the
stream. The test is `-at_padmenu` with mission AutotestPadMenu (`PadMenuTest.cpp`): a script feeds a
fake pad below the layer, and is run when the recording is made, never in a replay.

## Pad tree (from Tom), no longer bound
No button opens the tree since #151. The code (`PadCore`, `PadHud.cpp`) stays and old recordings
still draw it: whether to delete it is undecided (#151).

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
| (4th face button) | South: voice push-to-talk, so no fourth category for now (ETS / power was the idea). |

Which face button opens which category is just a table entry in `pad.cfg`. Default
(arbitrary): West = Targeting, North = Weapons, East = Commands. South is voice push-to-talk.

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
- A commit from anywhere but its button: South never sends Ctrl+Enter, so the cursor has to be on Commit.
- Screens with no arrow keys or hotkeys: the button does nothing there, the stick and South still work.

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
menu_cursor_speed = 1.0  # on the menus, screen widths per second at full left stick
```
The binding table is in code (`PadGame.cpp`) for now. An in-game options screen is out
of scope for the prototype.

## Known clash
The legacy joystick code also sees the 360 pad, and picks it even with the joystick
option set to none; its hat is bound to the padlock views. So `joy_init()` turns the
legacy joystick off when an XInput pad is connected at startup. Side effect: a real
flight stick plugged in alongside the pad is ignored too.
