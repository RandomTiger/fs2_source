# Fix review: ai_fire_from_turret reads Weapon_info[-1] for a turret with no weapon

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer in the campaign autotest and in a hand-played recording of sm1-04.

## The bug
```cpp
// If beam weapon, check beam free
if ( (Weapon_info[tp->turret_weapon_type].wi_flags & WIF_BEAM) && !(ss->weapons.flags & SW_FLAG_BEAM_FREE) ) {
    return;
}

Assert( shipp->objnum == parent_objnum );

if ( tp->turret_weapon_type < 0 ){
    return;
}
```
A turret with no primary or secondary bank has `turret_weapon_type` -1. The beam check runs before the check for that, so it reads `Weapon_info[-1]`.

## Proposed fix
Add `(tp->turret_weapon_type >= 0) &&` to the beam check.

## Effect on mission flow
None. With -1 the function returned either way: from the beam check if the data read happened to have the beam flag, otherwise from the `< 0` check just below.
