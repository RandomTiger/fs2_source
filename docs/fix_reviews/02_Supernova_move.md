# Fix review: Supernova.cpp `supernova_set_view` uninitialised `move`

**Recommendation: TAKE.**

## The bug
`vector move;` (line 360) is passed to `vm_vector_2_matrix(&whee, &move, ...)` at line 372, before anything writes it. `move` is only set later, in the `else` branch (line 392). The camera position is then built from `whee`:

```cpp
	matrix whee;
	vm_vector_2_matrix(&whee, &move, NULL, NULL);
	vm_vec_scale_add(&Supernova_camera_pos, &Player_obj->pos, &whee.rvec, sn_cam_distance);
	vm_vec_scale_add2(&Supernova_camera_pos, &whee.uvec, 30.0f);
```

In the `else` branch the value that `move` gets is fixed:

```
at   = player_pos + sun * sn_distance
move = normalize(player_pos - at) = -sun
```

The intent is therefore `move = -sun`. `sun` has already been computed and normalised by line 368.

## Proposed fix
```diff
 	vm_vec_normalize(&sun);
+
+	// Claude: move was used below before being set; the else branch always sets it to -sun.
+	move = sun;
+	vm_vec_negate(&move);
```
The `else` branch is left as it is; it recomputes the same value.

## Effect on mission flow
None. This is only the camera for the supernova cutscene at the end of the final campaign mission. The player has no control there and the mission is already won or lost.
- **Before the fix:** the camera sits at an offset that depends on stack contents. Debug uses 0xCC (a fixed but wrong diagonal); Release uses whatever is on the stack. The offset can change between builds or even between frames.
- **After the fix:** the camera sits to the side of the player, perpendicular to the sun direction, as designed.

## Effect on replays
Camera only, so there is no object-state change. It also stops a possible stack-dependent NaN or null-vector warning in `vm_vector_2_matrix`. Existing recordings don't reach this code.

## Why take it
It's visible, deterministic, and does not affect gameplay.
