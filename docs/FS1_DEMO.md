# FreeSpace 1 demo data set

The FS1 demo as a data set (`DATA_SET_FS1_DEMO`, `-dataset fs1demo`): getting its files out of the original
installer, recognising them, and what the engine lacks to play them. Branch `FS1Demo`, off `FS1Compat`, with
`origin/FS2Demo` merged in once for the data-set code (`docs/FS2_DEMO.md`).

Nothing from the demo is committed. Its `readme.txt` licence forbids redistributing it, as the FS2 demo's
does; the test extracts from the installer at run time into a temporary folder.

---

## 1. The installer

`fsdemo10.exe` (26,742,784 bytes) is a WinZip self-extractor, like the FS2 demo's: a Windows program with a
zip appended. The zip holds a 16-bit InstallShield 5 setup (`SETUP.EXE`, `_INST32I.EX_`, `_SETUP.DLL`,
`setup.ins`, `_sys1.cab`, `_user1.cab`, and so on) and the game, in one cabinet:

| zip entry | size | holds |
|---|---|---|
| `data1.cab` | 25,974,157 | the game, as an InstallShield 5 cabinet |
| the rest (15 entries) | under 0.6 MB together | the setup program and its scripts and pictures |

`data1.cab` is version 5 (`ISc(`, `0x01005100`) with its header inside the cabinet (no `data1.hdr`). It holds
6 files, all compressed:

| file | size | taken |
|---|---|---|
| `Data\freespace.vp` | 37,933,752 | yes: every table, mission, model, sound and picture the demo has |
| `FS.exe` | 1,541,120 | no: the original demo game |
| `FreeSpace.exe` | 956,416 | no: the original launcher |
| `fsuninst.dll` | 135,168 | no |
| `readme.txt` | 17,916 | no |
| `refcard.rtf` | 6,633 | no |

A file's data is a run of chunks, each a 16-bit length and that many bytes of raw deflate, ended by a sync
flush rather than a final block, and no chunk records its output size. The cabinet keeps no checksum per file
(per-file MD5s came with later versions), so a file is accepted when every chunk decodes, the chunks use up
exactly its compressed size, and the output is exactly its expanded size. The cabinet itself is covered by
the zip's CRC.

The 16-bit setup cannot run on 64-bit Windows, so there is no "what the real installer produces" to compare
with. The test compares against an independent decoder instead (section 3).

## 2. Code

| file | what |
|---|---|
| `code/CFile/CabReader.h/.cpp` | InstallShield 5 cabinet reader: file list (`cab_open`), one file (`cab_extract`). Refuses split, obfuscated and multi-volume files rather than guessing |
| `code/CFile/DataSetFs1.h/.cpp` | `dataset_fs1_detect(dir)` and `dataset_fs1_extract(exe_dir, err, err_len)` |
| `code/CFile/DataSetInstall.h/.cpp` | shared with the FS2 demo's extraction (Tom's OK, 2026-10-19): `dataset_inflate` (the inflater, now reporting how much it wrote and accepting a sync-flushed stream), `dataset_crc32`, and `dataset_zip_read` (one zip entry into memory). The FS2 path is unchanged: `inflate_buffer` is a wrapper with its old behaviour, and `zip_extract` is untouched |
| `tools/fs1demo_test.ps1` | the test (section 3) |

**`int dataset_fs1_detect(char *dir)`**: 1 if `dir\Data\freespace.vp` exists and its directory lists
`demo.fsc`. FS1 retail has a `Data\freespace.vp` too, without `demo.fsc`.

**`int dataset_fs1_extract(char *exe_dir, char *err, int err_len)`**: reads `data1.cab` out of
`exe_dir\fsdemo10.exe` (CRC checked), takes the cabinet's `Data\` files and any top-level VP into
`exe_dir\fs1demo.partial`, and renames that to `fs1demo` only when every file is whole. An existing `fs1demo`
is never written to. 0 on success; otherwise non-zero and the reason in `err`.

**The two hooks in `DataSetInstall.cpp`** (done, Tom's choice of the FS2 demo's start-up extraction,
2026-10-20): `use_dataset()` checks `folder_has_dataset()`, which for `DATA_SET_FS1_DEMO` is
`dataset_fs1_detect()` (the demo keeps its only VP in `Data\`) and for the others `folder_has_vps()` as before;
the FS1 demo's row extracts with `dataset_fs1_extract()` from `fsdemo10.exe` beside the exe on first start,
as the FS2 demo does from its installer.

## 3. Test

`tools\fs1demo_test.ps1 -Installer <fsdemo10.exe> [-Fs2Installer <fs2demo10.exe>]` builds a small program from
the files above, then:

1. lists and extracts every file in `data1.cab` with the game's reader;
2. extracts them again with an independent reader (.NET's zip and deflate, with the cabinet layout read in
   C#) and compares the two, file by file, by size and SHA-256;
3. runs `dataset_fs1_extract` into a fresh folder: `fs1demo` must hold exactly `Data\freespace.vp`, identical
   to the reference, with no `fs1demo.partial` left; `dataset_fs1_detect` must accept it and refuse FS1
   retail and an empty folder; a second extract must refuse the existing folder;
4. with `-Fs2Installer`, makes the FS2 demo's folder through `dataset_select_folder` and compares every file
   with .NET's extraction of the same zip entries, to show the shared inflater left the FS2 path working.

Result on 2026-10-19: all 20 checks pass.

## 4. What the demo contains

One archive, `Data\freespace.vp` (947 entries, 905 files). FS1 retail splits its data over `root.vp` and
`Data\freespace.vp` (3,065 files); the demo is a cut-down copy of the same game, with a few files of its own.

| folder | files | notes |
|---|---|---|
| `effects` | 41 | |
| `fonts` | 4 | `font01.vf` differs from retail; `font01.pcx`, `font02.pcx` (the fonts' source art) are demo-only |
| `hud` | 60 | 4 shield anims differ from retail |
| `interface` | 354 | 348 of retail's 982 (below); demo-only: `DemoTitle1`, `DemoUpsell1`-`4`, `OptionsMainPalette` |
| `maps` | 61 | 17 differ from retail |
| `missions` | 5 | `demo.fsc`, `btmdemo.fsm` (training), `demo01.fsm`, `demo02a.fsm`, `demo02b.fsm` |
| `models` | 42 | `cruiser01.pof` differs from retail |
| `music` | 16 | 4 differ from retail |
| `players\images` | 8 | pilot pictures |
| `sounds\8b22k`, `16b11k` | 59, 35 | 13 differ from retail |
| `tables` | 15 | below |
| `voice` | 205 | briefings, debriefings, training, special, personas; 31 briefing and debriefing files are demo-only |

**Tables** (all but `menu.tbl` encrypted with FS1's old scheme, `0xDEADBEEF`, which `FS1Compat` already reads):
`ai`, `asteroid`, `credits`, `fireball`, `icons`, `medals`, `menu`, `messages`, `music`, `rank`, `ships`,
`sounds`, `tooltips`, `traitor`, `weapons`. Retail's `cutscenes.tbl` and `species.tbl` are missing. Nine
differ from retail's: `ships.tbl` has 13 ships (retail 67), `weapons.tbl` 15 weapons (retail 45), `medals.tbl`
16 medals as in retail; `credits.tbl` has no `#end`, like retail's.

**Missions**: FS1's mission format (`$Version: 0.10`), encrypted like the tables. The campaign `demo.fsc`
("Freespace Demo Campaign", single player, starting ship GTF Apollo) runs `btmdemo` (until its "Flight
Training" goal is met), then `demo01`, then `demo02a` or `demo02b`. Its mission names have upper-case
extensions (`btmdemo.FSM`).

**Pilots**: none ship; the demo makes them when played. The original demo's `.plr` files are FS1's format,
which this engine does not read (FS1 retail is the same: `FS1Compat` plays with an FS2 pilot).

## 5. Gaps against the engine

What the engine reads that the FS1 demo lacks or has in another form, assuming `FS1Compat`'s FS1 support.

1. **Data set identification.** `cfile` sets `Cfile_fs1_data` from a `Data\freespace.vp` pack, so the demo
   takes FS1's loading paths (table conversion, old decryption, `.fsm`/`.fsc`, missing-table stand-ins). Code
   that must tell FS1 demo from FS1 retail needs the data set itself (the profile table's FS1 rows).
2. **Folder check in `use_dataset()`**: see section 2; `folder_has_vps()` does not see `Data\freespace.vp`.
3. **Screens with no art.** Of retail's interface pictures, the demo lacks the tech room (`tech`, `techdata`,
   `techshipdata`), campaign room (`campaign`), simulator (`readyroom`), load mission (`loadmission`),
   credits (`credits`, `creditswin*`, `creditsart*`), medals (`medal*`, `medalsdisplayempty`), hotkeys
   (`hotkeys`), red alert (`redalert`), training menu (`training`), the second main hall (`mainhall2`), the
   multiplayer screens (`multi*`, `mp*`, `pxo*`), the help overlays (`*_help*`), the debriefing's award, medal, rank and badge pictures
   (`debrief*`), and the loading pictures (`load*`). The demo must not reach those screens, as the FS2 demo's
   restrictions keep the FS2 demo from its missing ones; FS1's `DEMO` blocks (held under D-064) are where FS1
   did this.
4. **Upsell and title screens.** FS2's demo code shows `UpSell01`, `UpSell02` and FS2's title screen; the FS1
   demo's are `DemoUpsell1`-`4` and `DemoTitle1`.
5. **Missing tables.** `cutscenes.tbl` (the cutscene screen, which the demo lacks) and `species.tbl` (only the
   tech room reads FS1's). Neither screen is reachable in the demo (item 3), but `cutscenes.tbl` is read at
   start-up, so FS1 data now gets an empty one when the file is missing (`Parse/Fs1Tables.cpp`, with Tom's OK).
6. **Smaller tables.** 13 ships and 15 weapons. FS1 retail's table conversion applies; whether anything the
   engine looks up by name (a default ship, a weapon in `sounds.tbl` or `icons.tbl`) is absent is not checked
   yet.
7. **Missions and campaign.** Same format as FS1 retail, so `FS1Compat`'s mission loading applies; none of the
   four missions has been loaded yet.
8. **Pilots.** None shipped; the engine makes its own.
9. **Web cursor.** The demo has no `cursorweb.ani`, which `gr_init` Asserts on in Debug. Its only user
   (popups with web links) checks for it, so the Assert now allows it missing in FS1 data
   (`Graphics/2d.cpp`, with Tom's OK).

### First run (2026-10-19)

The demo, extracted by `dataset_fs1_extract` and run by hand with the Debug exe, stopped at start-up on
`cutscenes.tbl`, then on the web cursor (items 5 and 9, both fixed). Then, under `-autotest` with each of
`btmdemo`, `demo01` and `demo02a`, every mission stops before loading:

- **Error: "Error loading animation file: thruster03"**, the Shivan thrusters, which the demo does not ship. The
  original game skipped them in its `DEMO` build (below). This is the stopper.
- **Warnings**: the demo's own `ships.tbl` lets three ships carry `D-Missile`, `Interceptor` and `Hornet#Weak`,
  which its `weapons.tbl` does not define (Volition's data; the original demo would have skipped them quietly).
- **Int3s**: in Debug only, from art the demo lacks: the loadout screens' ship and weapon icons, the briefing's
  `BriefMap.ani` and the particles' `ParticleSmoke02.ani`. The original demo was an `NDEBUG` build, where
  `Int3` and `Assert` do nothing, so it skipped these quietly, as this engine's Release build does. All three
  are fixed by the blocks below.

### Now (2026-10-20)

All four missions (`btmdemo`, `demo01`, `demo02a`, `demo02b`) start and fly under `-autotest`, 60 s each: Release
PASS with no Assert, Int3 or Warning; Debug likewise. The three `PARSELO` warnings above (17 to 20 per mission,
the same lines repeated) are Volition's data: on FS1 demo data they are now logged, not shown
(`unknown_weapon_warning` below, D-20261020-213146); on every other data set they stay Warnings.

A fresh ini on FS1 data, retail or demo, starts the game at 640x480 (`FS1Resolution`, merged from FS1Compat);
an existing ini keeps its setting. At 1024x768 the Debug exe stopped on the missing `2_*` HUD art.

Started by hand in Debug, the demo also stopped (Int3) on the missing `attacker.pcx` when the target
brackets loaded it; fixed by `attackers` below.

### The original `DEMO` blocks

The FreeSpace 1 demo was a separate build (`#define DEMO`); these are the blocks it compiled differently. FS2's
own demo blocks are `FS2_DEMO`. Each is converted to a field of the data-set profile table
(`GlobalIncs/DataSetProfile.h`), its `#ifdef DEMO` arm removed in the same commit (D-20261020-161656: walls,
demo experience, data handling and play restrictions become profile fields). Row values are FS2 retail, FS2
demo, FS1 retail, FS1 demo.

| block | what the demo build did | needed by the FS1 demo data | status |
|---|---|---|---|
| `Ship.cpp:3527`, `:3544` | loads 4 thruster anims and 4 glows, not 6 (no Shivan thrusters) | yes: `thruster03`, `thruster03a`, `thrusterglow03`, `thrusterglow03a` are absent; start-up stops without it | done: wall, `thruster_species` 3, 3, 3, 2 |
| `Shield.cpp:178`, `:255` | no shields at all: every shield function stubbed, so no hit effect and damage passes straight through | yes: the demo has no `shieldhit` bitmaps | done: play restriction, `shields` 1, 1, 1, 0 (D-20261020-161655) |
| `MissionScreenCommon.cpp:532`, `:1043`, `:1064`, `:1081`, `:1117` | ship and weapon selection disabled; their buttons and keys play a fail sound | yes: without it Debug stops on the missing icons | done: wall, `loadout` 1, 1, 1, 0. `:680` held: the block only wraps code that is commented out |
| `MissionShipChoice.cpp:2823`, `:2864`, `MissionWeaponChoice.cpp:1406`, `:1477`, `:1640` | ship and weapon icons and anims not loaded | goes with the line above | done with `loadout`: the load is skipped, the loops that clear the handles still run (so nothing stale is released). `:1640` held: it has no effect |
| none (new) | the demo's `BriefMap.ani` is missing; the demo build ignored it as `NDEBUG` | yes: Debug stops at the first briefing stage cut | done: wall, `brief_fade` 1, 1, 1, 0 (D-20261020-163608): not loaded, and the cut plays its static sound and draws nothing. The `#ifndef FS2_DEMO` arm around the same code is left for the FS2 demo work (`FS2Demo` batch 8) |
| none (known difference) | the briefing shows `BRIEF.PCX` with no window frame: the demo has none of retail's frame art (`BriefTrans.ani`, `BriefTransMulti`, `BriefMap.ani`, `Briefwin`), and the demo's `FS.exe` names only `brief`, `brief-m` and `BriefPop` | no: nothing to load | not converted, no code change (D-20261020-231903): the data and the exe both say the shipped demo showed no frame. Unverified against the original running (its exe does not start on this PC); revisit if it ever does |
| none (new) | the demo's `ParticleSmoke02.ani` is missing; ignored likewise | yes: Debug Asserts on every such particle | done: wall, `smoke2` 1, 1, 1, 0 (D-20261020-163608), at the draw: the particles live and draw nothing; the emitters (`Ship.cpp:3242`, `:3348`, `ShipFX.cpp:2239`) are untouched. Drawing FS1 retail's other smoke instead was considered and not done; it would be a one-value change if Tom prefers it |
| none (new) | the demo's `attacker.pcx` is missing; ignored likewise | yes: Debug stops (Int3, `HUDbrackets.cpp`) when the target brackets load it | done: wall, `attackers` 1, 1, 1, 0: the brackets show no '+' per extra attacker, as the original demo showed none |
| `Ship.cpp:1042`, `Weapons.cpp:769` | reads only the ships and weapons whose names start with `@` | the demo's entries all carry `@` (13 ships, 14 of 15 weapons); the `@` itself is stripped outside the block, so the block only drops the un-marked ones (one weapon, `Type One`) | done: data handling, `unmarked_entries` 1, 1, 1, 0. No demo ship or mission names `Type One`: the Debug warnings are unchanged |
| none (new) | the demo's `ships.tbl` names `D-Missile`, `Interceptor` and `Hornet#Weak`, which its `weapons.tbl` lacks; the demo build was `NDEBUG`, where `Warning` does nothing | yes: Debug shows 17 to 20 Warning dialogs per start (`PARSELO.CPP`, `stuff_int_list`) | done: data handling, `unknown_weapon_warning` 1, 1, 1, 0 (D-20261020-213146): logged, not shown, on FS1 demo data; Warning on every other row. The weapon is dropped either way, as before. The log line is an `mprintf`, which this engine compiles in Debug only, so Release logs nothing, as it showed nothing before |
| `MissionMessage.cpp:1505` | no talking-head anims for messages | yes: the demo has no head anims | done: wall, `talking_heads` 1, 1, 1, 0: the message's voice and text play, no head anim |
| `MainHallMenu.cpp:1124`, `OptionsMenu.cpp:911` | the main hall's multiplayer door and the Options multi tab answer `game_feature_not_in_demo_popup()` | yes: the demo has no multiplayer screens and no `OptionsMulti*-M` | done: wall, `full_version_screens` 1, 1, 1, 0 (D-20261020-214042): the `DEMO` half of each `#if` is now the field, read at run time; the `OEM_BUILD` half stays compile-time. The sibling `#if !defined(DEMO)` blocks around the multi tab's init, accept and close are held: they compile in and are harmless with the tab walled |
| none (new) | the same popup on the tech room and campaign room doors (main hall), F3 Hotkeys in flight (`FreeSpace.cpp:4724`), the mission objectives key (`KeyControl.cpp:2581`), Medals in the barracks (`Barracks.cpp:1086`) and the debriefing (`MissionDebrief.cpp:2027`) | yes: the demo has no `Campaign*`, `TechShip*`, `Hotkeys-M`, `Objectives-m` or `Medals-m`; Release stopped with "Could not load in Hotkeys-M!" on F3 | done with `full_version_screens`: each site shows the popup before any art load. The `FS2_DEMO` arms at the same sites are untouched. Not reachable on the demo data, so not walled: Credits, ViewFootage and LoadMission (behind the walled rooms), ship and weapon select (`loadout`), Red Alert, Command Brief and Loop Brief (no such demo missions). The popup reads "Sorry, this feature is available only in the retail version" (`XSTR` 200); the FS1 demo's `FS.exe` carries "Sorry, this feature is only available in the retail version" |
| none (new) | the demo's `OptionsMain.PCX` has no mouse on/off buttons and no mouse sensitivity, joystick sensitivity or deadzone sliders, and `freespace.vp` has no `OPa_60` to `OPa_64` | yes: Release stopped with "Could not load OPa_64.ani" (`UI/slider.cpp`) on entering Options | done: wall, `options_mouse_sliders` 1, 1, 1, 0 (D-20261020-213842): the two buttons and three sliders are not created (`OptionsMenu.cpp`). Mouse on/off, mouse sensitivity, joystick sensitivity and deadzone are therefore not adjustable on the demo and keep their values, which matches the shipped screen |
| none (FS1 art placement) | FS1 retail and the demo ship their own `LoadingBG.PCX` and `Loading.ani` (640 only); the engine drew the bar at FS2's place for every data set, off in X on FS1 retail, in X and Y on the demo | yes: the bar is misplaced | done: data handling, `loading_ani_coords` (D-20261020-222222), 640 / 1024: FS2 retail and FS2 demo {63,316} / {101,505}, FS1 retail {117,317} / {187,507}, FS1 demo {133,337} / {213,539}. Measured as the best pixel fit of the anim's frame 0 over each background (mean difference 0.2 on retail, 1.4 on the demo, with the neighbouring positions clearly worse); no screenshot of the original demo exe exists. The FS1 1024 pairs are the 640 pair x 1.6, FS2's own rule, derived and unreachable (FS1 art is 640 only). An FS1 retail behaviour change too |
| none (new) | the demo's own F1 overlays for the briefing and the main hall: `freespace.vp` has `Briefing_Help_Overlay_Demo.PCX` and `Main_Help_Overlay_Demo.PCX`, not retail's `brief_help_over` and `main_help_overlay`, and the demo's `FS.exe` names `briefing_help_overlay_demo` and `main_help_overlay_demo` | yes: without it F1 draws no overlay on those two screens | done: data handling, `help_overlay_brief_main` NULL, NULL, NULL, { `Briefing_Help_Overlay_Demo`, `Main_Help_Overlay_Demo` } (D-20261020-231902): NULL keeps the table's name; read where the overlay loads (`ContextHelp.cpp`) |
| `UI/WINDOW.cpp:929` | tooltips not parsed | no: the demo's `tooltips.tbl` starts with `#End` anyway | held |
| `FreeSpace.cpp:6792`, `:8192` | no intro movie, no CD check | `:6792` no: its movie and CD prompt are inside `RELEASE_REAL`, which this build does not define; `:8192` (finding the CD drive) not checked | held |
| `FreeSpace.cpp:7606` | version string `Dv1.00` | no: inside a comment | held |

Each conversion is gated on the four FS1 demo missions (btmdemo, demo01, demo02a, demo02b) under autotest in
Release and Debug with no Error, Warning, Assert or Int3 (D-20261020-211001, Tom: FS1 branches test only their
own data; the allowance for the demo's ships.tbl PARSELO Warnings is withdrawn, D-20261020-213146). `tools\fs1demo_run.ps1 -Configs r,d` runs it in
minutes. FS2 retail and FS2 demo replays are not run here: FS1Compat gates this branch's merge-in on
`recordings/fs1`, and Enhanced gates FS1Compat's batches on the FS2 suites. `tools\fs1demo_gate.ps1` runs the FS1
demo missions, `recordings/fs1` and the FS2 suites together (about an hour) and is the merge-in tool, not the
per-change gate. Both need the FS1 demo folder (`FS1DEMO_PATH`): `fsdemo10.exe` from the original
download, with a pilot made by starting the game once by hand.

## 6. Not done

- Playing the demo through by hand (Tom): the screens it must not reach (item 3), the upsell screens (item 4),
  the campaign's branch to `demo02a` or `demo02b`.

Not to be done: `cab_open` reads only what this cabinet uses and refuses other InstallShield versions. The only
demos are FS2's (a zip with loose files, no cabinet reader) and FS1's, so nothing else needs it (Tom, 2026-10-20).
