# Fix review: the single-wing wingman gauge has positions for 4 ships but draws 6

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer: 26 reports across the campaign autotest.

## The bug
`hud_wingman_status_blit_dots` draws a dot for each of up to 6 ships in a wing (`HUD_WINGMAN_MAX_SHIPS_PER_WINGS`). When only one wing is shown it takes the positions from a special table:
```cpp
int HUD_wingman_status_single_coords[GR_NUM_RESOLUTIONS][4][2] = ...

for ( i = 0; i < HUD_WINGMAN_MAX_SHIPS_PER_WINGS; i++ ) {
    ...
    if ( num_wings_to_draw == 1 ) {
        sx = HUD_wingman_status_single_coords[gr_screen.res][i][0];
```
That table has only 4 entries per resolution, so ships 5 and 6 read past it. At 640x480 they come from the 1024x768 entries; at 1024x768 they come from whatever follows the table in memory. A wing of 5 or 6 shown alone drew those dots in the wrong place.

## Proposed fix
Give the table 6 entries per resolution. Ships 5 and 6 are placed as in the multi-wing table `HUD_wingman_status_coords`, where every wing has them at ship 1's x minus and plus 11, at y 175:

| | ship 1 | ship 5 | ship 6 |
|---|---|---|---|
| 640 | 589,159 | 578,175 | 600,175 |
| 1024 | 971,159 | 960,175 | 982,175 |

## Effect on mission flow
None. HUD drawing only.
