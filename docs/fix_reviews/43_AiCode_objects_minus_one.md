# Fix review: three AI reads of `Objects[-1]` when there is no target or parent

**Recommendation: TAKE.** No recording changes: every one of the 48 replays identically with the fix in all four builds (x86 and x64, Release and Debug). It removes three out-of-bounds reads whose outcome depends on memory layout. Decision D-080 (assessment per D-079). GitHub issues #2, #3, #4.

## How they were found
The Debug bounds guard on the `Objects` array (D-076, on `PrePortCleanup`) fired at five places across the 48 recordings. Two only take an out-of-range address and never read through it (#5, rewritten on `PrePortCleanup` as `Objects + i`). The other three read `Objects[-1]`: whatever bytes the linker placed just before the array.

## The bugs
All three are in `Ship/AiCode.cpp`, and all index `Objects` with -1, which means "none".

**#2, `set_target_objnum`.** Clearing a ship's target calls it with `objnum == -1`; it then stores
```cpp
aip->target_signature = Objects[objnum].signature;
```
The value is never used: every reader of `target_signature` checks `target_objnum` first, and the next real target overwrites it. Fired 84 times across the recordings.

**#3, `set_primary_weapon_linkage`.** With no target, the "is the target a ship?" test reads garbage:
```cpp
// AL: ensure target is a ship!
if ( Objects[aip->target_objnum].type == OBJ_SHIP ) {
    if ( ship_get_SIF(&Ships[Objects[aip->target_objnum].instance]) & SIF_BIG_SHIP) { ... link all weapons ... }
```
If those bytes ever equal `OBJ_SHIP`, the code indexes `Ships[]` with a garbage instance and may link primaries and dual-fire secondaries. Fired 4 times.

**#4, `ai_bay_depart`.** The "is the parent ship still there?" test, with no parent, compares the saved signature with garbage:
```cpp
if ( aip->goal_signature != Objects[aip->goal_objnum].signature ) { aip->mode = AIM_NONE; return; }
```
The intent is to abort the departure. If those bytes ever matched, the ship would carry on along its bay path instead. Fired once.

## Developers' intent
In each case -1 means "no target" or "no parent", and the surrounding code shows what was meant: no signature (#2), not a ship (#3), the parent is gone (#4). The comment in #3 even says "ensure target is a ship". None of them intends a read outside the array.

## Proposed fix
Test for -1 first, giving the outcome the intent describes:
- #2: store -1 when `objnum < 0`.
- #3: `(aip->target_objnum >= 0) && (Objects[...].type == OBJ_SHIP)`.
- #4: `(aip->goal_objnum < 0) || (aip->goal_signature != Objects[...].signature)`.

## Why retail is unchanged
In every build we have, the bytes before `Objects` give the intended answer: #3's test is false and #4 aborts. The fix makes that answer certain, whatever the memory layout. That matters for the port: a new compiler, the 64-bit build or Godot's allocator and linker could change those bytes and flip #3 or #4 without anyone noticing.

## What was checked
- Assessment (D-079, D-080) on `PrePortCleanup` with the fix applied: all 48 recordings replayed headless in x86 Release, x86 Debug, x64 Release and x64 Debug. All 192 runs gave MATCH and REPLAY OK, with no Asserts.
- The three sites are the only `Objects[-1]` reads the guard found in the 48 recordings. Other code paths may hold more; later guards will show them.

## Not changed
`Objects` reads elsewhere that the recordings did not reach. The address-only sites (#5) and the formation leader tests (#1) are not reads and are handled on `PrePortCleanup`. #1 is a behaviour question, not part of this fix.

## Effect on mission flow
None. **No recording changed.**
