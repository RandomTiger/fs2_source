# Fix review: a wing ship finishing waypoints with no goal reads goals[-1] or goals[999]

**Recommendation: TAKE.** Out-of-bounds read on a path the original comments call legal; it decides whether a whole wing's goal is marked complete. Found with `/analyze` (C6385) on 2026-10-13.

## The bug
When a ship reaches the end of its waypoints, `ai_waypoints` (`Ship/AiCode.cpp`) completes the goal that sent it there. For a ship in a wing it first looks at the goal's type, to tell an order given to the whole wing from one given to the ship:
```cpp
// I don't think that you can fly waypoints as dynamic goals!!!
// -- This is legal, just stupid. -- Assert( (aip->active_goal != AI_GOAL_NONE) && (aip->active_goal != AI_ACTIVE_GOAL_DYNAMIC) );

//	Clean up from above Assert, just in case we ship without fixing it.  (Encountered by JimB on 2/9/98)
if ( (aip->active_goal == AI_GOAL_NONE) || (aip->active_goal == AI_ACTIVE_GOAL_DYNAMIC) ) {
	aip->mode = AIM_NONE;
	Int3();	//	Look at the ship, find out of it's supposed to be flying waypoints. -- MK.
}

type = aip->goals[aip->active_goal].type;
if ( (type == AIG_TYPE_EVENT_WING) || (type == AIG_TYPE_PLAYER_WING) ) {
	treat_as_ship = 0;
```
The clean-up resets the mode but still reads the goal: `AI_GOAL_NONE` is -1 and `AI_ACTIVE_GOAL_DYNAMIC` is 999, while `goals` has 5 entries (`MAX_AI_GOALS`). The read lands before the array or far past it, in other ships' AI data. If those bytes happen to read 2 or 4 (a wing order), the code calls `ai_mission_wing_goal_complete` with that bogus goal, which marks a matching goal complete for every ship in the wing, and logs the wing as having finished its waypoints.

## Proposed fix
With no goal slot, don't read one: use a type that is not a wing order, so the ship is treated on its own. `ai_mission_goal_complete` already returns at once for `AI_GOAL_NONE` and `AI_ACTIVE_GOAL_DYNAMIC`, so the ship just logs that it finished its waypoints, as a ship not in a wing does.

## What was checked
- `AIG_TYPE_*` values are 1 to 5, so -1 matches none of them.
- `ai_mission_goal_complete` (`Ship/AiGoals.cpp`) does nothing for the two goal values.
- The Int3 stays, so a Debug build still stops there.

## Not changed
The Int3 and the reset of the AI mode.

## Effect on mission flow
Only a ship in a wing that finishes flying waypoints without a goal of its own is affected. It no longer risks completing a goal for its whole wing based on unrelated memory. The replay check after the merge into `RecordPlayback` shows whether any recording reaches it.
