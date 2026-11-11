# Fix review: beam whack normalizes a null vector

Issue #291. **Recommendation: FIX (steward, D-20261109-092602).** When a beam hits a ship whose centre lies exactly on the beam line, the whack direction is the null vector. `vm_vec_normalize` then raises the Debug warning "Null vector in vector normalize" and substitutes 1,0,0. `beam_apply_whack` now makes that same substitution itself, with the same test, so the result is bit-identical and the warning is gone.

## The bug
`code/Weapon/Beam.cpp`, `beam_apply_whack`, as released:

```
	vm_vec_dist_to_line(&objp->pos, &b->last_start, &b->last_shot, &temp, &dist);
	vm_vec_sub(&whack_dir, &objp->pos, &temp);
	vm_vec_normalize(&whack_dir);
```

`temp` is the nearest point on the beam line to the ship's centre. If the centre is on the line, `whack_dir` is 0,0,0. `vm_vec_copy_normalize` (`code/Math/VecMat.cpp`) tests `vm_vec_mag(src) <= 0.0f`, warns, and sets 1,0,0. Seen in an autoplay run of `sm3-05` about 15 s in (trace: `vm_vec_copy_normalize` ← `vm_vec_normalize` ← `beam_apply_whack` ← `beam_handle_collisions` ← `beam_move_all_post`). Ordinary geometry; nothing outside the original code sets it up.

## Fix
`if (vm_vec_mag(&whack_dir) <= 0.0f)` set 1,0,0, else `vm_vec_normalize` as before. The test and threshold are exactly those of `vm_vec_copy_normalize`, so both paths give the same bits as retail. Every changed line carries a `// Claude:` comment.

## What was checked
Gate on this branch per the planner: Release and Debug build. The replay proof that no recording moves is RecordPlayback's merge gate, which names this fix. The held autoplay recording of `sm3-05` replays in Debug with no warning after the merge.

## Not changed
- `vm_vec_copy_normalize` keeps its warning: other callers that reach it are separate bugs.
- The 1,0,0 direction is arbitrary, but it is what retail applies.

## Effect on mission flow
None: the same whack is applied in both builds. No missions were re-recorded.
