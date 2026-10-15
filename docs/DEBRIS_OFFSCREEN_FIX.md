# Capital-ship debris off screen

Branch `DebrisOffscreenFix`, off `PrePort`, folder `fs2_DebrisOffscreenFix`. An **intended gameplay change**, listed in `docs/GAMEPLAY_CHANGES.md`. Decided 2026-10-13 as part of PrePort step 3 (`docs/PREPORT.md`); limits decided 2026-10-15.

## What the original did
When a capital ship is destroyed it splits in two, and the two halves burn away from the split plane (`shipfx_large_blowup_do_frame`, `ShipFX.cpp`). As the burn passes each debris submodel, that piece breaks off as a free debris object, which flies, collides and does damage.

The break-off was done in `half_ship_render_ship_and_debris`, the function that draws the halves, called from `ship_render` through `obj_render_all`. So the pieces only broke off while the ship was being drawn: in the view cone and not hidden by the nebula. A ship that blew up behind the player, or out of sight in the nebula, left no debris; one that came into view half way through left only the pieces still attached. The same mission played differently depending on where the player was looking, and a game that does not draw (a headless build, or the port, where Godot draws) would have no capital-ship debris at all.

## The change
- `half_ship_create_debris` (`ShipFX.cpp`) makes the break-off test and creates the pieces, with the same positions, velocities and random numbers as before. `shipfx_large_blowup_do_frame` calls it every frame for each half still burning (`length_left > 0`, the condition the drawing used), after the fireball wipe. It runs whether or not the ship is on screen.
- `half_ship_render_ship_and_debris` only draws the pieces still attached.
- `MAX_DEBRIS_PIECES` (`Debris.h`) is 128 instead of 64, so the extra debris from break-ups the player is not looking at is not dropped for lack of slots. `MAX_HULL_PIECES` (10, `Debris.cpp`) is unchanged: when an 11th large hull piece appears the oldest still blows up, as it always did when a break-up was on screen. (Tom, 2026-10-15: raise the slots only.)

A break-up that is on screen happens as before, except that the pieces break off a little earlier in the frame (in the simulation, before drawing), so pieces from two ships breaking up at once are created in ship order instead of drawing (depth) order.

## Recordings
Re-recorded with this change (`tools\record_autotest.ps1`, the AI flying the player): AutotestAI, AutotestChain, AutotestDamage, AutotestNebActive, AutotestNebMedium, AutotestNebStandard, AutotestWeapons, loop1-1, sm2-04, sm2-07, sm2-09, sm3-03, sm3-06. Every other recording replays unchanged.

## Checked
- `tools\replay_check.ps1`: the recordings above failed before re-recording, all others passed, sm1-01 builds identical.
