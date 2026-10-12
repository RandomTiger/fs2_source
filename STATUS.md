# PadControl status

Design: `docs/PAD_CONTROL.md`.

## Proposed batch 1: core layout working (waiting for Tom's go-ahead)
1. `code/Pad/PadXInput.*`: XInput backend, first connected pad, normalised state.
2. `code/Pad/PadCore.*`: deadzone, curve, sensitivity, invert; gestures (press, release,
   hold, double tap, double tap and hold); layer support (no layers used yet).
3. `code/Pad/PadGame.*`: the core layout table from the design doc; feeds
   `button_info`, `check_control` and the analog flight values.
4. Hooks (marked `// Claude:`): vcxproj, init/close, `game_process_keys`,
   `check_control`, `read_keyboard_controls`.
5. `pad.cfg` with invert pitch, sensitivity, deadzone, double-tap window. Defaults
   built in if the file is missing.
6. Build Debug and Release. No game launch without asking.

## Later batches (not proposed yet)
- Tier 1 controls not in the core layout (fire secondary, targeting, squad menu layer).
- Tier 2 behind a modifier layer.
- Other backends (DirectInput / SDL), rumble.
