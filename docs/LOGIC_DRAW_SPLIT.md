# Logic and drawing split (PrePort step 3)

Branch `LogicDrawSplit`, folder `fs2_LogicDrawSplit`. Step 3 of the plan in `docs/PREPORT.md`, which has the background: the section "Rendering feeds gameplay" lists everything the game decides while drawing, and the plan lists the decisions taken for this step.

## Why
The game makes gameplay decisions inside its drawing code: sounds, missile lock, cargo scan, talking-head timing (which messages wait for), and random numbers drawn by effects. Godot will do the drawing, and a headless build (step 4) will not draw at all, so all of that has to move into a logic phase that runs whether or not anything is drawn. As a side effect it fixes gameplay that today changes with the HUD detail setting, the view or the resolution.

## Branch rules
- Branched off `PrePort` on 2026-10-14. It takes merges from `PrePort` only (`git merge PrePort` in `fs2_LogicDrawSplit`), never from `Enhanced` or other branches.
- It goes into `PrePort` once, when finished, with `git merge --no-ff LogicDrawSplit` and the missions re-recorded in the same go (see Finishing).
- The checked-in recordings in `recordings/autotest` do **not** replay on this branch: the work changes the effect random numbers, so a replay desyncs on a sound part way through (sm1-01 at frame 2032, sm1-02 at 1010, sm2-01 at 1455). That is expected. Don't re-record them on this branch until it is finished; use `tools\nodraw_check.ps1`, which records fresh.
- Capital-ship break-up debris always spawning is an intended gameplay change, decided separately: it goes on its own branch off `PrePort` with its own document, not here.

## How each change is made
- A new logic phase runs where drawing starts today and does the gameplay parts in the same order and under the same conditions (HUD shown, cockpit view, gauge active, detail flag). Drawing then only reads state.
- Pattern so far: split `foo_show()` into `foo_update()` (timers, sounds, state) and `foo_show()` (drawing only), call `foo_update()` from the logic phase in `game_frame` (`FreeSpace.cpp`) under the conditions the drawing call had, and leave a `// Claude:` comment at both ends.
- One commit per piece. After each: `tools\nodraw_check.ps1 -Missions sm1-01` and the missions the piece is meant to fix; the `draw` column must stay MATCH everywhere. Stop on any new divergence.
- Missile lock and the view-cone test use the fixed game view (art resolution, 4:3, original FOV), not the display.

## Test
Build Release (and Debug for the final run) from `fs2_LogicDrawSplit`. `tools\bin\TraceDiff.exe` is a build output, so a new folder has to build it once: `MSBuild tools\TraceDiff\TraceDiff.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=<folder>\`. Then:
```
tools\nodraw_check.ps1 -Missions sm1-01
tools\nodraw_check.ps1
```
For each mission it records a fresh run with the current build, replays it normally (`draw`) and with `-nodraw`, and TraceDiffs each against the recorded trace. `summary.txt` in the output folder has one line per mission. All 46 missions take about 2 hours.

Reading a failure:
- `nodraw ... DESYNC at frame F: this run asked for X, the recording has Y`: the recorded run made the read Y while drawing (usually a sound or the clock), which `-nodraw` skips, or the other way round. Find what in drawing makes that read and move it to the logic phase.
- `nodraw REPLAY OK / DIVERGED at frame F`: same inputs, different game state. Replay both with `-fulltrace` and TraceDiff again for the field. Frame 2 means something every frame depends on (the camera, the view cone, lock).
- `draw` not MATCH: the build is not deterministic; fix that before anything else (see the `fs2-replay-debugging` notes and `docs/REPLAY.md`).
- `-nodraw` still clears and flips the screen, so the flip's own reads stay; only the mission's 3D scene, the target box model and the HUD are skipped.
- To find what drawing does that gameplay depends on: in a Debug build, temporarily log a call stack (as `replay_log_stack` in `Replay.cpp` does) from `w_tag` (recording) or `r_tag` (replay) and from `myrand` (`SystemVars.cpp`) whenever `Cosmetic_rand_drawing` is set, once per distinct stack, then record or replay the mission normally. Each logged stack is a recorded read or a gameplay random number made inside drawing. That is how the 2026-10-15 fixes were found; the logging is not committed. If a mission diverges with no such read, replay with `-fulltrace` and TraceDiff for the field (the flyby flag was found that way).

## Done
Pieces 1 and 2 (093683c on `PrePort`; reverted there for a day by 6ab7303, then brought back with the game view fix, 0f4fe41 and 9983a7a; see item 0 below): the game view and the eye position, orientation and shake set in the logic phase; `OF_WAS_RENDERED` set from the view cone there (`game_view_update_was_rendered`); the `-nodraw` flag.

On this branch (first commit, work in progress):
- HUD sounds moved to the logic phase: the homing-missile beep (`hud_update_homing_beep`, `HUDtarget.cpp`), the lock threat warning (`hud_update_lock_threat`, `HUDreticle.cpp`), the subspace and objective notifies (`HUD.cpp`). Called from `HUD_update_3d` and `HUD_update_2d`.
- Talking-head and other HUD anims advance in the logic phase (`anim_update_all`, `anim_show_next_frame(..., blit = 0)`, `AnimPlay.cpp`), since messages wait for them to finish.
- Drawing has its own cosmetic random stream (`Cosmetic_rand_drawing`, `SystemVars.cpp`), set around the drawing calls in `game_frame`, so effects drawn on screen don't shift the numbers that logic-side effects draw (some of which pick sounds, which are in the recording).

Found 2026-10-15 by logging every recorded read and gameplay random number made while drawing (see Test), one commit each:
- `c95bfc4` `nodraw_check.ps1` records with `-playernpc` and the mission's `-at_time`, like `record_autotest.ps1`. Before this nobody flew the player, so lock and combat were never covered.
- `475a3e4` Radar static and its sound (`radar_frame_update`, `hud_update_radar`).
- `64b15fa` Nebula poofs (`neb2_update_player`) and lightning (`nebl_update_all`): timers, jitter, thunder and EMP. The on-screen count that gates thunder and EMP is taken in the game view (`game_view_project`).
- `fb4545f` Laser flyby sound: `WF_CONSIDER_FOR_FLYBY_SOUND` was set when `g3_draw_laser` drew the laser; now `weapon_update_flyby_consider` with `game_view_laser_in_view`.
- `3686177` Piece 3: missile lock (lock point projected in the game view), cargo scan, target box static, target distance, and the hostile ship search that triggers wingman warnings (`hud_update_targeting`, from `HUD_update_3d`).

## Status (2026-10-15, Release, all 46 missions, at 3686177, player flown by the AI)
`draw` matched for all 46. `-nodraw` matched for 36.

The 10 that fail are all capital-ship break-up debris, which `half_ship_render_ship_and_debris` creates only while drawing the ship: AutotestChain, AutotestDamage, AutotestNebStandard, AutotestWeapons, loop1-1, sm2-04, sm2-07, sm3-01, sm3-06, sm3-08. sm2-07 and sm3-01 were confirmed on the failing recordings; the other eight showed the same debris creation in the logging run. That fix is the intended gameplay change on its own branch, `DebrisOffscreenFix` off `PrePort` (decided 2026-10-13): debris always spawns. This branch picks it up through `git merge PrePort`.

**Update 2026-10-15, after merging PrePort with `DebrisOffscreenFix` (f5e3df1):** all 10 now match with `-nodraw`. The other 36 passed before that merge and have not been re-run since.

The earlier tables (2026-10-14 and the first 2026-10-15 run) were recorded without `-playernpc`, so they are not comparable.

**Pieces 1-2 are not gameplay-neutral (found 2026-10-14).** The recordings added later on `RecordPlayback` (the test missions in `data/missions/Autotest*.fs2` and the hand-played SM1-07) pass on `Enhanced` but fail on `PrePort`: Autotest diverges at frame 615, AutotestChain at 2208, SM1-07 at 12815, and AutotestPlayer desyncs at 5580 (diverged at 5558). With 093683c reverted all four match, so the cause is in pieces 1-2 (the game view, the eye set in the logic phase, `OF_WAS_RENDERED` from the view cone). The 34 campaign recordings, in which the AI flies the player ship (`-playernpc`), all pass, and Autotest and AutotestChain keep the player still, so it is not simply the player moving. **Fixed 2026-10-15:** the first difference (Autotest, frame 615) was `OF_WAS_RENDERED` on asteroids. When the player is dead or the supernova is at stage 2 or later, `game_set_view_clip` letterboxes the 3D view to half height, and `g3_start_frame` takes its aspect from that clip; the game view always used the full canvas, so its view cone was taller. `game_view_canvas` (`Render/GameView.cpp`) now cuts the art-resolution canvas the same way, including the shrunken view below 100%. All four recordings then replay identically to `Enhanced` with full traces (the supernova missions, AutotestPlayer whose player dies, and SM1-07).

## Still to do
Roughly in this order; each is one or more commits.
Done 2026-10-15: pieces 1-2 matching on the four newer recordings (the game view letterbox), the drift from frame 2 (it was the laser flyby flag), missile lock and cargo scan (piece 3), nebula lightning (piece 5, apart from debris), and the sound and clock desyncs (they were consequences of the above).
1. Done: **Capital-ship debris** (piece 5, the rest), on `DebrisOffscreenFix`, merged through `PrePort` (f5e3df1).
2. **Squad orders and reinforcements** (piece 4), deferred by Tom 2026-10-15: the squad message menu ended itself while drawing in one old sm2-01 recording (`hud_squadmsg_toggle`); no current failure shows it, but check `HUDsquadmsg.cpp` for gameplay in the drawing path.
3. **Lock start position**: `hud_calculate_lock_start_pos` still takes the screen centre and clamp from `gr_screen.clip_*` (set by `game_set_view_clip`, so identical today). For the game view it should use the game view canvas.
4. **Mouse read in the screen flip**: `gr_d3d_flip` reads the mouse to draw the cursor; move the read to the end of the frame so drawing makes no recorded reads. `-nodraw` still flips, so the check does not cover it.
5. **`next_hit_spark` using `Viewer_obj`** (`Ship.cpp`): not seen in any failure yet.
6. **Debug run**: `tools\nodraw_check.ps1 -Config d` for every mission (Finishing step 1).

## Finishing
1. `tools\nodraw_check.ps1` passes for every mission in Release, and again in Debug (`-Config d`).
2. `git merge PrePort` one last time, rebuild, re-check.
3. Re-record `recordings/autotest` with this branch (move the old folder aside first; the recorder never overwrites), commit.
4. `tools\replay_check.ps1`: everything passes, sm1-01 builds identical.
5. Update the plan in `docs/PREPORT.md` (step 3 done), then `git merge --no-ff LogicDrawSplit` into `PrePort` and run the check again there.
