# TODO

## Priority
- [x] **Audit for other cross-file struct layout mismatches.** Alt-J was broken in
      Debug because FreeSpace.cpp and the game code disagreed on the size of time_t,
      so the player struct had two layouts (fixed in 11b65c5). Check for the same kind
      of bug elsewhere:
  - [x] Compare compile settings across every project and configuration in the
        solution: forced includes, struct packing (/Zp), alignment, defines.
  - [x] Find shared struct definitions that change with defines one project sets
        and another doesn't (_DEBUG, NDEBUG, NO_CD_CHECK, ...).
  - [x] If anything is suspect, have each project report the sizes of key structs
        (player, ship, object, scoring_struct) and compare them. Done for every
        struct by listing same-named types with different sizes in the link PDBs
        (llvm-pdbutil dump -types; relink with a fresh PDB first, as incremental
        links keep stale records).
  - [x] barracks_buttons was defined differently in PlayerMenu.cpp and Barracks.cpp,
        so they shared one destructor (wrong offset at exit). Renamed the
        PlayerMenu.cpp one to player_menu_buttons.

## Fred2
- [ ] Release defines FRED but links code.lib built without it (MAX_SHIPS 100 vs 150).
- [ ] Debug uses /MDd while code.lib uses /MTd.

## Recorder (RecordPlayback branch)
- [x] Full replay check after the focus fix (b82c40d) and the match-speed fix (review 27):
      all 34 `recordings/autotest` missions and `recordings/played/SM1-04` match in Release
      and Debug. Tagged latest-2026-10-13.
- [x] Fix the replay desync during mission load (test1_2: the recording has a mouse
      position where the replay reads the timer; likely focus/pause related).
- [x] Commit the recorder work on RecordPlayback.
- [x] Record a full mission and confirm the replay matches its trace (SM1-01).
- [ ] Threshold experiments E1-E4 for TraceDiff tolerances.

## Other
- [ ] Optional: GS_event_text in the debug log is out of step with the GS_EVENT
      numbers from event 15 on (debug output only).