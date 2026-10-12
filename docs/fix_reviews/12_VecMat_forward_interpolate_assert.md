# Fix review: vm_forward_interpolate asserts when the goal is straight behind

**Recommendation: TAKE.** No risk: it changes only a Debug `Assert`.

Found because Debug replays of Release recordings desynced in six missions (sm1-04, sm1-07, loop1-2, sm3-03, sm3-05, sm3-07). In Debug, this assert fires many times a mission: an autotest of sm1-07 hit it 45 times. A failed `Assert` calls `gr_activate(0)`, which minimises the window, even when the autotest only logs it. Losing focus clears `Mouse_hidden` on the window thread, which is not recorded, so the next page flip draws the cursor and the replay desyncs.

## The bug
`vm_forward_interpolate` finds the axis to turn about as the cross product of the current forward vector and the goal, divided by its length `t`:
```cpp
vm_vec_crossprod( &rot_axis, &orient->fvec, goal_f );
float t = vm_vec_mag(&rot_axis);
...
if ( t < SMALLER_NUM )  {		// SMALLER_NUM is 1e-20
    ...
} else {
    theta = (float) asin( t );
    vm_vec_scale ( &rot_axis, 1/t );
    if ( z_dotprod < 0.0f )
        theta = PI - theta;
}
...
vm_vec_copy_scale(&theta_goal, &local_rot_axis, theta);
Assert(fl_abs(theta_goal.z) < 0.001f);		// check for proper rotation
```
The axis is at right angles to the forward vector, so its `z` in ship coordinates should be 0. When the goal is almost straight behind, `t` is tiny and `theta` is close to pi. Float rounding in the cross product, about 1e-7, is divided by `t`, so `z` is about `3.6e-7 / t`. That passes 0.001 once `t` is below about 3.6e-4.

Logging every failure in an autotest of sm1-07 showed this every time. The goal was a unit vector, the dot product with the forward vector was -1 within float precision, `t` was between 5e-7 and 3e-5, and `theta_goal.z` was between 0.001 and 0.059.

`theta_goal.z` is not used after the assert. The function uses only `x` and `y` for the turn, and handles bank separately. So the assert flags rounding that has no effect.

## Proposed fix
Skip the check when `t` is below 0.001:
```cpp
Assert((fl_abs(theta_goal.z) < 0.001f) || (t < 0.001f));
```

## Not changed
`SMALLER_NUM` (1e-20) is far below float precision, so a goal straight behind still takes the `asin` path with an axis that is mostly rounding. The axis direction is arbitrary in that case anyway, since any axis at right angles to the forward vector turns the ship around. Changing it would change how AI ships turn, so it is left alone.

## Effect on mission flow
None. `Assert` is compiled out of Release builds, and in Debug the condition has no side effects.
