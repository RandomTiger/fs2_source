# Gate C frames expected to change: GodotInsignia depth fix (d4b81be)

Written before the gate C run of the Godot integration (merges 1f4d185, 42edbbd, 3a08b14, ac701f8 and the gr_host port
8b190df), as D-20261029-222606 condition e asks. Baseline: `before_off` (5fe0769). Modes: Release (r) and Debug (d),
each against its own baseline; the game state and draws are the same in both, so the list is the same for r and d.

## What d4b81be changes
`render_sink_godot.cpp` `add_to_layer`: depth offset per `gr_zbias` unit 2.0 -> 0.05. `gr_zbias(1)` covers two kinds
of draw in `ModelInterp.cpp`: the insignia decals (`model_render_insignias`, zbuffer READ) and the subobjects of every
model (turrets and other subobjects, zbuffer WRITE/FULL). Both move.

## How the list was found
- Insignias draw only on ships of the Alpha, Beta, Gamma and Zeta wings, from the pilot's squad image, on models with
  an INSG chunk. All 8 gate C recordings carry a pilot with a squad image (ravens.pcx).
- FS2 runs: every Alpha/Beta/Gamma/Zeta ship in sm1-01, sm2-01, sm2-07, sm2-09 and sm3-04 uses a POF with 4 insignias.
- FS1 runs (fs1_sm1-01, fs1_sm1-08a, fs1_sm2-07a): no POF in the FS1 data has an INSG chunk, so no insignia is drawn.
- A temporary per-frame count in `gr_host.cpp` (not committed) logged, for every committed host frame, the triangles
  drawn with zbias in READ mode (insignia) and in WRITE/FULL mode (subobjects), over the 5 FS2 recordings (r).
- Gate C shot n (n >= 1) shows host frame 304n - 1: `runner.gd` saves when the resume count reaches 304n
  (8 resumes per Godot frame, next shot 300 resumes on), and the image is the last frame drawn before that check.
  All logged committed frames were 7 mod 8, as this needs.

## Expected to change: insignia shown (class e)
| run | shot | host frame | insignia tris | subobject tris | modes |
|---|---|---|---|---|---|
| sm1-01 | shot_009 | 2735 | 16 | 0 | r, d |

No other gate C frame draws an insignia (sm2-07 frame 1207, with 10, lies between shot_003 and shot_004).

## Also drawn with zbias, no insignia: subobjects only (not class e)
These frames draw subobjects with zbias 1 and no insignia. If the 0.05 offset moves their pixels they change too,
and fall in neither class of D-20261029-222606 (condition f).
| run | shots (subobject tris) |
|---|---|
| sm1-01 | shot_002 (46) |
| sm2-01 | shot_005 (128) |
| sm2-07, sm2-07b | shot_001 (61), shot_002 (63), shot_003 (126), shot_004 (166), shot_005 (110), shot_007 (124), shot_008 (118), shot_009 (64) |

No zbias draws in any shot of sm2-09, sm3-04 or the three FS1 runs.
