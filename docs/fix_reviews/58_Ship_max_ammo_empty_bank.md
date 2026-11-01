# Fix review: get_max_ammo_count_for_bank indexes Weapon_info[-1] for an empty secondary bank

**Recommendation: TAKE.** In a red-alert mission, `ship_process_post` asks `get_max_ammo_count_for_bank` for the maximum ammo of every secondary bank of every ship, including the empty ones (`secondary_bank_weapons[bank]` is -1). The function then reads `Weapon_info[-1].cargo_size`. The fix returns 0 for an empty bank before any `Weapon_info` read. No reader ever looks at the stored value for an empty bank, so nothing in play changes. Found by the Debug array guards while re-recording the six red-alert missions on PrePort32 (#92); a sibling of #84 (review 56). Ruled by the steward: D-20261022-214722.

## The bug
`code/Ship/Ship.cpp`:

```
int get_max_ammo_count_for_bank(int ship_class, int bank, int ammo_type)
{
	float capacity, size;

	capacity = (float) Ship_info[ship_class].secondary_bank_ammo_capacity[bank];
	size = (float) Weapon_info[ammo_type].cargo_size;
	return (int) (capacity / size);
}
```

With `ammo_type == -1` it reads the `cargo_size` field one element before the array: whatever memory lies there. In Release the result is an undefined number; in Debug the guarded `Weapon_info` asserts on the index.

## The caller: what ship_process_post does with the count
Once per ship (until `SF_AMMO_COUNT_RECORDED` is set), `ship_process_post` records each secondary bank's start ammo, the cap for rearming:

```
for ( int i=0; i<MAX_SECONDARY_BANKS; i++ ) {
	if ( red_alert_mission() ) {
		int max_missiles = get_max_ammo_count_for_bank(shipp->ship_info_index, i, shipp->weapons.secondary_bank_weapons[i]);
		shipp->weapons.secondary_bank_start_ammo[i] = max_missiles;
	} else {
		shipp->weapons.secondary_bank_start_ammo[i] = shipp->weapons.secondary_bank_ammo[i];
	}
}
```

The count goes straight into `secondary_bank_start_ammo[i]` and nothing else. For an empty bank the ammo is 0, and with the fix the start ammo is now 0 too (before, it was `capacity / Weapon_info[-1].cargo_size`, an undefined number).

`secondary_bank_start_ammo` has three readers, and all three loop only over `0 .. num_secondary_banks-1`:
- Rearm (`Ship.cpp`, the support ship's rearm loop): tops a bank up to its start ammo.
- AI rearm desire (`AiCode.cpp`): skipped when the start ammo is not `> 0`.
- The rearm-request message (`Ship.cpp`): skipped when the start ammo is not `> 0`.

Every path that fills the banks packs the real weapons into `0 .. num_secondary_banks-1` and leaves the rest -1: the ship class (`Ship.cpp`, from `ships.tbl`, then fitted to the model), mission parse (`MissionParse.cpp`, the per-ship weapon overrides), loadout (`MissionWeaponChoice.cpp`, `wl_bash_ship_weapons`), red alert carry-over (`RedAlert.cpp`, `red_alert_bash_weapons`) and turrets (`Ship.cpp`). So an empty bank is always at index `num_secondary_banks` or above, which no reader visits. The value stored for it, old or new, is never used.

## Why only red-alert missions reach it
The call sits inside `if ( red_alert_mission() )`. `red_alert_mission()` is true only when the mission file sets the red-alert flag (`MissionParse.cpp` calls `red_alert_set_status(pm->red_alert)`). In every other mission the start ammo is copied from the current ammo, with no `Weapon_info` read. In a red-alert mission the loop runs over all `MAX_SECONDARY_BANKS` (4), so every ship with fewer than four secondary banks hits it once per empty bank. That is why the counts are per mission (sm3-10 124, md-09 84, sm2-06a 78, sm2-10a 32, sm3-05a 64, sm3-09a 55) and why the 48 non-red-alert recordings never showed it.

## Proposed fix
In `get_max_ammo_count_for_bank`, return 0 when `ammo_type < 0`, before any `Weapon_info` read. An empty bank holds no missiles, so its maximum is 0. `// Claude:` comments on the added lines. Putting the test in the function also covers the FRED weapon editor callers, which can pass -1 for "none".

## What was checked
- Trial on Enhanced 39f4881, uncommitted (D-20261022-214722 step 1): nothing moved. The 48 identical r/d, the FS1 recordings 41 of 41 identical r/d, the four FS1 demo missions r/d with 0 asserts, the FS2 demo three identical r/d, warnings 0, tools PASS. The one red in that run, the FS2 demo Debug menu tour's `Assert(instance->frame)` on leaving ship select (#93), also fires on the 39f4881 exe without this fix (attribution runs, D-20261022-224842), so it is not this change.
- PrePort32 (Debug guards on): the six red-alert re-recordings replay with no guard asserts. Results per layer on #92.

## Not changed
- `ship_process_post` still loops over all `MAX_SECONDARY_BANKS`; the empty banks now just store 0.

## Effect on mission flow
None: the start ammo of an empty bank is never read.
