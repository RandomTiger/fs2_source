# Fix review: ship death shockwave reads Weapon_info[-1] near a subsystem

**Recommendation: TAKE.** Out-of-bounds read that would differ between builds; affects only a rare case.

Found while reviewing the earlier Unreal port (`fs2_source_ue`); confirmed in this tree.

## The bug
`do_subobj_hit_stuff` (`Ship/ShipHit.cpp`) cuts all shockwave damage to subsystems by 4, then boosts it back for subsystems within 10 m of the shockwave centre (Volition's "HORRIBLE HACK" for Helios bombs against the Juggernaut):
```cpp
if ((dist < 10.0f) && (other_obj->type == OBJ_SHOCKWAVE)) {
    damage_left *= 4.0f * Weapon_info[weapon_info_index].subsystem_factor;;
}
```
`weapon_info_index` comes from `shiphit_get_damage_weapon`, which returns the shockwave's weapon type, or -1 for a shockwave not caused by a weapon (a ship's death shockwave, `Weapon/Shockwave.cpp`). So when a dying ship's shockwave centre is within 10 m of another big ship's subsystem, the boost reads `Weapon_info[-1].subsystem_factor`: whatever lies just before the table. That value differs between builds, so this would split recordings on an x64 or Godot build.

## Proposed fix
With no weapon, boost by 4 only, which undoes the cut as the comment intends. With a weapon, unchanged.

## What was checked
- The earlier `subsystem_factor` scaling in the same function is already guarded (`weapon_info_index >= 0`, and only for `OBJ_WEAPON`).
- Small ships take no shockwave subsystem damage at all (`SIF_SMALL_SHIP` returns early), so only big ships reach this line.

## Not changed
The alternative was no boost for death shockwaves. The original applied the boost to every shockwave, so the fix keeps it and only replaces the unknown factor with 1.

## Effect on mission flow
Only a death shockwave centred within 10 m of a subsystem of a big or capital ship is affected: such a ship's subsystems take 4 times the cut damage, instead of 4 times an unknown factor. Recordings where a ship dies that close to a capital ship's subsystem can change; the replay check after the merge into `RecordPlayback` says which.
