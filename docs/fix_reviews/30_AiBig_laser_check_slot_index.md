# Fix review: big-ship attack AI checks the wrong weapon for "is it a laser"

**Recommendation: TAKE.** Out-of-bounds read that would differ between builds; small AI change.

Found while reviewing the earlier Unreal port (`fs2_source_ue`); confirmed in this tree.

## The bug
`ai_big_chase_attack` (`Ship/AiBig.cpp`), the attack submode for a ship attacking a big ship. When the attacker is moving slower than 3 m/s, it looks for enemy lasers heading at it, to evade them or turn on the ship that fired:
```cpp
for ( objp = GET_FIRST(&obj_used_list); objp !=END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
    if ((objp->type == OBJ_WEAPON) && (Weapons[objp->instance].team != Ships[Pl_objp->instance].team))
        if (Weapon_info[objp->instance].subtype == WP_LASER) {
```
`objp->instance` is the weapon's slot in `Weapons[]` (up to `MAX_WEAPONS`, 350), not its weapon type. So the test reads the subtype of an unrelated weapon type, and for slots 200 and above it reads past the end of `Weapon_info` (`MAX_WEAPON_TYPES`, 200 entries). What lies past the end differs between builds, so this would split recordings on an x64 or Godot build.

## Proposed fix
Index with the weapon's type, as everywhere else: `Weapon_info[Weapons[objp->instance].weapon_info_index]`.

## What was checked
Every other `Weapon_info[...]` lookup from an object in `AiBig.cpp` goes through `Weapons[objp->instance].weapon_info_index`.

## Not changed
The rest of the evade logic, including the 200 m and 0.95 dot thresholds.

## Effect on mission flow
A slow ship attacking a big ship now evades, or turns on, enemy lasers fired at it, and ignores missiles, as intended. Before, the choice depended on which weapon type happened to share its slot number. Recordings in which a ship attacking a big ship slows below 3 m/s with enemy weapons in flight can change; the replay check after the merge into `RecordPlayback` says which.
