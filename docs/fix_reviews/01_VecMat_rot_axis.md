# Fix review: VecMat.cpp `vm_matrix_to_rot_axis_and_angle` (180° case)

**Recommendation: TAKE. Re-record SM1-01 and SM1-02 afterwards.**

## The bug
When a rotation is almost exactly 180°, the axis is taken from the matrix's largest diagonal term. In all three cases the code takes the reciprocal of the axis component *before* setting that component. So it divides by whatever was already in the caller's `rot_axis`.

The caller is `vm_matrix_interpolate` (VecMat.cpp ~1969). There `vector rot_axis;` is an uninitialised local, so:
- **Debug:** the value is 0xCCCCCCCC, filled in by the runtime checks.
- **Release:** the value is whatever earlier code left on the stack.

The other two components come out scaled by the wrong amount. After normalising, the axis points in a wrong direction that depends on the stack.

```diff
 		case 0:
 			float ix;
-			ix = 1.0f / rot_axis->x;
-
 			rot_axis->x = fl_sqrt(m->a2d[0][0] + 1.0f);
+			ix = 1.0f / rot_axis->x;
+
```
Cases 1 (`iy`) and 2 (`iz`) get the same one-line move. Case 2 has no normalise of its own, but the `vm_vec_normalize` after the switch covers it.

## Effect on mission flow
- **Where it's reached:** `ai_turn_towards_vector`, via the interpolate call. That is every AI ship turning towards a goal, and also the player in some autopilot/AI-controlled cases.
- **When it fires:** only when the goal orientation is about 180° from the current one, for example a fighter reversing onto a target directly behind it.
- **Before the fix:** the ship briefly turns about the wrong axis, then corrects on later frames once the angle is no longer near 180°.
- **After the fix:** it turns about the correct axis straight away.

This is a few frames of turning in a slightly different direction. It doesn't change goals, targets, events or scripting. Dogfight outcomes are chaotic, though, so over a long mission small differences can change *which* fighter dies *when*. Mission events and objectives stay the same; individual kills may vary.

## Effect on replays
- **Today:** Release behaviour depends on stack contents, so *any* code change that shifts the stack can change it. That makes existing Release recordings fragile against unrelated edits. It's the cause of the Debug/Release desync at SM1-02 frame 11969.
- **After the fix:** the result is deterministic and matches across builds.
- **Recordings:** existing recordings that hit this branch will desync and must be re-recorded.

## Why take it
It removes a source of nondeterminism that would otherwise break the replay-verification goal at random. It is the documented intent of the maths: for a 180° rotation `m = 2aaᵀ − I`, so `sqrt(m_ii + 1)` and `m_ij / that` share the same scale. The change is three lines.

Note: the fs2_source_ue audit (`CODE_ISSUES.md`) found the same bug independently.
