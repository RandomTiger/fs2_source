# Fix review: hud_escort_ship_hit reads Quadrant_xlate[-1] on a hull hit

**Recommendation: TAKE.** No risk.

Found by AddressSanitizer: 51 reports across the campaign autotest.

## The bug
```cpp
num = Quadrant_xlate[quadrant];
hud_gauge_popup_start(HUD_ESCORT_VIEW);
if ( quadrant >= 0 ) {
    shi->shield_hit_timers[num] = timestamp(SHIELD_HIT_DURATION);
} else {
    shi->shield_hit_timers[HULL_HIT_OFFSET] = timestamp(SHIELD_HIT_DURATION);
}
```
A hit with no shield is passed as quadrant -1, so the lookup reads the byte before the table. The value is only used when the quadrant is 0 or more.

## Proposed fix
Move the lookup inside `if ( quadrant >= 0 )`.

## Effect on mission flow
None. The value read for -1 was never used.
