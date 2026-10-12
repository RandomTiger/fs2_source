# Fix review: hud_target_change_check reads Objects[-1] when there is no target

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer: 34 reports across the campaign autotest.

## The bug
Every frame where the target has not changed, the target's speed trend is updated:
```cpp
current_speed = Objects[Player_ai->target_objnum].phys_info.speed;
```
With no target, `target_objnum` is -1, so this reads before `Objects[]`. The value goes into `Player_ai->last_speed` and the speed trend shown in the target box.

## Proposed fix
Read the speed only when there is a target. Otherwise `current_speed` keeps its initial 0.

## Effect on mission flow
None. The speed trend is only shown with a target, and acquiring a target resets the trend.

## Not changed
`player_match_target_speed` (`PlayerControl.cpp`) also reads `Objects[Player_ai->target_objnum].type` before its own check for -1. It is reached here only with auto speed matching on and no target. AddressSanitizer did not report it, because the autotest does not turn auto speed matching on.
