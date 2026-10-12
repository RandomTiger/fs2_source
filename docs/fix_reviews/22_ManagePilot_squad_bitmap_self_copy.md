# Fix review: player_set_squad_bitmap copies the squad file name onto itself

**Recommendation: TAKE.** No risk.

Found by AddressSanitizer (strncpy-param-overlap) every time a pilot is loaded.

## The bug
```cpp
void player_set_squad_bitmap(player *p, char *fname)
...
strncpy(p->squad_filename, fname, MAX_FILENAME_LEN);
```
`read_pilot_file` and the barracks call it with the player's own `squad_filename`, so the source and destination are the same buffer. `strncpy` with overlapping buffers is undefined behaviour. The CRT's version happens to leave the string as it was.

## Proposed fix
Skip the copy when `fname` is `p->squad_filename`.

## Effect on mission flow
None.
