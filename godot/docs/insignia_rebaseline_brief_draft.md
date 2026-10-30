# Draft: rebaseline brief for the steward, D-20261029-095514 (insignia/subobject depth fix, GodotInsignia2)

Fill the bracketed parts after the run. The expected list is `insignia_expected_frames_v2.md`, committed before the run.

**Decision request for fs2_decision_steward**
Question: May gate C on Godot be rebaselined after the z-bias scale change (2.0 -> 0.05 depth units per gr_zbias, Godot wrapper only), a deliberate fidelity fix towards retail D3D?
Base: Godot [tip] + GodotInsignia2 [commit]. Baseline: 5fe0769 (`before_off`), kept beside the new one.

Conditions of D-20261029-095514, each with its evidence:
- a. Gates A and B TraceDiff 0 r/d: [results]. L0, L1, packaged: [results]. No recording or trace changed.
- b. Full gate C set (104 shots per mode, 9 runs), r and d each against its own baseline, run twice, identical hashes: [pass 1 vs pass 2]. Changed frames: [list: run, shot, mission time, mode, old and new hash, changed pixels, bbox].
- c. Every changed frame explained by the fix: the list was declared first (commit [hash]) and names 13 frames per mode (FS1 7, FS2 6). Result: [changed set equals the list / differences]. Mechanism: subobject pass and insignias use zbias 1; 60 m -> 1.5 m advantage over the hull; changes only where a subobject surface lies 1.5-60 m behind a hull surface. FS1 counts per shot: [from open run 1]. Frames that must not change (91 per mode, including sm1-01 shot_009, the one insignia frame): [unchanged / exceptions].
- d. D3D eye check by a worker other than the author (not the one who wrote the fix or this list), the x86 D3D build at the same mission time, images local, none committed: every changed frame, or at least one per mission and caller (FS1 sm1-08a, FS1 sm2-07a, sm1-01, sm2-01, sm2-07 target monitor). Result per frame: [match / mismatch]. A mismatch blocks.
- e. Off-mode changes: gate C runs with kNebFog off, so these changes are off-mode changes and are called out by name here; D-20261029-052041 ("off leaves gate C unchanged") then holds for the new baseline.
- f. New baseline, old one kept, the list and the D3D check go into the commit message and R2_fog_design.md or a fix note, citing this entry.

Open points the steward should know (from the v2 list): [state results of open items 1-3: FS1 counts, the 0.05 value against D3D, close-up insignia visibility]. If open item 2 or 3 fails the eye check, the offset changes and the list is rewritten and re-declared before another run.

Consequence if approved: manager rebaselines gate C on Godot only, pushes GodotInsignia2 merge as a fast-forward. If wrong: turrets or insignias mis-ordered against retail in a later D3D eye check; the old baseline restores.
