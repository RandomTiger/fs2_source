# VoiceControl status

Design: `docs/VOICE_CONTROL.md`. Plan and rulings: steward decisions D-20261021-105201 and D-20261021-105202. Branch `VoiceControl`, off `RecordPlayback` (be8fd99); takes nothing from `PadControl`. Not merged into `Enhanced`.

## Batch 1: squad orders by voice (built, not yet run in game)
1. `code/Voice/VoiceCore.*`: normalisation, per-step phrase lists, matching, two-step state machine.
2. `code/Voice/VoiceEngine.*`: Vosk 0.3.42 win32 loaded at run time, model and recognition on a worker thread.
3. `code/Voice/VoiceCapture.*`: microphone through `dscap`, 16 kHz mono, while push-to-talk is held.
4. `code/Voice/VoiceGame.*`: `[Voice]` settings, push-to-talk (default Caps Lock), recording and replay of results, applying them.
5. Hooks (marked `// Claude:`): `hud_squadmsg_voice_*` in `HUDsquadmsg.cpp`, `voice_frame()` in `game_process_keys` before `button_info_do`, `voice_game_init()`/`voice_game_close()` in `FreeSpace.cpp`, `code.vcxproj` and `.filters`.
6. `tools/VoiceDispatchTest`: the no-microphone dispatch check. PASS, 144 checks, Win32 and x64, Release and Debug; with the real win32 `libvosk.dll` 148 checks, PASS.
7. Release and Debug `Freespace2` build with exit 0. The only warnings in the new code are C4996 (`strncpy` and similar: 5 in Release, 17 in Debug), which this base has thousands of and `Enhanced` turns off with its CRT defines. No other new warnings.

Tom's rule for this batch (2026-10-21): no game window, so nothing below that needs the game exe has been run.

## Deferred: needs the game running
- [ ] Quick suite, Debug and Release, no `libvosk.dll` anywhere (voice off): identical to `RecordPlayback`.
- [ ] Quick suite, Debug and Release, with the DLLs and model present and `[Voice] Enabled=1`: identical (a replay never opens the engine; live play with voice idle records nothing).
- [ ] One custom-mission recording with voice orders (ship, wing, "all fighters", a refused greyed-out order, a missile function), replayed with no microphone and no DLL: `REPLAY OK`, TraceDiff 0. Make it on `Enhanced` after the merge, not here: a recording from this branch has no pad entries, so the pad would read its voice entries (`docs/VOICE_CONTROL.md`, Recording and replay).
- [ ] Live check by ear: model load time, recognition of ship names with numbers, the 8-second order step, the fail sound on refusals, Caps Lock as push-to-talk.
- [ ] Full suite, Debug and Release, identical to `Enhanced`.
- [ ] `e_gate` plus the demo step at the merge.
- [ ] Steam Deck check.
- [ ] Steward brief before the merge into `Enhanced`: DLL and model location (machine-profile setting, never committed), Vosk and model licences, x64 DLL (0.3.45 win64, same name) for `PrePort64`, replay and no-DLL paths, the dispatch test.
- [ ] `tools\warning_check.ps1` at `Enhanced` level (the zero-warning rule starts there): not run on this branch.

## At the merge into `Enhanced`
- `KeyControl.cpp`: `voice_frame()` goes after `pad_frame()`, so the pad reads its `RT_INPUT_DEVICE` entry first.
- `HUDsquadmsg.cpp`, `code.vcxproj`, `.filters` and this file will conflict with `PadControl`'s additions in the same places; keep both.

## Follow-ups
- `swap.ps1` (fs2-source-editing skill) drops the UTF-8 BOM from `.vcxproj` files; it was put back by hand here.
