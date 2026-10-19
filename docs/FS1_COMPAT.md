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

### Progress (2026-10-18)
Each piece was its own branch off `FS1Compat`. Each is merged only after the FS2 replay suite passed with it: 47 of 47 recordings, with Debug and Release traces identical.

| Branch | Batch | What it does |
|---|---|---|
| `FS1Detect` | 1 | `Cfile_fs1_data` is 1 when a pack file `Data\freespace.vp` exists (FS1's main archive; FS2 has none). Everything below checks it |
| `OldDecrypt` | 3 | `unencrypt()` picks the decoder by signature, so FS1's `0xDEADBEEF` tables and missions decode. Checked on all 145 FS1 files against `tools\fs1_decrypt.ps1` |
| `FS1Models` | 6 | FS1's `OHDR`/`SOBJ` chunks read in FS1 field order. Checked on all 103 FS1 models |
| `FS1FileSystem` | 1 | `.fsm`/`.fsc` indexed; `x.fs2` finds `x.fsm`; plural voice folders mapped; stand-ins for files FS1 lacks (`font03.vf`, `debris02.pof`) |
| `FS1MissingTables` | 2 | `Parse/Fs1Tables.cpp` supplies text for the 11 tables FS1 lacks that the game reads. The tips popup returns when there are no tips |
| `FS1TableConvert` | 4 | FS1 tables rewritten while loading: medals, sounds (flyby section), music (14 patterns cut to 12), weapons (trails, swarm), ships (default ship, engine wash), asteroid, and `/* */` comments |
| `FS1Backgrounds` | 7 | FS1's old-style nebula draws again (FS2 had switched it off), coloured from entries 0-19 of `gamepalette1-NN.pcx`. FS1's `$Bitmap: Sun01` backgrounds become `$Sun` (angles from the transposed orientation matrix, scale 20 / distance); `$Light: 0` suns give no light. Size and placement follow FS1's old formulas, not a side-by-side check |
| `FS1BriefingScreen` | 9 (start) | In FS1 data, `gr_bitmap` skips a bitmap that did not load: FS2's interface overlays FS1 lacks (`Briefwin`, `InfoBox`), whose frames FS1's screen backgrounds already hold. Positions are still FS2's |
| `FS1DebugNoise` | 4 | Debug runs clean. `icons.tbl`: each FS1 set is written three times, once per species. The context-help bracket checks (the two start-up Int3s) are skipped in FS1 data. `$Weapons Precedence` drops names the table does not define ("Disruptor Missile"). Converter state is reset for each file |
| `FS1TurretPoints` | 6 | The turret firing-point Assert allows `MAX_TFP` (4), the size of the array; it stopped at 3. FS2's models never have 4; FS1's `freighter03.pof` has a turret with 4 |
| `FS1Recordings` | - | 25 FS1 recordings, see below |
| `FS1BriefingArt` | 9 | Briefing screen in FS1's layout. `BriefTrans.ani` plays when it opens (FS2 had commented this out), and its last frame, the only place FS1 keeps the map window and text box, becomes the background. Button positions come from matching each button's frames against that frame (mean colour error under 3 of 255); the map window, text box, info box and close-up popup are measured from the art. The stage text and title positions are estimates above the map frame. No drawn labels on the common and briefing buttons, since FS1's art has them; the common buttons use the same positions on the ship and weapon screens. Clicks on FS1's VCR controls, which sit inside the map window, do not pick icons. The scene-cut static is clipped to the window |
| `FS1MainHall` | 9 | The stand-in `mainhall.tbl` gives each FS1 hall its six door anims and its ambient anims (2 and 4), at positions found by matching each anim's first frame against the hall background (exact for every door); each door goes with the mask region it covers. FS1's door and intercom sounds; the ambient anims are silent. Fixes the main hall crash: with no door anims, lingering over a door read a missing anim instance in `main_hall_mouse_grab_region` (now guarded). Checked by hovering all six regions of hall 1 in Debug |
| `FS1ShipWeaponScreens` | 9 | Ship select and weapon loadout use Brief's background (FS1 has no `ShipSelect` or `WeaponLoadout`). FS1's ship anims (633x224) and weapon anims (244x214) carry the name, stats and description in the art; they are placed by matching their first frame against that background, and FS2's drawn ship info and weapon description are skipped. Ship anims loop from frame 37, FS1's value. The weapon screen's secondary list scrolls with `SSB_08/09` (FS1 has no `WLB_08/09`). The ship screen's scroll buttons and both reset buttons are placed by FS1's masks: they were cut from a background FS1 does not ship, so they could not be matched |
| `FS1Labels` | 9 | FS1's interface art has its labels painted in, so in FS1 data `UI_WINDOW::add_XSTR` adds none and FS2's labels no longer sit a few pixels off FS1's on every screen. Where a label still shows twice, it is FS1's button art drawn at FS2's position over the copy in the background |

**FS1 missions play** (`-autotest`, Release, FS1 install's `fs2.ini` set to 640x480):
- `btm-01`, the first training mission: 400 s of mission time, PASS with no Assert, Int3 or warning. Its later events wait on the player following instructions, so they do not fire under autotest.
- `sm1-01`, the first combat mission: the waves are destroyed, both goals ("Eliminate Hostiles", "Protect the Orff") complete, "Mission End" at 202 s. PASS with no Assert, Int3 or warning.

In Debug, `sm1-01`, `btm-01` and `btm-02` (60 s each) PASS with no Assert, Int3 or warning.

These runs did not use `-playernpc`, so nobody flew the player ship.

**FS1 recordings** (`recordings\fs1`): the main campaign (`freespace.fsc`) without the 5 training missions (`btm-*`) and the 4 that start in red alert (`sm2-06a`, `sm2-10a`, `sm3-05a`, `sm3-09a`; replay does not support red alert). 120 s each, the AI flying the player. Made with `tools\record_autotest.ps1 -Fs1`; `tools\replay_check.ps1` replays them in `PATH_FS1` with the exes found there, after the FS2 recordings.
- All 25 replay with identical traces in Release and Debug, with no Assert, Int3 or warning. The whole suite, 47 FS2 and 25 FS1 recordings, passed 72 of 72 in 9 minutes.
- Once, the Release replay of `sm3-06a` desynced at frame 3320 of 3544 (the run asked for `mouse_pos` where the recording has `timer_ms`). Two reruns matched. Not explained yet.
- The exes must be in the FS1 install. On `FS1Compat` the build puts them there; elsewhere they have to be copied in.
- Re-recorded for `FS1BriefingArt`: with the briefing's transition anim, 6 of the old recordings desynced on sound calls mid-mission, the same way every time, while all 47 FS2 recordings matched. The cause is in the original code: `message_queue_process` (`MissionMessage.cpp:1268`) keeps a message's head anim instance pointer after the anim ends and its instance goes back to the free list, so `anim_playing()` is true again once another anim reuses that instance. Message timing therefore depends on which anim instances earlier screens used. Not fixed (a candidate for `OriginalSourceBugFixes`).

**Known, not yet handled:**
- Music: FS1's chained normal patterns are cut for now (batch 5 brings them back).
- Medals: 16 to FS2's 18; slots 16 and 17 stay empty. Badge voices are `<persona>_badge_x`, and FS2's default persona 9 has no FS1 file.
- Asteroids: 3 types to FS2's 12; FS1 has no debris fields.
- Icons: FS1 has 27 briefing icon types to FS2's 35; the last 8 get no FS1 set.
- Species: FS1's `Species.tbl` is a different file; only the tech room reads it. Not converted yet.
- Backgrounds: no starfield bitmaps besides the sun; FS1 missions use none.
- Resolution: FS1 needs 640 mode. The trial sets it in the ini; the interface art-mode setting (D-20261017-225737) is the real fix.
- Trials use your FS2 pilot `Tom.plr`, copied into the FS1 install's `Data\Players\Single`.

**Interface issues seen by hand (2026-10-18, Release, FS1 data):**
- Pilot select: the buttons are drawn at FS2's positions but clicked through FS1's mask, so the click areas are well off the art. FS2's drawn labels sit over the ones in FS1's art, and "FreeSpace 2 / Copyright 1999" is drawn over FS1's logo.
- Main menu: crashed when the mouse lingered on a door. Fixed by `FS1MainHall`.
- Main menu: the region tooltips and the version label are FS2's ("Exit FreeSpace 2", "v1.20"). Hall 2's door anims have not been checked by hovering yet. The ambient anims have no sounds.
- Ship select and weapon loadout: fixed by `FS1ShipWeaponScreens`, including the mouse trail there (a screen whose background did not load is not redrawn). Other screens may still lack backgrounds.
- FS1 does have a map window bitmap, `mapwin.pcx` (549x260, the size of `BriefMap.ani`), with `MapWin01-04` as its edges and `ShipWin01-04` as a frame for the ship screen; `FS1BriefingArt` uses the last frame of `BriefTrans.ani` instead, which looks the same. `ssbutton01-03.ani` animate the selected screen in the top-left common buttons; not played yet.
- The original FS1 (`FS.exe`, GOG) does not run on this machine (Windows 11): it hangs on a yellow screen, so there are no side-by-side comparison shots.

**Interface survey (2026-10-19, Debug, FS1 data, `sm1-01`).** Every single-player screen was opened from the main hall or during the mission by a temporary autotest tour, and screenshotted. Two problems run through nearly every screen:
- **Doubled labels.** FS1's art has its labels painted in. FS2 draws its own on top (`UI_WINDOW::add_XSTR`), a few pixels off, so most labels appear twice. Fixed by `FS1Labels`.
- **Buttons at FS2's positions.** Click areas come from FS1's masks, so the art and the click areas disagree. Each screen needs its own FS1 positions, found the way the briefing's were.

| Screen | Other problems |
|---|---|
| Pilot select | "FreeSpace 2 / Copyright 1999" drawn over FS1's logo |
| Barracks | Pilot picture and squad boxes empty |
| Tech room | FS2's Intelligence tab, which FS1 lacks. Asserts on opening: `buttonHeight > 5` (`SLIDER2.cpp:83`) and a bad bitmap handle (`BmpMan.cpp:1231`); the list's slider art does not fit |
| Simulator room | Mission list empty |
| Campaign room | No campaigns listed: the list looks for FS2's `.fc2` files, FS1's are `.fsc` |
| Load mission | FS2's developer screen; works |
| Options | Sliders drawn away from FS1's tracks; a grey and white checker block where a slider bitmap is missing |
| Control config | Right-hand buttons drawn twice over, at both sets of positions |
| HUD config | The gauge preview is not drawn (only the reticle). Same slider Asserts as the tech room |
| Medals | Looks right |
| Credits | **Crashes** (heap corruption, `0xC0000374`) |
| Cutscenes | Only "Introduction" listed; movies are not played by this source anyway |
| Gameplay help | Text names FreeSpace 2 ("Quit FreeSpace 2 immediately") |
| In-game HUD | Works |
| Pause | Magenta full-screen background |
| Mission log | Works apart from the labels |
| Objectives (`GS_EVENT_SHOW_GOALS`) | Stops the game: `Objectives-m` is missing. FS2 may never open this screen |
| Hotkeys | From a mission: works apart from the labels. From the main hall: crashes (access violation); it is normally only opened from a briefing or a mission |
| Debriefing | Works apart from the labels and buttons |
- `gr_bitmap_ex` in Direct3D locks the back buffer in the middle of a frame and hung the game, so it is best avoided.

---

## 5. Branch scope

Nothing was changed outside `FS1Compat`. The branch has no upstream set, so a plain `git push` cannot land on `RecordPlayback`. Nothing has been pushed.

### Branch-local build target
On `FS1Compat`, `Freespace2.vcxproj` and `Freespace2.vcxproj.user` build into, and debug from, `PATH_FS1` instead of `PATH_FS2` (commit `cebafe2`), so trial runs load FS1 data. This must not reach Enhanced or the port.

### Before merging
1. Revert the build-target commit on `FS1Compat`: `git revert cebafe2`.
2. Run `tools\branch_check.ps1`. It reports an error for any layer, local or on origin, whose project files mention `PATH_FS1`.
3. Check FS2's menus visually, once the FS1 layout is finished: screenshot every screen on FS2 data with this branch and with `RecordPlayback`, and compare the images. The replays check gameplay only, and every FS1 layout change is meant to leave FS2 data untouched.
4. Merge into Enhanced. If FS1 work goes on afterwards, re-apply the build target with `git revert <the revert commit>`.

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
