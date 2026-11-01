# Fix review: a subsystem's stepped rotation block is allocated with new and never freed

**Recommendation: FIX by deleting the block before it is allocated again and at shutdown (steward D-20261024-063231, option 1).** `set_subsystem_info` allocates a `stepped_rotation` block for every subsystem with `$stepped` in its properties, each time its model is read. Nothing deletes it. Models are freed and read again at every level start (`model_free_all` in `game_level_init`), so the leak is one block per stepped subsystem per mission, and it grows with in-process restarts. No gameplay effect. Issue #8.

## The bug
`code/Model/ModelRead.cpp`, `set_subsystem_info`:

```
		// CASE OF STEPPED ROTATION
		if ( (p = strstr(props, "$stepped")) != NULL) {

			subsystemp->stepped_rotation = new(stepped_rotation);
```

`code/Ship/Ship.cpp`, `ship_close` frees the subsystem list but not the blocks it points to:

```
		if ( Ship_info[i].subsystems != NULL ) {
			free(Ship_info[i].subsystems);
		}
```

## Every user of the field
`git grep -n stepped_rotation`:
- `MODEL.H`: the struct and the `model_subsystem::stepped_rotation` pointer.
- `ModelRead.cpp` `set_subsystem_info`: the only allocation, then writes of its fields.
- `ModelRead.cpp` `submodel_stepped_rotate`: reads only.
- `Ship.cpp` `parse_ship`: sets the pointer to NULL on its stack array before parsing. The array is then struct-copied into the malloc'd `Ship_info[i].subsystems`, while the pointer is still NULL. Models are read later, at page-in.

Nothing else frees, copies or shares the pointer:
- `model_load` (every caller) passes `&sip->subsystems[0]` or NULL, so the block is always written into a `Ship_info` subsystem list. That list owns it.
- `model_copy_subsystems` (for ships that share a model) copies named fields one by one and does not copy `stepped_rotation`. So no second list aliases the block.
- No code copies `Ship_info` entries after models are read.

So the pointer in `Ship_info[i].subsystems[j]` is always NULL or the only reference to its own block.

## Fix
- `set_subsystem_info`: if the pointer is not NULL, `delete` it before the `new`. This frees the block from the previous read of the same model. The new block is filled in full, as before.
- `ship_close`: before `free(Ship_info[i].subsystems)`, `delete` each subsystem's non-NULL `stepped_rotation` and set it to NULL.

Both use `delete`, matching the `new`. Both lines carry a `// Claude:` comment. The brief named the free at model close only. That alone would free just the last read's blocks, since every level start reads the models again; the delete before `new` covers those earlier reads.

## What was checked
- The field survey above.
- Builds: Release and Debug, on RecordPlayback after the merge.
- `rp_gate.ps1`: all 48 recordings, Release and Debug. A double free or a bad delete would show as a Debug heap assert.

## Not changed
- `model_copy_subsystems` copies `flags`, including `MSS_FLAG_STEPPED_ROTATE`, but not the pointer. A second ship class sharing a model with a stepped subsystem would then have the flag with a NULL pointer. No retail table seems to hit this (nothing has crashed); it is a separate issue and out of scope.
- `ai_rotation`, the other pointer `parse_ship` sets to NULL, is never allocated.

## Effect on mission flow
None. The block's values are the same; only its lifetime changes. Nothing reads the freed block: the delete before `new` is followed at once by the new allocation, and `ship_close` runs at shutdown. It does not touch `rand()` or game state, so none of the 48 recordings can move.
