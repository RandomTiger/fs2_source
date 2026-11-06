# Render3D: effect classes still on the CPU projection path (2026-11-05)

Read from code, not measured. Already on the GPU: model meshes, thruster mesh, nebula background mesh, `g3_draw_bitmap` (sun, glows, particles, fireballs), lasers, nebula fog.
"Gate" for every row is `frame_compare`; traces cannot move unless a draw return value feeds logic (`g3_draw_laser` visibility drives the flyby sound, `Weapons.cpp:1523`, `GameView.cpp:240`).

| # | Class | Where | Size | Risk |
|---|---|---|---|---|
| 1 | Beams (DONE 2026-11-05, 6f94544; off: `FS2_BEAM_GPU=0`) | `Weapon/Beam.cpp:1352` | M | done |
| 2 | Trails (DONE 2026-11-06, a956a75, `FS2_SINK_BB_QUAD4` + `FS2_SINK_BB_TRI3`; off: `FS2_TRAILS_GPU=0`) | `Weapon/Trails.cpp` | M | done |
| 3 | Shockwaves, rotated puffs | `g3_draw_rotated_bitmap` (`3ddraw.cpp:708`); DONE 2026-11-05: `FS2_SINK_BB_ROTATED` on by default (off: `FS2_BB_ROT=0`, used by the frame runner for pixel compares) | S-M | done |
| 4 | Shield hits (DONE 2026-11-06, `FS2_SINK_BB_TRI3` orient bit 4, two tris per quad; off: `FS2_SHIELD_GPU=0`, set by the frame runner) | `Ship/Shield.cpp:477-540`, gouraud tinted quads | M | ship-relative transform |
| 5 | Lightning (DONE 2026-11-05, 5f157ea, `FS2_SINK_BB_QUAD4`; off: `FS2_LIGHTNING_GPU=0`) | `Nebula/NebLightning.cpp` | S | done |
| 6 | Warp-in cone, subspace tunnel | `Fireball/WarpInEffect.cpp:154`; `Interp_subspace` skipped by mesh path | M | animated uv offset |
| 7 | Starfield bitmaps/backgrounds | `3ddraw.cpp:1633`, `StarField.cpp:816-823`, no depth | M | aspect-dependent scale |
| 8 | Stars | `StarField.cpp:1033-1181`, 2D lines/pixels | M | high: 1-px exactness |
| 9 | Model-path leftovers | forced/tiled textures, breakup, fogged thrusters, saved lighting | L total | nebula fog colour |
| 10 | Lines, spheres, HUD markers | `g3_draw_line/sphere`, `HUDbrackets.cpp` | S | low value, leave |

Order proposed: 3, 1, 5, 2, 4, 6, 7, 9, 8; 10 stays.
