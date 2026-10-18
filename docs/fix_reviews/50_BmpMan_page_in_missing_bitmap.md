# Fix review: paging in a missing bitmap writes before the bitmap table

**Recommendation: TAKE.** Memory corruption reachable from any missing bitmap; retail data never reaches it, so no retail recording changes. GitHub issue #25. Decided by the steward (D-20261018-194722); made on `FS2Demo` with the demo work, written so it can be cherry-picked to `OriginalSourceBugFixes`.

## The bug
`stars_page_in` (`Starfield/StarField.cpp`) loads the subspace glow and pages it in with no check:

```
Subspace_glow_bitmap = bm_load( NOX("SunGlow01"));
bm_page_in_xparent_texture(Subspace_glow_bitmap);
```

When the bitmap is missing, `bm_load` returns -1. `bm_page_in_xparent_texture` (`Bmpman/BmpMan.cpp`) takes `n = bitmapnum % MAX_BITMAPS`, which is -1, and writes `bm_bitmaps[n+i].preloaded` and `.used_flags`, so it writes into whatever the linker put before `bm_bitmaps`. `bm_page_in_texture`, `bm_page_in_nondarkening_texture` and `bm_page_in_aabitmap` do the same.

The FreeSpace 2 demo's data has no `SunGlow01.pcx` (only `SunGlowGreen` and `SunGlowRed`); retail has it in `sparky_fs2.vp`. In the Release build's link map `bm_bitmaps` follows `MissionParse.cpp`'s statics, so the write sets `Arrival_message_delay_timestamp` to 0x02030000 and overwrites `Allow_arrival_message_timestamp`. A hardware watchpoint on the timer caught both writes in `stars_page_in`. With the timer left valid, `mission_eval_arrivals` skipped the random delay for a hostile wing's arrival message, so the demo's Release and Debug builds made different random calls (SPDemo-02 desynced at frame 1379 in Debug).

## Developers' intent
-1 is the "no bitmap" value throughout BmpMan, and callers elsewhere check it before use. The page-in functions only mark a loaded bitmap for preloading; for no bitmap there is nothing to mark.

## Proposed fix
Each of the four page-in functions returns straight away for a negative handle. No message: a missing bitmap is legitimate in older data (the demo), and the subspace glow is simply absent.

## What was checked
- The watchpoint run above, on the `FS2_DEMO` Release build with symbols.
- Retail: the quick set (12 Autotest recordings, sm1-01, sm3-06) in Release and Debug, and the full 48 before the batch is called done: see the commit and `docs/FS2_DEMO.md`.
- Demo: the three demo recordings were made again from the fixed build, and replay identically in Release and Debug.

## Not changed
`stars_page_in` and every other caller; `bm_load` and the rest of BmpMan.

## Effect on mission flow
Retail: none, because the write never happens with retail data. **No retail recording changed.** Demo: the hostile-wing arrival message timer now runs as written instead of being overwritten in Release.
