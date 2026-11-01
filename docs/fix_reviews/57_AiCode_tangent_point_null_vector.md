# Fix review: get_tangent_point normalizes a null vector when the ship faces its target

**Recommendation: TAKE.** `get_tangent_point` builds a sideways vector from two cross products and normalizes it with `vm_vec_normalize`. When the ship points straight at the point it orbits, that vector is zero; `vm_vec_normalize` substitutes (1,0,0) and, in Debug only, raises a Warning. The fix calls `vm_vec_normalize_safe`, which returns the same (1,0,0) without the Warning. The result is identical in every build, so nothing in the game changes. Found by AutotestAI's Debug replay (two Warnings, exit 2, #78). Ruled by the steward (D-20261022-133901).

## The bug
`code/Ship/AiCode.cpp`, `get_tangent_point`:

```
vm_vec_normalized_dir(&vec_to_point, point, &objp->pos);
vm_vec_crossprod(&up_vec, &vec_to_point, &objp->orient.fvec);
vm_vec_crossprod(&perp_vec, &vec_to_point, &up_vec);
vm_vec_normalize(&perp_vec);
```

If `objp->orient.fvec` is parallel to `vec_to_point`, `up_vec` is zero, so `perp_vec` is zero. `vm_vec_normalize` → `vm_vec_copy_normalize` (`code/Math/VecMat.cpp`) then sets (1,0,0), returns 1.0, and calls `Warning` ("Null vector in vector normalize").

Caller seen: `ai_cruiser_chase` → `ai_chase_big_circle_set_goal` → `get_tangent_point`, at about 45 s of mission time in AutotestAI (a cruiser heading straight at its target).

## Developers' intent
The authors wrote `vm_vec_normalize_safe` for exactly this input: its comment reads "If vector is 0,0,0, return 1,0,0. Don't generate a Warning()." A cruiser facing its target is a normal game situation, not a coding error, so the Warning is noise here; the (1,0,0) fallback is the behaviour the game has always had.

## Proposed fix
Replace `vm_vec_normalize(&perp_vec)` with `vm_vec_normalize_safe(&perp_vec)`, with a `// Claude:` comment. One line.

## Why the result is identical
- Null vector: both return 1.0 and leave (1,0,0).
- Non-null vector: both compute `m = vm_vec_mag(v)`, `im = 1.0f / m`, and multiply each component by `im` (`vm_vec_copy_normalize` with `dest == src`). Same operations in the same order, so the same bits.
- So the replay traces do not move and no recording is re-recorded.

## What was checked
- A Debug replay of AutotestAI with a temporary stack log on the Warning (removed) named both Warnings' caller as above.
- OriginalSourceBugFixes: Release and Debug compile; no new warnings against VS2022.
- At RecordPlayback after the merge-up: the quick suite, every recording MATCH in Release and Debug, AutotestAI Debug exit 0. Results are on #78 and in the merge commit message.

## Not changed
- The other `vm_vec_normalize(&perp_vec)` in `AiCode.cpp` (near line 1793) is a different function and was not seen to warn.
- `vm_vec_copy_normalize`'s Warning stays: it still traps genuine bad input elsewhere.

## Effect on mission flow
None: the ship gets the same goal point as before.
