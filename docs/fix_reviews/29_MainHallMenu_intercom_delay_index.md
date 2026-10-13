# Fix review: main hall intercom delay read with index -1

**Recommendation: TAKE.** Low risk, menu only.

Found by the compiler's code analysis (`/analyze`, warning C6385).

## The bug
```cpp
if((Main_hall_next_intercom_sound_stamp == -1) && (Main_hall_intercom_sound_handle == -1)){
    Main_hall_next_intercom_sound_stamp = timestamp((int)(((float)rand()/(float)RAND_MAX) *
        (float)(Main_hall->intercom_delay[Main_hall_next_intercom_sound][1]
              - Main_hall->intercom_delay[Main_hall_intercom_sound_handle][0])) );
}
```
The condition guarantees `Main_hall_intercom_sound_handle` is -1, so the second index reads `intercom_delay[-1][0]`, part of the field before the table in the main hall struct. The first delay after entering the main hall uses that value instead of the sound's minimum delay. The same expression further down, used after each sound finishes, indexes both entries with `Main_hall_next_intercom_sound`.

## Proposed fix
Index with `Main_hall_next_intercom_sound`, as the second copy does.

## Not changed
Both copies compute `rand * (max - min)` without adding `min`. That may not be what was meant, but it is how the game has always timed the intercom, so it stays.

## Effect on mission flow
None. Only the timing of the first random intercom sound in the main hall changes. Recordings start at the mission, after the main hall.
