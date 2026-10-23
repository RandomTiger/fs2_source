# FS2 demo: finished batches 1-3

Moved out of `docs/FS2_DEMO.md` so that file stays short; history, not instructions.

### Batch 1: old table and pilot loading (done)

Approved as fixes by D-20261018-154908 and committed one item per commit, each with its review in
the message. The proposal as approved is below. What the runs found besides it:
- **Weapon impact explosions.** Demo weapons name impact explosions (`ExpMissileHit1`, `Exp20`,
  `exp06`; 25 of 47 weapons). With no `weapon_expl.tbl` they take the existing "none" path and play no
  impact animation. The shipped demo exe probably loaded them by name. This is a presentation gap for
  a later data-handling item. Batch 1 adds no new fallback (condition 2).
- **Install path length.** An install in a deep folder (a scratch copy under the session scratchpad,
  about 110 characters) crashed at start-up in both the fixed and the unfixed build, before any table
  was read. That is the source's 128-character `MAX_PATH_LEN`. Keep the demo install and the test folder
  in a short path.
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

### After batch 3 (done)

- **Sales screens** (D-20261018-221703): autotest and replay leave them at once; a demo run ends with
  exit 0 in 5 to 17 s instead of the watchdog's 2 minutes.
- **Suite scripts** (D-20261018-221705): `tools/replay_check.ps1 -DemoDir <folder>` (or `FS2DEMO_TEST`, section 6) replays
  `recordings/demo` there in Release and Debug; with no demo folder they are SKIP lines.
- **Data set enumeration** (Tom, 2026-10-18): FS2 retail, FS2 demo, FS1 retail, FS1 demo
  (`CFile/DataSetInstall.h`); logs say "data set: FS2 demo". Only the FS2 pair is detected.
- **Installer extraction** (D-20261018-221704, Tom's two additions): our exe next to `fs2demo10.exe`
  extracts it once into `.\fs2demo\` (own inflater, CRC per file, `.partial` then rename, never into an
  existing folder) and runs there, with its own `fs2.ini` and pilots. `-dataset fs2demo|fs1demo|retail`
  chooses; otherwise a replay's recorded set, retail VPs next to the exe, the only set present, or
  fs2demo. A first start takes about 10 s; the files match the installer's byte for byte and
  `demo_install.py`'s. The FS1 demo's cab reader is for the FS1 branch.
- **Replay race** (#27, D-20261018-232002, on RecordPlayback): the music streamer's thread could make
  the main thread skip a recorded read. Before the fix about 1 demo replay in 10 drifted; after, 60 of 60
  match.
