# P1: camera and depth in the sink (design, GodotRender3D)

Order: Tom, D-20261031-111802; scope: full P1 first, D-20261031-120701. Exit: gate C frames unchanged. No mesh yet.

## What exists (read from code, not measured)

**Game projection** (`code/Render/3dSetup.cpp`, `3dMath.cpp`):
- `g3_set_view_matrix` folds the aspect and zoom scale (`Matrix_scale`) into the rows of `View_matrix`, so a rotated vertex `(x, y, z)` is already in "scaled eye space".
- `g3_project_vertex`: `w = 1/z`, `sx = (W + x*W*w)/2`, `sy = (H - y*H*w)/2`, and `sw = min(w, 1)`. So the NDC is `x/z`, `y/z`, a plain pinhole. `sw` is clamped to 1, so for z < 1 game unit `1/sw` is no longer the eye depth.
- The frustum test is `|x| <= z`, `|y| <= z`, `z >= MIN_Z`, plus an optional user clip plane (`G3_user_clip`). The game clips polygons on the CPU before it projects them.

**Host vertex** (`fs2game/gr_host.cpp` `tmapper_internal`, 886-888): `sw` and `z = vertex.z / 30000` (linear eye depth, capped 0.98).

**Sink depth** (`godot/src/render_sink_godot.cpp` `add_to_layer`, 1469-1560): the Godot side is already a tiny 3D scene. The camera looks down -z, a vertex sits at `(sx - W/2, H/2 - sy, -(origin - d))`, and `d = 2 + (MAX_EPOCH - epoch)*1100 + z*1000 - zbias*0.05`. It is not a perspective camera. The sink draws a flat pre-projected picture whose depth is a synthetic linear value. Transparent instances are ordered back to front by `origin`, which carries the call order.

## What a mesh needs that the sink does not have

1. The camera: view position and matrix (scaled), canvas size, zoom. The sink only ever sees screen pixels.
2. Depth values on one scale with the legacy draws. Legacy depth is `z_eye/30000` interpolated linearly in screen space between vertices (D3D `sz` behaviour). A hardware perspective mesh interpolates differently across a triangle. The mesh shader must write the same function: pass `z_eye` as a noperspective varying and output the legacy scale, or the two worlds disagree at intersections (the insignia and briefing bugs came from this).
3. The same epoch and zbias ordering for mesh draws.
4. The user clip plane, and canvas clipping, which the game does on the CPU today and a mesh would do on the GPU.

## P1 deliverables

1. **New sink call (render_sink.h v3):** `set_view(const fs2_sink_view *)` with the scaled view matrix (3x3), view position, canvas w and h, zoom, `Matrix_scale`, and the user clip plane. Sent from a hook in `g3_set_view_matrix` (via a `gr_screen` function pointer, null in non-Godot builds, so FRED and the other layers are unaffected). Sent again on `g3_start_instance_*` only if a mesh needs the instance stack; not in P1.
2. **Depth contract in the sink:** one function `legacy_depth(z_eye, epoch, zbias)` used by `add_to_layer` and, later, by the mesh path. Today's inline expression is moved into it with no change of result.
3. **Reconstruction check (sink_check):** from `(sx, sy, sw, z)` and the camera, rebuild the eye vertex and compare with the game's rotated vertex over a fixed set. Documents where it fails: z < 1 (sw clamp) and z above 29400 (z cap). A mesh must reproduce those same clamps, or it will not match the CPU path there.
4. **Mesh depth test harness:** a selftest scene with a quad drawn the legacy way and a quad drawn through a dummy mesh path crossing it, checking the crossing line matches. This is the proof that the two worlds share one depth buffer. Dummy path only, no POF. **Status 2026-10-31: deferred to the first P3 commit.** The dummy mesh path needs the mesh shader (noperspective eye z, sw clamp, z cap), which is P3 work; building it twice is waste. L2 Full for P1 then runs once, after the harness passes, together with the first mesh commit.
5. **Gate C unchanged:** the call is informational in P1; no draw changes.

## Code changes

- `fs2game/render_sink.h`: v3 struct and call (version bumped, size check kept).
- `fs2game/gr_host.cpp`: forward the view.
- `code/Render/3dSetup.cpp`: one guarded call at the end of `g3_set_view_matrix`. This is game code, so it needs replay verification (trace must not change).
- `godot/render_sink/fake_sink.h`, `sink_check.cpp`, `selftest_scenes.h`: record and check the view and the depth contract.
- `godot/src/render_sink_godot.*`: store the view; move the depth expression into `legacy_depth`.

## Testing

- Rule 4: this touches the renderer, so **L2 Full** applies once at the end of P1, not per commit. Per commit: L0 Build and the sink_check/selftest.
- Gates A and B must stay identical (trace unchanged by construction). Gate C hashes must stay identical (0de77d4 baseline).

## Open for later phases (not P1)

- Instance transforms (`g3_start_instance_angles`) for submodels: P3.
- Per-vertex lighting in a shader: P2/P3.
- Per-submodel state the GPU needs each frame (rotation angles, blown_off, LOD, thruster scale): P3, see the ship survey.
