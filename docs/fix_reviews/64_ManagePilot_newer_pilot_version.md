# Fix review: a pilot file newer than the build is read as the old layout and crashes

**Recommendation: TAKE (steward D-20261031-065901, issue #114).** The pilot reader refuses files older than `LOWEST_COMPATIBLE_PLAYER_FILE_VERSION` but never files newer than `CURRENT_PLAYER_FILE_VERSION`. Enhanced writes version 141, so a base-layer build sharing Enhanced's players folder reads a 141 file as 140 and fails an Assert (`len < n` in `cfread_string_len`) in Debug, or crashes with an access violation in Release.

## The bug
`code/Playerman/ManagePilot.cpp`, `read_pilot_file` (the same lower-bound-only test is in `verify_pilot_file` and `is_pilot_multi(CFILE *)`):

```
	Player_file_version = cfread_uint(file);
	cf_set_version( file, Player_file_version );

	if (Player_file_version < LOWEST_COMPATIBLE_PLAYER_FILE_VERSION) {
		...
		return -1;
	}
```

On the base layers `CURRENT_PLAYER_FILE_VERSION` is 140 and `LOWEST_COMPATIBLE_PLAYER_FILE_VERSION` equals it, so any version above 140 passes and the rest of the file is parsed with the 140 layout. Version 141 adds a data-set tag after the version, so every later field is read four bytes off.

## Fix
At each of the three places that test the lower bound, one added line refuses a version above `CURRENT_PLAYER_FILE_VERSION`: it closes the file and returns -1, as the lower-bound test does, with an `mprintf` saying why. Unlike the lower-bound test it does not call `delete_pilot_file`: the newer file belongs to a newer build and is left exactly as it is. Enhanced already refuses `version > CURRENT_PLAYER_FILE_VERSION` in its own reader; only that upper bound is mirrored. Each line carries a `// Claude:` comment. No Assert or Error, and no bounds or end-of-file guard in cfile.

## What was checked
- Builds: Release and Debug, on OriginalSourceBugFixes and on RecordPlayback after the merge.
- With a copy of a version-141 pilot (an `AutotestPilot.plr` written by Enhanced) in the players folder, Release and Debug RecordPlayback builds: `-autotest -at_pilot` on it logs that it cannot load and falls back to the scratch pilot with no crash; the Barracks pilot list (menu tour) leaves it out; its SHA256 is unchanged afterwards.
- A version-140 pilot (a new pilot made by the RecordPlayback Release build) still loads, Release and Debug.
- `replay_check.ps1` (quick suite) Release and Debug on RecordPlayback, and `rp_gate.ps1`.

## Not changed
- The lower-bound test still deletes an older file (#23 is fixed above this layer).
- `player_select_get_last_pilot` (only under the multiplayer `-startgame` and `-connect` switches) ignores `read_pilot_file`'s result, for an older file as before and now for a newer one too.
- Base layers and Enhanced must not share a players folder: a base build never shows or loads Enhanced's pilots.

## Effect on mission flow
None: pilot files the build wrote itself (140) load as before. Re-recorded missions: none.
