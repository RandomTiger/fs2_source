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
For each mission it records a fresh run with the current build, replays it normally (`draw`) and with `-nodraw`, and TraceDiffs each against the recorded trace. `summary.txt` in the output folder has one line per mission. All missions take about 1.5 hours.

Reading a failure:
- `nodraw ... DESYNC at frame F: this run asked for X, the recording has Y`: the recorded run made the read Y while drawing (usually a sound or the clock), which `-nodraw` skips, or the other way round. Find what in drawing makes that read and move it to the logic phase.
- `nodraw REPLAY OK / DIVERGED at frame F`: same inputs, different game state. Replay both with `-fulltrace` and TraceDiff again for the field. Frame 2 means something every frame depends on (the camera, the view cone, lock).
- `draw` not MATCH: the build is not deterministic; fix that before anything else (see the `fs2-replay-debugging` notes and `docs/REPLAY.md`).
- `-nodraw` still clears and flips the screen, so the flip's own reads stay; only the mission's 3D scene, the target box model and the HUD are skipped.

## Done
Pieces 1 and 2 (093683c, made on `PrePort`, reverted there by 6ab7303 and kept on this branch; see item 0 below): the game view and the eye position, orientation and shake set in the logic phase; `OF_WAS_RENDERED` set from the view cone there (`game_view_update_was_rendered`); the `-nodraw` flag.

On this branch (first commit, work in progress):
- HUD sounds moved to the logic phase: the homing-missile beep (`hud_update_homing_beep`, `HUDtarget.cpp`), the lock threat warning (`hud_update_lock_threat`, `HUDreticle.cpp`), the subspace and objective notifies (`HUD.cpp`). Called from `HUD_update_3d` and `HUD_update_2d`.
- Talking-head and other HUD anims advance in the logic phase (`anim_update_all`, `anim_show_next_frame(..., blit = 0)`, `AnimPlay.cpp`), since messages wait for them to finish.
- Drawing has its own cosmetic random stream (`Cosmetic_rand_drawing`, `SystemVars.cpp`), set around the drawing calls in `game_frame`, so effects drawn on screen don't shift the numbers that logic-side effects draw (some of which pick sounds, which are in the recording).

## Status (2026-10-14, Release, 27 of the missions)
`draw` matched for all 27. `-nodraw`:

| Result | Missions |
|---|---|
| Identical | sm1-01, sm1-03, loop1-1 |
| Replay OK, traces differ from frame 2 | sm1-05, sm1-08 |
| Replay OK, traces differ later | sm1-09 (563), loop1-3 (244), sm2-06 (1108), sm2-10 (3422), sm3-02 (3466) |
| Desync, sounds out of step (`snd_play_3d` / `snd_time_remaining`) | sm1-04, sm1-06, sm1-07, loop1-2, sm2-03, sm2-05, sm2-07, sm2-09, sm3-01 |
| Desync, recorded run played a sound while drawing (`mouse_pos` vs `snd_play`, nebula missions, likely lightning) | sm2-01, sm2-02, sm2-08, sm3-03 |
| Desync on a clock read drawing makes (`timer_ms`) | sm1-10, sm2-04 |
| Desync, real gameplay difference at frame 430 | sm1-02 |

Not run yet: sm3-04 to sm3-09, loop2-1, loop2-2, and the Autotest* missions (`data/missions/Autotest*.fs2`).

**Pieces 1-2 are not gameplay-neutral (found 2026-10-14).** The recordings added later on `RecordPlayback` (the Autotest* missions, re-recorded with `-playernpc` so the AI flies the player ship, and the hand-played SM1-07) pass on `Enhanced` but fail on `PrePort`: Autotest diverges at frame 615, AutotestChain at 2208, SM1-07 at 12815, and AutotestPlayer desyncs at 5580 (diverged at 5558). With 093683c reverted all four match, so the cause is in pieces 1-2 (the game view, the eye set in the logic phase, `OF_WAS_RENDERED` from the view cone). The original 34 autotest missions never move the player ship, which is why they didn't show it.

## Still to do
Roughly in this order; each is one or more commits.
0. **Make pieces 1-2 match when the player flies.** Replay AutotestPlayer (the earliest clear case) on `PrePort` and on `Enhanced` with `-fulltrace` and TraceDiff the two for the first differing field. Suspects: the eye position or orientation in the logic phase differing from the one drawing sets (shake, chase or padlock views, zoom), and the view-cone test disagreeing with `obj_render_all` near the edge of the screen (same-team collisions, fireball freeing, shield effects read `OF_WAS_RENDERED`). This comes first: every other result builds on it.
1. **Drift from frame 2** (sm1-05, sm1-08 and probably the desyncs that diverge at frame 2 before desyncing, such as sm1-06, sm1-07, sm2-01). Something read every frame still comes from drawing: likely missile lock (`hud_update_lock_indicator`, in screen pixels) or something still using the display instead of the game view.
2. **Missile lock and cargo scan** (piece 3): `HUD_render_3d` → `hud_update_lock_indicator` (`HUDtarget.cpp`, `HUDlock.cpp`) and `hud_cargo_scan_update` → `player_inspect_cargo`, `ship_do_cargo_revealed`. Lock moves to the game view.
3. **Effects and sounds out of step**: the most common desync. Sparks, impact effects and their sounds still started from drawing code; find the caller of the sound in each desync (Debug replays log the call stack).
4. **Nebula lightning** (piece 5): `nebl_render_all` applies EMP, plays thunder and draws gameplay random numbers while drawing.
5. **Clock reads in drawing** (sm1-10, sm2-04): find the `timer_ms` call in drawing; either it moves to the logic phase or, if it only affects drawing, it reads an unrecorded clock (as was done for the timing display, 91fe2c0).
6. **sm1-02 frame 430**: a real state difference; investigate with `-fulltrace`.
7. **Squad orders, reinforcements, wingman warnings** (piece 4): HUD-gated gameplay, same treatment as lock.
8. **Mouse read in the screen flip**: `gr_d3d_flip` reads the mouse to draw the cursor; move the read to the end of the frame so drawing makes no recorded reads.
9. **Brackets and subsystem view**: `Player->subsys_in_view` set while drawing brackets (`HUDbrackets.cpp`), `next_hit_spark` using `Viewer_obj` (`Ship.cpp`).

## Finishing
1. `tools\nodraw_check.ps1` passes for every mission in Release, and again in Debug (`-Config d`).
2. `git merge PrePort` one last time, rebuild, re-check.
3. Re-record `recordings/autotest` with this branch (move the old folder aside first; the recorder never overwrites), commit.
4. `tools\replay_check.ps1`: everything passes, sm1-01 builds identical.
5. Update the plan in `docs/PREPORT.md` (step 3 done), then `git merge --no-ff LogicDrawSplit` into `PrePort` and run the check again there.
