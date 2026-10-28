# S-1: mission geometry, option 2 vs option 3 (2026-10-28, 07:52)

Frame: sm2-03 replay, frame 2464, 1086 draw ops, 1456 triangles, 725 z-full / 48 z-read / 302 UI ops, 70 textures, one depth epoch (one zbuffer_clear). Replayed windowed 1024x768 from a private copy (C:\Games\S1_scratch). Reference = CPU software rasteriser of the same ops (no ref images exist). Images in godot/docs/s1: ref, canvas, opt2, opt3, diff_*.

## Recommendation: option 3
| | mean abs err vs ref (/255) | GPU ms | submit ms (Debug build, not meaningful) |
|---|---|---|---|
| canvas only (no depth, baseline) | 16.8 | 0.11 | 48 |
| option 2, all through 3D | 57.0 | 0.37 | 58 |
| option 3, hybrid | 12.7 | 0.27 | 13 |

- Option 2 loses the gamma-space look of everything that is not geometry: the nebula and HUD go through Godot's linear HDR 3D blend (99.8% of pixels off by >8). Option 3 keeps them on canvas and only the 256 depth-tested batches go to 3D; ship shape and overlap match ref (canvas-only gets overlaps wrong).
- Seam in this frame: 4 HUD corner quads (ops 734-737) are 2D inside the 3D range, drawn in the front canvas. No target box or warp flash was in the frame, so those two seams are untested. A warp flash is z-tested geometry (goes to 3D); the target box is UI (front canvas); both appear to need no special handling.
- Open, same in canvas/opt3 so not discriminating: HUD arcs (e.g. op 897) come out additive/white where ref is yellow, and the hull lacks ref's warm overlay (hull ~(54,92,172) vs ref (138,114,133)). Cause not found. Godot sorting_offset sign made no difference in this frame (err 12.718 vs 12.715).
- Costs of option 3: 1 sub-viewport with own World3D, premul composite quad, CPU clip of triangles to the clip rect (3D has no scissor), epoch depth banding for zbuffer_clear, per-vertex z side channel (sink has no z). Linear vs gamma blending differs from D3D for stacked transparent layers in 3D.
- Quirk: emitting a full-screen opaque untextured canvas quad blanks the frame; skipped (viewport clears black). Canvas clip needs the clip item to be the parent of drawn items.

## Seam for a later native Godot 3D world layer (not built)
Narrow world interface, object-level, placed near model_render:
- begin(camera pose, fov, viewport rect), end().
- draw_object(model id, orientation, position, per-object state: lighting, transparency, flags) instead of triangles.
- depth_clear(rect) for zbuffer_clear semantics (clear in rect, not whole buffer).
- effects (lasers, flashes, thrusters) as separate calls with z read, no write.
- Needs: model/texture residency by id, light list, transparent-object ordering decided per object by the host, colour-space policy (3D is linear), and the composite order (3D under HUD canvas, UI over it). Today's sink carries triangles with no z; either add z to fs2_sink_vertex or take this interface instead. zbuffer_get/set in gr_host currently return NONE, though real values are trace-safe.
