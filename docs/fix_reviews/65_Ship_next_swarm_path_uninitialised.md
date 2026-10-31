# Fix review: a ship's swarm missile path count is never set, so a later mission starts it where the last one left it

**Recommendation: TAKE (asked for by Tom).** `ship::next_swarm_path` picks which of four paths each swarm missile flies, and flips the path direction every fourth missile. Nothing in the game ever sets it. A new process reads 0 because `Ships[]` is static, but a second mission in the same process reads whatever count the last ship in that slot left, so the first swarm volley of that mission can start on the wrong path and with the direction flipped.

## The bug
`code/Ship/Ship.h`:

```
	int	next_swarm_path;					// next path number for swarm missile to take
```

`code/Weapon/Swarm.cpp`, `swarm_move`, is the only code that touches it:

```
				swarmp->path_num = (parent_shipp->next_swarm_path++)%4;

				if ( parent_shipp->next_swarm_path%4 == 0 ) {
					swarmp->flags ^= SWARM_POSITIVE_PATH;
				}
```

`ship_level_init` resets each `Ships[]` slot's `objnum` only, and `ship_set` sets the other swarm fields (`num_swarm_missiles_to_fire`, `next_swarm_fire`) but not this one. So the count carries over from one mission to the next.

## Fix
`code/Ship/Ship.cpp`, `ship_level_init`: one added line in the per-slot loop, next to `Ships[i].objnum = -1;`:

```
		Ships[i].next_swarm_path = 0;	// Claude: ...
```

Every mission now starts each slot's count at 0, which is what the first mission in a new process has always read.

## What was checked
- Builds: Release and Debug with no new warnings, on OriginalSourceBugFixes and on RecordPlayback after the merge.
- `rp_gate.ps1` on RecordPlayback after the merge: the quick suite in Release and Debug, every replay `REPLAY OK` and TraceDiff 0. Each recording is the first mission since the game started, where the count was already 0, so no recording changes.

## Not changed
- `ship_set` still leaves the count alone, so a ship created in a slot that an earlier ship of the same mission used carries on from that ship's count. Changing that would change the existing recordings; it is left as the original game plays.
- The swarm paths themselves and `SWARM_POSITIVE_PATH` handling are unchanged.

## Effect on mission flow
None for the first mission in a process, which is what every recording covers. In a second mission in the same process (a campaign played on, or the simulator after a mission), the first swarm volley of each ship starts on path 0 again, as it does in a fresh game. Re-recorded missions: none.
