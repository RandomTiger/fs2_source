# Fix review: FRED's string hash init clears four times the table

**Recommendation: TAKE.** Wrong size in a memset, FRED only. Found with `/analyze` (C6386) on 2026-10-13.

## The bug
`fhash_init` (`Localization/fhash.cpp`) clears FRED's hash table of localised strings:
```cpp
fhash_node *Hash_table_fred[HASH_TABLE_SIZE];
...
memset(Hash_table_fred, 0, sizeof(fhash_node) * HASH_TABLE_SIZE);
```
The table holds pointers, but the size uses the node struct (a pointer, an int and two more pointers: 16 bytes in the 32-bit build, against 4 for a pointer). With 253 entries it clears 4048 bytes instead of 1012, zeroing about 3 KB of whatever globals follow the table.

## Proposed fix
Use `sizeof(fhash_node *)`.

## What was checked
- `fhash_init` is called only from FRED (`FRED2/Management.cpp`, at startup, followed by `fhash_activate`); the game never calls it.

## Not changed
Nothing else in the hash code.

## Effect on mission flow
None in the game. In FRED, startup no longer zeroes the globals that follow the table. Which globals those are depends on the link.
