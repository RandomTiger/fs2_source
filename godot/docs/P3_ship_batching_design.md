# P3: ships in the fewest batches (design, GodotRender3D)

Order: Tom, D-20261031-111802. Answers: opaque draws may be reordered across textures; alpha, additive glow and decals keep submission order (D-20261031-120702). Per-class acceptance: eye-check list, pixel tolerance proposed before converting, frame-cost number (D-20261031-120703). Tom 2026-10-31: test in bulk, not per step.

## Floor: batches a ship needs
One batch per distinct texture per z-state phase (child submodels, hull, insignia decal, thrusters), plus the Shivan glow and additive thruster draws. Opaque phases may regroup across textures; alpha/additive keep order. Rotating sections are separate submodels with their own transform, so they stay separate draws in the CPU path and need only a per-submodel matrix in the mesh path.

## P3a (done, sink only): regroup opaque batches
`godot/render_sink/opaque_reorder.h`, called from `GodotSink::end_run`. Within one clear epoch, a stretch of opaque, depth-written (zmode 2) batches is regrouped by full batch key (texture, perspective, zbias, fog), groups in first-appearance order, same-key batches joined (60000-vertex cap). Alpha, additive and write-less batches stay in place and end the stretch. No game change, no trace change, no sink API change. Only equal-depth ties can change winner. Off switch: `FS2_OPAQUE_REORDER=0`. Counter: `stat_reorder_saved`.

## P3b (next): static per-submodel mesh cache
Sink v4 `mesh_create(id, local verts, per-texture index groups)` once per (model, submodel, LOD) and `mesh_draw(id, object-to-eye matrix, state)` per frame. The shader reproduces `legacy_depth` (noperspective eye z, sw clamp at z<1, z cap at 29400), so mesh and flat draws share one depth buffer. Per-frame state the host must send per submodel: local transform (instance stack incl. `$rotate`/turret angles), `blown_off`, LOD choice, thruster scale, lighting inputs, zbias/z-mode phase, and the sortnorm replacement (opaque needs none; alpha keeps CPU order).

## Order of work
1. P3a regroup (done). 2. Mesh depth harness with the dummy mesh path. 3. Fighters (no moving parts). 4. Classes with `$rotate` parts (`VisualMovingParts`). 5. Turrets, capital ships. Each step bulk-tested at the end: L0 Build, sink_check, one L2 Full, eye checks, frame-cost on a ship-heavy map.

## P3b working assumptions (reversible, inside the Tom order; change if the steward or Tom objects)
- Lighting stays on the CPU in P3b: the game's per-vertex colours are sent each frame with `mesh_draw`; GPU lighting is P7. This keeps fidelity and the gate baselines.
- Geometry is static per (model, submodel, LOD) and cached by the sink; the game sends the matrix, state and colours only.
- Everything stays behind `kRender3D` (default off) until the bulk test passes.

## Clip plane (Tom 2026-10-31: "maybe just when warping")
The mesh shader gets one eye-space clip plane (from `fs2_sink_view.user_clip/clip_normal/clip_point`) and discards fragments behind it. It is used for warp-in/out only (`Ship.cpp` ~2539). A ship in large-ship breakup (`ShipFX.cpp` ~2010/2039) is sent to the CPU path by the host, so it renders as retail. Eye checks and a pixel tolerance for the warp cut edge go in the per-class list; a warp-in/out ship joins the bulk test map.

## Background (P5)
Stars, suns, bitmaps, backgrounds and the subspace tunnel are meshes at far depth after a depth clear, ships in front; nebula fog reuses the GPU fog path. Not before the ship mesh path.

## Engine glow (Tom 2026-10-31: "if it looks the same as retail it's fine")
Retail draws thruster glow after the whole model: a camera-facing additive quad (`g3_draw_bitmap`, `ModelInterp.cpp` ~2744) at the nozzle point, one depth for the whole quad, depth read only. The mesh path keeps that: opaque batches first, then the glow quads as their own additive draws in the original order. No glow shader in P3. A glow that is both over and behind parts of the same mesh (depth fade, volume) is a P7 option, off by default.
