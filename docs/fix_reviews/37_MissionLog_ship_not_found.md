# Fix review: mission log reads Ships[-1] when a ship name is not found

**Recommendation: TAKE.** Out-of-bounds reads after an Assert that Release skips. Found with `/analyze` (C6385) on 2026-10-13.

## The bug
`mission_log_add_entry` (`Mission/MissionLog.cpp`) looks up the ship an entry is about to record its team and decide whether the entry is shown:
```cpp
index = ship_name_lookup( pname );
Assert ( index != -1 );
if(index < 0){
	mission_log_flag_team( entry, ML_FLAG_PRIMARY, TEAM_FRIENDLY );
} else {
	mission_log_flag_team( entry, ML_FLAG_PRIMARY, Ships[index].team );
}
...
} else if ( (type == LOG_SHIP_SUBSYS_DESTROYED) && (Ship_info[Ships[index].ship_info_index].flags & SIF_SMALL_SHIP) ) {
	entry->flags |= MLF_HIDDEN;
} else if ( (type == LOG_SHIP_ARRIVE) && (Ships[index].wingnum != -1 ) ) {
	entry->flags |= MLF_HIDDEN;
}
```
The team lookup handles a ship that is not found (`index < 0`), but the two checks below it use `Ships[index]` anyway, reading `Ships[-1]`, and the subsystem one then indexes `Ship_info` with whatever it found. The docking entry does the same for the second ship:
```cpp
index = ship_name_lookup( sname );
Assert( index != -1 );
mission_log_flag_team( entry, ML_FLAG_SECONDARY, Ships[index].team );
```
In Debug the Asserts stop it; in Release it reads memory before the `Ships` array.

## Proposed fix
Skip the two "hidden" checks when the ship was not found, and give the docking entry's second ship the same default team (`TEAM_FRIENDLY`) the first ship already gets.

## What was checked
- The destroyed-ship branch in between already handles a missing second ship (it also looks among the departed ships and breaks out when not found).
- `MLF_HIDDEN` and the team flags only affect how the log is shown (the mission log screen and its colours).

## Not changed
`/analyze` also flagged the log display reading `Ship_info` with the class packed into a subsystem entry's index; that value is written by the game itself when a subsystem is destroyed, so it is left alone.

## Effect on mission flow
None on gameplay. When a log entry names a ship that can't be found, it is now shown with the default team and not hidden, instead of depending on memory before the ship array.
