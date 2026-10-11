# Fix review: Physics.cpp `physics_predict_vel` constant-velocity branch

**Recommendation: SKIP.** It's unused, so it doesn't fit the minimum-changes rule.

## The bug
```cpp
	if (pi->flags & PF_CONST_VEL) {
		predicted_vel = &pi->vel;      // reassigns the local pointer
```
This should be `*predicted_vel = pi->vel;`. As written, the caller's output is never written for constant-velocity objects.

## Effect on mission flow
None. Nothing calls `physics_predict_vel`: the only references are its definition (Physics.cpp:795) and its declaration (Physics.h:250). The related function that *is* used, `physics_predict_pos_and_vel`, doesn't have this bug.

## Effect on replays
None.

## Why skip
It's dead code. Fixing it changes nothing at runtime and adds a diff to fs2_source. Fix it in the port if the function is ever used.
