# Fix review: CollideShipWeapon.cpp `check_inside_radius_for_big_ships` uninitialised `hit_time`

**Recommendation: TAKE.** Low risk.

Found by the clang static analyser scan (`core.UndefinedBinaryOperatorResult`), not by the fs2_source_ue audit.

## The bug
`check_inside_radius_for_big_ships` handles a weapon that is inside a big ship's radius. It calls `ship_weapon_check_collision(..., &hit_time)` and branches on `hit_time` whenever that returns a hit:
```cpp
	int hit_time;	// estimated time of hit in ms

	if ( ship_weapon_check_collision( ship, weapon, limit_time, &hit_time ) ) {
		if (hit_time < 0) {
			return 1;                                        // hit this frame
		} else if (hit_time > 200) {
			pair->next_check_time = timestamp(hit_time - 200);
			return 0;
		} else {
			pair->next_check_time = 1;                       // check next frame
			return 0;
		}
```
`ship_weapon_check_collision` has a second way of reporting a hit. A homing missile older than half a second, within its inner radius of its homing target, and already past it, has `lifeleft` set to 0.001 and returns 1. That path never writes `*next_hit`, so `hit_time` is stack garbage and picks the branch at random.

## Proposed fix
```diff
-	int hit_time;	// estimated time of hit in ms
+	// Claude: Initialised to -1.  ...
+	int hit_time = -1;	// estimated time of hit in ms
```
`-1` takes the "hit this frame" branch. That is the intent: the missile has just been told to detonate.

## Effect on mission flow
None at a high level. Whatever branch is taken, the missile's `lifeleft` is already 0.001, so it detonates on the next frame. The garbage only decides whether that missile/ship pair is checked again in the meantime. It's at most one frame of collision checking for a missile that is about to explode, and only for homing missiles near big ships.

## Effect on replays
Today Release depends on stack contents here, which is a possible source of Debug/Release divergence. After the fix it is deterministic. A recording that hits this path could replay one frame differently, but the existing recordings have already been deleted.
