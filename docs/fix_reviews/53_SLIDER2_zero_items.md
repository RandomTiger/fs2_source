# Fix review: UI_SLIDER2 divides by zero when the list fits on one page

**Recommendation: TAKE.** Debug Assert only: wrapping a Tech Room list that fits on one page (previous entry from the first item, or next entry from the last) sets the slider position to INT_MIN, and `UI_SLIDER2::draw` asserts. Release does not draw the slider in that state, so nothing visible changes there. No recording moves: the Tech Room and the multiplayer list screens are in no recording. Found by the FS1 demo play-through (2026-10-20). Taken by the steward (D-20261020-222221).

## The bug
`force_currentItem`, `set_currentItem`, `forceDown` and `forceUp` (`code/UI/SLIDER2.cpp`) set the position with

```
currentPosition = fl2i(((float)currentItem/(float)numberItems) * (float)numberPositions);
```

with no check that `numberItems` is above zero and no clamp. `numberItems` is the number of scroll steps, not the number of entries: the Tech Room passes `Current_list_size > max_num_entries_viewable ? Current_list_size-max_num_entries_viewable : 0` (`TechMenu.cpp`), so it is 0 for every list that fits on one page. When the list wraps, `tech_prev_entry` calls `force_currentItem(get_numberItems())` and `tech_next_entry` calls `force_currentItem(0)`. With `numberItems` 0 and `currentItem` 0 that is 0.0f/0.0f, NaN; `fl2i` is a plain `(int)` cast, and the cast of NaN gives INT_MIN. The next `draw()` runs

```
Assert((currentPosition >= 0) && (currentPosition <= numberPositions));
```

and fails (`SLIDER2.cpp:100` on this branch before this change).

`force_currentItem` also has no upper clamp: a `currentItem` above `numberItems` gives a position past `numberPositions`.

`MultiUI.cpp` reaches the same functions: the join screen calls `set_numberItems(... : 0)` then `force_currentItem(Multi_join_list_start)`, and the create screen calls `forceUp`/`forceDown`, both with lists that can fit on one page.

## Developers' intent
Every other path into `currentPosition` in the same file keeps it in `[0, numberPositions]`: `set_numberItems` resets it to 0 or recomputes and clamps both ends, and `process()` clamps after each of its recomputes. `create` sets it to 0. The four setters were missed. `set_numberItems` also sets `disabled_flag` when `numberItems <= 0`, so the developers meant a slider with nothing to scroll to sit at 0, unused.

## Proposed fix
In each of the four functions: when `numberItems <= 0`, set `currentPosition` to 0; otherwise compute it exactly as before and clamp it to `[0, numberPositions]` with the same code `set_numberItems` uses. Nothing else in the functions changes (the `currentItem` handling and the callbacks are as they were). Each change carries a `// Claude:` comment.

In `forceDown` the zero arm cannot be reached (it only runs when `currentItem < numberItems`), but it gets the same shape so the four read alike, as ruled.

## Release effect
`draw()` only draws the slider when `!disabled_flag`, and `process()` returns at once when `disabled_flag` is set; `set_numberItems` sets `disabled_flag` whenever `numberItems <= 0`. So in Release the INT_MIN position was normally never drawn or read, and the fix changes nothing visible there. With `numberItems` above zero the computed value is unchanged unless it was out of range, which only `force_currentItem` with an item past the end could produce.

## What was checked
- Release and Debug build with no warning in `SLIDER2.cpp`.
- Debug hand check on FS2 retail data (Tom, 2026-10-20): wrapping the Tech Room ships, weapons and intelligence (species) lists past both ends; no Assert. On FS2 data the weapons and intelligence lists fit on one page, the case that divided by zero.
- Tom's recollection of trouble scrolling the FS2 Tech Room weapons and species lists is consistent with this bug; it was not reproduced in Debug on FS2 data before the fix.
- Multiplayer join and create screens: not opened. Multiplayer is blocked at the main hall in this build, so those screens are not reachable; the code path is covered by the same four functions.
- Recordings: none touch the Tech Room or the multiplayer screens; re-recorded missions: none. The quick suite runs at `RecordPlayback` on the carry-up.

## Not changed
- The callers in `TechMenu.cpp` and `MultiUI.cpp`: guarding them would leave the slider able to hold the bad value from other callers.
- `process()`'s own recomputes already clamp.

## Effect on mission flow
None. The slider is menu UI only.
