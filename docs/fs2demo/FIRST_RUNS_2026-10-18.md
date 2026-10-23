# FS2 demo: first runs of project builds on the demo data (2026-10-18)

Moved out of `docs/FS2_DEMO.md` so that file stays short; history, not instructions.

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
