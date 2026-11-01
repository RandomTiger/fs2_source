# Fix review: four more index -1 reads, in AI goals, missile lock, the bomb check and the shockwave sound

**Recommendation: TAKE.** No recording changes: with the fixes, all 48 recordings replay identically in x86 and x64, Release and Debug. Each fix gives the outcome retail's out-of-range read gave in every recording, without the read. Steward decision D-20261018-030013 (assessment per D-079). GitHub issues #14, #15, #16, #17 (D-20261018-043124).

## How they were found
The Debug bounds guards on `Weapon_info` and `Wings` (D-076 follow-on, `PrePortCleanup`) fired at nine places across the 48 recordings. Four only take an address (#18). These four read index -1, the bytes just before the array.

## The bugs

**#14, `ai_mission_goal_achievable` (`Ship/AiGoals.cpp`).** For chase-wing and guard-wing goals:
```cpp
int num = wing_name_lookup( aigp->ship_name );
wing *wingp = &Wings[num];
if ( wingp->flags & WF_WING_GONE ) return AI_GOAL_NOT_ACHIEVABLE;
else if ( wingp->total_arrived_count == 0 ) return AI_GOAL_NOT_KNOWN;
else return AI_GOAL_ACHIEVABLE;
```
`wing_name_lookup` returns -1 for a wing that is not in the mission (not yet arrived), and the status then comes from `Wings[-1]`.

**#15, `hud_lock_reset` (`Hud/HUDlock.cpp`).** With no secondary weapon, `wip` is `&Weapon_info[-1]`, and the player's lock timer is set from it: `Player->lock_time_to_target = i2fl(wip->min_lock_time*lock_time_scale);`.

**#16, `firing_aspect_seeking_bomb` (`Ship/AiCode.cpp`).** It checks that `current_secondary_bank` is not -1 and that the bank has ammo, then reads the bank's weapon. A ship whose class has no secondary banks (`num_secondary_banks` 0) still has bank 0 selected, with stale ammo and a stale weapon in slot 0: weapon -1 with ammo 5 in AutotestAI, weapon 62 (an aspect-seeker, not a bomb) with ammo 32 in loop1-2.

**#17, `shockwave_move` (`Weapon/Shockwave.cpp`).** When a shockwave hits the player, the impact sound plays at `max(0.4f, damage/Weapon_info[sw->weapon_info_index].damage)`. A shockwave not made by a weapon (a ship exploding) has `weapon_info_index` -1, and the trial shows `Weapon_info[-1].damage` is 0. So the scale is infinite, and `snd_play_live` clamps the volume to 1.0: full volume, whatever the player's sound settings.

## What retail actually does
A trial build logged, at each site, what the original read gave, per mission, over all 48 recordings:
- #14: `NOT_KNOWN` every time (AutotestSupport, NebActive, Flow, NebMedium, NebStandard, AI, Arrivals). A first trial that returned `NOT_ACHIEVABLE` instead moved four recordings (AutotestAI, AutotestArrivals, AutotestFlow, AutotestNebMedium) in all four builds, so the status matters, and retail's is `NOT_KNOWN`.
- #15: 0 every time (AutotestAI, AutotestDamage, sm2-08, sm3-01).
- #16: "not a bomb" every time.
- #17: an infinite volume every time (sm2-04, sm3-06, AutotestWeapons, AutotestChain): full volume after the clamp. If the sound volume setting is 0, infinity times 0 is NaN, which plays nothing.

## Developers' intent and fix
- #14: a wing that is not in the mission has not arrived, which is what `NOT_KNOWN` means. Return it when `num < 0`.
- #15: with no secondary weapon there is nothing to lock with. The lock time is 0.
- #16: the bank index must name a bank the ship has. The check becomes `bank_index != -1 && bank_index < num_secondary_banks`. This fixes the cause, the stale bank, not just the read.
- #17: retail's audible result is kept (steward, D-20261018-043124), without the read: a non-weapon shockwave's impact passes a scale of 1.0e30, which clamps to full volume exactly as infinity did, and plays nothing at volume setting 0 as before. Respecting the volume settings instead would change what players hear; that choice is on Tom's audio assessment list.

## What was checked
- Trial, all 48 recordings, headless, in x86 Release, x86 Debug, x64 Release and x64 Debug: all MATCH with these fixes.
- Each fix encodes the outcome every build gave. That outcome came from the bytes before `Weapon_info` and `Wings`, which a new compiler or the Godot port could change.

## Not changed
The address-only sites (#18) are rewritten on `PrePortCleanup` when their guards land.

## Effect on mission flow
None. **No recording changed.** Per-layer re-run: in the merge chain that carries this fix.
