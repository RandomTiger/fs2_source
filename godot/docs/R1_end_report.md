# R1 end report (2026-10-28), branch GodotR1End off Godot 5ab740c

Result: L2 green (gates A, B, C, -nodraw subset, packaged gate; Release and Debug; L0 0 warnings). 

## Closing items
1. Texture cache freed on bm_release. bmpman got an additive, host-neutral `Bm_release_hook` (BmpMan.h/.cpp, no engine name, called in `bm_release` before the data is freed, user bitmaps only). gr_host sets it in host_init, drops every cached texture of that handle (all kinds and sections) and frees the sink ids at the next accepted frame (texture_free elsewhere only runs while drawing). No clock, rand or host_hooks use; no branch-visible return changes.
2. `h_get_region` no longer assumes 32 bit. It packs the sink's RGBA8 read into the screen format by `gr_screen.bytes_per_pixel` (2 or 4) and the `Gr_red/green/blue/alpha` guns, the same layout D3D's back-buffer copy yields. Callers: Neb (tpixels, 32 bit), Autotest `autotest_shot`.
3. Stubs: none left in gr_host (stub removal 02ef174 is in; grep for stub/not implemented finds nothing).
4. Tools: `godot_check.ps1` gained `-Shown` (gate C: no --headless, game without -headless, FS2_GR_HOST=1) and `-NoDraw` (game gets -nodraw, host renderer). The native baseline is unchanged in both.

## Gates (own scratch C:\Games\R1E_*, one game at a time, all TraceDiff 0 vs native x64 of the same base)
| Gate | What | Result |
|---|---|---|
| L0 | Freespace2 + fs2game x64, Release and Debug | 0 warnings |
| L1 | godot_check -Level L1, r and d | green |
| A | godot headless, GR_DUMMY, all sets (fs2 52, fs1 43, fs2 demo 5, fs1 demo 6), r and d, restart and fatal checks | pass, 0 fail |
| B | same with FS2_GR_HOST=1 | pass, 0 fail |
| C | shown window, host renderer: L1 FS2 set (4 fixed + 15 feature tests), 4 FS1, SPDemo-01, demo01; r and d | pass, 0 fail |
| nodraw | `-nodraw` host mode: sm1-01, sm3-06, 4 FS1, SPDemo-01, demo01; r | pass, 0 fail |
| Packaged | FS2Godot.exe + pck + fs2godot_rel.dll (always host mode), all sets, r and d | pass, 0 fail (fs2 52, fs1 43, fs2 demo 5, fs1 demo 6, r and d, restart checks) |

fs1demo is scored by the clean-run rule (D-20261026-223829). No trace moved in host mode; nothing was re-recorded.

## Known gaps (not R1 faults)
- Shivan glow (D-20261028-111135): left out until after R1. The glow-pass gf_* calls are no-ops with dummy-equal returns, so traces are unaffected; Shivan ships render without their glow.
- Fog (D-20261028-135516): fog is R2. Fog missions are reported "fog-missing". gr_fog_set stays a no-op and sink v2 fog fields stay reserved.
- Not done by order: the performance pass and frame-time work (Tom: later, one pass).
