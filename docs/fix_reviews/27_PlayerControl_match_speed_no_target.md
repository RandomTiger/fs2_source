# Fix review: player_match_target_speed reads Objects[-1] with no target

**Recommendation: TAKE.** Low risk.

Found while reviewing fix 18 (the HUD target speed read), which has the same pattern. AddressSanitizer has not reported it, because the autotest never turns speed matching on.

## The bug
```cpp
if ( Objects[Player_ai->target_objnum].type != OBJ_SHIP ) {
    return;
}
...
if ( Player_ai->target_objnum == -1) {
    // print no_target_text, if given
    return;
}
```
With no target, `target_objnum` is -1, so the first check reads `Objects[-1]`, the memory before the object array. It is reached when the player presses the match speed key with no target, and from the HUD with auto speed matching on.

## Proposed fix
Check `target_objnum >= 0` before reading the object, so a missing target reaches the check for -1 below it, which was written for that case.

## Effect on mission flow
None. Every caller passes no text, so with no target the function returned without doing anything either way; it now gets there without the bad read.
