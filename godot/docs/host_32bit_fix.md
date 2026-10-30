# Host mode 32-bit pixel reads (nebula fog lost red)

Fix note for D-20261029-095513 (fix), D-20261029-095514 (gate C rebaseline) and D-20261029-095515 (gpu prototype re-run).

## Bug
`D3D_32bit` (GrD3D.cpp) is 0 unless D3D init sets it. Host mode (gr_host, Godot port) never set it, so `bm_get_components`
took its 16-bit path on the host's 32-bit screen pixels, read only the low 16 bits and returned R = 0. The nebula fog colour
of the `same` path (gr_host tmapper, PIXEL_FOG, via `neb2_get_pixel`) therefore never had red. Found by the GPU fog
prototype (R2_fog_design.md section 8).

## Fix
`fs2game/gr_host.cpp` only, no `code/` line: `host_init` saves `D3D_32bit` and sets it to 1, since the host screen is
32 bit (`bits_per_pixel` 32), as D3D init does for a 32-bit screen; `host_cleanup` restores it. gr_host.cpp is built only
into fs2game, so the native exes are unchanged.

## x86 D3D check (095513 a)
Retail x86 D3D build, sm2-07 autotest recording, back-buffer shot and fog-colour sample every 300 frames (temporary DIAG
exe in a scratch install, not committed; images local). `D3D_32bit` = 1 at every sample and every fogged polygon has
R > 0, e.g. frame 300: 23643 of 23643 polygons R > 0, mean fog colour 39.4 / 91.9 / 42.3; frame 1500: mean 45.4 / 106.2 /
48.8. Retail fog carries red, so the fix is right. The D3D shots look olive/purple where the host `same` path looked teal.

## Readers of D3D_32bit and bm_get_components, and what changes in host mode (095513 b)
| Site | Reached in host mode | Change |
|---|---|---|
| `neb2_get_pixel` (Neb.cpp, 9 `bm_get_components` calls on the 32x32 `tpixels` buffer) | yes, only from gr_host.cpp tmapper PIXEL_FOG, and only when kNebFog is `same` | the per-polygon fog colour passed to `gr_fog_set` now has its red; G and B unchanged. Render state only: no game logic reads it |
| `neb2_get_pixel` from GrD3DRender.cpp:969 | no (D3D renderer) | none |
| `bm_get_components` in GrD3D.cpp:2213 | no (D3D-only function) | none |
| `D3D_32bit` in BmpMan.cpp 2409 / 2434 / 2459 | no (inside `GR_DIRECT3D` branches) | none |
| `D3D_32bit` in BmpMan.cpp 2479 | no (commented out) | none |
| `D3D_32bit` in BmpMan.cpp 2552 | yes: `bm_get_components` itself | the 32-bit read is taken (the fix) |
| other `D3D_32bit` uses in GrD3D.cpp | no (D3D-mode functions) | none |

In `off` mode (the committed kNebFog) the `tpixels` buffer is not filled and the fog read is not made, so nothing changes.

## Gates
- TraceDiff, gates A (headless) and B (-NoDraw host), r and d, on sm1-01, sm2-01, sm2-07, sm2-09, sm3-04, sm3-06 and the
  nebula recordings AutotestNebActive, AutotestNebMedium, AutotestNebStandard, plus FS1 sm1-08a, sm2-07a, sm1-01:
  all 8 runs MATCH (exit 0), no trace moved.
- L0 (`tools\warning_check.ps1`, Release and Debug): PASS, 0 warnings; fs2game x64 r and d (the only project the fix touches) build with 0 warnings.
- L1 (Godot headless, r and d, autotest, fs1, demo, fs1demo): green.

## Gate C before / after (095514)
Shown host mode, one shot every 300 game frames (shot_NNN = game frame NNN x 300), the full gate C set: sm1-01, sm2-01,
sm2-07, sm2-07b, sm2-09, sm3-04, FS1 sm1-01, sm1-08a, sm2-07a. r and d each against their own baseline.

**off mode:** 0 changed shots in r and in d. The D-20261029-052041 statement (off leaves gate C unchanged) holds.

**same mode** (kNebFog set to same locally for the run, not committed): r and d list the same shots. Only R changes, and
it only rises; G and B are identical. Each changed shot is a nebula-fogged frame (the same shots differ between off and
same in the old baseline), so each comes from the `neb2_get_pixel` fog read above.

| Mission | Changed shots (game frame / 300) | Largest R rise |
|---|---|---|
| sm2-01 | 002, 003, 004, 005, 006, 007, 008, 010, 011 | 33-45 |
| sm2-07 | 001, 002, 003, 004, 005, 007, 008, 009, 010 | 38-47 |
| sm2-07b | 001, 002, 003, 004, 005, 007, 008, 009, 010 | 38-47 |
| sm2-09 | 004, 005, 006, 010 | 1 (004), up to 117 |
| sm3-04 | 001, 006 | 89, 101 |
| sm1-01, FS1 sm1-01, sm1-08a, sm2-07a | none | - |

Eye check against x86 D3D at the same mission frames (images local): sm2-01 shot 005, the fogged ship silhouette now blends
into the purple nebula as in D3D; sm2-07 shot 004, the fogged hull moves from teal to the D3D olive. sm2-09 and sm3-04
change a few pixels each, mostly on the target-box model, where the D3D fog has strong red (R about 60-112).

**gpu mode:** not run here; re-run on GodotR2 after the merge (D-20261029-095515).

**Repeat:** after-fix runs made twice, hashes equal in both modes for r and d.

**Baselines (local, not committed):** old `C:\Games\FS2FF_out\gatec\before_off`, `before_same`; new `after_off1`
(= `after_off2`), `after_same1` (= `after_same2`), kept beside the old ones.
