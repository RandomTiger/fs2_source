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
| `FS1BriefingArt` | 9 | Briefing screen in FS1's layout. `BriefTrans.ani` plays when it opens (FS2 had commented this out), and its last frame, the only place FS1 keeps the map window and text box, becomes the background. Button positions come from matching each button's frames against that frame (mean colour error under 3 of 255); the map window, text box, info box and close-up popup are measured from the art. The stage text and title positions are estimates above the map frame. No drawn labels on the common and briefing buttons, since FS1's art has them; the common buttons use the same positions on the ship and weapon screens. Clicks on FS1's VCR controls, which sit inside the map window, do not pick icons. The scene-cut static is clipped to the window. `FS1BriefingText` (2026-10-20, Tom: the text sat too far up and left, over the box's frame) puts the briefing text inside the text box's black area, measured from the same frame (x 27 to 429, y 409 to 471, its left corners bevelled): x 40, y 415, 385 by 56, five lines, the rest scrolled |
| `FS1MainHall` | 9 | The stand-in `mainhall.tbl` gives each FS1 hall its six door anims and its ambient anims (2 and 4), at positions found by matching each anim's first frame against the hall background (exact for every door); each door goes with the mask region it covers. FS1's door and intercom sounds; the ambient anims are silent. Fixes the main hall crash: with no door anims, lingering over a door read a missing anim instance in `main_hall_mouse_grab_region` (now guarded). Checked by hovering all six regions of hall 1 in Debug |
| `FS1ShipWeaponScreens` | 9 | Ship select and weapon loadout use Brief's background (FS1 has no `ShipSelect` or `WeaponLoadout`). FS1's ship anims (633x224) and weapon anims (244x214) carry the name, stats and description in the art; they are placed by matching their first frame against that background, and FS2's drawn ship info and weapon description are skipped. Ship anims loop from frame 37, FS1's value. The weapon screen's secondary list scrolls with `SSB_08/09` (FS1 has no `WLB_08/09`). The ship screen's scroll buttons and both reset buttons are placed by FS1's masks: they were cut from a background FS1 does not ship, so they could not be matched |
| `FS1Labels` | 9 | FS1's interface art has its labels painted in, so in FS1 data `UI_WINDOW::add_XSTR` adds none and FS2's labels no longer sit a few pixels off FS1's on every screen. Where a label still shows twice, it is FS1's button art drawn at FS2's position over the copy in the background |
| `FS1Credits` | 9 | Fixes the credits crash. FS1's `credits.tbl` has no `#end` and no newline after its last line; FS2 reads lines until `#end`, so it read past the end of the text and kept appending to its buffer. In FS1 data the read stops at the end of the text |
| `FS1Sliders` | 9 | FS1 has no slider art (`slider*`), which FS2's tech room list and HUD config colour sliders load; the missing bitmap set off two Asserts on opening either screen. In FS1 data a slider with no art is created, but neither drawn nor clicked |
| `FS1Lists` | 9 | File lists asked for by extension (`*.fs2` missions, `*.fc2` campaigns) also take FS1's `.fsm` and `.fsc` in FS1 data, so the campaign room lists FS1's campaigns and the simulator its single missions. The simulator's list loads behind a popup, which `-autotest` cancels, so autotest screenshots show it empty |
| `FS1PilotSelect` | 9 | Pilot select in FS1's layout. FS1's buttons are `CHP_xx` (FS2's `CPB_xx` do not exist in FS1, so none were drawn), placed by matching each against a retail FS1 screenshot, with `ChoosePilot-m.pcx`'s hotspots; the pilot list moves to where retail draws it; the copyright lines are FS1's ("Conflict: FreeSpace - The Great War, Copyright © 1998, Volition, Inc." / "All rights reserved") in its colour. Matches retail pixel for pixel apart from the mouse cursor |
| `FS1Barracks` | 9 | Barracks in FS1's layout: the `BAB_00`-`16` buttons placed by matching each against a retail FS1 screenshot (FS1 has no `BAB_17`/`18`, FS2's squad logo arrows: they get no hotspot and stay hidden, as FS1 has no squad logos); the pilot list, pilot picture and its "n of m" where retail draws them. Checked against the retail screenshot, with the picture forced to retail's for the check |
| `FS1MainHallText` | 9 | Main hall tooltips and version label as retail FS1 draws them: FS1's own tooltip strings (from retail `FS.exe`), at y 456, in FS1's grey, with no shading band behind them (FS2 added that); the version label reads "v1.06" at retail's position (Tom, 2026-10-19). Checked against the retail screenshot with the Ready Room tooltip forced on |
| `FS1Simulator` | 9 | Simulator in FS1's layout: buttons placed by matching each against a retail FS1 screenshot (`LoadMission-m.pcx`'s hotspots are FS2's); Silent Threat's missions (found in `mdisk.vp`) get FS1's "ST" icon (`icon-silent`) at x 57, and the mission names sit 50 pixels right of FS2's, as retail. Also: FS1 missions that start with a `//` line FRED wrote (`mdu-02`, `-04`, `-07`) now read, as the FS1 converter turns a line-start `//` in a mission into a `;` comment. Checked against the retail screenshot |
| `FS1Options` | 9 | The options tab in FS1's layout. FS1's art (`OP_`, `OPa_`) and mask numbers differ from FS2's (`OPT_`, `OMB_`); its sliders are FS1's own `UI_DOT_SLIDER` (a bar anim whose frame is the setting, arrows at x and x + 216), which FS2 still has but no longer used, made in place of FS2's and kept in step with them; skill level is FS1's 5-frame `OPa_11` with a hotspot per level. FS1's volume bars run 0 to 10 (FS2's 0 to 9), so in FS1 data the volume is the position / 10. Brightness box and value, and the skill name (left-aligned) where retail draws them. Matches the retail screenshot apart from the pilot's own settings. Detail tab in `FS1OptionsDetail`, multiplayer tab in `FS1OptionsMulti` |
| `FS1OptionsDetail` | 9 | The detail tab in FS1's layout, placed by matching FS1's `OPc_` art against a retail screenshot. FS1's sliders are 5-frame dot anims like FS2's and take FS2's sliders; its on/off pairs and presets take FS2's buttons. FS1 controls with a direct FS2 setting work: model detail, 3D hardware textures, impact effects (FS2's small debris, which FS2 also calls Impact Effects), shield hit effect, stars, particles, target view rendering, lighting, presets. The rest have no direct FS2 setting and are shown at their highest and disabled (a click gives the fail sound): model update, weapon rendering, engine glow, nebulas, effects alpha blending. FS2 already runs the first three and alpha blending at their best (model caching, the engine glow check and stippled alpha are commented out). In FS1 data FS2's nebula detail, planets/backgrounds and weapon extras, which FS1 has no control for, are held at their highest (`SystemVars.cpp`, also when a preset is chosen), are not saved, and presets are matched with them so applied. The presets are FS2's, so very high shows 3D hardware textures at 5 dots where FS1's showed 4. Replays: the 25 FS1 recordings match; FS2 data is unchanged |
| `FS1OptionsMulti` | 9 | The multiplayer tab in FS1's layout: the general sub-tab placed by matching FS1's `OPb_` art against a retail screenshot, the voice sub-tab (no retail screenshot) against FS1's own `OptionsMultiVox` background, where each button's resting frame is painted in. The IP list, PXO login and password and the voice player list sit inside FS1's boxes, measured from its backgrounds. FS1 has no TCP/IPX choice, LAN object update or squadron name, so those are hidden (tab skips the squadron box); its local broadcast is a yes/no pair where FS2 has one toggle, so FS1's no button is added and its yes button only sets it. FS1's multiplayer masks number exit game 4 (3 on its other tabs), so the button is relinked per tab, and their backgrounds differ under the controls and HUD config buttons, whose resting frame is the options tab's, so on this tab it is not drawn. Matches the retail screenshot apart from Parallax Online, which FS2 always starts off. UI only, so no replays |
| `FS1TechRoom` | 9 | The tech room in FS1's layout, its `TDB_` and `TSB_` art placed by matching against retail screenshots of all three tabs. FS1's ships tab has its own background and mask (`TechShipData`); its weapons and species tabs use `TechData`, with no list: the description fills the left box (FS2's commented-out coordinates were FS1's), the animation the right, and prev/next sit under it with `TDB_11`/`12` where the ships tab has `TSB_22`/`23`, on other mask numbers; the scroll arrows are only on the ships tab. FS2 still has the list-less render (`techroom_weapons_render`, marked obsolete), used for both tabs. FS1's `weapons.tbl` names no tech anims and its `Species.tbl` has only `$<species> Tech Description:` entries: the anims were tables in FS.exe (weapon name to `CB_` anim; `tech_tpilot`, `tech_vasudan`, `tech_shivan`, which FS2 keeps commented out), now in `TechMenu.cpp` for FS1 data. Fixes a crash opening the weapons tab with a pilot saved under FS2 data: its tech room flags for FS2's longer weapon table marked empty entries past FS1's 48, whose NULL description `strcmp` read; in FS1 data those flags are skipped (`ManagePilot.cpp`). Such a pilot's ship and species flags still go by FS2's indexes, so its lists differ from an FS1 pilot's. UI only, so no replays |
| `FS1Cutscenes` | 9 | The cutscenes and credits screens in FS1's layout: their buttons (the tech room's `TDB_00`-`03` tabs, `VFB_04`-`07`, `CRB_04`) placed by matching against retail screenshots, the cutscene list and description inside FS1's boxes (the description wraps where retail's does), the credits artwork centred in FS1's window. UI only, so no replays |
| `FS1Medals` | 9 | The medals screen in FS1's layout. FS1 has 16 medals and its rank insignia (`medal16*`, the pips) is mask region 16, where FS2's is 12, FS1's Ace badge: the rank is drawn and labelled from region 16 and region 12 is a medal. Each FS1 medal bitmap is exactly the size of its mask region, so it is drawn at the region's corner. The callsign sits in FS1's name bar (region 50) and exit is `MX_17` (region 17). Medal counts a pilot saved past FS1's 16 (one saved under FS2 data) are not loaded or drawn, which would otherwise Assert on a medal with no bitmap. Matches the retail screenshot apart from the pilot's own medals. UI only, so no replays |
| `FS1ControlConfig` | 9 | The control config screen in FS1's layout. FS1's `CCB_` art and mask numbers are FS2's, so only the button places change (matched against a retail screenshot); the list starts lower, under FS1's tab labels, and the conflict text goes in FS1's box. Not changed: FS2 names a few controls differently ("Turn Off Auto-Targeting" where FS1 has "Stop Targeting Ship", and the like), and the list text was dimmer than retail's (fixed by `FS1TextColours`). UI only, so no replays |
| `FS1HudConfig` | 9 | The HUD config screen in FS1's layout, with its gauge preview, which drew nothing (FS1 has none of FS2's `HCB_` gauge art). FS1's previews are `HG_` bitmaps (some `.ani`, loaded as animations, first frame drawn), each exactly the size of the mask region of the same number, so drawn at its corner; which gauge each is comes from FS.exe's table, whose strings are pooled, so checked against FS2's preview layout. `HG_00a`, the reticle circle, takes FS2's empty reticle slot with no hotspot. FS1's buttons (`HG_27`-`32`, `36`, `37`: amber, blue, green, on, off, popup, reset, accept) replace FS2's, whose hotspots 0-26 are gauges in FS1's mask; FS2's colour sliders, hcf files and select all have no FS1 art and are hidden. FS1's brightness is its old `UI_DOT_SLIDER` (`HG_33`-`35`) over FS2's user range of `HUD_color_alpha`, 3 to 13, applied to every gauge as the colour buttons apply theirs (what FS2's commented-out brightness slider did). FS1 has one HUD colour, so on opening the screen every gauge takes the selected colour (cancel restores the old settings), and only the hostile triangle and missile warnings keep IFF colours, as retail draws them. Matches the retail screenshot apart from the pilot's own gauge settings. UI only, so no replays |
| `FS1CmdBrief` | 9 | The command briefing in FS1's layout: the text in the left box (its width set so lines wrap where retail's do; the stage count was 3 pixels right of and below retail's, fixed by `FS1SmallDiffs`), the animation in the right box, the stage buttons along the bottom. FS1's `CBB_` art and mask number the buttons differently (pause is `CBB_07`, in the middle), so each FS2 button takes FS1's art, place and hotspot, found by matching against a retail screenshot. UI only, so no replays |
| `FS1Briefing` | 9 | The briefing's stage count and mission title move under the map, where retail draws them (`FS1BriefingArt` had them above it). UI only, so no replays |
| `FS1ShipWeapon` | 9 | Ship select and weapon loadout checked against retail screenshots: they match apart from the reset button, which `FS1ShipWeaponScreens` placed from its mask region before the screenshots existed; now where retail draws it on both. UI only, so no replays |
| `FS1Pause` | 9 | The single-player pause screen shows FS1's `Pause` box ("Mission Paused" is in its art) where retail draws it, in place of FS2's `PleaseWait` box with a "Paused" string. Not changed: in FS1 data the paused game frame behind it comes out magenta. This is not FS1's: in windowed mode the D3D renderer's `gr_d3d_save_screen` copies the desktop's top-left corner instead of the game window, so FS2 data's paused frame is shifted by the title bar and FS1 data's (window elsewhere) shows whatever is there. Handed to the work manager as a shared renderer bug (2026-10-20), fixed by review 52 on `OriginalSourceBugFixes`; with it merged in (via `Enhanced`, 95ee575), FS1 data's paused frame is the game behind FS1's box (checked by screenshot, 2026-10-20). UI only, so no replays |
| `FS1MissionLog` | 9 | The mission log's buttons where retail draws them (FS1's `LB_` art and mask number them as FS2's; the list, objectives key and time already sit in FS1's boxes). The objectives key's text was 4 pixels and its icons 1 pixel higher than retail's (fixed by `FS1SmallDiffs`). Not changed: the continue button's label reads "Accept" in the `MissionLog` background this install loads (Silent Threat's), "Continue" in the retail screenshot. UI only, so no replays |
| `FS1Hotkeys` | 9 | The hotkey screen's list in FS1's box: the values FS2 keeps commented out in `MissionHotKey.cpp` (list at 40, 70, 280 high; wing icons at 280; ships at 302; the big key name centred on 575, 41) are FS1's, with FS1's key columns 30 apart (measured from a retail screenshot) and the list as wide as FS1's box. UI only, so no replays |
| `FS1Debrief` | 9 | The debriefing in FS1's layout: title and text in FS1's box, buttons where a retail screenshot draws them. FS1's `DB_` art and mask keep FS2's numbers for the tabs, scroll, replay, recommendations and page buttons, but number help, options and accept 10-12 and the multiplayer ones 15-17, 21 and 25 (placed at their mask regions' corners, with no screenshot to match). The title was white where retail's is light blue (fixed by `FS1SmallDiffs`). UI only, so no replays |
| `FS1GameplayHelp` | 9 | The gameplay help (F1) screen's page buttons where retail draws them, and its continue button, which FS1's `F1` art and mask number 3 (FS1's 2 is an options button, which FS2 drew in continue's place). Page 1's own strings take FS1's wording ("context sensitive help", "F5..F12", "quit FreeSpace immediately"). Not changed: control names come from FS2's control table ("Set Throttle to Max" where FS1 has "Set Max Throttle"), as on the control config screen, and the other pages have no retail screenshot to check. UI only, so no replays |
| `FS1DebriefStats` | 9 | The debriefing's statistics tab checked against a retail screenshot: it matches through `FS1Debrief`'s text box apart from the page count ("1 of 4"), now in the box's top right where retail draws it. UI only, so no replays |
| `FS1HelpOverlays` | 9 | The help overlays, which drew nothing in FS1 data (FS1 has no `help.tbl`). FS1's are whole 640x480 bitmaps drawn over FS2's greyed screen; their names are FS.exe's table, in FS2's overlay order, with a second tech room overlay (`tech_help2`) for FS1's weapons and species tabs. Checked against retail screenshots of the tech room, barracks, control config, campaign room, hotkeys, debriefing, command briefing and briefing overlays. The multiplayer variants (`*_Multi`) are not used. UI only, so no replays |
| `FS1HudColour` | 9 | FS1's HUD colour. FS1 has one HUD colour (amber, blue or green) and a brightness; FS2 draws each gauge in the pilot's own colour, which for a new pilot comes from `hud_3.hcf` (a pale grey-green, with yellow-grey shield and speed gauges), so the FS1 HUD came out grey where retail's is green. In FS1 data every gauge, and the message head, is now drawn in the one colour, the brightness scaling it, as retail. The pilot's per-gauge colours are kept in the pilot file, unused. The pilot file has no place for FS1's colour or brightness, so they are kept in the registry or ini (`FS1HudColor`, `FS1HudBrightness`): per install, not per pilot as in retail. The HUD config screen's colour buttons and brightness slider set them; accept saves them, cancel reloads them. A toggle for FS2's per-gauge colours was tried and dropped: editing them needs FS2's whole HUD config interface (section 7). Also fixes the HUD config screen resetting the colour to green each time it opened, and its reset leaving the brightness slider where it was. The colours' brightness was tuned to retail's afterwards by `FS1HudBrightness`. UI only, so no replays |
| `FS1TextColours` | 9 | FS1's normal interface text is brighter than FS2's: on every screen with a retail screenshot to measure, each anti-aliasing level of retail's grey text is about 1.46 times ours. FS2 dimmed its normal text colour (`Color_white`, also `Color_normal` and `Color_text_normal`) from 185 to 105 grey; in FS1 data it is now 155, which puts our text within 2 levels of retail's (112 and 144 against 114 and 145) on the control config, tech room, barracks, command briefing, briefing and debriefing screens. Ship select and weapon loadout were already right, as their text uses other colours. UI only, so no replays |
| `FS1SmallDiffs` | 9 | Three small differences from retail screenshots, now pixel-exact: the debriefing title in `Color_bright_blue` (retail's matches it at every anti-aliasing level), the mission log's objectives key icons 1 and labels 4 pixels lower, and the command briefing's stage count 3 pixels left and up. UI only, so no replays |
| `FS1HudBrightness` | 9 | The HUD's brightness as retail's. Retail HUD config screenshots in amber, blue and green show FS1's three colours are FS2's `HC_colors` in hue, and the preview within one 16-bit step of ours. In flight, every level of retail's green HUD was about 1.18 times ours at the same setting (the HUD config showing 5 brightness dots in both), so in FS1 data the gauges' alpha is 1.18 times FS2's; ours is now within one 16-bit step of the retail in-flight screenshot on the weapons, radar and speed gauges (medians 78, 75, 56 against 80, 76, 56). Not checked in flight: amber and blue, and other brightness settings (no retail screenshots). UI only, so no replays |
| `FS1CampaignRoom` | 9 | The campaign room in FS1's layout. FS1's `CAB_` art and mask number restart 7 and select 4 (FS2's 4 and 5), and add help (`CAB_05`) and options (`CAB_06`), which FS2 dropped (its code for them is commented out); each button is placed by matching its art against a retail screenshot, and help and options work as FS2's commented-out code had them. FS1's labels ("Restart Campaign", "Select") are in its background, so FS2's are not added. The list and the description sit in FS1's boxes: both match the retail screenshot to the pixel, the description wrapping where retail's does. UI only, so no replays |
| `FS1ShieldIcon` | 9 | The player's and target's shield gauges (the ship outline with its four shield quadrants) were never drawn in FS1 data. The generated `hud.tbl` asked the file list for `shield-*.ani`, but the list filters by extension only, so it held every HUD `.ani` in name order, cut off at 40 before the first shield icon; no ship's `$Shield_icon` matched, and a ship with none draws no shield gauge. The generator now lists every `.ani` and keeps the `shield-` ones (22). The player's shield gauge now matches the retail in-flight screenshot to the pixel. HUD drawing only, so no replays |
| `FS1DataSetProfile` | 9 | The FS1 rows of Enhanced's data set profile table (D-20261020-053500..053503, merged in from `origin/Enhanced`), filled in and marked `filled_in`. **Retail**: settings name and window title "FreeSpace" (FS.exe), v1.06, built-in campaign `freespace` ("The Main Freespace Campaign", from `freespace.fsc`), no title screen, no `.hcf` (FS1 has none, so a new pilot keeps `hud_set_default_hud_config`'s one colour; `ManagePilot.cpp` now skips the load when the row names none), and FS1's built-in list: 124 entries read from FS.exe's table, FS1's flag bits mapped to FS2's (FS1 adds a Silent Threat bit, which FS2 has no flag for). **Demo**: "FreeSpaceDemo" / "FreeSpace Demo", v1.00 (its exe printed "Dv1.00", which a suffix cannot give), campaign `demo` ("Freespace Demo Campaign"), title screen `DemoTitle1` (640 only), its five mission and campaign files as the list, since the demo's exe has none (docs/FS1_DEMO.md). FS1 checked no version or MOTD on the web, so both URLs are empty. `game_detect_data_set` names FS1 data (cfile's `Cfile_fs1_data`) FS1 demo when it has `demo.fsc` without `freespace.fsc`, otherwise FS1 retail, and applies the row. The main hall's FS1 version label now comes from the profile (still "v1.06" on retail). A recording names no data set: the game works it out from FS2's campaign and mission names, which FS1's share, so every FS1 recording would read as FS2 retail and stop; in FS1 data that check is skipped and the skip logged ("replay data-set check skipped: ...", D-20261020-103504), until recording version 6 (GodotPrep) names the data set in the header and removes the skip (docs/FS2_DEMO.md, batch 10). The merge gate (2026-10-20) then replayed all 25 FS1 recordings in Release and Debug, identical, with the built-in campaign `freespace` now loading (FS2's `FreeSpace2` was never found in FS1 data). Release and Debug build |
| `FS1Reticle` | 9 | The reticle in FS1's layout (Tom, 2026-10-20: "ours does not look like that"). FS1's 640x480 reticle art is placed by matching each frame against a retail in-flight screenshot: the threat icons sit in the top arc's gap (300 and 320, 137), the left arc at (216, 240) and the right arc at (374, 242); the max speed number at x 229. FS1 draws the throttle's dark portion, which FS2 commented out. The right arc's holes hold FS1's weapon link indicators again (`rightarc2`-`6`: primary banks, linked, and the selected secondary bank of one, two or three), drawn as the removed `hud_show_reticle_weapons()` did, at its commented-out coords, and only while the weapon linking gauge is on (else the arc without holes). Matches the retail screenshot apart from flight state. The radar's joins in a stretched window are scaling only: at 640x480 it matches retail. HUD drawing only, so no replays |
| `FS1Resolution` | 9 | A new `fs2.ini` on FS1 data starts at 640x480, not FS2's 1024x768 (Tom, 2026-10-20). FS1 has only 640 art, so at 1024x768 the HUD had no art and Debug stopped on the missing loading animation; first seen with the FS1 demo's fresh ini. Only a missing `Resolution` is written; an existing ini keeps its setting. Checked by starting FS1 data with `-ini` on a new file. Settings only, so no replays |
| `FS1Popups` | 9 | Every popup stopped the game in FS1 data ("Could not load in Pop2!", first seen opening the mission simulator, whose "Loading missions" popup is the first most players meet). FS1's popup art is `Pop2a` (one or two choices) and `Pop3` (three), with its own buttons `Pop2a_00` to `Pop2a_04` (negative, positive, three generic); it has no `Pop2` and no `Pop_` buttons. The buttons are cut from the panel at the art's right, so each was matched against the art on its border pixels (error about 1 of 255): the slots are x 343, y 135, 169 and 199 within the art, which is drawn where FS2 draws its own (129, 99). The message text and title keep FS2's size, moved inside FS1's frame (141, 117). The death popup (`PopDeath`, `PopD_00` to `PopD_02`, which FS1 has under FS2's names) loaded but its buttons were a few pixels off the art; they now sit where they match it (x 343, y 32, 53 and 73 within the art). An autotest answers popups without opening them, which is why no replay found this. Checked in Debug by a temporary build that showed and screenshotted each kind of popup (one, two with the yes/no icons, two with a title, three with eleven lines) and the simulator's loading popup. UI only, so no replays |
| FS1 retail fixes from `FS1Demo` | 9 | Fixes found on `FS1Demo` that FS1 retail needs too, re-landed here so `FS1Compat` stands on its own and `FS1Demo`'s diff over it is demo-only (Tom, 2026-10-21; D-20261021-100828). **Inherited from `OriginalSourceBugFixes`** (cherry-picked, D-20261021-100829): review 54, the PCX 16bpp reader's pixel temp was 2 bytes and the 32 bit screen setter wrote 4 (Debug stopped entering the barracks); review 55, D3D `bitmap_ex`'s source offset lacked the bytes-per-pixel factor. **Data set profile fields**, in `FS1Demo`'s order: `loading_ani_coords` (the loading bar on FS1's `LoadingBG`, retail {117,317}; D-20261020-222222), `barracks_image_coords` and `barracks_image_window` (the pilot picture at {460,33}, clipped to FS1's window shape, rows 36 to 152; D-20261020-231901). The FS1 demo row's values are copied from `FS1Demo` and inert here. **Barracks**: the clipped picture's rows are drawn as textured polys where `gr_bitmap` is, since `gr_bitmap_ex` writes 16 bit pixels into a 32 bit back buffer. **Briefing**: FS1 text at x 50, as `fs2_reference/fs1_retail/briefing/briefing.png` shows (was 40). UI and bitmap loading only |

**FS1 missions play** (`-autotest`, Release, FS1 install's `fs2.ini` set to 640x480):
- `btm-01`, the first training mission: 400 s of mission time, PASS with no Assert, Int3 or warning. Its later events wait on the player following instructions, so they do not fire under autotest.
- `sm1-01`, the first combat mission: the waves are destroyed, both goals ("Eliminate Hostiles", "Protect the Orff") complete, "Mission End" at 202 s. PASS with no Assert, Int3 or warning.

In Debug, `sm1-01`, `btm-01` and `btm-02` (60 s each) PASS with no Assert, Int3 or warning.

These runs did not use `-playernpc`, so nobody flew the player ship.

**FS1 recordings** (`recordings\fs1`): the main campaign (`freespace.fsc`) without the 5 training missions (`btm-*`) and the 4 that start in red alert (`sm2-06a`, `sm2-10a`, `sm3-05a`, `sm3-09a`; replay does not support red alert). 120 s each, the AI flying the player. Made with `tools\record_autotest.ps1 -Fs1`; `tools\replay_check.ps1` replays them in `PATH_FS1` with the exes found there, after the FS2 recordings.
- All 25 replay with identical traces in Release and Debug, with no Assert, Int3 or warning. The whole suite, 47 FS2 and 25 FS1 recordings, passed 72 of 72 in 9 minutes.
- Once, the Release replay of `sm3-06a` desynced at frame 3320 of 3544 (the run asked for `mouse_pos` where the recording has `timer_ms`). Two reruns matched. Not explained yet. A second case on 2026-10-19: `sm3-08a` desynced at frame 3543 of 3544 (the run asked for `end` where the recording has `mouse_pos`); three reruns matched.
- The exes must be in the FS1 install. On `FS1Compat` the build puts them there; elsewhere they have to be copied in.
- Re-recorded for `FS1BriefingArt`: with the briefing's transition anim, 6 of the old recordings desynced on sound calls mid-mission, the same way every time, while all 47 FS2 recordings matched. The cause is in the original code: `message_queue_process` (`MissionMessage.cpp:1268`) keeps a message's head anim instance pointer after the anim ends and its instance goes back to the free list, so `anim_playing()` is true again once another anim reuses that instance. Message timing therefore depends on which anim instances earlier screens used. Not fixed (a candidate for `OriginalSourceBugFixes`).

**Known, not yet handled:**
- Music: FS1's chained normal patterns are cut for now (batch 5 brings them back).
- Medals: 16 to FS2's 18; slots 16 and 17 stay empty. Badge voices are `<persona>_badge_x`, and FS2's default persona 9 has no FS1 file.
- Asteroids: 3 types to FS2's 12; FS1 has no debris fields.
- Icons: FS1 has 27 briefing icon types to FS2's 35; the last 8 get no FS1 set.
- Species: FS1's `Species.tbl` is a different file; only the tech room reads it. Read in FS1's format by `FS1TechRoom`.
- Backgrounds: no starfield bitmaps besides the sun; FS1 missions use none.
- Resolution: FS1 needs 640 mode. A new ini on FS1 data now starts at 640x480 (`FS1Resolution`); an existing ini keeps its setting. The interface art-mode setting (D-20261017-225737) is the real fix.
- Trials use your FS2 pilot `Tom.plr`, copied into the FS1 install's `Data\Players\Single`.

**Interface issues seen by hand (2026-10-18, Release, FS1 data):**
- Pilot select: the buttons are drawn at FS2's positions but clicked through FS1's mask, so the click areas are well off the art. FS2's drawn labels sit over the ones in FS1's art, and "FreeSpace 2 / Copyright 1999" is drawn over FS1's logo.
- Main menu: crashed when the mouse lingered on a door. Fixed by `FS1MainHall`.
- Main menu: the region tooltips and the version label were FS2's ("Exit FreeSpace 2", "v1.20"); fixed by `FS1MainHallText`. Hall 2's door anims have not been checked by hovering yet. The ambient anims have no sounds.
- Ship select and weapon loadout: fixed by `FS1ShipWeaponScreens`, including the mouse trail there (a screen whose background did not load is not redrawn). Other screens may still lack backgrounds.
- FS1 does have a map window bitmap, `mapwin.pcx` (549x260, the size of `BriefMap.ani`), with `MapWin01-04` as its edges and `ShipWin01-04` as a frame for the ship screen; `FS1BriefingArt` uses the last frame of `BriefTrans.ani` instead, which looks the same. `ssbutton01-03.ani` animate the selected screen in the top-left common buttons; not played yet.
- The original FS1 (`FS.exe`, GOG) does not run on this machine (Windows 11): it hangs on a yellow screen, so there are no side-by-side comparison shots.

**Interface survey (2026-10-19, Debug, FS1 data, `sm1-01`).** Every single-player screen was opened from the main hall or during the mission by a temporary autotest tour, and screenshotted. Two problems run through nearly every screen:
- **Doubled labels.** FS1's art has its labels painted in. FS2 draws its own on top (`UI_WINDOW::add_XSTR`), a few pixels off, so most labels appear twice. Fixed by `FS1Labels`.
- **Buttons at FS2's positions.** Click areas come from FS1's masks, so the art and the click areas disagree. Each screen needs its own FS1 positions, found the way the briefing's were.

| Screen | Other problems |
|---|---|
| Pilot select | "FreeSpace 2 / Copyright 1999" drawn over FS1's logo. Fixed by `FS1PilotSelect` |
| Barracks | Pilot picture and squad boxes empty. Layout fixed by `FS1Barracks`. The picture stays empty for a pilot made in FS2 data: its picture is not among FS1's 8, and the original code then draws none |
| Tech room | FS2's Intelligence tab, which FS1 lacks. Asserts on opening: `buttonHeight > 5` (`SLIDER2.cpp:83`) and a bad bitmap handle (`BmpMan.cpp:1231`); the list's slider art does not fit. Asserts fixed by `FS1Sliders`; layout, species and weapon anims by `FS1TechRoom` |
| Simulator room | Mission list empty. Fixed by `FS1Lists`: it lists Silent Threat's 25 single missions (`mdu-02` to `mdu-26`), as retail. `mdu-02`, `mdu-04` and `mdu-07` raised a Warning ("Required token = [#Mission Info]") when the list loaded; fixed, with the layout, by `FS1Simulator` |
| Campaign room | No campaigns listed: the list looks for FS2's `.fc2` files, FS1's are `.fsc`. Fixed by `FS1Lists`; the list was drawn at FS2's position, over the title, fixed with the rest of the layout by `FS1CampaignRoom` |
| Load mission | FS2's developer screen; works |
| Options | Sliders drawn away from FS1's tracks; a grey and white checker block where a slider bitmap is missing. Options tab fixed by `FS1Options` (the checker block is the brightness test bar, as retail, now in its place); detail tab fixed by `FS1OptionsDetail`, multiplayer tab by `FS1OptionsMulti` |
| Control config | Right-hand buttons drawn twice over, at both sets of positions |
| HUD config | The gauge preview is not drawn (only the reticle). Same slider Asserts as the tech room, fixed by `FS1Sliders`. FS1's colours are the Amber, Blue and Green presets in its art |
| Medals | Looks right |
| Credits | Crashed (heap corruption, `0xC0000374`). Fixed by `FS1Credits` |
| Cutscenes | Only "Introduction" listed; movies are not played by this source anyway |
| Gameplay help | Text names FreeSpace 2 ("Quit FreeSpace 2 immediately") |
| In-game HUD | Works. The reticle was FS2's layout (arcs and threat icons misplaced, no weapon link indicators): fixed by `FS1Reticle` |
| Pause | Magenta full-screen background |
| Mission log | Works apart from the labels |
| Objectives (`GS_EVENT_SHOW_GOALS`) | Stops the game: `Objectives-m` is missing. FS2 may never open this screen |
| Hotkeys | From a mission: works apart from the labels. From the main hall: crashes (access violation); it is normally only opened from a briefing or a mission |
| Debriefing | Works apart from the labels and buttons |
- `gr_bitmap_ex` in Direct3D locks the back buffer in the middle of a frame and hung the game, so it is best avoided.

---

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

### Branch-local build target
On `FS1Compat`, `Freespace2.vcxproj` and `Freespace2.vcxproj.user` build into, and debug from, `PATH_FS1` instead of `PATH_FS2` (commit `cebafe2`), so trial runs load FS1 data. Before the first merge into `Enhanced` it is replaced by a per-data-set property or reverted on `FS1Compat`; the `.user` half goes with the repo-wide untracking of `*.vcxproj.user` (Tom, 2026-10-20).

### Before each merge into Enhanced
1. Replace or revert the build-target change (above), the first time.
2. Run `tools\branch_check.ps1`. It reports an error for any layer, local or on origin, whose project files mention `PATH_FS1`.
3. The gate: the builds, and `recordings/fs1` identical in Release and Debug on `FS1Compat` (D-20261020-211001, Tom: FS1 branches test only their own data). The full 48 and the retail quick suite are `Enhanced`'s gate, run on the merged batch. Then a merge brief to the steward: what changed, the `recordings/fs1` results, any recording that moved, the FS1 data used, and that the FS2 suites are run at `Enhanced`. Then the merge and the carry-up.
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

## 8. Handover (2026-10-20, from DESKTOP-MAIN)

The FS1 work moves to another machine. Everything below is on GitHub; nothing depends on files on DESKTOP-MAIN. The FS1 demo also needs its installer, `fsdemo10.exe`, on each machine (never committed).

**The first FS1 batch is carried up every layer** (Tom's steward review, D-20261020-103501 to 103505; carry rule D-20261020-103502: each layer's builds, quick suite, `warning_check`, `tools_check`, pushed when green):

| Layer | Merge | Gate |
|---|---|---|
| `Enhanced` | ac7bf5a | 47 retail recordings Release and Debug MATCH (AutotestAI Debug's two known warnings, #78), 25 FS1 MATCH, 3 FS2 demo MATCH, `warning_check` and `tools_check` PASS |
| `PrePort` | 70bfeff | 48 autotest + 3 demo recordings Release and Debug MATCH; `warning_check`, `tools_check` PASS |
| `PrePortCleanup` | 1e824b7 | as PrePort |
| `PrePort64` | 93958e0 | x86 builds 0 warnings; `warning_check`, `tools_check` PASS; quick suite: replay 25/25, headless Release/Debug 23/23, menus 2/2, demo PASS, nodraw 22/23 (below); x64 headless Release/Debug 48/48 |
| `GodotPrep` | bfd5f8a | x64 builds; `warning_check`, `tools_check` PASS; quick suite: x86 builds 0 warnings, demo PASS, replay 25/25, nodraw 23/23, headless Release/Debug 23/23, menus 2/2, restart 2/2; x64 headless Release/Debug 48/48 |

Above `Enhanced`, `recordings/fs1` cannot replay (format 3), so FS1 there is proven only by the build, the warnings check and the retail suite. On `PrePort` and `PrePortCleanup` the full replay ran instead of the quick suite, so their nodraw, headless and menu checks did not run.

**Open investigation** (`STATUS.md` on `PrePort64` and `GodotPrep`, D-20261020-132256 and 133957): AutotestNebMedium, x86 Release `-nodraw`, exit 5 (the autotest watchdog) at frame 1680 of 5203 in `PrePort64`'s quick suite, host busy with other builds; 1 in 13 on that exe (10 serialised reruns on a quiet host and the nodraw set, 23/23, all MATCH). No dump: the D-053 stuck dump exists only on `GodotPrep`. While the line is open, a second exit 5 in any check at any layer stops work and goes to the steward with both dumps.

**Ordered, not started** (D-20261020-133957 ii): D-053's stuck dump (cherry-pick d4d9935) and `autotest.log` kept beside each replay log in every check script, as one tooling commit on `RecordPlayback`, merged up every layer with each quick suite.

**After the carry, on `FS1Compat`** (merged, tagged, pushed): `FS1Popups` (every popup stopped the game in FS1 data; first met in the mission simulator) and `FS1BriefingText` (the briefing text sat on its box's frame). These are not in `Enhanced` yet: they go with the next batch.

**Still open in FS1 data** (sections 3 and 4 have the detail): FS1's chained music patterns (cut until batch 5); hotkeys opened from the main hall crashes; the objectives screen stops the game (no `Objectives-m`); hall 2's door anims unchecked and ambient anims silent; medals, briefing icons and asteroids have fewer FS1 entries than FS2 slots; the head-anim pointer fix (D-20261019-135214) is not on `FS1Compat`; the section 7 backlog. Whether `FS1Compat` is finished is Tom's call: no `FS1Compat-done` tag until he says.

**Tooling found on the way** (for `RecordPlayback`, none fixed):
- `replay_check.ps1` counts a recording as passed when `tools\bin\TraceDiff.exe` is too old to read the trace (it prints an error and still passes). `tools\bin` is local to each worktree: build TraceDiff in every new worktree first.
- `replay_check.ps1`'s Debug guards check reads `autotest.log` from `$GameDir`, not from the folder the recording ran in, so for FS1 recordings it reads the FS2 folder's log.
- `build_demo.ps1` sets `PATH_FS2` to the demo folder in the caller's shell: run it with `powershell -File`. Run `warning_check` and `tools_check` each in their own `powershell -File` too.
- Smart App Control once refused the fresh `Fred2_check.exe`; a rerun passed. The setting is never changed.
- Above `Enhanced`, `suite_check` passes no `-Fs1Dir`, so with `PATH_FS1` set `replay_check` also runs `recordings/fs1` there, with whatever exe that folder holds, and counts it as passed although TraceDiff cannot read its trace (`GodotPrep`'s suite, 2026-10-20: `sm1-01 (fs1)`).
- Suite results are hard to read while other sessions build on the same PC (steward note for Tom).

**FS1 demo**: branch `FS1Demo`, off `FS1Compat` (with `FS1Compat` merged in), pushed 2026-10-20. Its state is in `docs/FS1_DEMO.md` on that branch: the game extracts the demo from `fsdemo10.exe` beside the exe on first start (none of its files are committed), and all four demo missions run with no Error, Assert or Int3 in Release and Debug. Thrusters, shields, loadout, the briefing fade and smoke2 are data-set profile fields (D-20261020-161655, 161656, 163608); the `@` filter and talking heads are next. `tools\fs1demo_run.ps1` and `tools\fs1demo_gate.ps1` there run the demo missions and the full gate.

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
