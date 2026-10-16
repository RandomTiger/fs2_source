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
Before the `Int3`, ask the mission log whether the ship was destroyed (`LOG_SHIP_DESTROYED`), self-destructed (`LOG_SELF_DESTRUCT`) or departed (`LOG_SHIP_DEPART`), and skip the `Int3` if so. This is a new helper, `sexp_ship_has_left_mission`, under `#ifndef NDEBUG`, used in all five places. A name that never was a ship still stops a Debug build.

## What was checked
- The `Int3` and the helper are both inside `#ifndef NDEBUG`, so Release compiles to the same code.
- `mission_log_get_time` only reads the log. It draws no random numbers and makes no recorded reads, so Debug and Release traces stay identical.
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
None. Release is unchanged, and in Debug the only difference is that the mission is no longer stopped. Re-run result: (filled in after the run)
