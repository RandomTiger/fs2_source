# Fix review: ship_get_secondary_weapon_range indexes Weapon_info[-1] for an empty secondary bank

**Recommendation: TAKE.** When the ship's current secondary bank holds no weapon (`secondary_bank_weapons[bank]` is -1), `ship_get_secondary_weapon_range` forms `&Weapon_info[-1]` before it checks the ammo. The pointer is only read when the bank has ammo, and an empty bank never has ammo in single player, so the returned range does not change. Found by the Debug array guards on the FS1 demo (`btmdemo`, 58 guard asserts, #84). Ruled by the steward (D-20261021-202101 to 202103).

## The bug
`code/Ship/Ship.cpp`:

```
if ( swp->current_secondary_bank >= 0 ) {
	weapon_info	*wip;
	int bank=swp->current_secondary_bank;
	wip = &Weapon_info[swp->secondary_bank_weapons[bank]];
	if ( swp->secondary_bank_ammo[bank] > 0 ) {
		srange = wip->max_speed * wip->lifetime;
	}
}
```

With `secondary_bank_weapons[bank] == -1` the address is one element before the array. In Release it is never dereferenced unless the ammo test passes; in Debug the guarded `Weapon_info` asserts on the index itself.

## Developers' intent
The function's comment says the range is 0 when the bank has no missiles left. An empty bank has no missiles, so 0 is the intended answer.

## Proposed fix
Test `secondary_bank_weapons[bank] >= 0` before forming `wip`; everything else unchanged, `// Claude:` comments on the two added lines.

## Who reaches it
Three callers: `HUDtarget.cpp` (the HUD's secondary range), `PlayerControl.cpp` (player auto-target range) and `Ship.cpp` (AI weapon range). `btmdemo` reaches it with a ship whose current secondary bank is empty from mission start.

## Can an empty bank have ammo? (D-20261021-202101 (b))
Every single-player write of `secondary_bank_ammo` was checked:
- `ship_set_default_weapons` / `ship_weapons_init` (`Ship.cpp`): zero every bank first, then set ammo only for banks `0 .. num_secondary_banks-1`, which the ship class fills with real weapons.
- Firing (`ship_fire_secondary`, `Ship.cpp`): only lowers the ammo, and only on a bank that already has some.
- Rearm (`Ship.cpp`): tops a bank up to at most its `secondary_bank_start_ammo`, which is 0 for an empty bank.
- Mission parse (`MissionParse.cpp`): ammo only for `0 .. num_secondary_banks-1`, computed from that bank's own weapon. The subsystem (turret) writes are turret weapons, not the ship's banks.
- Loadout and red alert (`MissionWeaponChoice.cpp`, `RedAlert.cpp`): a bank gets ammo only together with a weapon `>= 0`.
- The Debug cheat key `debug_max_secondary_weapons` (`KeyControl.cpp`) fills every bank's ammo, including empty ones: the only single-player path to ammo on an empty bank. There the old code read `Weapon_info[-1]` as a weapon (undefined); now the range is 0.
- Multiplayer writes (`MultiMsgs.cpp`, `multi_ingame.cpp`) are out of scope (multiplayer is not supported).

So outside the Debug cheat key the result is identical: an empty bank had ammo 0, and the function returned 0.

## What was checked
- OriginalSourceBugFixes: Release and Debug compile clean.
- (a) Every data set replays identical in Release and Debug at each layer the fix passes (D-20261021-202101 (a)): the quick suite at RecordPlayback, the full gate at Enhanced with the FS1 retail and FS1 demo tests; `btmdemo` Debug must drop from 58 guard asserts to 0. The results per layer are recorded on #84 and in the merge commit messages.

## Not changed
- `current_secondary_bank` stays at 0 on a ship with no secondary weapons; callers that index by it are not touched here.

## Effect on mission flow
None outside the Debug cheat key: the returned range is the same 0 as before.
