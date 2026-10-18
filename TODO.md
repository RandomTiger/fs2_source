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
- [x] Release defines FRED but links code.lib built without it (MAX_SHIPS 100 vs 150). FRED now uses 150 too, review 40.
- [x] Debug uses /MDd while code.lib uses /MTd. FRED Debug now uses static MFC and /MTd, review 41.

## Original-source bugs from /analyze (fix on OriginalSourceBugFixes, one review doc each)
Found with a `/analyze` build of RecordPlayback on 2026-10-13. Reviews 28 and 29 fixed the two that matter.
- [x] `SEXP.CPP` add_sexps, sexp_gt, sexp_lt, sexp_equal: read `Sexp_nodes[-1].value` when the
      first argument is a plain number (CAR is -1). Only compared with the NAN markers. Review 32.
- [x] `SEXP.CPP` sexp_and_in_sequence: writes `Sexp_nodes[-1].value` when the first argument
      is a plain non-zero number. Review 32.
- [x] `BmpMan.cpp` bm_create: `first_slot` starts at MAX_BITMAPS but the check is for -1, so
      with no free slot it writes `bm_bitmaps[MAX_BITMAPS]`. Review 33.
- [x] `localize.cpp` lcl_xstr_init writes `Xstr_table[index]` offsets outside the range check;
      lcl_ext_lookup_sub reads `Ts_text[-1]` on an empty string. Review 34.
- [x] Reached only after a failed Assert/Int3 or not at all in normal play: KeyControl debug
      ship cycling (`> MAX_SHIP_TYPES` should be `>=`), MissionDebrief award text (`>` should
      be `>=`) and `Campaign.missions[-1]`, MissionLog `Ships[-1]`, AiCode `goals[-1]`. Reviews 35 to 38.
- [x] FRED only: `fhash.cpp` fhash_init clears `sizeof(fhash_node)` per entry of a pointer table. Review 39.

## Recorder (RecordPlayback branch)
- [x] ASan campaign run at 6ee5ce3 (after the focus fix and reviews 27 to 29): all 34
      missions, 0 reports.
- [x] Replay check for reviews 28 and 29: all 34 `recordings/autotest` missions and
      `recordings/played/SM1-04` and `SM1-05` match in Release and Debug. Tagged
      latest-2026-10-13b.
- [x] Full replay check after the focus fix (b82c40d) and the match-speed fix (review 27):
      all 34 `recordings/autotest` missions and `recordings/played/SM1-04` match in Release
      and Debug. Tagged latest-2026-10-13.
- [x] Fix the replay desync during mission load (test1_2: the recording has a mouse
      position where the replay reads the timer; likely focus/pause related).
- [x] Commit the recorder work on RecordPlayback.
- [x] Record a full mission and confirm the replay matches its trace (SM1-01).
- [ ] Tom (2026-10-17): back-fill every historical fix as a GitHub issue on RandomTiger/fs2_source: one per fix, from the OriginalSourceBugFixes commits and docs/fix_reviews, then each later layer's fixes, labelled and linked to its commit. New findings are logged as issues as they are found (#1, #2 so far).
- [ ] Threshold experiments E1-E4 for TraceDiff tolerances.

## 32 bit rendering (32BitRendering branch)
- [x] D3D 32 bit screen and textures by default (PCX: model textures, VFX, UI,
      backgrounds); `-force_16bit` gives the old 16 bit path.
- [x] Last pass: ANIs (in-mission animations, UI menu animations, loading screen).
- [ ] Before Latest: replay every recording in Release and Debug, at 32 and 16 bit.

## Other
- [ ] Tom: play SM1-07 again on `PrePort` and commit it as `recordings/played/SM1-07`.
- [ ] Tom (back burner, 2026-10-17): demo mode. The demo is the only freely distributable data, so keep the demo logic (`FS2_DEMO`, `DEMO` blocks) and make the game detect demo data and switch to demo mode at run time instead of at build time. Tom to be involved. The build-variant dead-code group (D-063 G1: OEM, beta, press tour and the rest) waits for this, since much of it shares conditions with the demo blocks.
      The old one was removed there because the debris change (docs/DEBRIS_OFFSCREEN_FIX.md)
      makes it desync; it still passes on `Enhanced` and below.
- [ ] Optional: GS_event_text in the debug log is out of step with the GS_EVENT
      numbers from event 15 on (debug output only).
## PrePort
- [x] Mission files check out with CRLF (recordings checksum the bytes): `.gitattributes` on
      `RecordPlayback`, merged up (D-028, e9adc97).
- [ ] FRED cannot be built on DESKTOP-CLAUDE: the Visual Studio MFC component is missing (MSB8041).
      `gr_flip_frame` (db2118b) touches `GrSoft.cpp`, which FRED uses; build FRED on
      DESKTOP-MAIN, or install MFC here (Tom's call: it changes the VS install).
