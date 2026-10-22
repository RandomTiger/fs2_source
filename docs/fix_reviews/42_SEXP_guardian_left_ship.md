# Fix review: Debug Int3 when a sexp names a ship that has been destroyed or has left

**Recommendation: TAKE.** Debug builds only. Release builds and gameplay are unchanged. Decision D-029: assertions are never left in recordings.

## The bug
Five sexps that set a flag on a list of ships end the same way when a name is not a ship in the mission:
`sexp_protect_ships`, `sexp_beam_protect_ships`, `sexp_ships_visible`, `sexp_ships_invulnerable` and `sexp_ships_guardian` (`Parse/SEXP.CPP`).
```cpp
		num = ship_name_lookup(ship_name);
		if ( num != -1 ) {
			// set or clear the flag on the ship
		} else {
			p_object *parse_obj;

			parse_obj = mission_parse_get_arrival_ship( ship_name );
			if ( parse_obj ) {
				// set or clear the flag on the ship still to arrive
				break;
	#ifndef NDEBUG
			} else {
				Int3();	// get allender -- could be a potential problem here
	#endif
			}
		}
```
The `Int3` is meant to catch a name that matches no ship at all, which the developers flagged as "a potential problem". But it also fires for a ship the mission did have and that has since been destroyed, self-destructed or departed. That is not a problem: the right thing is to do nothing, which is exactly what Release does.

Retail missions reach it. In sm3-06, event "Twink" makes the convoy ship Nebtuu a guardian, and event "Twinkle" clears it when the Shivan cruiser Abraxis is destroyed:
```
( when ( is-destroyed-delay 0 "Abraxis" ) ( ship-no-guardian "Nebtuu" ) )
```
Meanwhile "Convoy one lost" fires when Nebtuu is down to its last hull points, and "Bang", chained 7 seconds after it, self-destructs Nebtuu. When Abraxis dies after that, which depends only on how the fight goes, "Twinkle" names a ship that no longer exists and a Debug build stops at the `Int3`. In the PrePort recording made on 2026-10-16 the order was: Convoy one lost 47.0 s, Bang 54.1 s, Nebtuu lost 64.1 s, Twinkle 67.4 s.

## Developers' intent
The comment says the `Int3` is a request to be told about a name that may be a mistake, not a rule that the case cannot happen. Their own mission script reaches the destroyed-ship case on purpose, and the code around it does the right thing for it (nothing). So the intent is clear: warn about unknown names, not about ships that have gone.

## Proposed fix
Before the `Int3`, ask the ship system whether the ship has left the mission, and skip the `Int3` if so. The ship system keeps its own list of ships that left, `Ships_exited` (`Ship/Ship.cpp`), with a name lookup, `ship_find_exited_ship_by_name`. `ship_destroyed` adds a ship marked `SEF_DESTROYED`, and `ship_departed` marks it `SEF_DEPARTED`. A self-destructed ship goes through the normal death roll, so it is added as destroyed when it blows up. Until then `ship_name_lookup` still finds it, so the `Int3` is never reached in between. The new helper, `sexp_ship_has_left_mission`, is under `#ifndef NDEBUG` and used in all five places. It accepts a list entry marked destroyed or departed. Ships removed in ship select (`SEF_PLAYER_DELETED`) are left out, as before. A name that never was a ship still stops a Debug build.

The first version of this fix asked the mission log instead (`LOG_SHIP_DESTROYED`, `LOG_SELF_DESTRUCT`, `LOG_SHIP_DEPART`). Tom preferred the ship system's own record, since the question is about ships rather than about what was logged (decision D-032, follow-up). The list holds the last 200 ships that left, the oldest replaced first. That is ample for a Debug check, and no retail mission comes near it.

The five sites, all in `Parse/SEXP.CPP`. Each is the same case: a loop over ship names that sets or clears one flag, with the same lookup (ships present, then ships still to arrive) and the same `Int3` and comment for a name found in neither:
| Function | Sexps | Flag |
|---|---|---|
| `sexp_protect_ships` | `protect-ship`, `unprotect-ship` | `OF_PROTECTED` |
| `sexp_beam_protect_ships` | `beam-protect-ship`, `beam-unprotect-ship` | `OF_BEAM_PROTECTED` |
| `sexp_ships_visible` | `ship-visible`, `ship-invisible` | `SF_HIDDEN_FROM_SENSORS` |
| `sexp_ships_invulnerable` | `ship-invulnerable`, `ship-vulnerable` | `OF_INVULNERABLE` |
| `sexp_ships_guardian` | `ship-guardian`, `ship-no-guardian` | `OF_GUARDIAN` |

Only `sexp_ships_guardian` was reached by a recording. The other four were changed too because the code, the comment and the guard are identical (decision D-032).

Not covered, to watch: a ship that never arrives stays in the arrival list, so it does not reach the `Int3`. But a ship taken off that list without arriving, if a mission can do that, would be neither present, still to arrive, destroyed nor departed, and would still stop Debug. No recording reaches that.

## What was checked
- The `Int3` and the helper are both inside `#ifndef NDEBUG`, so Release compiles to the same code.
- `ship_find_exited_ship_by_name` only reads the list. It draws no random numbers and makes no recorded reads, so Debug and Release traces stay identical.
- Retail reach: see "Classification" below.

## Classification (D-029)
Original, not introduced by the logic and drawing split.
- The `Int3` depends only on mission script and mission state, never on drawing, sound or the window.
- What killed Abraxis in the recording: a replay of PrePort's sm3-06 recording with temporary logging of every hit on Abraxis showed Nebtuu's beams, then, at 67.3 s, a shockwave (100 damage, Abraxis at 51.5 % hull). That is Nebtuu's own death explosion at the end of its death roll, 13 s after its scripted self-destruct. No debris hit Abraxis, so the capital-ship debris change (D-007) plays no part. The replay matched the recording (REPLAY OK).
- The split cannot have changed this playthrough. D-027 showed that, for the same inputs, the split's game state equals PrePort's before it in every frame except one ship's death-scream flag.
- How often: in 12 AI-flown Debug runs (120 s and 300 s) on Enhanced, PrePort before the split and PrePort now, Abraxis died first in 4; in the other 8 Nebtuu self-destructed first, and Abraxis was never close enough and weak enough to die in the blast. Whether it dies depends on how the fight went, so retail reaches the `Int3` whenever a playthrough ends that way; the recording is such a playthrough.

## Not changed
The other "get allender" style `Int3`s in `SEXP.CPP` cover different cases (impossible states, unsupported arguments) and are not reached by any recording.

## Effect on mission flow
None. Release is unchanged, and in Debug the only difference is that the mission is no longer stopped. **No recording changed.**

Re-run on DESKTOP-CLAUDE, 2026-10-16:
- `OriginalSourceBugFixes`: Release and Debug build.
- `RecordPlayback` after the merge: all 47 recordings replay (REPLAY OK). 46 traces match frame for frame. For sm1-07, the autotest `sm1-07` and the hand-played `SM1-07` wrote the same trace file name on a case-insensitive disk, so `replay_check.ps1` overwrote one with the other. Replayed separately, both match (3544 and 13183 frames). The hand-played SM1-07 replay exits with code 1 after REPLAY OK, and did so before this change too (checked on e9adc97).
- `Enhanced` after the merge: the same, 47 replay, all match. sm1-07 and SM1-07 were also replayed separately here.
- `PrePort` after the merge: 46/46 MATCH. sm1-01 and sm3-06 give identical traces in Release and Debug, and a Debug replay of sm3-06 under autotest logs 0 Int3 (it logged 1 before).
