# FS1 compatibility: what it takes for this engine to play FreeSpace 1 data

Investigation only. No code has changed. Branch `FS1Compat`, off `origin/RecordPlayback` at `d0c412f`.

Decisions this works under:
- **D-064**: FS1 is a secondary goal behind the Godot port. Code that reads older formats is not dead code and is not cut.
- **D-20261017-225737**: GR_640 stays as an interface art mode, separate from render resolution. Table converters and mission file-name handling go in data loading, not game logic. FS1 fidelity is best effort; an FS1 recording suite would be regression-only.
- **D-20261017-223742**: FS1 is a feature branch off RecordPlayback, merged into Enhanced when green.
- **D-20261018-134401**: the 37 FS1 demo code blocks are held under D-064, not cut.

Data surveyed: GOG *Conflict: FreeSpace - The Great War* 1.06, with Silent Threat, at `PATH_FS1` (`U:\GOG Games\Freespace`). Nothing was copied into the repo. The data was read with throwaway scripts in a scratch folder.

---

## 1. What FS1's data is

### Archives
FS1 uses the same VP archive format as FS2: signature `VPVP`, version 2, the same directory layout.

| Archive | Where | Contents |
|---|---|---|
| `root.vp` | install root | all 17 tables, the main campaign (55 missions, 3 campaign files), 3 models |
| `mdisk.vp` | install root | Silent Threat: 60 missions, 5 campaign files, 5 replacement tables, 6 models, music, voice |
| `freespace.vp` | `Data\` | models, textures, interface art, HUD art, effects, sounds, music, fonts, persona and training voice |
| `voc.vp` | `Data\` | briefing and debriefing voice |
| `cbanim.vp` | `Data\` | command briefing animations |
| `data1\`, `data2\` | install | copies of the two CDs: the same VPs again, plus the movies (`.mve`) |

The movies exist only in `data1\` and `data2\`, not under `Data\Movies`.

### Tables
- FS1 has 17 tables. FS2 has 30. FS1 has one FS2 lacks (`tooltips.tbl`, which the FS2 code still reads).
- **14 of the 17 are encrypted** with the old scheme: signature `0xDEADBEEF`, characters packed into 7 bits, then XOR with the byte offset. `credits.tbl`, `cutscenes.tbl`, `menu.tbl` and `Species.tbl` are plain text. FS2's tables use the newer `0x5C331A55` scheme.
- Once decrypted, most tables are the same format as FS2's, with a few fields renamed or missing. The differences are listed in section 3.
- Silent Threat's `mdisk.vp` replaces `ships`, `weapons`, `sounds`, `music` and `credits`. They are supersets of the main game's tables (71 ship classes against 67).

### Missions and campaigns
- Missions are `.fsm`, campaigns `.fsc`. FS2 uses `.fs2` and `.fc2`.
- Every FS1 mission says `$Version: 0.10`.
- The 55 main campaign missions are plain text. 59 of the 60 Silent Threat missions and all 5 of its campaign files are encrypted with the same old scheme as the tables.
- The campaign files name missions with their extension (`$Mission: SM1-01.fsm`, `next-mission "BTM-02.FSM"`), in mixed case.
- Every SEXP operator the FS1 missions use exists in FS2's operator table (112 different operators used).

### Models
- `.pof`, POF versions 1901 to 2014. FS2 accepts 1900 to 21xx, and FS2's retail models are 2117.
- They use the **FS1 header chunks**: `OHDR` (object header) and `SOBJ` (sub-object), where FS2 uses `HDR2` and `OBJ2`. The fields inside are in a different order.
- They have none of the chunks FS2 added (`INSG` insignia, `ACEN` autocentre, `GRID`).
- FS1 also ships 101 `.p3d` files beside the models. Nothing in this source reads `.p3d`, so they look like editor or source files.

### Textures and art
- Textures and interface art are 8-bit paletted PCX (one plane), the same as FS2's 640 art. Each file carries its own palette. The `*Palette.pcx` files and the `.clr` palette cache are from FS1's 8-bit display mode.
- Animations are `.ani` version 2, the same as FS2.
- Fonts: `font01.vf` and `font02.vf`, the same `VFNT` format as FS2. FS1 has **no `font03.vf`**.
- Old-style nebulas: `nebula01-03.neb`.

### Art resolution
**FS1 has no 1024 art.** Full-screen interface art is 640x480 (121 files at exactly 640x480). FS2 marks its 1024 art with a `2_` prefix (1384 files). FS1 has 120 `2_` files, but all of them are persona voice `.wav` files, not art. This confirms Tom's recollection in D-20261017-225737.

---

## 2. What the FS2 engine already reads, and where it fails (static reading)

This section is from reading the code. The trial run that followed is in "Trial run" at the end of this section.

### Already works, or very close
- **VP archives**: same format. The engine looks for `*.vp` in the root and in every known data folder, so it finds `Data\freespace.vp`, `voc.vp` and `cbanim.vp` (`code/CFile/CfileSystem.cpp:264`).
- **Old POF versions**: the reader still has branches for versions below 1903, 2003, 2007, 2009 and 2014 (`code/Model/ModelRead.cpp:1310-1385`).
- **Mission file body**: the parser still accepts FS1 fields. It logs an older `$Version` and carries on (`code/Mission/MissionParse.cpp:650`). FS1's debriefing format, `#Plot Info`, `+Persona Index` and old `+Nebula:` are all still handled. Old `.neb` nebulas still load (`code/Starfield/Nebula.cpp:269`).
- **Campaign file body**: same fields (`code/Mission/MissionCampaign.cpp:495-610`).
- **SEXPs**: every operator FS1 uses exists.
- **Table formats**: `ai`, `cutscenes`, `menu`, `rank`, `traitor`, `credits` and `messages` match FS2 field for field once decrypted. Ship class entries match too: every FS1 ship flag is known to FS2 (`code/Ship/Ship.cpp:1347-1402`).
- **Textures, animations, fonts 1 and 2**: same formats.

### Fails
In load order, the first failure blocks everything after it:

1. **Encrypted tables come out as garbage.** `Encrypt.cpp` defines `ENCRYPT_NEW`, so `unencrypt()` only decodes the new signature. `is_encrpyted()` still reports `0xDEADBEEF` files as encrypted, and `unencrypt_new()` then copies them through still scrambled. The old decoder is still in the file under `#else` (`code/Parse/Encrypt.cpp:56`, `:204`, `:380`). The first table read fails, and so do Silent Threat's encrypted missions.
2. **FS1 missions and campaigns are invisible.** The file system only indexes extensions listed for each folder, and `Data\Missions` lists `.fs2 .fc2 .ntl .ssv` (`code/CFile/cfile.cpp:194`, filter applied at `CfileSystem.cpp:542`).
3. **Mission names with extensions break.** `cf_add_ext("SM1-01.fsm", ".fs2")` gives `SM1-01.fsm.fs2` (`code/CFile/cfile.cpp:519`). FS1 campaign files and `next-mission` name missions this way.
4. **Briefing and debriefing voice is invisible.** FS1 stores it in `data\voice\briefings` and `data\voice\debriefings`. FS2's folders are `Data\Voice\Briefing` and `Data\Voice\Debriefing`. Folder names must match exactly (`CfileSystem.cpp:538`).
5. **Models fail to load.** The engine is compiled for FS2 headers (`#define FREESPACE2_FORMAT`, `code/Model/ModelsInc.h:27`), so it skips FS1's `OHDR` and `SOBJ` chunks. The FS1 field order is still in the source under `#if defined(FREESPACE1_FORMAT)` (`ModelRead.cpp:1272`, `:1414`), but only as a compile-time switch.
6. **Tables FS1 does not have**, which FS2 reads: `mainhall`, `hud`, `stars`, `nebula`, `lightning`, `mflash`, `weapon_expl`, `ssm`, `strings`, `tstrings`, `tips`, `help`, `pixels`, `launchhelp`. A missing file aborts the parse with `longjmp(parse_abort, 5)` (`code/Parse/PARSELO.CPP:1131`). Callers that do their own `setjmp` turn that into an error or skip the table. Callers that don't (`palman_load_pixels`, for one) jump into a stack frame that has already returned, which corrupts the stack. The trial run hit this.
7. **`font03.vf` is missing.** It is loaded unconditionally (`code/Graphics/Font.cpp:875`).

Movies do not play in this source for FS2 either (`movie_play` is commented out). That is not an FS1 gap.

### Trial run (2026-10-18)
`FS1Compat` at `6ca0db6`, built into `PATH_FS1`, started from the FS1 install with `-window -autotest`:
- **Debug**: start-up stops at a run-time check: "Stack around the variable 'buf' was corrupted". Autotest's watchdog ends it after 120 seconds (exit code 5).
- **Release**: exits with `0xC0000409`, a stack buffer overrun.
- **Where**: `palman_load_pixels()` in `game_init` (`code/Palman/PalMan.cpp:248`), found with temporary breadcrumbs that have since been removed. `pixels.tbl` is missing, so `read_file_text` long-jumps through a stale `parse_abort` (failure 6 above). `strings.tbl` is missing too, but its caller catches the jump: "Error parsing 'strings.tbl'", and start-up goes on.
- **So**: start-up dies at the first missing table, after graphics and before any encrypted FS1 table is read. Batch 4 (missing tables) has to come before anything else can be seen running. A missing table must never reach a parser that has no `setjmp`.
- The same Debug exe started from the FS2 install gets past this point.
- Files the runs left in the FS1 install: `FS2vs2022d.exe`, `FS2vs2022r.exe`, `fs2.ini` (created with defaults on the first run) and `autotest.log`.

---

## 3. The gaps, by area

### Tables
| Table | Gap | Kind |
|---|---|---|
| all 14 encrypted | old `0xDEADBEEF` encryption | code path (bring back the old decoder) |
| `ships` | no `#Default Player Ship` section (FS1 marks it with a `default_player_ship` flag instead), no `#Engine Wash Info` section; both required (`Ship.cpp:1664`, `:1672`) | converter |
| `weapons` | `$Trail:` is `YES`/`NO` in FS1. FS2 treats any `$Trail:` as "has a trail" and then requires `+Start Width`, `+End Width`, `+Start Alpha`, `+End Alpha`, `+Max Life`. FS1 gives `+Head Width`, `+Tail Width`, `+Life` (`Weapons.cpp:1045`). Every `$Trail: NO` weapon would fail | converter |
| `sounds` | 144 game and 52 interface sounds against FS2's 192 and 65. No `#Flyby Sounds` section (required, `GameSnd.cpp:359`). The engine finds sounds by slot number, and 87 game slots and 33 interface slots hold a different sound. Ships and weapons refer to sounds by FS1 slot number | converter (remap slots, add flyby section, renumber references in ships and weapons) |
| `music` | FS1 soundtracks have 14 patterns (three "normal" patterns that chain). FS2 has 12 with one normal pattern, and asserts at 12 (`EventMusic.cpp:1060`, `EventMusic.h:125-138`) | converter to fit the slots, plus a code path that restores the chaining (Tom, question 3) |
| `medals` | 16 medals against 18; `$Wavefile 1:`/`$Wavefile 2:` against `$Wavefile Base:` (`Medals.cpp:340`). `NUM_MEDALS` is 16 or 18 at compile time (`Scoring.h:180`) | converter, plus a run-time medal count |
| `Species` | an entirely different file: three species descriptions, against FS2's tech-room intel entries read from the same file name (`TechMenu.cpp:1270`) | converter |
| `asteroid` | 3 entries against 12; the engine asserts it has all 12 (`Asteroid.cpp:2017`) | converter (fill in the debris entries) |
| `icons` | 27 briefing icon types, one set each. FS2 has 35 types with three species sets each (`MissionBriefCommon.cpp:730`) | converter (copy each set per species, map types by order) |
| `fireball` | `$LOD:` is optional; same 6 entries | none |
| 14 FS2-only tables | see section 2, item 6 | defaults, generated in code in data loading (Tom, question 2) |

### Missions
- Extensions `.fsm` and `.fsc`, and mission names that carry the extension: file-name handling in data loading.
- Background: FS1 writes `$Bitmap:`, `$Orientation:` (a matrix), `$Rotation rate:`, `$Distance:`, `$Light:`. FS2 reads only `$Sun:` and `$Starbitmap:` with angles and scale (`MissionParse.cpp:3232-3296`). FS1 backgrounds and suns would be dropped silently. Needs a converter: matrix to angles, `$Distance` to scale, `$Light: 1` to a sun.
- Silent Threat missions are encrypted: covered by the decryption fix.
- Everything else read cleanly against the parser's required fields.

### Models
- `OHDR`/`SOBJ` chunks: a code path. Recognise both chunk IDs at run time and read each in its own field order. The FS1 order is already in the source behind the compile switch.
- The older POF versions already have branches. FS1 models have no insignia or autocentre data, which FS2 treats as optional.
- `.p3d`: ignore.

### Interface art
- All art is 640, which fits the kept GR_640 mode. Render resolution is a separate setting (D-20261017-225737).
- No `mainhall.tbl`. FS1 has the art (`mainhall1`, `mainhall2`, `main1-*`, `main2-*` animations), but the door and animation positions were built into the FS1 exe. A main hall table for FS1 has to be written.
- **Screen layouts are the largest unknown.** FS2's screens place their buttons from coordinate arrays in the code, set for FS2's art. FS1's screens are laid out differently. For example, FS1 has `shipselect-m.pcx` (the click mask) but no `ShipSelect.pcx` background, and no `WeaponLoadout` background. Only 296 of FS2's 1567 interface art names exist in FS1. Without per-screen FS1 layouts, FS1 menus will draw with buttons in the wrong places. Static reading cannot measure this; the trial run will.
- No `font03.vf`: fall back to `font02`.
- HUD: all 98 FS1 HUD art names exist in FS2. FS2 adds 57 more, such as engine wash and extra head animations. Gauge positions come from code and `hud.tbl`, which FS1 does not have.

### Campaign flow
- Campaign file format: compatible.
- Mission names carry `.fsm`: file-name handling.
- The default campaign name, the built-in mission list and the "is this a Volition mission" check all assume FS2 names (`FreeSpace.cpp:726` onwards).
- FS1 pilots (`.plr`, `.csg`) are not imported. New pilots only.
- Silent Threat is a second campaign in the same install (`SilentThreat.fsc`), and its tables replace the main game's. In scope (Tom, question 4).

### Sound and music
- The sound slot remap is the main piece (see Tables). Without it, built-in sounds play the wrong file and data-driven ones go out of range.
- Briefing and debriefing voice folders need mapping (section 2, item 4).
- Music: restore the chaining of FS1's three normal patterns in FS1 mode (Tom, question 3).
- Movies: not played in this source for either game.

---

## 4. Batch plan

Each batch is a branch off `FS1Compat` (or work on it directly if small), verified, then merged into `FS1Compat`. The FS2 recordings must replay unchanged after every batch: FS1 support must not move FS2 play. All FS1 handling sits behind FS1 data detection at start-up, the same pattern as demo mode (D-063), so FS2 runs take none of these paths.

| Batch | What | Converter or code path |
|---|---|---|
| 1. Data detection and file system | Detect FS1 data at start-up. Index `.fsm`/`.fsc`. Map FS1 folder names (`briefings`, `debriefings`). Mission file-name handling (strip or swap `.fsm`) | code path, in data loading |
| 2. Missing tables | Generate default text in code, at run time, for the 14 FS2-only tables, including an FS1 main hall. Nothing table-shaped is committed. Needed first: the trial run dies at the first missing table | generated in code, in data loading |
| 3. Old decryption | Decode `0xDEADBEEF` and `0xCACACACA` at run time, chosen by signature. The old decoder is already in `Encrypt.cpp` | code path |
| 4. Table converters | ships, weapons, sounds (with renumbering), medals, species, asteroid, icons, and music down to FS2's pattern slots. They rewrite the table text in memory after reading and before parsing, so the parsers are unchanged. Silent Threat's replacement tables go through the same converters | converters, in data loading |
| 5. FS1 music chaining | Restore FS1's normal 1, 2, 3 chain in event music, used only in FS1 mode | code path |
| 6. Models | Read `OHDR`/`SOBJ` by chunk ID | code path |
| 7. Missions | Background `$Bitmap` converter. Load every FS1 and Silent Threat mission headless and check it parses | converter |
| 8. First mission | Start on FS1 data, reach the main hall, fly the first training mission | — |
| 9. Interface | Every screen usable: buttons where they can be clicked. Exact FS1 layouts decided after that, once it is clear how far off they are | code path |
| 10. Silent Threat | Second campaign in the same FS1 mode | data loading |
| 11. Regression suite | FS1 recordings, regression-only (D-20261017-225737 item 2) | — |

Batches 1, 2, 3 and 6 are small and independent. Batch 4 depends on 3, because the converters work on decrypted text. Batch 5 follows 4. Batch 8 can run as soon as 1 to 6 are in. Batch 9 is the open-ended one. Multiplayer is out of scope.

Static reading suggests the engine core (missions, SEXPs, ship classes, physics fields, models after batch 5) is close. The work is in the loading layer and the interface.

### Progress

The per-batch log (what each batch did, its results and decisions) is in `docs/fs1/PROGRESS.md`. Read it only for the batch you are working on.

## 5. Branch scope

`FS1Compat` is a live branch off `RecordPlayback`, on origin since 2026-10-20 (first 8de2eec, the screenshot-checked tip, then e5d233d). It merges into `Enhanced` after each green batch and stays live; FS1 work goes on here (Tom, 2026-10-20; `docs/BRANCHES.md`). It took `origin/Enhanced` once, for the data set profile table (95ee575). That merge also brought `origin/RecordPlayback`'s four newest commits (the windowed pause screen, `-at_menus`, ProcDump in `replay_check`), so the separate `RecordPlayback` merge the steward planned had nothing left to bring. `RecordPlayback` is merged down into `FS1Compat` before each FS1 batch. `FS1Demo` is the FS1 demo work's branch; nothing here touches it.

### The FS1 branches, in reading order
Each FS1 change was made on its own branch, merged into `FS1Compat` with `--no-ff`, then tagged `<Name>-done` at its tip (the tag's message names the merge commit) and its branch label removed (steward, D-20261020-091703). The tags are permanent.
- **Loading FS1 data:** `FS1Detect-done`, `FS1FileSystem-done`, `FS1MissingTables-done`, `FS1TableConvert-done`, `FS1Models-done`, `FS1TurretPoints-done`, `FS1Backgrounds-done`, `FS1Lists-done`, `FS1Credits-done`, `FS1Sliders-done`, `FS1DebugNoise-done`, `FS1Recordings-done`, `FS1DataSetProfile-done`
- **Main hall and menus:** `FS1MainHall-done`, `FS1MainHallText-done`, `FS1Labels-done`, `FS1PilotSelect-done`, `FS1Barracks-done`, `FS1Simulator-done`, `FS1CampaignRoom-done`, `FS1TechRoom-done`, `FS1Cutscenes-done`, `FS1Medals-done`
- **Options:** `FS1Options-done`, `FS1OptionsDetail-done`, `FS1OptionsMulti-done`, `FS1ControlConfig-done`, `FS1HudConfig-done`
- **Briefings:** `FS1BriefingScreen-done`, `FS1BriefingArt-done`, `FS1Briefing-done`, `FS1CmdBrief-done`, `FS1ShipWeaponScreens-done`, `FS1ShipWeapon-done`
- **In mission:** `FS1Pause-done`, `FS1MissionLog-done`, `FS1Hotkeys-done`, `FS1GameplayHelp-done`, `FS1HudColour-done`, `FS1HudBrightness-done`, `FS1ShieldIcon-done`, `FS1Reticle-done`, `FS1Resolution-done`
- **Debriefing:** `FS1Debrief-done`, `FS1DebriefStats-done`
- **Across screens:** `FS1HelpOverlays-done`, `FS1TextColours-done`, `FS1SmallDiffs-done`
- **Docs only:** `FS1Fs2Check-done`

### FS1 demo split (D-20261023-202744)
The FS1 demo's code is not on this branch. The merge `f2363f5` ("Merge FS1Demo into FS1Compat") was reverted here (rule 4 of D-20261021-173031); of its hunks only the FS1 retail one was re-applied as its own commit (the FS1 popup text inset, `Popup.cpp`). `FS1Demo` merged the revert down and reverted the revert, so the demo code lives on `FS1Demo` only. The data set profile keeps the FS1 demo fields and row values: they are inert here.
### Branch-local build target
Since the `Enhanced` merge-down (`24cd57f`), `FS1Compat` builds into `PATH_FS2` like every other branch: one exe for every data set, which picks FS1 data at run time. The branch-local `PATH_FS1` target (commit `cebafe2`) is gone. To run on FS1 data, the exes are copied from `PATH_FS2` into the FS1 folders (the FS1 retail install, and the FS1 demo folder on `FS1Demo`); `fc_gate.ps1` does this after its builds.

### Before each merge into Enhanced
1. Replace or revert the build-target change (above), the first time.
2. Run `tools\branch_check.ps1`. It reports an error for any layer, local or on origin, whose project files mention `PATH_FS1`.
3. The gate: the builds, and `recordings/fs1` identical in Release and Debug on `FS1Compat` (D-20261020-211001, Tom: FS1 branches test only their own data). The per-commit check is `fc_gate.ps1` in the gate scripts folder (`fs2_tools`, beside `rp_gate` and `demo_gate`, not in the repo; D-20261022-152103): Release and Debug builds, every recording in `recordings/fs1` identical in Release and Debug, `tools_check`, and the FS1 retail menu tour (`tools\menu_check.ps1 -GameDir` on the FS1 install) Release and Debug to its end with 0 FAIL; all green, no exceptions, last line PASS. The full 48 and the retail quick suite are `Enhanced`'s gate, run on the merged batch. Then a merge brief to the steward: what changed, the `recordings/fs1` results, any recording that moved, the FS1 data used, and that the FS2 suites are run at `Enhanced`. Then the merge and the carry-up.
4. Check FS2's menus visually, once the FS1 layout is finished: screenshot every screen on FS2 data with this branch and with `RecordPlayback`, and compare the images. The replays check gameplay only, and every FS1 layout change is meant to leave FS2 data untouched. Done 2026-10-20 (`FS1Fs2Check`), FS1Compat against `RecordPlayback` 0236e70, both Debug, FS2 data at 1024x768: main hall, tech room, options, barracks, control config, HUD config, medals, cutscenes, simulator, campaign room, command briefing, briefing, ship select, weapon loadout, HUD, pause, mission log, hotkeys, gameplay help and debriefing are pixel-identical apart from things that move between runs (the tech room's turning ship, ships and debris in flight). Credits were left out, as their artwork is random. Repeat it for each batch that changes any screen.

---

## 6. Questions for Tom, and his answers (2026-10-18)

1. **Trial run**: build into `PATH_FS1` on this branch only (section 5). Result in section 2, "Trial run".
2. **Missing tables**: generate them in code at run time. Nothing table-shaped is committed.
3. **FS1 music**: restore FS1's chaining normal patterns, as a code path used only in FS1 mode.
4. **Silent Threat**: in scope, as a second campaign in the same FS1 mode.
5. **Interface**: usable first. Decide on exact FS1 layouts once the screens work.
6. **Multiplayer**: out of scope.
7. **Tools**: committed as `tools\vp_list.ps1` and `tools\fs1_decrypt.ps1`. They read from the install and commit no data. Their output goes to a scratch folder, never the repo.

No questions are open.

---

## 7. Backlog

Nice to have later, not needed to play FS1 as retail did.

- **FS2's HUD and interface options in FS1 data** (Tom, 2026-10-20). FS1 data now works as FS1 does: one HUD colour and a brightness (`FS1HudColour`). FS2's per-gauge HUD colours, its RGB sliders, `.hcf` colour files and similar FS2 options would need FS2's whole interface for them (FS1 has no art for those controls), so they wait.

---

## 8. Handover

The 2026-10-20 handover from DESKTOP-MAIN (branch results at that date and known suite quirks) is in `docs/fs1/HANDOVER_2026-10-20.md`.

## 9. Visual check (on demand, not a gate)

`tools\fs1_visual_check.ps1` compares our screens on FS1 retail data with Tom's retail captures, 45 screens at 640x480 including the F1 help overlays, and lists the static UI differences as a todo list. Nothing is fixed automatically, and no check or merge waits on it (D-20261021-082612). The captures are Volition art and live only in the private repo `fs2_reference` (`fs1_retail\<category>\<screen>.png`, listed in its README). Our shots, the diffs and the report hold the same art, so they never go into this repo: the script refuses an `-Out` inside it.

Run it from PowerShell once the build is in `PATH_FS1`, with no game running (it refuses to start if one is):
```
tools\fs1_visual_check.ps1 -Reference U:\Projects\GitHub\fs2_reference
```
- `-Reference` defaults to `$env:FS2_REFERENCE`. `-GameDir` defaults to `$env:PATH_FS1`, `-Config` to `r`.
- It runs the menu tour with screenshots (`-window -autotest -at_menus -at_shots <dir>`, docs/AUTOTEST.md) with `-Mission` (default: the first campaign mission) and, with `-TrainingMission <file>`, again for `briefing_training`. Leave that off for now: the tour does not handle a training mission yet (it logs that briefing as `briefing/briefing`).
- `-Shots <dir>` compares a shot folder from an earlier run and does not start the game.
- Output goes to `-Out`, by default `<Reference>\fs1_retail_check\<time>`, which `fs2_reference` ignores:
  - `report.md`: every reference screen, as same, its number of differences, not captured (with what the tour logged for it), not reached, or a setup error. A shot of another size is a setup error (wrong mode or UI art), not a visual difference.
  - `TODO.md`: one checkbox per difference, with the screen, a description (shifted by how many pixels, drawn in the reference and missing in ours, extra in ours, or differs; with its place and box), and links to the reference, our shot and the diff.
  - `diff\<screen>.png`: reference, ours and the difference side by side. Red = differing pixels, blue = ignored, yellow numbered boxes = the TODO entries.
- The comparison is `tools\visual_compare.cs`, compiled by the script, about 0.1 s a screen.

**Masks.** Some content varies by design and is not compared: the pilot's name, picture and stats, the mission text and briefing map, the 3D ship and weapon models, animations, the in-flight scene, and the mouse cursor (where it sits in each reference). The masks are in `fs2_reference\fs1_retail\masks.json`:
- `screens.<screen>.ignore`: rectangles `[x, y, w, h]` in 640x480 pixels, each with a `why`. `only`: if given, only these are compared (the in-flight HUD).
- `<screen>_help` uses its base screen's masks as well as its own.
- An optional `<screen>.mask.png` next to the reference adds any shape: white = ignore.
- `ourCursor`: where the tour parks our cursor while it takes shots (client 0,0, the top-left corner), masked on every screen.
- `threshold` (largest channel difference that still counts as the same), `minPixels` (smallest difference reported) and `maxShift` (largest offset looked for when telling a shift from a change).

To update a mask: open the screen's `diff\<screen>.png`, read the box of the content that should not count from its TODO entry, add a rectangle with a `why` to that screen in `masks.json`, rerun with `-Shots <the run's shots folder>`, and commit the change in `fs2_reference`.
