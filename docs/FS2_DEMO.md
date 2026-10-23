# FreeSpace 2 demo

Findings and plan for demo mode (D-063, D-20261017-224829, D-20261017-223742, D-20261017-231126; G1 build
variants D-066; Glide/E3 block D-070). Branch `FS2Demo`, off `RecordPlayback`. Investigation only: the
one source change so far is the compile fix in section 2.

## 1. Installing the demo without its installer

The demo arrives as a 1999 InstallShield 5 setup image (Tom's copy: `C:\Games\Freespace 2 Demo setup`).
Its installer is not needed and must not be run. Everything it installs is a loose file on the image, and
`data1.cab` is only the file table (4.7 KB, no file data).

```
python -I tools\demo_install.py "<setup image>" "%FS2DEMO_PATH%"
```

The script only reads the image. It parses the file table in `data1.cab`, copies each file to the same
folder under the target, and makes the 10 empty `Data\...` folders the table lists (`interface`,
`missions`, `MultiData`, `music`, `players`, `players\images|multi|single|squads`, `tables`). It also gives
the three programs the names `setup.ins` renames them to. The `.ex_` files on this image are plain,
uncompressed Windows programs (PE, not SZDD), so `expand.exe` is not needed. The script would call it on
an SZDD file. It refuses a target inside the image, or a non-empty one without `--force`.

| Installed | From | What it is |
|---|---|---|
| `FS2Demo.exe` | `fs2demo.ex_` | The demo game. PE timestamp 1999-08-07; version resource 1.03.01 |
| `FreeSpace2Demo.exe` | `freespace2demo.ex_` | The demo launcher (writes the registry settings) |
| `UpdateLauncher.exe` | `UpdateLauncher.ex_` | Updater |
| `root_fs2.vp`, `sparky_fs2.vp`, `sparky_hi_fs2.vp` | same | All game data (below) |
| `readme.txt`, `PXO.url`, `FS2uninst.dll`, `patchw32.dll`, `Data\multi.cfg` | same | Support files |

Not reproduced, on purpose: the registry key `HKLM\SOFTWARE\Volition\Freespace2Demo` that setup and
the launcher write, the Start menu shortcuts, and the uninstall entry. The project's builds read
`fs2.ini` (docs/SETTINGS.md) and never the registry. `_sys1.cab` and `_user1.cab` hold only the
installer's own UI and support files. `DATA.TAG`, `setup.lid`, `layout.bin`, `lang.dat` and `os.dat`
are installer metadata.

Image checksums (MD5), for the reference entry: `root_fs2.vp` 8ae3cc20b480f6131898a7a1fa7cff1b,
`sparky_fs2.vp` 76f7e8f13da0d8ca64948a46240daa99, `sparky_hi_fs2.vp` 2f129e0c741fe29aef4ecbb6bddb82d9,
`fs2demo.ex_` 83a881107295c33e49abbd4550383ecd.

**The install is good.** On 2026-10-18 Tom ran the original `FS2Demo.exe` from this install, through its
launcher, and got into a mission. The launcher's settings went to the registry, which Windows
virtualised to `HKCU\Software\Classes\VirtualStore\MACHINE\SOFTWARE\WOW6432Node\Volition\FreeSpace2Demo`
(Videocard `Direct 3D - Primary Display Driver (32 bit) (1024x768)`). Later in that session the exe hit
an access violation in the system `DDRAW.dll` (`errorlog.txt` in the demo folder), with no DDrawCompat
present. It probably came on exit or a mode switch, not at start-up. Running the original exe is not
part of the plan for now (Tom, 2026-10-18). It may become a reference run later (section 6, item 4).

## 2. What the demo data is

All data is in the three VPs (`root_fs2.vp`: 27 tables, players, fonts; `sparky_fs2.vp`: missions,
models, interface, sounds, voice, music; `sparky_hi_fs2.vp`: the 1024x768 interface art). The demo has
GR_1024 art, so the reference configuration's resolution is available.

| | Demo | Retail (1.20 data) |
|---|---|---|
| VPs | root, sparky, sparky_hi (98 MB) | root, sparky, sparky_hi, smarty, stu, tango1-3, warble |
| Missions | `DemoTrain`, `SPDemo-01` ("Crimson Sky"), `SPDemo-02` ("Crisis in Capella"), campaign `demo.fc2` (those three in order); multiplayer `MPDemo-01`, `Demo-DOG-01` | 92 files, campaign `freespace2.fc2` |
| Ships / weapons in tables | 31 / 47 | 126 / 90 |
| Models | 35 `.pof`, no asteroid models | all |
| Medals (`medals.tbl`) | 16, the FreeSpace 1 set (Conspicuous Gallantry ... Wings) | 18 (Epsilon Pegasi Liberation ... SOC Unit Crest) |
| Tables missing | `weapon_expl.tbl`, `credits.tbl`, `launchhelp.tbl`, `tstrings.tbl` | |
| Mission file `$Version` | 0.10, all modified 1999-08-06 | 0.10 |

Identical tables: `ai`, `menu`, `mflash`, `nebula`, `pixels`, `ssm`. The rest are older or smaller
versions of the retail tables. Every field name and sexp in the five demo missions is known to the
source. None is new, and `shield-quad-low` is the only sexp no retail mission uses.

Versions: the source's `FS2_DEMO` build says 1.10 (`version.h`), retail 1.20. The shipped demo exe's
version resource says 1.03.01. The demo's revision history starts at "FS2_DEMO initial checkin",
1999-07-15. The source is therefore the later retail code base, and its `FS2_DEMO` arms were not kept
in step with the demo data.

### Retail code against demo tables (static reading; the refactor's work list)

These parse calls in the 1.20 source cannot read the demo's tables as they stand. They apply to the
retail build and the `FS2_DEMO` build alike, because none of them is inside an `FS2_DEMO` block:

1. `weapon_init` reads `weapon_expl.tbl` unconditionally (`Weapons.cpp`, `parse_weapon_expl_tbl`), and
   the demo has none. That is an `Error` at start-up.
2. `mainhall.tbl`: the demo's has no `+Music:`, which `MainHallMenu.cpp` reads with `required_string`.
3. `medals.tbl`: the demo's badges have `$Wavefile 1:` / `$Wavefile 2:` where `Medals.cpp` requires
   `$Wavefile Base:` (FRED's copy of the parser still reads the old pair). It also has 16 medals where
   the code asserts `NUM_MEDALS` (18, or 16 under `FS2_DEMO`).
4. Older formats that are already tolerated: `species.tbl` (no `$Entry`, so no intel entries),
   `fireball.tbl` (no `$LOD`, which is optional), shorter `ships`/`weapons` field sets (the missing
   fields are all optional).

Only a run can show the full list. These are the ones a reading finds.

### Does the source's FS2_DEMO build compile?

Yes, after one fix. With `/DFS2_DEMO` (through the `CL` environment variable, `PATH_FS2`, `IntDir` and
`OutDir` pointed at a scratch folder), Release and Debug failed in one place only:
`Stats/Medals.cpp` `Medal_coords[GR_NUM_RESOLUTIONS][NUM_MEDALS][2]` has 18 rows of initialisers, and
`FS2_DEMO` makes `NUM_MEDALS` 16 (C2078). Commit "Medals.cpp: size Medal_coords at 18 rows" fixes it,
which leaves retail unchanged by construction. With that, both configurations link. The 18 rows are the
retail layout. The demo's 16 FS1 medals have no layout in this source, but the demo walls off the
medals screen (`Barracks.cpp`, `game_feature_not_in_demo_popup`), so no layout is needed.

### First runs of project builds on the demo data

The 2026-10-18 results are in `docs/fs2demo/FIRST_RUNS_2026-10-18.md`.

## 3. Detection rule

**Demo if the cfile index has `demo.fc2` and has no `freespace2.fc2`; otherwise retail.** It is checked
once, right after `cfile_init` and before `os_init` and any table is read (`game_init`, FreeSpace.cpp).

Evidence:
- Each data set names its built-in campaign in source (`BUILTIN_CAMPAIGN` "demo" / "FreeSpace2",
  `MissionCampaign.h`). The demo's `sparky_fs2.vp` holds `data/missions/demo.fc2`. Retail's
  `root_fs2.vp` holds `freespace2.fc2` and no file in any retail VP is named `demo*`.
- VP names alone cannot decide it, because both sets have `root_fs2.vp`, `sparky_fs2.vp` and
  `sparky_hi_fs2.vp` with different contents. The retail-only VPs (`stu_fs2.vp` and the rest) would work
  but would tie detection to packaging.
- Both checks together guard against a loose `demo.fc2` dropped into a retail `data\missions`, and
  against a mixed folder, which comes out as retail.
- The original demo exe runs this data into a mission (section 1), so the data set is complete for the
  demo's own code path.

The result is logged and stored in the recording header, and a replay refuses a recording made on the
other data set. It replaces every compile-time `FS2_DEMO` test with one flag (`Game_data_set` or
similar).

## 4. Demo recording suite

- **Missions:** `DemoTrain`, `SPDemo-01` and `SPDemo-02`, the demo campaign. Autotest's default
  ("every single-player campaign mission") picks them up from `demo.fc2` once the built-in campaign
  follows detection. The two multiplayer missions are out, as in the retail suite. Project-written
  missions limited to the demo's 31 ships and 47 weapons (like the `Autotest*` set) can be added later.
- **Reference entry:** D-023 and D-056 unchanged (1024x768, GR_1024, medium skill, English, LowMem
  off), plus the data set: the MD5s above and detection = demo. Demo fidelity follows
  D-20261017-224829: walls and demo experience as shipped, quality and limits at retail.
- **What it needs without a retail install:** the demo install (section 1), the exe, `fs2.ini` and a
  pilot. Recordings, traces, the install script and an ini are ours and can be committed
  (`recordings/demo/`). The demo data cannot (section 6, item 1).
- **Order:** the data-handling batch comes first (section 5, batch 1), because no build of this source
  loads the demo data yet. The suite is recorded after that, with the compile-time `FS2_DEMO` build. Each
  later batch must replay both suites unchanged. A batch that deliberately moves a demo recording (a
  limit going to retail) logs the re-record.

## 5. Batch plan for one exe

Scope: 85 `FS2_DEMO` blocks in 47 files (the same 85 as `docs/DEMO_CODE.md` on PrePortCleanup;
`demo_blocks.py` gives the same count on RecordPlayback), each classified under D-20261017-224829:
walls (W), demo experience (X), quality and limits (Q, logic-affecting ones marked Q!), data
handling (D). Every batch: build both configurations, replay the retail suite and, from batch 3 on, the
demo suite.

| # | Batch | Blocks |
|---|---|---|
| 1 | **Old table and pilot loading (D).** Done (D-20261018-154908): 35b2c0c, f980292, 6b7a34e, cdb9b9f, efca275. | ManagePilot.cpp, Weapons.cpp, MainHallMenu.cpp, Medals.cpp, HUDconfig.cpp (outside `FS2_DEMO` blocks) |
| 2 | **Detection flag.** Done (D-20261018-175706): the rule in section 3, logged with its inputs, and the replay check. No header field below GodotPrep: the replay takes the recording's data set from its campaign, else its mission name (demo names in Replay.cpp, retail from the build's built-in list); our own test missions are "unknown" and play on either set. | FreeSpace.cpp/.h, Replay, Autotest |
| 3 | **Record the demo suite.** Done (D-20261018-184854/-184856, D-20261018-194722): the three campaign missions in `recordings/demo`, from the compile-time `FS2_DEMO` build; replay and autotest load their own pilot under `FS2_DEMO`; the missing-bitmap write fixed (#25, review 50). | recordings/demo, FreeSpace.cpp, BmpMan.cpp |
| 4 | **Names, versions, files (D/X).** App name and title, version, MOTD URL, built-in campaign and mission list, title screen file, `hud_1.hcf` vs `hud_3.hcf`. | OsRegistry.cpp, version.h, MissionCampaign.h, FreeSpace.cpp (builtin list, title fname, version " D"), ManagePilot.cpp |
| 5 | **Pools and limits (Q/Q!).** Arrays keep retail sizes. Under D-20261017-224829 and Tom's decision (section 6, item 3) the demo runs at retail limits, so these `#ifdef` arms go. Q!: objects 300, weapons 100, sexp nodes 1600, wings 15, arrivals 45/10, collision pairs 3000, ship subobjects 360, subsystem status 125, debris 48. Q: particles 500, heads 1/1. `NUM_MEDALS` becomes 18 storage with a run-time count (pilot files). | Object.h, Weapon.h, SEXP.H, Ship.h, MissionParse.h, ObjCollide.cpp, Ship.cpp, Debris.h, Particle.cpp, MissionMessage.cpp, Scoring.h, BmpMan.h (identical arms) |
| 6 | **Asteroids (W).** The demo has no asteroid models. Asteroid.cpp is compiled always behind the `asteroids` profile field (its 18 entry points return the demo stub value when it is 0), `Asteroids_enabled` stays the retail Debug toggle, and the 13 dependent blocks became `if`. | Asteroid.cpp, FreeSpace.cpp (sound env, enable), HUDtarget.cpp x2, HUDtargetbox.cpp, CollideDebrisShip/Weapon.cpp, CollideShipShip.cpp, AiCode.cpp, Beam.cpp, MissionParse.cpp, MissionBrief.cpp (asteroid closeup), KeyControl.cpp (debug key) |
| 7 | **Menus and flow (W/X).** Tech room, campaign room, barracks medals, HUD config, load mission and quick start walls; IPX hidden; initial player select; end-of-campaign popup and `GS_EVENT_END_DEMO`; upsell screens; title screen; trailer timer. | MainHallMenu.cpp, Barracks.cpp, OptionsMenu.cpp, OptionsMenuMulti.cpp, ReadyRoom.cpp, PlayerMenu.cpp, MissionCampaign.cpp, MissionDebrief.cpp, FreeSpace.cpp (upsell, title, trailer, state flow), Joy.cpp, Mouse.cpp, MissionBrief.cpp (trailer resets) |
| 8 | **Mission UI, HUD, controls (W/X/Q!).** Briefing fade (W: no `briefmap.ani` in the demo), ship and weapon anim loop frames (W), gameplay help lines, break-up particle sizes (Q, presentation). Q!: disabled shield-management keys (`Invalid_demo_keys`), F5-F12 hotkeys and the hotkey screen, artillery lock off (`ship_update_artillery_lock`), demo cheat code. Kept as shipped (section 6, item 2). | MissionBrief.cpp, MissionShipChoice.cpp, MissionWeaponChoice.cpp, GameplayHelp.cpp x4, ShipFX.cpp x3, ControlsConfig.cpp, KeyControl.cpp x3, FreeSpace.cpp (hotkey screen, show_framerate), Ship.cpp |
| 9 | **G1 build variants and the Glide/E3 block (D-066, D-070).** After batches 4 to 8 nothing compile-time remains for the demo, so the other variants are cut under D-060: `OEM_BUILD` (35 blocks, 13 files; its arms share chains with `FS2_DEMO` in batches 4 and 7, which leave them in place for this batch), `E3_BUILD` (15), `PRESS_TOUR_BUILD` (12), `PD_BUILD` (11), `MULTIPLAYER_BETA_BUILD`, `RELEASE_REAL`, `INTERPLAYQA`, `STANDALONE_ONLY_BUILD`, `MULTI_USE_LAG`, and `GERMAN_BUILD` with condition 2 of D-066 (run-time localisation untouched). Also D-070 (a): the "3DFX Glide" renderer branch in `game_init`, its `E3_BUILD` arm and then `GR_GLIDE`, with the OsRegistry "3DFX Glide" mapping where it is still present on this layer. The FS1 `DEMO` blocks (37) are held for FS1 (section 6, item 5). | FreeSpace.cpp/.h, PsTypes.h, version.h, MenuUI, Cutscenes.cpp, MissionLoad.cpp, TechMenu.cpp, ContextHelp.cpp, HUD.cpp, HUDreticle.cpp, localize.cpp/.h, MissionGoals.cpp, Scoring.cpp, Player.h, WinDebug.cpp, ShipContrails.cpp, OsRegistry.cpp |
| 10 | **Header data-set field (GodotPrep, recording version 6).** When the demo work reaches GodotPrep, the header states the data set (D-056: a recording states what it was made with); the batch 2 check stays as the fallback for headers below 6. FS1 data skips the batch 2 check until then, logging that it does: FS1's campaign and mission names are FS2's (sm1-01), so a recording below version 6 cannot name FS1 against FS2 (D-20261020-103504). This batch removes that skip. | Replay.cpp (GodotPrep) |

### Finished batches

Batches 1, 2 and 3 and the work after batch 3 are done; their records are in `docs/fs2demo/DONE_BATCHES.md`.

### Batch 6: asteroids (D-20261023-182930)

Profile field in `GlobalIncs/DataSetProfile.h` (1 = the full game, 0 = what the `FS2_DEMO` build did):

| Field | FS2 retail | FS2 demo | FS1 retail | FS1 demo | Sites |
|---|---|---|---|---|---|
| `asteroids` | 1 | 0 | 1 | 1 | Asteroid.cpp entry points and the 13 dependent blocks below |

Asteroid.cpp is compiled on every data set: its `#ifndef FS2_DEMO`, the `#else` arm of demo stubs and the
`#endif` are gone, and each of the 18 entry points the stubs covered opens with one early return of the stub's
value when the field is 0 (`asteroid_init`, `asteroid_level_init`, `asteroid_level_close`, `asteroid_create_all`,
`asteroid_render`, `asteroid_delete`, `asteroid_process_pre`, `asteroid_process_post`, `asteroid_hit`,
`asteroid_show_brackets`, `asteroid_target_closest_danger`, `asteroid_page_in`, `asteroid_sub_create`,
`asteroid_frame`: return; `asteroid_check_collision`, `asteroid_count`, `asteroid_collide_objnum`: return 0;
`asteroid_time_to_impact`: return 0.0f). The 19th stub, `asteroid_save_restore`, had no retail body, declaration
or caller, so it simply went.

The 13 dependent `#ifndef FS2_DEMO` blocks (11 files) read the field at run time: FreeSpace.cpp (sound
environment's `Num_asteroids` term, inside a commented-out block; `Asteroids_enabled = 1` compiled always),
HUDtarget.cpp x2 (closest-target search; `OBJ_ASTEROID` case body), HUDtargetbox.cpp
(`hud_render_target_asteroid`, early return), CollideDebrisShip.cpp (`collide_asteroid_ship`, early return 0),
CollideDebrisWeapon.cpp (`collide_asteroid_weapon`, early return 0), CollideShipShip.cpp
(`check_special_cruiser_asteroid_collision`, early return 0), AiCode.cpp (`Asteroid_obj_list` loop in the
target search), Beam.cpp (`OBJ_ASTEROID` case body; 0 falls through to the default as the demo build with no
case), MissionParse.cpp (`parse_asteroid_fields`, early return), MissionBrief.cpp (`ICON_ASTEROID_FIELD`
closeup), KeyControl.cpp (debug key U, asteroid field toggle). Blocks inside a `switch` keep the case label and
wrap the body, since a label cannot sit inside an `if`.

`Asteroids_enabled` stays the retail Debug toggle (never set from the profile) and has no effect in demo mode,
because the field's early returns come before every read of it.

Sites the run-time switcher re-runs (D-20261019-135406 rule 2): `asteroid_init`, the asteroid.tbl parse at
start-up, which returns at once when the field is 0 and so never loaded the table for a demo start; a switch
to a data set with asteroids must re-run it (D-20261020-161656 change 3: table load re-run, noted, not solved).

Each commit passed the per-commit check (`commit_check_demo.ps1`): retail Release and Debug rebuilds, retail
quick suite 13 of 14 (AutotestAI Debug's known warnings, #78), FS2_DEMO build, 3 demo recordings, tools check.

### Batch 7: menus and flow (D-20261021-071013, D-20261021-071014)

Profile fields in `GlobalIncs/DataSetProfile.h`, each read where the `FS2_DEMO` build read it:

| Field | FS2 retail | FS2 demo | FS1 retail | FS1 demo | Sites |
|---|---|---|---|---|---|
| `full_version_screens` | `FVS_ALL` | 0 | `FVS_ALL` | `FVS_ALL` (placeholder) | bits below |
| `always_pilot_select` | 0 | 1 | 0 | 0 | `GS_EVENT_GAME_INIT` (FreeSpace.cpp) |
| `demo_campaign_end` | 0 | 1 | 0 | 0 | campaign end (MissionCampaign.cpp, MissionDebrief.cpp), ready room replay offer |
| `upsell_screens` | 0 | 1 | 0 | 0 | end of demo campaign and exit (FreeSpace.cpp) |
| `trailer_timer` | 0 | 1 | 0 | 0 | timer resets in Joy.cpp, Mouse.cpp, MissionBrief.cpp, game_poll |

`full_version_screens` bits (set: the screen opens as in the full game; clear: what the demo build did):

| Bit | Value | Site | Clear | FS2 retail | FS2 demo | FS1 retail | FS1 demo |
|---|---|---|---|---|---|---|---|
| `FVS_TECH_ROOM` | 1 | main hall tech room | not-in-demo popup | 1 | 0 | 1 | 0 |
| `FVS_CAMPAIGN_ROOM` | 2 | main hall campaign room | `fs2_demo_walls` 1: restart-campaign popup (FS2 demo); 0: not-in-demo popup (FS1 demo) | 1 | 0 | 1 | 0 |
| `FVS_LOAD_MISSION` | 4 | main hall load mission key | nothing | 1 | 0 | 1 | 1 |
| `FVS_QUICK_START` | 8 | main hall quick start, Debug only | nothing | 1 | 0 | 1 | 1 |
| `FVS_MEDALS` | 16 | barracks medals button | not-in-demo popup | 1 | 0 | 1 | 0 |
| `FVS_HUD_CONFIG` | 32 | options HUD config button | not-in-demo popup | 1 | 0 | 1 | 1 |
| `FVS_IPX` | 64 | multi options IPX button | hidden and ignored | 1 | 0 | 1 | 1 |
| `FVS_HOTKEYS` | 128 | F3 hotkey screen and its help line | `fs2_demo_walls` 1: nothing, no help line (FS2 demo); 0: not-in-demo popup, help line kept (FS1 demo) | 1 | 0 | 1 | 0 |
| `FVS_MULTIPLAYER` | 256 | main hall multiplayer region | not-in-demo popup | 1 | 1 | 1 | 0 |
| `FVS_OBJECTIVES` | 512 | in-mission objectives key | not-in-demo popup | 1 | 1 | 1 | 0 |
| `FVS_OPTIONS_MULTI` | 1024 | options multiplayer tab | not-in-demo popup | 1 | 1 | 1 | 0 |
| `FVS_DEBRIEF_MEDALS` | 2048 | debriefing medals button | not-in-demo popup | 1 | 1 | 1 | 0 |
| `FVS_CUTSCENES` | 4096 | Simulator room, Credits and Tech room Cutscenes buttons | not-in-demo popup (#87) | 1 | 0 | 1 | 1 |

`FVS_ALL` is `(1<<13) - 1`. Bits 8-11 are FS1Compat's, numbered at the first Enhanced merge; `FVS_CUTSCENES`
(FS2Demo, #87) takes bit 12 above them, and a child's new bit takes the first bit above Enhanced's `FVS_ALL`
(D-20261022-161946). The FS1 demo row sets `FVS_CUTSCENES`: it walls only the FS2 demo, and FS1Compat ran
retail code at those sites (D-20261021-142001 rule 5). No code, recording, pilot or save stores the numbers.

FS1Demo's `full_version_screens` (D-20261020-214042), one 1/0 wall over the FS1 demo's missing screens, was
joined into these bits at the FS2Demo merge into Enhanced (D-20261021-142001): FS1 walls with an FS2 bit use it,
the rest got bits 256 up. A bit means only "walled"; where the two demos' walled branches differ, `fs2_demo_walls`
picks the branch. `docs/FS1_DEMO.md` lists the FS1 sites.

Sites the run-time switcher re-runs (D-20261019-135406), since they are read once: the pilot-screen choice
at `GS_EVENT_GAME_INIT` and the IPX button hidden when the multi options screen is created. The upsell
screens are read on exit, after any switch. Kept as they were: the replay and autotest pilot load at start-up
(D-20261018-184854) and the sales-screen skip (D-20261018-221703). Held: the PlayerMenu demo title, dead in
both builds, for the D-060 pass; the `OEM_BUILD` and `MULTIPLAYER_BETA_BUILD` arms, for batch 9.

The Options H hotkey reads `FVS_HUD_CONFIG` as the button does (D-20261021-195004). **Open check:** the FS2
demo menu tour now opens HUD Config through the player's entry (D-20261021-201402) and passes it, but stops at
Medals: that stop still posts the state event, bypasses the Barracks wall and hits a missing-bitmap Error (#85). It
stays open until every walled screen in the tour opens through the player's entry.

**Unreachable screens** (D-20261022-090902). A demo bug on a screen that a demo player cannot reach gets no
fix, and the menu tour SKIPs that screen: a stop whose host screen ended in SKIP logs "SKIP not in demo (host
Tech room)". Tom: "OriginalBugFixes should be minimal to reduce risk to FS2. Fixing bugs that can't be reached
in UI for FS2 demo in particular doesn't cross that threshold." So #87, the demo Cutscenes wall, is unreachable
in the shipped demo (the Tech room is walled) and stays as a latent guard; #88, the demo Credits crash, is
likewise kept, with no fix. #91, the Hotkeys stop, is the same case: the shipped demo has no Hotkeys screen (no art, no help line), so the tour SKIPs it through the F3 wall's signal rather than a tour-side list (D-20261022-144601, D-20261022-150211).

### Batch 8: mission UI, HUD, controls (D-20261021-100301)

Profile fields in `GlobalIncs/DataSetProfile.h` (1 = the full game, 0 = what the `FS2_DEMO` build did):

| Field | FS2 retail | FS2 demo | FS1 retail | FS1 demo | Sites |
|---|---|---|---|---|---|
| `brief_fade` | 1 | 0 | 1 | 1 | briefing static fade (`briefmap.ani`) and ship select button flash (MissionBrief.cpp); FS1 demo 1 with FS1's `brief_fade_anim` 0, which keeps the static sound and draws nothing (D-20261021-142002) |
| `loadout_anim_loop` | 1 | 0 | 1 | 1 | ship and weapon select loop frames 51 / 52, demo 0 / 1 (MissionShipChoice.cpp, MissionWeaponChoice.cpp) |
| `hotkeys` | 1 | 0 | 1 | 1 | F5-F12 target hotkeys (KeyControl.cpp) and their help lines (GameplayHelp.cpp) |
| `artillery_lock` | 1 | 0 | 1 | 1 | `ship_update_artillery_lock` (Ship.cpp) |
| `cheat_code` | 1 | 0 | 1 | 1 | full game's cheat codes, or the demo's one (KeyControl.cpp) |
| `breakup_fx` | 1 | 0 | 1 | 1 | ship break-up particle range, life and scale (ShipFX.cpp); affects game logic (D-20261021-110833) |

`full_version_screens` gains `FVS_HOTKEYS` (128): the F3 hotkey screen (FreeSpace.cpp) and its help line;
clear, F3 does nothing for the player; under the autotest its refusal branch calls `autotest_not_in_demo()` so the menu tour logs the Hotkeys stop and its help view "SKIP not in demo" (#91, D-20261022-144601). `FVS_ALL` is now `(1<<13) - 1`, with `FVS_CUTSCENES` at bit 12 (D-20261022-161946).

No field: the demo key check in ControlsConfig.cpp is compiled always, as the shipped demo's list was empty
(D-20261021-100517); the `show_framerate` console command is compiled in Release on every data set.

Each commit passed the per-commit check (`commit_check_demo.ps1`): retail Release and Debug rebuilds, retail
quick suite 13 of 14 (AutotestAI Debug's known warnings, #78), FS2_DEMO build, 3 demo recordings, tools check.

## 6. Tom's decisions (2026-10-18)

The open questions from the first version of this brief, as Tom answered them.

1. **Distributing the demo data.** Only our files are committed: recordings, traces, scripts and ini. The
   demo's `readme.txt` licence (lines 886-901) forbids copying or distributing it, so the VPs never go in
   the repo. Nothing downloads it either (Tom, 2026-10-19, D-20261019-204553, replacing the CI download of
   the first answer): each PC keeps a known local copy, named in its machine profile.
   - `FS2DEMO_PATH`: the demo install, or the folder `tools/demo_install.py` made from the local
     installer. Only ever read.
   - `FS2DEMO_TEST`: the test folder, a copy of its VPs made by `tools/make_demo_test.ps1`.
     `tools/build_demo.ps1` builds the `FS2_DEMO` exe into it and `tools/replay_check.ps1` replays
     `recordings/demo` there (D-20261019-213450). Unset, the demo lines are SKIP, which no merge-up or
     branch-done claim may rest on.

   Hosted CI could therefore only build; a self-hosted runner on Tom's PCs is a later decision of his.
2. **Demo restrictions that change play: kept as shipped.** The disabled shield-management keys, the
   F5-F12 hotkeys and hotkey screen, and the artillery lock stay off in demo mode, as demo experience
   (batch 8).
3. **Demo limits: retail values.** Every pool and cap in batch 5, logic-affecting ones included, runs
   at the retail size in demo mode.
4. **Original FS2Demo.exe as a reference run: not now.** It may become one later, if needed. It would
   need DDrawCompat in the demo folder and its registry settings (already present, virtualised).
5. **The FS1 `DEMO` macro blocks: held** for the FS1 decision (D-064). Batch 9 leaves them alone.

## 7. Open: the demo's second title screen (Tom, 2026-10-21)

The FS2 demo has two title screens. The first is `display_title_screen()` in `FreeSpace.cpp`, drawn
during `game_init` from the data set profile's `title_screen`. The second is in `PlayerMenu.cpp`
(`#ifdef FS2_DEMO`): `DemoTitle1` loaded in `player_select_init` and held for 5 seconds by
`demo_title_blit()`. Volition commented out its only call (`// demo_title_blit();`), so it never shows.
Still to decide whether to bring it back, and how it relates to the data set profile's title screen.

## Files

- `tools/demo_install.py`: the installer replacement (section 1).
- `tools/make_demo_test.ps1`, `tools/build_demo.ps1`, `tools/fs2demo.props`: the demo test folder and the
  `FS2_DEMO` build into it (section 6, item 1).
- `docs/DEMO_CODE.md` and `tools/demo_blocks.py` on PrePortCleanup: the block inventory this plan
  classifies.
