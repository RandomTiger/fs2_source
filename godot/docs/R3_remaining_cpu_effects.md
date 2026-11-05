# Render3D: effect classes still on the CPU projection path (2026-11-05)

Read from code, not measured. Already on the GPU: model meshes, thruster mesh, nebula background mesh, `g3_draw_bitmap` (sun, glows, particles, fireballs), lasers, nebula fog.
"Gate" for every row is `frame_compare`; traces cannot move unless a draw return value feeds logic (`g3_draw_laser` visibility drives the flyby sound, `Weapons.cpp:1523`, `GameView.cpp:240`).

| # | Class | Where | Size | Risk |
|---|---|---|---|---|
| 1 | Beams | `Weapon/Beam.cpp:1352` (poly :1389), additive `TMAP_FLAG_CORRECT` quad | M | shots: perspective-correct quad |
| 2 | Trails | `Weapon/Trails.cpp:318-340`, per-vertex alpha | M | low |
| 3 | Shockwaves, rotated puffs | `g3_draw_rotated_bitmap` (`3ddraw.cpp:708`); GPU path `FS2_SINK_BB_ROTATED` exists, opt-in `FS2_BB_ROT`, off because shots differ 7.7% | S-M | shots (open blocker) |
| 4 | Shield hits | `Ship/Shield.cpp:477-540`, gouraud tinted quads | M | ship-relative transform |
| 5 | Lightning | `Nebula/NebLightning.cpp:1242-1275`, after row 1 | S | low |
| 6 | Warp-in cone, subspace tunnel | `Fireball/WarpInEffect.cpp:154`; `Interp_subspace` skipped by mesh path | M | animated uv offset |
| 7 | Starfield bitmaps/backgrounds | `3ddraw.cpp:1633`, `StarField.cpp:816-823`, no depth | M | aspect-dependent scale |
| 8 | Stars | `StarField.cpp:1033-1181`, 2D lines/pixels | M | high: 1-px exactness |
| 9 | Model-path leftovers | forced/tiled textures, breakup, fogged thrusters, saved lighting | L total | nebula fog colour |
| 10 | Lines, spheres, HUD markers | `g3_draw_line/sphere`, `HUDbrackets.cpp` | S | low value, leave |

Order proposed: 3, 1, 5, 2, 4, 6, 7, 9, 8; 10 stays.
