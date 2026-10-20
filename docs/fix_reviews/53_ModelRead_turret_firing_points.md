# Fix review: a turret with MAX_TFP firing points trips the Debug Assert

**Recommendation: TAKE.** Debug only: `model_load` Asserts that a turret has fewer than `MAX_TFP` firing points, but the array it reads them into holds `MAX_TFP` entries, so a turret with exactly `MAX_TFP` is valid and the Assert is wrong by one. Release compiles the Assert out, so no recording moves. Found by the FS1 compatibility work (`FS1TurretPoints`, 2026-10-18): FS1's `freighter03.pof` has a turret with 4 points. It reaches FS2's layers through the FS1Compat merge into `Enhanced`, accepted by the steward with this review as its condition (D-20261020-103501, item 7). Whether to backport it to `OriginalSourceBugFixes` is Tom's decision at review.

## The bug
`code/Model/Model.h`:

```
#define MAX_TFP						4				// maximum number of turret firing points
...
vector	turret_firing_point[MAX_TFP];		//	in parent object's reference frame, point from which to fire.
```

`code/Model/ModelRead.cpp`, reading a turret's points from the model (the `ID_TGUN` chunk), as Volition wrote it:

```
n_slots = cfread_int( fp );
subsystemp->turret_gun_sobj = physical_parent;
Assert(n_slots < MAX_TFP);		// only MAX_TFP firing points per model_subsystem
for (j = 0; j < n_slots; j++ )	{
	cfread_vector( &subsystemp->turret_firing_point[j], fp );
}
Assert( n_slots > 0 );

subsystemp->turret_num_firing_points = n_slots;
```

The loop writes `turret_firing_point[0]` to `[n_slots - 1]`. With `n_slots == MAX_TFP` that is `[0]` to `[3]`, all inside the 4-entry array. The Assert refuses that case, although its own comment says "only MAX_TFP firing points", meaning up to and including `MAX_TFP`.

## Developers' intent
Stop a model from writing past the end of `turret_firing_point[]`. The bound that does that is `n_slots <= MAX_TFP`; `<` also refuses the one legal count that fills the array. It is a plain off-by-one in a range check.

## Proposed fix
`Assert(n_slots <= MAX_TFP);` (the `FS1TurretPoints` commit f9cd111, now on Enhanced's line through the merge, where the loop variable is Enhanced's `turret_slot`). Nothing else changes.

## What was checked
- Every reader of the count and the array stays inside it: the copy for a duplicate subsystem (`ModelRead.cpp`, `nfp < turret_num_firing_points`), the AI's gun position (`AiCode.cpp`, `turret_next_fire_pos % turret_num_firing_points`), the eye position (`FreeSpace.cpp`, index 0), and the `> 0` tests (`PlayerControl.cpp`, `AiCode.cpp`, `ModelRead.cpp`). None assumes fewer than `MAX_TFP` points.
- No FS2 model has a turret with 4 points: every FS2 recording replays in Debug without this Assert firing, so FS2 data never reached `n_slots == MAX_TFP`. The changed line is reachable in FS2 data only with a model that does.
- FS1 data: with the fix, sm1-10a, sm2-01a and sm2-04a (which load `freighter03.pof`) load and replay in Debug; before it, Debug stopped at the Assert in each.
- The FS1Compat merge gate (2026-10-20, Release and Debug): the 47 retail recordings and the 25 FS1 recordings replay identically, with no Assert.

## Not changed
- `MAX_TFP` itself, and the `ID_TGUN` read.

## Effect on mission flow
None. The Assert is compiled out of Release, and in Debug it only fired for models that FS2's data does not have.
