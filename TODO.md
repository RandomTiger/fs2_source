# TODO

## Priority
- [ ] **Audit for other cross-file struct layout mismatches.** Alt-J was broken in
      Debug because FreeSpace.cpp and the game code disagreed on the size of time_t,
      so the player struct had two layouts (fixed in 11b65c5). Check for the same kind
      of bug elsewhere:
  - [ ] Compare compile settings across every project and configuration in the
        solution: forced includes, struct packing (/Zp), alignment, defines.
  - [ ] Find shared struct definitions that change with defines one project sets
        and another doesn't (_DEBUG, NDEBUG, NO_CD_CHECK, ...).
  - [ ] If anything is suspect, have each project report the sizes of key structs
        (player, ship, object, scoring_struct) and compare them.

## Recorder (RecordPlayback branch)
- [x] Fix the replay desync during mission load (test1_2: the recording has a mouse
      position where the replay reads the timer; likely focus/pause related).
- [x] Commit the recorder work on RecordPlayback.
- [x] Record a full mission and confirm the replay matches its trace (SM1-01).
- [ ] Threshold experiments E1-E4 for TraceDiff tolerances.

## Other
- [ ] fs2_source_ue Debug has the same missing PSTypes.h forced include.
- [ ] Optional: GS_event_text in the debug log is out of step with the GS_EVENT
      numbers from event 15 on (debug output only).