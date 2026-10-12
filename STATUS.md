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

## Next
- Tom retests with the 360 pad.

## Proposed batch 2: face button D-pad overrides (pad tree), waiting for go-ahead
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

## Later batches (not proposed yet)
- Tier 1 controls not in the core layout.
- Tier 2 in the pad tree.
- Other backends (DirectInput / SDL), rumble.
