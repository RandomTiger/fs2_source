# Fix review: a ship class sharing a model gets the stepped rotation flag but no data

**Recommendation: TAKE (steward D-20261024-074459, option A).** `model_copy_subsystems` copies a subsystem's `flags`, including `MSS_FLAG_STEPPED_ROTATE`, but not its `stepped_rotation` pointer. A second ship class on a model with a stepped subsystem then has the flag and a NULL pointer, and `submodel_stepped_rotate` dereferences NULL: a crash. Retail data never reaches it; only mod data does. Issue #100, found while fixing #8 (review 61).

## The bug
`code/Model/ModelRead.cpp`, `model_copy_subsystems`, runs when a ship class's model was already loaded by another class (`Ship.cpp`, the page-in loop that calls it), so the model's subsystem data was written only into the first class's list:

```
			if ( !stricmp( source->subobj_name, dest->subobj_name) ) {
				dest->flags = source->flags;
				...
				dest->turret_gun_sobj = source->turret_gun_sobj;
```

`dest->stepped_rotation` keeps the NULL `parse_ship` set. `AiCode.cpp` calls `submodel_stepped_rotate` whenever the flag is set, and it reads `psub->stepped_rotation->num_steps` and the other fields.

## Retail reach
- FS2 retail: one model has `$stepped` in its subsystem properties, `miner2s-01.pof` (in `sparky_fs2.vp`), used by one ship class, SSG Rahu. No second class shares it, so the copy never runs for a stepped subsystem.
- FS1 retail and both demos: no model has `$stepped`.

So no retail mission takes the changed path.

## Fix
In `model_copy_subsystems`, after the named fields are copied:
- if `dest->stepped_rotation` is not NULL (from an earlier copy, since models are read again each level), `delete` it and set it to NULL;
- if `source->stepped_rotation` is not NULL, allocate a new block for `dest` and copy the source block into it.

Each class owns its own block. The pointer is never shared, because since review 61 `ship_close` deletes every class's blocks, and a shared pointer would be freed twice. The change carries a `// Claude:` comment. No Warning or Assert is added.

## What was checked
- Retail reach, above: every POF in the FS2 and FS1 VP files searched for `$stepped`, and `ships.tbl` for the classes using the one hit.
- Builds: Release and Debug, on RecordPlayback after the merge.
- `rp_gate.ps1`: all 48 recordings, Release and Debug, none changed.

## Not changed
- The other fields `model_copy_subsystems` leaves out. Only the stepped rotation pointer goes with a copied flag that dereferences it.

## Effect on mission flow
None on retail data: the copy path is never taken for a stepped subsystem. With mod data, a second class on a stepped model rotates that subsystem like the first class instead of crashing.
