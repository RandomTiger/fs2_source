# R3: bitmaps and laser ribbons on the GPU (design, not built)

Why: `g3_draw_bitmap` and `g3_draw_laser` project on the CPU and hand the sink screen triangles. Users: particles (bitmap and tracer), fireballs, weapon glows, beams, trails, shockwaves, warp effects. Order and acceptance: D-20261104-134130 (committed on GodotRender3D, no feature branch).

## What retail does (must be reproduced, not approximated)

`g3_draw_bitmap(pnt, orient, rad, flags)` (3ddraw.cpp:540)
- Size from the bitmap aspect: the shorter side is rad*2. w = width*Canv_w2/z*Matrix_scale.x, h = height*Canv_h2/z*Matrix_scale.y, centred on the projected point, axis aligned (not rotated).
- Whole sprite dropped when the centre is behind the eye or off the user clip (CC_BEHIND|CC_OFF_USER) or overflows.
- Quad depth z = pnt.z - rad/2 (nearer than the centre), sw = 1/z, 0 when z <= 0.
- orient bits 1 and 2 flip u and v.

`g3_draw_laser(head, hw, tail, tw, flags, max_len)` (3dLaser.cpp:139)
- Project head and tail; drop when either overflows or both share a clip code.
- Radii = width*Matrix_scale.x*Canv_w2*sw per end. 2D length capped to max_len pixels (tracers: 25), radius scaled by the same ratio.
- Half-width at least 1 px. When the 2D length is shorter than the larger radius: head-on case, length = max radius, ends widened by the shortfall, angle from atan2 (0 if dx < 0.01).
- Quad is a rotated screen rectangle (angle a), depth per end from pt1.z / pt2.z, vertex b = 191.

## Proposed sink call (v5, optional, NULL = host draws on the CPU path as now)

`billboard_draw(user, const fs2_sink_billboard *b)`: kind (BITMAP | RIBBON), texture id, blend, flags, tint, world points (centre, or head and tail), radii (rad, or head_w and tail_w), orient bits, bitmap aspect (bw, bh), max_len. The sink gets the camera from `set_view` (eye matrix, scale, canvas size, origin) and repeats the rules above per vertex in a shader. No world-space "camera facing quad" shortcut: the pixel rules (cap, 1 px minimum, head-on, centre cull) are what make retail look the same from every angle.

## Proof (per D-20261104-134130)

- Synthetic forced-path pair: a fixed set of sprites and ribbons at several camera angles (head-on, side, behind, near the screen edge, near clip, far), drawn once on the CPU path and once on the sink; shot compare under the standing numbers. The CPU branch is deleted in the commit after the pair passes.
- Counters: billboard draws on the sink, draws left on the CPU path, creation at a draw (must read 0).
- Texture ids from the level preload list; no lazy creation.
- Cost: median of 3, sink-zone rule on the sum, per-zone moves listed.

## Open

- Draw order: alpha and additive draws stay in game order; the sink must not reorder them across a billboard.
- Depth: retail writes z = pnt.z - rad/2; the shader must produce the same depth against the mesh depth run.
- Near clip is the GPU 0.1 rule (gpu-near-clip-final); the centre cull above stays CPU-equivalent in the shader.
