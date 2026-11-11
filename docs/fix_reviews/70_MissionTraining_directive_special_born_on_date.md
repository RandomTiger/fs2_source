# Fix review: directive sort asserts on a directive marked special

Issue #290. **Recommendation: FIX, Debug only (steward, D-20261109-092601).** When a wing is wiped out before its next wave arrives, its directive is marked special and shown as done, but it never gets a `born_on_date`. The directive list sort asserts that every event has one, so a Debug build asserts every frame the directive is on the list (789 asserts in an autoplay run of `loop2-2`). Release is unaffected. Both asserts now allow a special directive; every other event is still checked.

## The bug
`code/Mission/MissionTraining.cpp`, `comp_training_lines_by_born_on_date`, as released:

```
	Assert(Mission_events[*e1 & 0xffff].born_on_date != 0);
	Assert(Mission_events[*e2 & 0xffff].born_on_date != 0);
```

`code/Mission/MissionGoals.cpp`, `mission_get_event_status`: the `MEF_DIRECTIVE_SPECIAL` branch returns before the `MEF_CURRENT` branch, which is the only place that sets `born_on_date = timestamp()`. `mission_process_event` sets `MEF_DIRECTIVE_SPECIAL` when `Directive_count == DIRECTIVE_WING_ZERO`, i.e. the wing was destroyed before all its waves arrived. `training_check_objectives` then adds the event to the list, since `timestamp() > 0 + 3000` is true, and the sort reaches the asserts. A player who kills a wing quickly hits it; autoplay just does so reliably.

## Fix
Both asserts become `born_on_date != 0 || (flags & MEF_DIRECTIVE_SPECIAL)`. Every changed line carries a `// Claude:` comment. The sort itself is unchanged, so the HUD directive order is the same as retail in both builds.

## What was checked
Gate on this branch per the planner: Release and Debug build. The replay proof that no recording moves is RecordPlayback's merge gate, which names this fix. The held autoplay recording of `loop2-2` replays in Debug with no assert after the merge (results in the RecordPlayback/AutoPlay commits).

## Not changed
- Setting `born_on_date` in the special branch (rejected by the steward): it would change the retail HUD directive order and move recordings, which is Tom's call.
- `training_check_objectives`' `born_on_date + 3000` test still treats 0 as long ago, as in retail.

## Effect on mission flow
None. Assert has no code in Release, and in Debug the sort result is unchanged. No missions were re-recorded.
