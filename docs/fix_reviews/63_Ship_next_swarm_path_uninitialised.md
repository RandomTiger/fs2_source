# Fix review: ship.next_swarm_path is never initialised

**Recommendation: TAKE (document only; the fix is not applied on this branch).** `ship.next_swarm_path` (`Ship.h`) picks which of four paths the next swarm missile of a ship takes. Nothing sets it: `ship_create` and `ship_level_init` leave it alone. A new process reads 0 (static storage). A second game started in the same process reads the count the last mission left in that `Ships[]` slot, so the first swarm volleys take different paths. Found while making a restarted game match a fresh process on PrePort64 (D-055).

## The bug
`code/Ship/Ship.h:581`: `int next_swarm_path;  // next path number for swarm missile to take`.

The only uses are in `code/Weapon/Swarm.cpp` (`swarm_move`, the `path_num == -1` branch, lines 326 and 328):

```
parent_shipp = &Ships[Objects[objp->parent].instance];
swarmp->path_num = (parent_shipp->next_swarm_path++)%4;

if ( parent_shipp->next_swarm_path%4 == 0 ) {
	swarmp->flags ^= SWARM_POSITIVE_PATH;
}
```

A grep of `code/` for `next_swarm_path` finds Ship.h:581 and these two lines and nothing else. No write ever sets it to a starting value:
- `ship_create` (`Ship.cpp`) sets the neighbouring `next_swarm_fire = 1` (line 2111) but not `next_swarm_path`; it fills the slot field by field, with no memset.
- `ship_level_init` (`Ship.cpp:1733`) only sets `Ships[i].objnum = -1` per slot.
- `swarm_level_init` and `swarm_create` (`Swarm.cpp`) reset the `Swarm_missiles` entries, not the parent ship.

So the value is whatever the slot held: 0 in a new process (`Ships[]` is static), or the last mission's count in a second game in the same process (new mission, or a restarted game). Within one mission a freed slot reused by a later ship also keeps the old count, since `ship_create` does not clear it either; that part is the same in a fresh process and is not changed by the fix below.

## The PrePort64 fix
`ship_level_init`, per slot (PrePort64 `Ship.cpp:1746-1748`, D-055):

```
memset(&Ships[i], 0, sizeof(ship));	// Claude: D-055
Ships[i].objnum = -1;
Ships[i].next_swarm_path = 0;	// Claude: never initialised: ...
```

The memset zeroes the whole slot so a second game starts from the same state as a new process; the explicit `next_swarm_path = 0` line documents the one field this bug is about. On OriginalSourceBugFixes the minimal form would be the single line `Ships[i].next_swarm_path = 0;` in the same loop, marked `// Claude:`.

## Behaviour effect
`path_num = count % 4` selects the path of each swarm missile, and the `SWARM_POSITIVE_PATH` flip happens when the count reaches a multiple of 4. With a stale count c, the first volley starts at path c%4 instead of 0 and flips the positive-path flag at a different missile. Only ships that fire swarm missiles are affected; the first mission in a process (count 0) behaves as today. After the fix every mission, in any position in the process, behaves like the first one in a fresh process.

## Same-class fields (pools whose create functions leave fields unset)
Confirmed by reading code; none are fixed here.
- `weapon` (`Weapon.h:355`, `weapon_create`, `Weapons.cpp:2503`): create does not set `target_num` (set only by the tracking-info code, `Weapons.cpp:2461`/`2480`), `nearest_dist` (set at `2463`), `big_attack_point`, and sets `trail_num` only when the weapon has `WIF_TRAIL` (`2709`) and `particle_spew_time` only for `WIF_PARTICLE_SPEW` (`2608`). `weapon_delete` reads `trail_num` unconditionally (`Weapons.cpp:1609`), so a non-trail weapon in a reused slot can pass a stale trail number to `trail_object_died`. `weapon_process_post` reads `target_num` for every weapon (`2347`).
- `swarm_info` (`Swarm.cpp:118`, `swarm_create:235`): create sets `flags`, `change_timestamp`, `path_num`, `homing_objnum`; `original_target`, `new_target`, `circle_rvec`, `circle_uvec`, `last_offset`, `change_count`, `change_time`, `angle_offset`, `last_dist` are set later, in the `path_num == -1` branch (`Swarm.cpp:326-371`), before use as far as this read shows; not a confirmed stale read.
- `ai_info` (`ai.h:397`, `init_ai_object`, `AiCode.cpp:14033`): not set by `init_ai_object`: `stealth_last_pos`, `stealth_velocity`, `stealth_sweep_box_size`, `stealth_last_cheat_visible_stamp`, `guard_vec`, `current_target_is_locked`, `last_dist`, `last_speed`, `last_target`, `current_target_distance`, `current_target_dist_trend`, `current_target_speed_trend`. `ai_level_init` (`AiCode.cpp:1093`) resets only `shipnum`. Of these, `current_target_is_locked` is read (`AiCode.cpp:5948`) where earlier writes depend on the path taken; whether any read comes before the first write was not traced.
- `debris` (`Debris.h:146`, `debris_create`, `Debris.cpp:652`): `sound_delay` is set only for hull pieces (`Debris.cpp:800`); `debris_level_init` sets it to 0 (`Debris.cpp:308`), so no stale read is confirmed. `arc_pts` is written when an arc starts.
- `fireball` (`FireBalls.cpp:358`, `fireball_create:971`): every field is set by create. None found.
- `wing` (`Ship.h`): `ship_level_init` sets only `num_waves = -1` per slot; fields set by wing creation were not audited here.

## Risk to recordings
- Recordings made from a fresh process (first mission of the run) see count 0 in both old and new code: replay is unchanged.
- A recording made as a second game in one process (or a replay suite that runs several missions in one process) with a swarm-firing ship would have been recorded with a stale count. After the fix those missions take the count-0 path and the trace diverges at the first swarm launch; those recordings would need re-recording, which is legitimate because the code changed gameplay.
- Not checked: whether any existing recording was made as a second game in a process. The replay gate must run when the fix is applied.

## Not changed
- No code, no builds, no replay gate on this branch; this commit is the review document only.
- The pool fields listed above are not fixed.
