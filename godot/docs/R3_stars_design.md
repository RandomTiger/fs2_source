# R3 row 8: stars on the GPU

## What the CPU route does (StarField.cpp `stars_draw`; gr_host.cpp `h_line`/`segment`/`h_pixel`)
Per star i < Detail.num_stars * Num_stars / MAX_DETAIL_LEVEL (Stars[i].pos is a fixed unit vector made at level load):
1. p2 = View_matrix * pos (`vm_vec_rotate`). Dropped on any `g3_code_vertex` code or `g3_project_vertex` overflow (z <= MIN_Z).
2. Default flags TAIL|DIM. The host turns the line into `h_line` on the truncated ints, after the "same pixel, p1.sx += 1" fudge.
3. Tail: dist = vm_vec_dist_quick(last_star_pos, p2), capped at Star_max_length with ratio, times Star_amount; p1 = p2 + (last - p2) * ratio; coded and projected too.
4. last_star_pos = View_matrix of the previous loop pass * pos.
5. Colour index: DIM: (fl2i(max(255 - dist*Star_dim, Star_cap)) * (i & 7)) / 256, else i & 7; white, alpha (index+1)*24, alpha blend.
6. sx = (W + x*W/z)*0.5, sy = (H - y*H/z)*0.5, endpoints truncated with `(int)`; axis-aligned lines are pixel rows/columns inclusive, others a 1-pixel strip.

## Built (steward D-20261106-123810, -123811, -123812)
- Billboard kind 6 (`FS2_SINK_BB_STARS`), fields appended to `fs2_sink_billboard` only. One call per star pass, one instance per star (unit vector and index in the instance data); view, previous view, Star_amount/dim/cap/max_length and flags are material uniforms. The shader repeats steps 1-6 in the C operation order, including the truncation, the same-pixel fudge (clamped at the right canvas edge on horizontal lines) and the pixel strip; emitted coordinates are the Godot ones minus 0.5 because the `org` uniform adds the half pixel.
- `StarField.cpp`: the GPU route is tried first. `Star_prev_view`, `Star_last_n`, `Star_gpu_stale` keep last_star_pos right: a CPU pass after GPU passes first runs `star_sync_last_pos()`. Eligible only with `gpu_n > 0`, `Star_flags & (TAIL|DIM)`, `gr_screen.hardware` and (first pass, or the star count equals the previous pass). Other flags or a software screen stay on the CPU loop in StarField.cpp (not counted).
- Host declines, counted (`render3d: stars: N offered, N drawn on the GPU, N declined`, must be 0 in `render3d_fallback_check.ps1`): user clip plane, HUD scale != 1, clip rectangle other than the canvas, sink refusal. A skipped frame returns 1 uncounted.
- Off: `FS2_STARS_GPU=0` (also off with `FS2_BB_OFF`); the frame runner sets it to 0 for picture compares. The CPU loop stays for it.

## Exactness (measured)
GPU vs CPU route, `frame_compare` thresholds unchanged, diff images written (`replays/stars_r/diff`): 15 frames, 6 identical, 8 within with max channel delta 1 on 2-56 pixels (alpha-blend rounding), 1 within (AutotestDamage 4500): 3 differing pixels, one at delta 22 (pixel 39,688, a star end or a tail pixel flipped by a float edge).
