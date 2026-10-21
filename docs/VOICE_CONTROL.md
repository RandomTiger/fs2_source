# Voice control

Squad orders by voice, offline. Hold the push-to-talk key and say a ship or wing ("Alpha 2", "Beta wing", "all fighters"): the squad menu opens at it. Then say one of the orders the menu shows ("Cover me", "Engage enemy"): the menu sends it, as if its key had been pressed. Recognition runs on the PC with Vosk and a small English model; nothing goes over the network.

The code is in `code/Voice/`:
- `VoiceCore.cpp`: phrase normalisation, the phrase lists (grammars), matching and the two-step state machine. No FreeSpace types.
- `VoiceEngine.cpp`: the recogniser. Vosk, loaded at run time with `LoadLibrary`, on a worker thread. No FreeSpace types.
- `VoiceCapture.cpp`: the microphone, through the game's DirectSoundCapture code (`dscap`), 16 kHz 16-bit mono.
- `VoiceGame.cpp`: settings, push-to-talk, recording and replay, and what each result does in the game.
- `code/Hud/HUDsquadmsg.cpp`: the `hud_squadmsg_voice_*` functions, which open the menu at a target and pick a row.

Single player only. With voice off, no DLL, no model or no microphone, the game plays as it does without this code.

## Setup
Nothing for voice is committed: the DLLs and the model are set up on each machine.

1. **DLLs.** The exe is 32-bit, and Vosk's last 32-bit Windows build is 0.3.42. Download `vosk-win32-0.3.42.zip` from the `v0.3.42` release of `alphacep/vosk-api` on GitHub. It holds `libvosk.dll` and the MinGW runtime DLLs it needs (`libgcc_s_sjlj-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`). Keep the four DLLs together, either next to the exe or in a folder named by `Dll` below.
2. **Model.** Download `vosk-model-small-en-us-0.15` from the Vosk models page (alphacephei.com/vosk/models) and unzip it. The folder that holds `am`, `conf`, `graph` and so on is the model folder. Another language works by using that language's small model, as long as the ship names and orders are said in it.
3. **Settings.** Add a `[Voice]` section to `fs2.ini` in the game folder (`docs/SETTINGS.md`):

```
[Voice]
Enabled=1
Dll=D:\Vosk\vosk-win32-0.3.42
Model=D:\Vosk\vosk-model-small-en-us-0.15
```

| Key | Meaning | Default |
|---|---|---|
| `Enabled` | 1 turns voice on. | 0 (off) |
| `Dll` | The folder holding `libvosk.dll` and its runtime DLLs. Empty: the exe's folder, then the DLL search path. | Empty |
| `Model` | The model folder. | Empty (voice stays off) |
| `PushToTalk` | The push-to-talk key, as a FreeSpace key code (a DirectInput scan code), in decimal. | 58 (0x3A, Caps Lock) |
| `MinConfidence` | Results whose lowest word confidence is below this are ignored. 0 to 1. | 0.6 |

Caps Lock is the default because no game control uses it. The backtick key is not used: it is the debug key, and while it is held the game ignores the ship keys (in a Debug build it also turns primary fire into the debug laser). Whatever key is chosen still does what it is bound to in the controls config, so pick an unbound one.

The model loads in the background when the game starts, so the first few seconds may not hear anything. The log (`mprintf`, Debug builds) says what happened: `Voice: loading model ...`, `Voice: off: <reason>` when the DLL or model could not be loaded, `Voice: cannot open the microphone`, and `Voice: heard '<text>' (<confidence>): result ...` for each utterance.

## Saying an order
Two steps, as the old 2009-2014 SAPI version worked:

1. **Target.** Hold push-to-talk, say a target, let go. The squad menu opens at it.
   - A ship, by the name the game shows: "Alpha 2", "GTC Aquitaine".
   - A wing, by its name or with "wing" after it: "Beta", "Beta wing". When a ship and a wing have the same name, the name alone is the ship and "... wing" is the wing.
   - "all fighters".
   - The phrases list only the friendly ships and wings that are in the mission now and can be messaged. The menu then makes its own checks (departing, dying, not taking orders) and refuses with the fail sound.
2. **Order.** Hold push-to-talk again and say one of the orders the menu shows: its text as written ("Destroy my target", "Form on my wing", "Cover me", ...), or "attack" for "Engage enemy". The menu sends it exactly as if its number key had been pressed, so every check the menu makes still applies. Only the first ten rows (the ones with number keys) can be said.

Numbers are said as words: "Alpha 2" is "alpha two", "Delta 12" is "delta twelve". Two-digit numbers are said as tens and ones ("twenty one"); a number with a leading zero or of three or more digits is said digit by digit ("zero seven", "one zero zero").

- A greyed-out order is heard, refused with the fail sound, and the order step stays open for another order.
- Anything else, or a result below `MinConfidence`, does nothing.
- The order step lasts 8 seconds, the menu's own timeout. After that, or once the menu closes, the next utterance is a target again. A menu opened at a target with the keyboard also takes a spoken order.
- A target refused by the menu (the ship cannot take orders now, comms are down, the player is under EMP) plays the fail sound.

Three ship functions can be said at the target step and act at once:

| Phrase | What it does |
|---|---|
| "single missile" | Turns dual-fire missiles off, if on. |
| "dual missile" | Turns dual-fire missiles on, if off. |
| "cycle missile" | Cycles the secondary weapon. |

They press the same control actions the keys do (`CYCLE_NUM_MISSLES`, `CYCLE_SECONDARY`). The old version also had gun phrases ("all guns", and turning the primary or secondary banks off) through `CHOOSE_*` control actions; this source has no such actions, only cycling, so those phrases are left out.

Recognition uses a small grammar for each step: only the phrases above, built from the mission as it is when push-to-talk is released. The result is matched against the game as it is when recognition finishes, so a ship that left in the meantime is not ordered.

## Recording and replay
Voice works with the gameplay recorder (`docs/REPLAY.md`):
- Microphone, push-to-talk state, the engine and its clock are not recorded. Only a result that does something is recorded, on the frame it is applied, as one `RT_INPUT_DEVICE` entry of four ints: `0x56434531` ("VCE1"), the result kind, the index (ship, wing, menu row or function) and the order. The kinds, orders and functions are numbered in `VoiceCore.h` and are never renumbered.
- A replay never opens the DLL, the model or the microphone. It applies each recorded result at the same point in the frame, so it needs neither the DLL nor a microphone, and a recording made with voice replays on any machine.
- A recording made by a build without voice has no voice entries, so it replays the same with voice on or off.
- At the merge into `Enhanced`, `voice_frame()` goes after `pad_frame()` in `game_process_keys`. The pad writes an `RT_INPUT_DEVICE` entry every frame and voice writes one only on a result frame, so the pad must read its entry first. In the other order voice would read the pad's entry (up to 76 ints, more than voice reads) and the replay would stop with a desync.
- A recording made on this branch with voice orders has no pad entries, so on `Enhanced` the pad would read the voice entries. Make recordings with voice orders on `Enhanced`, after the merge.

## Other builds
- **Multiplayer:** voice is off.
- **No DLL, no model, model fails to load:** voice turns itself off and logs why. Nothing else changes.
- **No microphone:** push-to-talk does nothing; logged once.
- **64-bit (`PrePort64` and later):** use the win64 build instead, `vosk-win64-0.3.45.zip` from the `v0.3.45` release. The DLL has the same name, `libvosk.dll`; the code loads it the same way. The code is x64-clean (the dispatch test builds and runs for both).

## Licences
Vosk (`vosk-api`) and the `vosk-model-small-en-us-0.15` model are released under the Apache 2.0 licence. The MinGW runtime DLLs in the same zip come under their own licences. None of these files is committed; check the licences before shipping them with the game.

## Dispatch test
`tools/VoiceDispatchTest` checks everything voice does short of hearing, with no game, no microphone, no DLL and no model. It links `VoiceCore.cpp` and `VoiceEngine.cpp` and feeds recognised text against stubbed targets and menu rows:
- normalisation, including "(*)" and the number rules;
- every target, "name wing", "all fighters", function and order phrase (row text and synonym) resolving to the right result;
- a greyed-out row refused, an unmatched phrase and a low confidence ignored;
- the step changes and the 8-second order step, including across the clock wrapping;
- the result JSON (text and the lowest word confidence);
- the engine failing cleanly with no model folder, a missing model folder, or no DLL.

```
MSBuild tools\VoiceDispatchTest\VoiceDispatchTest.vcxproj /p:Configuration=Release /p:Platform=Win32
tools\bin\VoiceDispatchTest.exe
```
It prints each failure, then `PASS` or `FAIL` with the count, and exits with 0 on `PASS`. `/p:Platform=x64` builds `tools\bin\x64\VoiceDispatchTest.exe`.

`VoiceDispatchTest.exe <dll_dir>` also loads the real `libvosk.dll` from `dll_dir` (same bitness as the test exe) against an empty model folder: the DLL and all its functions must load, and the model failure must be reported cleanly. Vosk prints its own error line for the empty model; that is expected.
