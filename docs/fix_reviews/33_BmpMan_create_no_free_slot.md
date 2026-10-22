# Fix review: bm_create writes past the bitmap table when no slot is free

**Recommendation: TAKE.** One-line fix to an out-of-bounds write; only reached when all 3500 bitmap slots are in use. Found with `/analyze` (C6386) on 2026-10-13.

## The bug
`bm_create` (`Bmpman/BmpMan.cpp`) makes a bitmap from data in memory: talking-head and other anim frames, fonts, the nebula, the gamma test image, model caches. It searches the table from the top for a free slot:
```cpp
int i, n, first_slot = MAX_BITMAPS;
...
for (i = MAX_BITMAPS-1; i >= 0; i-- ) {
	if ( bm_bitmaps[i].type == BM_TYPE_NONE )	{
		first_slot = i;
		break;
	}
}

n = first_slot;
Assert( n > -1 );

// Out of bitmap slots
if ( n == -1 ) return -1;

memset( &bm_bitmaps[n], 0, sizeof(bitmap_entry) );
```
"No slot" is `MAX_BITMAPS`, but the check is for -1, so with the table full it clears and fills in `bm_bitmaps[MAX_BITMAPS]`, one entry past the end, corrupting whatever global follows the table, and returns a handle to it. `bm_load`, a few lines below, uses `MAX_BITMAPS` for "no slot" and checks for it correctly, so this one was probably copied from it and half changed.

## Proposed fix
Start `first_slot` at -1, so the existing check and Assert work as written: with the table full, Debug asserts and Release returns -1, the function's own "out of slots" value.

## What was checked
- `bm_load` uses `MAX_BITMAPS` for "no slot" consistently and is fine.
- `MAX_BITMAPS` is 3500. Filling it is not expected in normal play, but mods and the stress tests planned for the `Limits` branch could reach it.

## Not changed
The callers were not reviewed for how they handle -1. With the table full they would now get -1 instead of a handle into corrupted memory; how each copes with that is a matter for the `Limits` work, where running out of slots can be tested.

## Effect on mission flow
None expected in normal play, where the table does not fill. With it full, the game no longer overwrites the memory after the table.
