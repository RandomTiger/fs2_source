# Fix review: a stealth sweep can run past its last point and steer on an unset vector

Issue #309. **Recommendation: FIX.** A fighter sweeping for a stealth ship can skip the "sweep done" state. From then on it steers every frame towards an uninitialised stack vector, so where it flies depends on stack contents: the x64 build flies elsewhere than the x86 build, and the autoplay recording of sm2-03 desyncs on x64.

## The bug
`ai_stealth_sweep` steps through seven box points with `aip->submode_parm0` (`SM_SS_BOX0` 0 to `SM_SS_BOX2` 6). It can add one twice in one frame:

```
	if (dist_to_goal < 15) {
		aip->submode_parm0++;
	}

	// check for collision with big ship
	if (maybe_avoid_big_ship(Pl_objp, En_objp, aip, &goal_pt, 10.0f)) {
		// skip to the next pt on box
		aip->submode_parm0++;
		return;
	}
```

At the last point (6) that gives 8. `ai_chase` ends the sweep only on an exact match:

```
		} else if ( /*(Missiontime - aip->submode_start_time) > i2f(30) || */(aip->submode_parm0 == SM_SS_DONE) ) {
```

so 8 never ends it. Every later frame, the sweep's `switch` takes `default: Int3();` (empty in Release). `goal_pt` is never set, and the ship turns towards it and keeps adding to the counter. In sm2-03, two wingmen were at 78 and 69 and still climbing.

## Fix
End the sweep on `submode_parm0 >= SM_SS_DONE`. This check runs in the same frame, after the sweep moved the counter, so the counter never reaches the `switch` out of range.

## What was checked
- A DIAG log of `submode_parm0` in `ai_turn_towards_vector` (PrePort64, sm2-03 autoplay) showed the counter climbing past 7 in sweep mode, with `dest` holding different garbage on x86 and x64.
- The other ways out of the sweep (target seen, back to find mode) leave sweep mode before the counter matters.

## Not changed
- The `default: Int3();` in the sweep stays. With the fix it is unreachable.
- The commented-out 30-second limit stays commented out.

## Effect on mission flow
A ship whose sweep overran now gives up the sweep (it clears its target and goes back to attack mode) instead of flying towards a random point. This was already meant to happen when the sweep ended normally. Recordings in which a sweep overran play differently and get re-recorded. Missions without stealth ships are unaffected.
