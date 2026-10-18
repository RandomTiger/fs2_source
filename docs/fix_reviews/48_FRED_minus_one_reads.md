# Fix review: FRED reads index -1 with nothing selected, and writes it for an empty weapon bank

**Recommendation: TAKE.** FRED only; the game is not touched, so no recording can change. Steward decisions D-20261018-060847 (the Debug FRED sweep) and D-20261018-063535 (#21). GitHub issues #20, #21.

## How they were found
With the array guards on `PrePortCleanup`, Debug FRED stopped at start-up on the `Objects` guard. A bounded sweep followed: start, open sm1-01, the ship, wing and waypoint editors, save. It found the two bugs here. The sweep then stopped at the software renderer's `tmap_scanline != NULL` Assert (#13), which ends it.

## The bugs

**#20, `waypoint_path_dlg` (`FRED2/WaypointPathDlg.cpp`).** `initialize_data` and `update_data` choose what to show or update:
```cpp
if (cur_waypoint_list >= 0) {
    ...
} else if (Objects[cur_object_index].type == OBJ_JUMP_NODE) {
```
With nothing selected, `cur_object_index` is -1 (the main frame creates the dialog at start-up), so this reads `Objects[-1].type`. If those bytes matched `OBJ_JUMP_NODE`, it would index `Jump_nodes` with garbage.

**#21, `CFred_mission_save` (`FRED2/MissionSave.cpp`).** Ship banks (`save_common_object_data`) and turret banks (`save_turret_info`) are written by name:
```cpp
fout("\"%s\" ", Weapon_info[wp->secondary_bank_weapons[i]].name);
```
An empty bank (weapon -1) writes `Weapon_info[-1].name`, the bytes before the array, into the mission file. FRED autosaves when it opens a mission, so opening sm1-01 is enough.

## Developers' intent and fix
- #20: the line above tests `query_valid_object()` before reading `Objects`. The `else if` should too: it now tests `cur_object_index >= 0` first, in both functions.
- #21: an empty bank has no weapon name. It is written as `""`. The parser reads bank names with `stuff_int_list(..., WEAPON_LIST_TYPE)`, which turns an unknown name, `""` included, into -2. So the saved file loads exactly as retail's garbage name did (unless the garbage happened to spell a real weapon name), and it is now the same every time.

## What was checked
- Debug FRED, with the guards, now starts, and opens and autosaves sm1-01 with no guard firing. The saved file still has `#Mission Info` and `#End`.
- Release FRED: not run with these fixes; it changes only where index -1 was read. The tools build-and-run check (D-20261018-022658) will cover it.

## Not changed
The `tmap_scanline` Assert in the software renderer (#13). It is correct (no scanline routine is linked) and stays (D-20261018-063536). Until textured tool views are elected, Debug FRED is usable only on missions without background bitmaps; Release FRED skips those draws.

## Effect on mission flow
None: no game code changes. **No recording changed.** The recordings checksum the mission files in the repo, and FRED rewrites none of them unless one is saved from it. Only files FRED saves from now on change: an empty bank is written as `""` instead of garbage, and such a file loads exactly as before.
