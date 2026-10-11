# Fix review: PlayerControl.cpp `player_level_init` `killer_weapon_index`

**Recommendation: TAKE (low priority).** It only affects text, and the intent is clear.

## The bug
```cpp
	Player->killer_objtype=-1;					// type of object that killed player
	Player->killer_weapon_index;				// weapon used to kill player (if applicable)
	Player->killer_parent_name[0]=0;			// name of parent object that killed the player
```
The middle line is a bare expression with no assignment. Every line around it resets to -1 or 0, so `= -1` was intended.

## When it matters
`Player` is zeroed with memset when a pilot is selected (PlayerMenu.cpp), so the field is never truly uninitialised. It is *stale*: it keeps the value from the previous death.

`shiphit_record_player_killer` sets it on every kind of death *except a beam kill* (ShipHit.cpp ~1068). So after a beam death, the field still holds the weapon from an earlier death.

It is only read by `player_generate_death_text` (and multiplayer packing), which builds the "X was killed by …" message. That function ignores negative indices.

## Proposed fix
```diff
-	Player->killer_weapon_index;				// weapon used to kill player (if applicable)
+	Player->killer_weapon_index=-1;				// weapon used to kill player (if applicable)
```

## Effect on mission flow
None. It's death-message text only.

## Effect on replays
None.

## Why take it
It's a one-line fix with clear intent and no gameplay impact. It's equally fine to skip under the minimum-changes rule, because the worst case is a wrong weapon name in a death message.
