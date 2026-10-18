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

### First runs of project builds on the demo data (2026-10-18)

Debug builds of FS2Demo (`d4376b0`), both the `FS2_DEMO` build and the retail build, ran under
autotest (`-window -autotest`) in `C:\Games\Freespace 2 Demo`. The setup followed
`docs/AUTOTEST.md`, with the exes copied in and `fs2.ini` created on first start. To get past each
stop and find the next one, two scratch aids were used, and neither was committed:
- loose converted copies of three demo tables in `Data\tables`: `medals.tbl` with `$Wavefile Base:`,
  an empty `weapon_expl.tbl` (`#Start` `#End`), and `mainhall.tbl` with `+Music: None`;
- three `// DIAG` lines:
  - let the `FS2_DEMO` `GS_EVENT_GAME_INIT` arm load the autotest pilot;
  - make a scratch pilot when none exists;
  - name `Int3`'s callers.

Afterwards the tables, exes, logs and scratch pilots were moved out of the demo folder and the DIAG
lines reverted.

What happened, in order:

1. **Unmodified data, `FS2_DEMO` build:** `Error` at start-up. First `medals.tbl` ("Required token
   `$Wavefile Base:`"). With that converted, `weapon_expl.tbl` (code 5, file missing). With that
   supplied, the game started and reached the pilot screen.
2. **The pilot screen deleted Tom's demo pilot.** The demo exe writes pilot file version 135.
   `LOWEST_COMPATIBLE_PLAYER_FILE_VERSION` is `CURRENT_PLAYER_FILE_VERSION` (140) in both builds, and
   `ManagePilot.cpp` calls `delete_pilot_file` on any older pilot. That removed `Tom.plr` and
   `Tom.demo.csg` with `_unlink` (not recoverable). `Tom.demo.css` survived. The players folder is now
   backed up before every run, and runs use a scratch pilot of our own.
3. **The `FS2_DEMO` build never reaches autotest's pilot load.** Its `GS_EVENT_GAME_INIT` arm
   (`FS2_DEMO || OEM_BUILD`) always goes to the pilot screen, where the retail arm calls
   `replay_load_pilot`/`autotest_load_pilot`. The watchdog stopped it there (state 38).
4. **New pilot, `FS2_DEMO` build:** `hud_1.hcf` (the demo's HUD colours) has 37 gauges where the code
   reads `NUM_HUD_GAUGES` (39). The parse aborts with a warning per missing gauge, and the last two
   gauges keep their defaults.
5. **Main hall:** the missing `+Music:` aborts the parse. `main_hall_read_table` has no `setjmp` of its
   own, so the abort jumps into a stale `parse_abort` buffer and the process died (exit 127, no log
   line).
6. **With the three converted tables, `FS2_DEMO` build:** all three campaign missions load and play
   with no `Assert`, `Int3`, `Warning` or `Error`. That covers `SPDemo-01` and `SPDemo-02` (120 s each,
   events firing to mission time 120) and `DemoTrain` (120 s). Autotest plays them in the built-in list
   order (SPDemo-01, SPDemo-02, DemoTrain), not the order in `demo.fc2`. After the last mission the game
   hangs in `GS_STATE_QUIT_GAME`: the demo upsell screens wait for a key press, and the watchdog ended
   the run.
7. **Retail build, same converted tables and a retail-format scratch pilot:** all three missions play.
   There are two reports each run:
   - `Assert num_medals == NUM_MEDALS` (16 vs 18, `Medals.cpp`);
   - one `Int3` in `hud_anim_load` from `brief_init` (`MissionBrief.cpp`), because the demo has no
     `briefmap.ani` (the briefing fade wall, batch 8).

   With `-at_mission`, the retail build exits normally.

So the static list held. The run added the pilot deletion, the short `hud_1.hcf`, the missing `setjmp`
in the main hall parse, and two autotest gaps in the `FS2_DEMO` flow (pilot load, upsell wait).

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
| 6 | **Asteroids (W).** The demo has no asteroid models. Asteroid.cpp is compiled always, `Asteroids_enabled` follows the flag, and the 13 dependent blocks become `if`. | Asteroid.cpp, FreeSpace.cpp (sound env, enable), HUDtarget.cpp x2, HUDtargetbox.cpp, CollideDebrisShip/Weapon.cpp, CollideShipShip.cpp, AiCode.cpp, Beam.cpp, MissionParse.cpp, MissionBrief.cpp (asteroid closeup), KeyControl.cpp (debug key) |
| 7 | **Menus and flow (W/X).** Tech room, campaign room, barracks medals, HUD config, load mission and quick start walls; IPX hidden; initial player select; end-of-campaign popup and `GS_EVENT_END_DEMO`; upsell screens; title screen; trailer timer. | MainHallMenu.cpp, Barracks.cpp, OptionsMenu.cpp, OptionsMenuMulti.cpp, ReadyRoom.cpp, PlayerMenu.cpp, MissionCampaign.cpp, MissionDebrief.cpp, FreeSpace.cpp (upsell, title, trailer, state flow), Joy.cpp, Mouse.cpp, MissionBrief.cpp (trailer resets) |
| 8 | **Mission UI, HUD, controls (W/X/Q!).** Briefing fade (W: no `briefmap.ani` in the demo), ship and weapon anim loop frames (W), gameplay help lines, break-up particle sizes (Q, presentation). Q!: disabled shield-management keys (`Invalid_demo_keys`), F5-F12 hotkeys and the hotkey screen, artillery lock off (`ship_update_artillery_lock`), demo cheat code. Kept as shipped (section 6, item 2). | MissionBrief.cpp, MissionShipChoice.cpp, MissionWeaponChoice.cpp, GameplayHelp.cpp x4, ShipFX.cpp x3, ControlsConfig.cpp, KeyControl.cpp x3, FreeSpace.cpp (hotkey screen, show_framerate), Ship.cpp |
| 9 | **G1 build variants and the Glide/E3 block (D-066, D-070).** After batches 4 to 8 nothing compile-time remains for the demo, so the other variants are cut under D-060: `OEM_BUILD` (35 blocks, 13 files; its arms share chains with `FS2_DEMO` in batches 4 and 7, which leave them in place for this batch), `E3_BUILD` (15), `PRESS_TOUR_BUILD` (12), `PD_BUILD` (11), `MULTIPLAYER_BETA_BUILD`, `RELEASE_REAL`, `INTERPLAYQA`, `STANDALONE_ONLY_BUILD`, `MULTI_USE_LAG`, and `GERMAN_BUILD` with condition 2 of D-066 (run-time localisation untouched). Also D-070 (a): the "3DFX Glide" renderer branch in `game_init`, its `E3_BUILD` arm and then `GR_GLIDE`, with the OsRegistry "3DFX Glide" mapping where it is still present on this layer. The FS1 `DEMO` blocks (37) are held for FS1 (section 6, item 5). | FreeSpace.cpp/.h, PsTypes.h, version.h, MenuUI, Cutscenes.cpp, MissionLoad.cpp, TechMenu.cpp, ContextHelp.cpp, HUD.cpp, HUDreticle.cpp, localize.cpp/.h, MissionGoals.cpp, Scoring.cpp, Player.h, WinDebug.cpp, ShipContrails.cpp, OsRegistry.cpp |
| 10 | **Header data-set field (GodotPrep, recording version 6).** When the demo work reaches GodotPrep, the header states the data set (D-056: a recording states what it was made with); the batch 2 check stays as the fallback for headers below 6. | Replay.cpp (GodotPrep) |

### Batch 1: old table and pilot loading (done)

Approved as fixes by D-20261018-154908 and committed one item per commit, each with its review in
the message. The proposal as approved is below. What the runs found besides it:
- **Weapon impact explosions.** Demo weapons name impact explosions (`ExpMissileHit1`, `Exp20`,
  `exp06`; 25 of 47 weapons). With no `weapon_expl.tbl` they take the existing "none" path and play no
  impact animation. The shipped demo exe probably loaded them by name. This is a presentation gap for
  a later data-handling item. Batch 1 adds no new fallback (condition 2).
- **Install path length.** An install in a deep folder (a scratch copy under the session scratchpad,
  about 110 characters) crashed at start-up in both the fixed and the unfixed build, before any table
  was read. That is the source's 128-character `MAX_PATH_LEN`. Keep the demo install, and any CI copy
  of it, in a short path.
- **Building both variants in one checkout.** `code.lib` is written to a fixed `.\<Config>\code.lib`
  whatever `IntDir` says. A demo build and a retail build share it, and an incremental build can link
  the other variant's library. Defining `FS2_DEMO` through the `CL` environment variable is not tracked
  by MSBuild either. Reliable recipe:
  - a props file adding `FS2_DEMO` to `ClCompile` `PreprocessorDefinitions`, passed with
    `/p:ForceImportBeforeCppTargets=`;
  - separate `IntDir`/`OutDir` per variant, `/t:Rebuild` and `/nodeReuse:false`;
  - `code.lib` deleted before each build.

  Check the result: only the demo exe contains the string "Freespace 2 Demo".
- **Pre-existing, not batch 1.** In the Debug build of `cc1a303` without the batch, the `AutotestAI`
  replay reports two "Null vector in vector normalize" Warnings (exit 2). Its trace still matches.

Proposal as approved:

Goal: with the unmodified demo install, both builds load every demo table and the demo pilot screen
without an `Error`, `Assert`, `Int3` or deleted file. On retail data nothing changes: every retail
recording replays unchanged. All of it is original code outside `FS2_DEMO` blocks, so each item gets a
fix-or-keep review.

1. **Never delete an older pilot** (`ManagePilot.cpp`: the invalid-signature and too-old paths in the
   pilot list reader and `read_pilot_file`). An unreadable or older pilot is skipped and logged, and
   its files are left alone. The same goes for the "Campaign save file too old ... Deleting" paths in
   `MissionCampaign.cpp`. Reading demo pilots (version 135; `read_stats_block` already has a branch
   for them) is not in this batch.
2. **`weapon_expl.tbl` optional** (`Weapons.cpp`): if the file does not exist, there are no weapon
   explosion entries (`Num_weapon_expl = 0`), as if the table were empty. A file that exists and fails
   to parse is still an `Error`.
3. **`mainhall.tbl` `+Music:` optional** (`MainHallMenu.cpp`): if it is absent, the music name is empty
   and the main hall plays no music (`main_hall_start_music` already handles an unknown name). Give
   `main_hall_read_table` its own `setjmp` with an `Error`, as the other table readers have, so a parse
   error is reported instead of jumping into a stale buffer.
4. **`medals.tbl` old badge sound fields** (`Medals.cpp`): accept `$Wavefile 1:` and `$Wavefile 2:` in
   place of `$Wavefile Base:`, taking the base name from `$Wavefile 1:` without its `1_` prefix. The
   demo has no badge voice files, so only the parse matters. **Medal count:** keep the arrays at
   `NUM_MEDALS` and add a run-time count from the table. The `Assert` becomes count <= `NUM_MEDALS`, and
   the unused slots stay empty. Pilot files still store `NUM_MEDALS` values.
5. **Short `.hcf` files** (`HUDconfig.cpp`, `hud_config_color_load`): read gauges until the file ends,
   at most `NUM_HUD_GAUGES`. The rest keep their defaults, and no warning is given.

Not in batch 1, recorded for later batches:
- the briefing fade `Int3` (batch 8 wall);
- the `FS2_DEMO` `GAME_INIT` arm bypassing the autotest and replay pilot load (batch 7, flow);
- autotest getting past the upsell screens (batch 7, experience);
- autotest's campaign order.

Checks:
- Retail build: quick suite (14 custom, `sm1-01`, `sm3-06`) with traces unchanged; `tools_check`.
- Demo, `FS2_DEMO` Debug: the three campaign missions under autotest on the unmodified demo install,
  with no `Error`, `Assert`, `Warning` or `Int3` from tables.
- Retail Debug build on the demo install: the same three missions, with only the briefing `Int3` left.
- The players folder is backed up before each run, with a scratch pilot only.
- One commit per item, each with its review in the message.

The rest of D-062's order still comes first (D-063 item 3). This branch merges into `Enhanced` when green
(D-20261017-223742).

### Batch 2: detection flag (done)

D-20261018-175706. `game_detect_data_set` runs straight after `cfile_init` and logs
`data set: demo|retail (demo.fc2: y/n, freespace2.fc2: y/n)` to the debug log and `autotest.log`. A folder
with both files runs as retail with one Warning. `Game_data_set` holds the result; no `FS2_DEMO` block reads it
yet (batches 4 to 8).

- **Probe (condition 1):** all 46 checked-in recordings made under `-at_mission` have an empty campaign
  field, so the replay falls back to the mission name. The demo's VPs hold six mission and campaign files
  (`SPDemo-01`, `SPDemo-02`, `DemoTrain`, `Demo`, `MPDemo-01`, `Demo-DOG-01`); none of the 92 in the retail
  VPs shares a name with them.
- **Replay check:** a recording is demo if its campaign is `demo` or its mission is one of those six, retail
  if its campaign is `freespace2` or its mission is a Volition mission in this build's built-in list,
  otherwise unknown (our own test missions), which is logged and plays on. A mismatch fails the replay with
  a log line naming both sets, exit 4, no dialog under autotest. Until batch 4 makes the built-in list
  run-time, the `FS2_DEMO` build cannot name a retail recording, so it logs "unknown" for one.
- **Found:** the retail build crashes reading a pilot made by the `FS2_DEMO` build (Debug: `Assert: len < n`
  in `cfread_string_len`, then an access violation). The two builds' pilot files differ (medal count among
  others). It goes away with one exe; until then a demo-build pilot must stay out of a retail build's
  players folder. Checks therefore ran in a scratch copy of the demo VPs (`C:\Games\FS2D_scratch`) with a
  retail-format pilot.

### Batch 3: demo suite recorded (done)

- **Reference configuration:** the demo has the 1024 interface art (854 `2_` interface files against
  retail's 1,278, including `2_MainHall1`), so the suite runs in retail's reference configuration:
  1024x768, GR_1024, medium skill (`-at_skill 2`), English, LowMem off, data set demo.
- **Recordings:** `SPDemo-01`, `SPDemo-02` and `DemoTrain`, 120 s of mission time each at 4x with
  `-playernpc` (3540, 3414 and 3543 frames). Recorded with the `FS2_DEMO` Release build from a scratch
  copy of the demo VPs (`C:\Games\FS2D_scratch`), never the demo install, with a copy of a pilot made by the
  `FS2_DEMO` build (`TomDemo`; the recording carries it). Every run goes through the demo's own start-up
  arm and its demo tables. No Assert, Error, Int3 or Warning in Release or Debug.
- **Release and Debug identical:** the first recordings desynced in Debug. A hardware watchpoint traced it
  to `stars_page_in` paging in the missing `SunGlow01` (handle -1), which wrote over MissionParse's
  arrival-message timers in the Release layout (#25, review 50). Recorded again from the fixed build:
  30 of 30 replays match (5 per mission in each build). Two earlier drifts, while the PC was in use, did
  not come back.
- **Not yet:** autotest still waits on the demo's sales screens after each mission until the watchdog ends
  it (exit 5, about 2 minutes a run); getting past them is batch 7. Suite scripts and CI are a later batch.
- **Build trap, again:** a demo build with its own `IntDir` still rebuilt `code`'s objects in the
  checkout's default folder, so the tools then linked a demo `code.lib`. Build retail last, with
  `/t:Rebuild`.

## 6. Tom's decisions (2026-10-18)

The open questions from the first version of this brief, as Tom answered them.

1. **Distributing the demo data: (a).** Only our files are committed: recordings, traces, scripts and
   ini. Public CI downloads the demo installer and installs it with `tools/demo_install.py`. The demo's
   `readme.txt` licence (lines 886-901) forbids copying or distributing it, so the VPs never go in the
   repo.
2. **Demo restrictions that change play: kept as shipped.** The disabled shield-management keys, the
   F5-F12 hotkeys and hotkey screen, and the artillery lock stay off in demo mode, as demo experience
   (batch 8).
3. **Demo limits: retail values.** Every pool and cap in batch 5, logic-affecting ones included, runs
   at the retail size in demo mode.
4. **Original FS2Demo.exe as a reference run: not now.** It may become one later, if needed. It would
   need DDrawCompat in the demo folder and its registry settings (already present, virtualised).
5. **The FS1 `DEMO` macro blocks: held** for the FS1 decision (D-064). Batch 9 leaves them alone.

## Files

- `tools/demo_install.py`: the installer replacement (section 1).
- `docs/DEMO_CODE.md` and `tools/demo_blocks.py` on PrePortCleanup: the block inventory this plan
  classifies.
