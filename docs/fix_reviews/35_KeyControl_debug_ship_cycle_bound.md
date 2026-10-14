# Fix review: debug ship cycling reads Ship_info[MAX_SHIP_TYPES]

**Recommendation: TAKE.** Off-by-one in two debug key handlers; reachable only with debug keys, which Release allows once cheats are on. Found with `/analyze` (C6385) on 2026-10-13.

## The bug
`debug_cycle_player_ship` and `debug_cycle_targeted_ship` (`Io/KeyControl.cpp`) change the player's or the target's ship class to the next one that fits, stepping through `Ship_info` and wrapping:
```cpp
si_index += delta;
if ( si_index > MAX_SHIP_TYPES ){
	si_index = 0;
}
...
sip = &Ship_info[si_index];
```
`Ship_info` has `MAX_SHIP_TYPES` entries, so `si_index == MAX_SHIP_TYPES` is one past the end and is read before it wraps. The other direction wraps to `MAX_SHIP_TYPES - 1`, correctly.

## Proposed fix
Wrap at `>= MAX_SHIP_TYPES`.

## What was checked
- Both handlers are called from `process_debug_keys` (debug key plus left/right, with shift for the target). It runs in Release too (its call site is commented "also processed for cheats"); the ship cycling cases are outside the `#ifndef NDEBUG` blocks in it.
- The step back from 0 already wraps to `MAX_SHIP_TYPES - 1`.

## Not changed
The loops also step through unused `Ship_info` entries above the number of classes loaded; as a global array it starts zeroed, so they have no flags and are skipped.

## Effect on mission flow
None without the debug keys. With them, cycling past the last ship class no longer reads one entry beyond the table, which could have picked it as a class if its bytes happened to have the right flag set.
