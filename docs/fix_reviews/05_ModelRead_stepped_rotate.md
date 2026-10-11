# Fix review: ModelRead.cpp `submodel_stepped_rotate` unset `ang_prev`/`ang_next`

**Recommendation: SKIP.** Retail data never hits it.

## The bug
```cpp
	float *ang_prev, *ang_next;
	switch( sm->movement_axis ) {
	case MOVEMENT_AXIS_X: ...
	case MOVEMENT_AXIS_Y: ...
	case MOVEMENT_AXIS_Z: ...
	}
```
There is no `default`. If the axis is anything other than X, Y or Z, both pointers are garbage and the later writes through them corrupt memory.

## When it can happen
`movement_axis` is read straight from the POF file (ModelRead.cpp:1434). Stepped rotation is used by a few rotating subsystems, such as radar dishes. A shipped model with stepped rotation on a non-X/Y/Z axis would already crash or corrupt memory in retail. FS2 doesn't, so retail data doesn't take this path. Its sibling function `model_get_rotating_submodel_axis` asserts the same assumption (line 2615).

## Possible fix (not recommended now)
```diff
 	case MOVEMENT_AXIS_Z:
 		...
 		break;
+
+	default:
+		Int3();
+		return;
 	}
```

## Effect on mission flow
None with retail data.

## Effect on replays
None.

## Why skip
It only guards against bad or modded model data. It changes nothing for retail missions, so it isn't a minimum change. Worth adding in the Godot port's model loader as validation instead.
