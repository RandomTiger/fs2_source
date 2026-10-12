# Fix review: ship_do_damage reads a non-weapon object as a weapon

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer: 60 reports across the campaign autotest, reading up to 11 KB past the end of `Weapons[]`.

## The bug
At the end of the damage setup, `ship_do_damage` decides whether the player feels pain:
```cpp
if((other_obj != NULL) && ((Weapon_info[Weapons[other_obj->instance].weapon_info_index].subtype != WP_LASER) || !MULTIPLAYER_CLIENT) && (Player_obj != NULL) && (ship_obj == Player_obj)){
    ship_hit_pain(damage);
}
```
`other_obj` is whatever did the damage: a weapon, but also a ship (collisions), a beam, a shockwave or an asteroid. For those, `other_obj->instance` indexes `Ships[]`, `Beams[]` and so on, and is read here as an index into `Weapons[]`. The `weapon_info_index` found there is then used to index `Weapon_info[]`, which can land anywhere.

## Proposed fix
Only look at the weapon subtype when the object is a weapon:
```cpp
if((other_obj != NULL) && ((other_obj->type != OBJ_WEAPON) || (Weapon_info[...].subtype != WP_LASER) || !MULTIPLAYER_CLIENT) && ...
```
A non-weapon hit is not a laser, so it gives pain, as it would for a missile.

## Effect on mission flow
None in single player. There `!MULTIPLAYER_CLIENT` is always true, so the condition was always true whatever the read returned; only the out-of-bounds read goes away. A multiplayer client now feels pain from collisions, beams and shockwaves, where before it depended on the data read.
