# Gate C frames expected to change, v2: GodotInsignia2 depth fix (FS1 and FS2)

Declared before any run of this branch (D-20261030-012421 step d; supersedes `insignia_expected_frames.md`, which stays as
history). Baseline: `before_off` (5fe0769), Release (r) and Debug (d) each against its own baseline; draws are the same in
both modes, so one list serves both. Gate C runs with kNebFog off.

## Precondition: the list holds for exactly this change
`godot/src/render_sink_godot.cpp` `add_to_layer`: depth offset per `gr_zbias` unit 2.0 -> 0.05 (the change of d4b81be,
reverted on Godot by 4369648), and nothing else. A different offset, or any other draw change, voids the list: rewrite it
first. The expected outcome below is the outcome measured on Godot 79b9812 (d4b81be on top of the carry-up), run twice,
r and d, which repeated; it is deterministic, so the same change on the same base must reproduce it.

## Mechanism (what v1 got wrong)
- `gr_zbias(1)` is not only the insignia decals. `ModelInterp.cpp` `model_render` sets it for the whole subobject pass
  (every non-thruster child submodel: turrets, dishes, engines, capital-ship parts, zbuffer WRITE) and for
  `model_render_insignias` (READ); the hull is drawn between them at bias 0 (FULL). That code is shared by FS1 and FS2.
- Sink depth is `d = 2 + (24 - epoch) * 1100 + z * 1000 - zbias * K`, z = game z / 30000, so one depth unit is 30 game m.
  K = 2.0 pulled every subobject about 60 m in front of the hull; K = 0.05 is about 1.5 m. A pixel changes only where a
  subobject surface lies between 1.5 m and 60 m behind a hull (or other bias-0) surface on the same ray: the old code drew
  the hidden part of the subobject over the hull, the new one lets the hull hide it. That is a join, an embedded base or a
  part tucked behind plating, so only some subobject frames change, and the large FS1 structures (many big subobjects, close)
  change most.
- v1's "no zbias draws in any FS1 shot" was never measured: the temporary triangle counter ran over the 5 FS2 recordings
  only ("over the 5 FS2 recordings (r)"). For FS1 it was inferred from "no INSG chunk in FS1 POFs", which only rules out
  insignias, not the subobject pass. Correct statement: FS1 shots draw subobjects with zbias 1, per-shot counts not taken.
- The HUD target-monitor model is drawn through the same `model_render`; it is a source of changed pixels (sm2-07 below).
- sm1-01 shot_009 (16 insignia triangles): the frame shows distant fighters a few pixels across (checked in the baseline
  image). Insignia triangles of that size cover at most a few pixels, and a decal on the near side of a hull passes the
  depth test under either offset. Not proven; see Open items.

## Expected to change (13 frames per mode, 26 in all)
"px" = changed pixels, "bbox" = x0-x1, y0-y1 on the 1152x648 frame. The pixel set must equal the measured one (local files
`C:\Games\FS2GI_out\pix_{r,d}_<run>_shot_NNN.txt`, sha256 prefix shown, r and d identical). Time = HUD mission clock.

| run | shot | px | bbox | sha256 | why it changes |
|---|---|---|---|---|---|
| fs1_sm1-08a | 002 | 657 | 536-891, 4-647 | 32920af79217 | Close view of the large cargo structure (00:22): subobject pieces now hidden by hull plating at joins. |
| fs1_sm1-08a | 007 | 628 | 151-867, 5-647 | ecb5e9fa4d24 | Same structure (01:12), more of it in view. |
| fs1_sm1-08a | 008 |  108 | 160-175, 261-400 | 2230e1bb94aa | Edge of the structure at the left border (01:22), thin join strip. |
| fs1_sm2-07a | 005 | 3 | 595-648, 369-496 | bfc41e0f379a | Three isolated pixels, distant ship parts (00:52). |
| fs1_sm2-07a | 007 | 72 | 893-981, 201-283 | 08b51a8dfe20 | Ship at the top right under the wingmen panel (01:12). |
| fs1_sm2-07a | 008 | 393 | 309-964, 325-447 | cbc528c73c52 | Two damaged ships left and right of the reticle (01:22). |
| fs1_sm2-07a | 009 | 604 | 908-958, 0-18 | 307e1cea0ee2 | Ship at the top right corner (01:33). |
| sm1-01 | 002 | 11 | 264-267, 129-158 | f3227eb4d364 | Thin vertical join line of the distant structure at upper left (00:22); 46 subobject tris in frame. |
| sm2-01 | 005 | 61 | 390-717, 235-299 | 54ecee9a5832 | Subobject/hull joins on the capital ships at upper centre (00:54); 128 subobject tris. |
| sm2-07 | 004 | 4 | 194-199, 574-583 | 0dde351e5028 | Inside the HUD target-monitor box: the targeted ship model's own subobjects (00:44); 166 tris. |
| sm2-07 | 005 | 1 | 193-193, 588-588 | f405be7d9b3f | Same target-monitor model, one pixel; 110 tris. |
| sm2-07b | 004 | 4 | 194-199, 574-583 | 0dde351e5028 | Same recording as sm2-07, same result. |
| sm2-07b | 005 | 1 | 193-193, 588-588 | f405be7d9b3f | As sm2-07 shot_005. |

FS1: 7 frames per mode (3 + 4 of the 36 FS1 shots). FS2: 6 per mode.

## Expected unchanged (every other shot: 91 per mode of 104)
- fs1_sm1-01 (12 shots), fs1_sm1-08a shots 000, 001, 003-006, 009-011, fs1_sm2-07a shots 000-004, 006, 010, 011: unchanged
  as measured. fs1_sm1-01 is a fighter-only run; its shots may still draw subobjects (count not taken), but no pixel moved.
- sm2-09 and sm3-04 (all shots): unchanged as measured (no subobject draws counted in either).
- FS2 frames that draw subobjects (v1 count) but did not move, so a change in any of them fails: sm2-07 and sm2-07b shots
  001, 002, 003, 007, 008, 009 (61, 63, 126, 124, 118, 64 tris).
- sm1-01 shot_009 (host frame 2735, 16 insignia tris, 0 subobject): the only frame with an insignia in the gate C set.
  Declared unchanged, as measured. If it changes, its changed pixels must lie on an insignia triangle of that frame
  (needs the open run below to check); otherwise it fails.
- Insignia frames in general: none other in the set (sm2-07 host frame 1207, 10 tris, lies between shots 003 and 004).

## Not changed by this list
Gates A and B (trace) are untouched: presentation code only. Any frame outside the 13, any frame of the 13 with a different
pixel set, or the listed unchanged frames moving, fails and goes to the steward.

## Open items that need a run (none run here)
1. FS1 subobject and insignia draw counts per shot (the v1 counter, temporary, uncommitted, extended to FS1 and to log
   screen bbox of insignia triangles): one headless-off shown pass of the 3 FS1 recordings and sm1-01 host frame 2735 in r.
   Confirms the mechanism for the 7 FS1 frames and the "few pixels" reading of shot_009. Not needed to run gate C, needed to
   close the mechanism claim before the rebaseline brief.
2. Is 0.05 right? Retail D3D ZBIAS 1 on a W buffer 0..30000 m is one W step, whose size in game m depends on the buffer
   depth (about 0.46 m at 16 bit, far less at 24 bit). 0.05 units = 1.5 m was chosen as "about one step" without a
   measurement against D3D. Resolving it needs a D3D build capture of a hull/subobject join, not a Godot run.
3. Whether the insignia, now behind the hull by interpolation error on large triangles, still shows on a close ship: no
   gate C frame covers it. Needs a new close-up insignia test frame (a ship of the Alpha wing at 100-300 m) in both builds.
