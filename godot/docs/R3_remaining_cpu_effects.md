# Render3D: effect classes still on the CPU projection path (2026-11-05)

Read from code, not measured. Already on the GPU: model meshes, thruster mesh, nebula background mesh, `g3_draw_bitmap` (sun, glows, particles, fireballs), lasers, nebula fog.
"Gate" for every row is `frame_compare`; traces cannot move unless a draw return value feeds logic (`g3_draw_laser` visibility drives the flyby sound, `Weapons.cpp:1523`, `GameView.cpp:240`).

| # | Class | Where | Size | Risk |
|---|---|---|---|---|
| 1 | Beams (DONE 2026-11-05, 6f94544; off: `FS2_BEAM_GPU=0`) | `Weapon/Beam.cpp:1352` | M | done |
| 2 | Trails (DONE 2026-11-06, a956a75, `FS2_SINK_BB_QUAD4` + `FS2_SINK_BB_TRI3`; off: `FS2_TRAILS_GPU=0`) | `Weapon/Trails.cpp` | M | done |
| 3 | Shockwaves, rotated puffs | `g3_draw_rotated_bitmap` (`3ddraw.cpp:708`); DONE 2026-11-05: `FS2_SINK_BB_ROTATED` on by default (off: `FS2_BB_ROT=0`, used by the frame runner for pixel compares) | S-M | done |
| 4 | Shield hits (DONE 2026-11-06, `FS2_SINK_BB_TRI3` orient bit 4, two tris per quad; off: `FS2_SHIELD_GPU=0`, set by the frame runner; parity proven on shield-hit frames of AutotestDamage/AutotestWeapons at detail 4 and, with the test-only `FS2_SHIELD_DETAIL=1`, the low-detail bitmap branch; the TRI3 shader had never compiled until `INSTANCE_COLOR` became `COLOR`, which also fixes trail tail triangles) | `Ship/Shield.cpp:477-540`, gouraud tinted quads | M | ship-relative transform |
| 5 | Lightning (DONE 2026-11-05, 5f157ea, `FS2_SINK_BB_QUAD4`; off: `FS2_LIGHTNING_GPU=0`) | `Nebula/NebLightning.cpp` | S | done |
| 6 | Warp-in cone, subspace tunnel (DONE 2026-11-06, `FS2_SINK_BB_TRI3` orient bits 8 warp / 16 tunnel, tiled+affine sink bits; off: `FS2_WARP_GPU=0`, set by the frame runner; tunnel parity on VisualSubspace 300/1500/3000 at 0.01% over, PSNR 48-51 dB; warp flash on AutotestArrivals 560/600 within; tunnel uses unclamped 1/z) | `Fireball/WarpInEffect.cpp:154`; `Interp_subspace` skipped by mesh path | M | animated uv offset |
| 7 | Starfield bitmaps/backgrounds (DONE 2026-11-06, `FS2_SINK_BB_TRI3` orient bit 5, uncut triangles clipped by the sink at 0.1, alpha blend allowed for xparent bitmaps; off: `FS2_STARFIELD_GPU=0`, set by the frame runner; parity cpu vs gpu on all 7 frame cases, max delta 1, 0.00% over, VisualSpace 99.99% identical; D-20261106-062111: the GR_HOST CPU route now takes the #152 no-screen-clip route like D3D, so the off baseline equals the GPU result) | `3ddraw.cpp:1633`, `StarField.cpp:816-823`, no depth | M | aspect-dependent scale |
| 8 | Stars | `StarField.cpp:1033-1181`, 2D lines/pixels | M | high: 1-px exactness |
| 9 | Model-path leftovers | forced/tiled textures, breakup, fogged thrusters, saved lighting | L total | nebula fog colour |
| 10 | Lines, spheres, HUD markers | `g3_draw_line/sphere`, `HUDbrackets.cpp` | S | low value, leave |

Order proposed: 3, 1, 5, 2, 4, 6, 7, 9, 8; 10 stays.
