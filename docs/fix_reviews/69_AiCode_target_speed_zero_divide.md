# Fix review: AI throttle is 0/0 for a stopped ship with dead engines

Issue #267. **Recommendation: FIX (steward, D-20261108-175954).** When an AI ship's engines are destroyed its top speed becomes 0. A ship flying waypoints that has coasted to a stop and turns away from its goal asks for a target speed of 0, and `set_accel_for_target_speed` computes the throttle as 0/0, which is NaN. The NaN passes every clamp in the physics, spreads to the ship's velocity and position, and the game crashes. 0/0 now gives a throttle of 0; every other case is unchanged.

## The bug
`code/Ship/AiCode.cpp`, `set_accel_for_target_speed`, as released:

```
	max_speed = Ships[objp->instance].current_max_speed;

	AI_ci.forward = tspeed/max_speed;
	aip->prev_accel = AI_ci.forward;
```

How both operands reach 0:
- `current_max_speed`: `code/Hud/HUDets.cpp`, `update_ets`, multiplies it by `fl_sqrt(strength)` when the engine subsystem strength is under 0.5. With every engine destroyed the strength is 0, so the top speed is set to 0 every frame. The code makes the zero divisor itself.
- `tspeed`: `ai_waypoints` (`AiCode.cpp`, just after the `nprintf` of the waypoint dots) calls `set_accel_for_target_speed(Pl_objp, Pl_objp->phys_info.speed * 0.95f)` whenever the ship points further from its goal than last frame. A disabled ship drifts to speed 0, and while it still rotates the call is made with 0.

`physics_read_flying_controls` clamps `ci->forward` with `>` and `<` comparisons, which are false for NaN, so the NaN goes into `goal_vel.z = ci->forward * max_vel.z`, then `prev_ramp_vel.z`, `desired_vel`, `vel` and `pos`. The next frame draws from a NaN position and crashes in `stars_draw`.

Probe log from the crash (FS1 data, `sm2-01a`, the transport Sigma 1 on its waypoint path, engines destroyed): `ci forward=NaN`, `max_vel=0,0,0`, `prev_ramp_vel.z` 0.4459 then NaN.

The intent is plain from the code: a ship that cannot move and is asked for speed 0 should have throttle 0. The division was written for a ship with a working top speed.

## Fix
`set_accel_for_target_speed`: when `max_speed` and `tspeed` are both 0, `AI_ci.forward` is 0; otherwise the original division runs unchanged. Every changed line carries a `// Claude:` comment.

## What was checked
Gate on this branch (planner, L0): Release and Debug build, no warning from the changed lines, both exes rebuilt. This branch has no recordings; the first replay proof that no recording moves is RecordPlayback's next merge gate, whose brief must name this fix.

FS1Compat (the same change, cherry-picked): the FS1 replay set and live FS1 autoplay of `sm2-01a`, which crashed before; results in the FS1Compat commit.

## Not changed
- A non-zero target speed with a top speed of 0 still divides to plus or minus infinity. The physics clamps that to plus or minus 1 and multiplies it by `max_vel` 0, so the ship stays put; `prev_accel` keeps the infinity, as in retail. Changing it would move behaviour no crash depends on (steward rejected the wider fix).
- `ai_waypoints`' later check `max_allowed_speed / shipp->current_max_speed` can only run when `prev_accel * current_max_speed` exceeds the cap, which is false when the top speed is 0.
- Other callers of `set_accel_for_target_speed` that can pass 0 (formation and guard code) take the same fix through this one function.

## Effect on mission flow
None for any mission that played to its end before: the changed case always ended in a NaN and a crash, so no recording can contain it, and none moves. No missions were re-recorded. Missions that crashed this way (seen with FS1 data under FS1Compat) now play on. FS2 data can reach it too, with any waypoint ship whose engines are all destroyed.
