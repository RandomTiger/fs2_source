# PadControl status

Design: `docs/PAD_CONTROL.md`.

## Batch 1: core layout (built, not yet tested in game)
1. `code/Pad/PadXInput.*`: XInput backend (loaded at run time), first connected pad,
   normalised state in `PadState.h`.
2. `code/Pad/PadCore.*`: deadzone, curve, sensitivity, invert; gestures (press, release,
   hold, double tap, double tap and hold); layer field (only the base layer used).
3. `code/Pad/PadGame.*`: the core layout table; feeds `button_info`, `check_control`
   and the flight values; Start pauses and unpauses.
4. Hooks (marked `// Claude:`): code.vcxproj/.filters, `game_process_keys`,
   `check_control`, `read_keyboard_controls`, `pause_do`. No init hook: the pad
   initialises on first use.
5. Optional `pad.cfg` in the game folder; built-in defaults otherwise.
6. Debug and Release build clean: `FS2pad_d.exe`, `FS2pad_r.exe`.

Defaults taken on open questions: pause = Start; double taps act on the first tap
with no delay.

First test fixes (built): D-pad was driving padlock views because the legacy joystick
code also read the pad; it is now off while an XInput pad is connected (hook in
`joy_init`). LB hold = slow, LB double tap = hard stop, RB hold = fire secondary.
Match speed is unassigned until the pad tree.

## Batch 2: face button D-pad overrides (pad tree) (built, not yet tested in game)
1. Core: hold a face button = its category; the D-pad picks; Left = back below the top
   level; releasing the face button closes. The action fires on the D-pad press. With
   no face button held, the D-pad stays shields.
2. HUD (`PadHud.cpp`): the four options drawn after 200 ms held, so quick clicks never
   show it.
3. West = Targeting:
   Up = target in reticle; Right = Hostile > (Up next, Down prev, Right closest attacking me);
   Down = Subsystem > (Up next, Down prev, Right in reticle);
   Left = Other > (Up next cargo, Down next turret, Right next friendly).
4. North = Weapons: Up cycle primary, Down cycle secondary, Right missile link,
   Left countermeasure.
5. East = Commands: opens the squad message menu; Up/Down move a highlight, Right
   sends, Left back. Needs a small marked hook in the squad menu to draw the highlight.
6. South = Flight: Up match speed; the rest empty for now.
7. Tap (press and release under 200 ms, no D-pad used) does nothing yet.

First test fixes (built): menu drawn in the top quarter of the screen; Back at the
Commands root no longer closes the squad menu; countermeasure on right stick down
(removed from Weapons); South press = target in reticle; match speed moved to
Targeting Up (Flight category removed). Then South press = nearest hostile (again: next
closest), in place of target in reticle.

Code: tree in `PadCore` (data tables in `PadGame.cpp`), drawing in `PadHud.cpp`, hook in
`HUD.cpp` after the supernova gauge. The squad menu driver is a marked block in
`HUDsquadmsg.cpp` (it needs that file's statics) plus a ">" highlight in its menu drawing.

## Batch 3: record and replay pad input (written, waiting on RecordPlayback)
1. Recorder (branch `RecorderInputDevice`, off `RecordPlayback`, commit e6cdc32):
   `RT_INPUT_DEVICE` entries and `replay_input_device()`. A missing entry reads as "not
   connected", so recordings replay across builds with and without the pad. Format version
   unchanged. No pad code there.
2. Pad (this branch, uncommitted): `pad_game_update` records the pad's output (actions,
   flight values, tree state) once per update, as fixed point, and plays it back. Replays
   do not depend on pad.cfg or a pad being plugged in.
3. Needs (1) in `RecordPlayback`, then merged into this branch, before (2) builds.
4. Test: record a mission flown with the pad, replay it; replay an existing recording.

## Next
- Tom tests batch 2 with the 360 pad.
- Get `RecorderInputDevice` into `RecordPlayback`.

## Proposed batch 4: energy (ETS) mode, from Tom, waiting for go-ahead
1. Hold Back (left special button): energy mode. A radial display of the three energy
   bars (engines, weapons, shields).
2. Right stick adjusts them; right stick click resets to defaults (`ETS_EQUALIZE`).
3. While Back is held, the right stick does nothing else (no roll, no countermeasure).
4. Proposed default for the radial: the three bars 120 degrees apart (Weapons top left,
   Shields top right, Engines bottom). Push the stick towards a bar to raise it one step
   (`INCREASE_*`, the game takes the energy from the others); back to centre before the
   next step. Holding to repeat is the alternative.

## Later batches (not proposed yet)
- Tier 1 controls not in the core layout.
- Tier 2 in the pad tree.
- Other backends (DirectInput / SDL), rumble.
