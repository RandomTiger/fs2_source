# Fix review: two different structs named barracks_buttons

**Recommendation: TAKE.** Low risk.

This fix was first committed straight onto `RecordPlayback` (f058d3f, 2026-10-10). It is repeated here, identically, so the original-game fix sits on the right layer; the merge up to `RecordPlayback` changes nothing there.

## The bug
`PlayerMenu.cpp` and `Barracks.cpp` each define their own `struct barracks_buttons`, with different layouts. C++ requires one definition per name across the program, so the linker keeps a single copy of the compiler-generated destructor for both. On exit, one of the two static button arrays is destroyed with the other file's layout, and its `UI_BUTTON` is destroyed at the wrong offset.

## Proposed fix
Rename the struct in `PlayerMenu.cpp` to `player_menu_buttons`. Nothing else changes.

## Effect on mission flow
None. The struct is only used for the pilot select screen's buttons, and the fault was only at shutdown. Recordings replay unchanged.
