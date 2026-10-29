# Godot performance profile (performance pass, step 1)

Measurement only. No fix is in this step. 2026-10-28, branch GodotPerf, machine DESKTOP-CLAUDE_Claude (display 60 Hz, NVIDIA, Godot 4.7.2, Release game DLL, wrapper template_debug flavour built Release).

## 1. How it was measured

- Timing code lives only in the Godot wrapper (`godot/src/perf_prof.*`, hooks in `render_sink_godot.cpp`, `fs2_node.cpp`, `runner.gd`). It is off unless `FS2_PERF_DIR` is set (a Zone is then one branch), so gate runs are unchanged. gr_host and game logic never read the clock.
- Source: recorded replays (`recordings\...`) run through the Godot host at the live 120 Hz game pace (`FS2_PACE=1`, shown window, no `-trace`, at most 4 game frames per Godot call, only the last game frame of a call is drawn). Every run ended `REPLAY OK`.
- Per game frame: total, sink zones (exclusive time in each sink callback and its inner parts), counters. Game-side = total - sink. Per Godot frame: interval, step (game + sink), RenderingServer draw calls / primitives, screen viewport CPU and GPU render time.
- Spike = game frame at or above 8.0 ms (the 120 Hz period is 8.33 ms). Body = game frames from #300 on; frames before are the mission load.
- Stack sampler (sampled runs only, 1 kHz requested, real period 1.8 ms): the game thread is suspended, its stack top copied, resumed; symbols from the game PDB. The Godot exe has no symbols, so Godot internals are only attributed by module. Sampling perturbs timing, so sampled runs give shares, not times.
- Noise: md-01 clean x3: drawn-frame mean 5.87 / 5.69 / 5.78 ms, spikes 12.2 / 10.9 / 11.6 %. Treat differences under about 4 % as noise. Another worker (GodotPad / Hook) was running games on the machine during the batch.
- Tooling: `tools\perf_profile.ps1` (driver), `tools\perf_profile.py` (summary). Raw CSVs are in `C:\Games\FS2Perf_out` (not in git).

## 2. Cases (clean timing, 120 Hz pace)

Times are per DRAWN game frame (the frame that reaches Godot); skipped-draw frames cost about 1.0 ms each (game logic only). Draw calls = game `gf_*` polygon calls; batches = batcher output; RS = RenderingServer calls per Godot frame.

| case | game frames (drawn) | drawn total | game-side | sink | gf draws | batches | tris/batch | RS draws | spikes >= 8 ms | Godot fps |
|---|---|---|---|---|---|---|---|---|---|---|
| FS1 md-01 (live feel case) | 3483 (1719) | 5.87 ms | 1.49 | 4.38 | 1438 | 196 | 11.8 | 194 | 12.2 % | 62.8 |
| FS1 sm1-06a (light) | 3547 (1768) | 3.32 | 1.14 | 2.18 | 744 | 99 | 11.5 | 101 | 0.65 % | 62.8 |
| FS1 sm2-04a (dense battle, FS1) | 3487 (1681) | 8.32 | 1.81 | 6.51 | 1674 | 292 | 9.0 | 286 | 25.4 % | 62.7 |
| FS1 md-10 | 3501 (1746) | 6.09 | 1.49 | 4.60 | 1236 | 213 | 9.5 | 218 | 17.1 % | 62.8 |
| FS2 sm3-06 (dense battle, FS2, no fog) | 3353 (1667) | 7.20 | 1.48 | 5.72 | 1056 | 269 | 6.4 | 271 | 16.7 % | 62.8 |
| FS2 sm3-10 | 3514 (1681) | 5.70 | 1.43 | 4.28 | 954 | 198 | 7.9 | 195 | 10.9 % | 62.8 |
| FS2 sm3-09 | 3343 (1647) | 6.16 | 1.56 | 4.61 | 1193 | 215 | 8.8 | 216 | 11.3 % | 62.8 |
| FS2 sm1-02 | 3549 (1768) | 4.05 | 1.20 | 2.84 | 865 | 130 | 11.2 | 136 | 2.1 % | 62.9 |
| FS2 sm3-04 (nebula, fog missing) | 3304 (1553) | 14.92 | 5.19 | 9.73 | 439 | 93 | 9.1 | 95 | 46.8 % | 59.3 |
| FS2 sm2-09 (nebula) | 3311 (1521) | 15.69 | 5.59 | 10.10 | 538 | 94 | 10.6 | 96 | 46.0 % | 57.5 |
| FS2 sm2-07 (nebula, many ships) | 3299 (1182) | 20.53 | 7.45 | 13.07 | 904 | 146 | 10.9 | 145 | 35.0 % | 41.7 |
| FS1 md-01, packaged FS2Godot.exe | 3483 (1727) | 5.47 | 1.45 | 4.02 | 1438 | 196 | 11.8 | 196 | 9.4 % | 62.7 |

- Nebula cases are sm3-04, sm2-09, sm2-07: the sampler shows `neb2_pre_render` in them and not in sm3-06 or md-01. sm3-06 is called "nebula" in the L1 set, but its profile has no `neb2` calls and no framebuffer read (0 reads, 0.5 texture creates per drawn frame), so it behaves as a dense non-fog mission and is used as such here.
- The packaged exe (template_release wrapper, stock export template) matches the editor-launched run within noise (5.47 vs 5.69-5.87 ms drawn). Packaging is not a cost.
- Screen viewport render time (the part Godot reports): md-01 CPU 0.49 ms, GPU 0.06 ms; sm2-04a 0.55 / 0.07; sm3-06 0.60 / 0.21; sm3-04 0.56 / 0.08. The depth-layer sub-viewport is not in these figures. The GPU is not the limit; the cost is CPU time making RenderingServer calls.
- Godot runs at the 60 Hz display (about 2 game frames per call); 6-8 % of Godot frames are over 1.5x the median interval in battle missions (2.4 % in sm3-04). sm2-07 falls to 41.7 fps and 2.83 game frames per call: it cannot hold 120 Hz.

## 3. Where the time goes (normal missions)

md-01, mean per drawn frame (us, exclusive): frame_end 1208 (frees last frame's instances), layer_add 1265 (one RenderingServer instance per depth-layer batch), draw_tris 817 (batcher.add for 1438 draws), mesh_build 596 (canvas perspective meshes), emit_canvas 325, run_end 138, tex_create 15. Sum 4.38 ms = 75 % of the drawn frame. Sink cost is about 3.0 us per game draw call, or about 22 us per batch.

Sampler shares of in-step samples (md-01 / sm3-06): 73 % / 74 % on a sink path (callbacks and what they call), 27 % / 26 % game logic and other. By leaf module in md-01: game DLL 34 %, Godot exe 28 %, ntdll 24 %, wrapper 9 %, NVIDIA driver 3 %.

Spike structure (md-01): all 388 spikes are drawn frames. They are scene weight: 2260 draws and 351 batches against 1168 and 145 on normal frames; sink 7.9 ms (layer_add 3.0, frame_end 2.4) against game-side 2.3 ms. In sm2-04a, 25 % of frames are spikes for the same reason (1674 draws). Spikes come in runs, 2-4 game frames apart (every drawn frame of a heavy scene). Texture uploads on spike frames are small (31 us in md-01).

One-off hitches (all cases, every mission):

| frame | cost | where |
|---|---|---|
| #0-#2 | 28 / 565-1040 / 57-112 ms | game-side mission load (bitmap decode `bm_set_components_argb_d3d_32_tex`, pcx read, file reads); not on the 120 Hz path |
| #7 (first drawn frame) | 49-77 ms | sink: emit_canvas 27-46 ms, layer_add 9-19 ms, run_end 9 ms, 44-54 texture creates (1.9 MB in md-01) |
| later, 1-2 per mission (md-01 #94, sm3-04 #65, sm1-02 #83, sm3-10 #1073) | 29-35 ms | sink: layer_add 15-18 ms or emit_canvas 10-18 ms on the first use of something new |
| sm3-10 #781 | 219 ms | sink frame_end 207 ms (561 meshes that frame); cause not found (that run was not sampled) |
| last frame (sm3-04 #3302, sm1-02 #3547, md-10 #3499) | 32-51 ms | game-side mission end |

Reading: the first-use hitches are consistent with Godot building shader or pipeline variants the first time a canvas mesh or layer mesh with a new material/state is drawn. That is an inference from where the time lands; Godot's own frames have no symbols here.

## 4. Nebula missions (fog is missing, but the fog work is done every frame)

- `neb2_pre_render` -> `neb2_render_setup` is 56 % (sm3-04) and 43 % (sm2-07) of the game-step time. Inside it `h_get_region` is 51 % / 39 %.
- Sink: `read_region` 6.5 ms (sm3-04) and 7.4 ms (sm2-07) per drawn frame for a 4096-byte read, one per frame. This is a GPU round trip (the sink must render and wait), not copy time. Fog is not applied yet, so the read buys nothing today.
- Textures: 48 (sm3-04) and 80 (sm2-07) texture creates and the same number of frees per drawn frame, 1.4 and 2.1 MB per drawn frame, 22-28 per game frame averaged over all frames (660-743 KB). tex_create 1.0 ms (sm3-04), 1.8 ms (sm2-07). `tex_get` is 26 % / 34 % of the step and `to_rgba8` + `expand_channel` 17-24 % of the game step (the bitmap -> RGBA8 conversion in gr_host). Every drawn frame in these missions is a spike.
- Together read_region + tex_create + conversion are about 9 ms of a 15 ms frame in sm3-04.

## 5. Hypotheses

1. Ships are drawn in tiny batches: CONFIRMED. 1438 game draws of 3.6 vertices on average become 196 batches of 11.8 triangles and about 190 RenderingServer draws per frame (107 depth-layer mesh instances, 76 canvas meshes). The cost scales with batch count (RS calls), not triangles: GPU time is 0.06 ms.
2. 3D projection in software: REFUTED as a main cost. On the sampler, stacks with `g3_*` transform or draw code outside the sink are 4.3 % (md-01), 2.4 % (sm3-06), 1 % (nebula) of the step, about 0.1-0.25 ms per drawn frame. Model traversal and vector math as the leaf function is 10-13 % in battle missions (0.4-0.7 ms per drawn frame); that is game logic the retail game does too.
3. Per-frame texture lock and upload: REFUTED for normal missions (0.2 creates per drawn frame, 15 us, 9.5 % of frames have an upload, 7.7 KB on normal frames and 60 KB on spike frames), CONFIRMED in the three nebula missions (section 4).
4. Pixel reads from textures: REFUTED for normal missions (0 reads, counted per call). CONFIRMED in nebula missions: one framebuffer read per drawn frame costs 6.5-7.4 ms (section 4). The game reads no texture pixels.

## 6. Ranked candidate fixes

Group 1 = Enhanced. Group 2 = Godot now, ahead of a possible 3D refactor. Group 3 = needs the 3D refactor. Gains are expected savings per drawn frame in the case named, from the zone numbers above (not tested).

| # | candidate | group | expected gain |
|---|---|---|---|
| 1 | Nebula missions: stop the per-frame framebuffer read stalling the frame. While fog is not applied, skip `read_region`; once fog lands (R2), read asynchronously with one frame of latency, or compute the average colour on the GPU, instead of a synchronous read. The result only drives fog colour, not game state | 2 (Godot sink). The synchronous-read semantics are the game's own, so an Enhanced alternative (derive the fog colour without a read) is possible but is not needed to get the gain | 6.5 ms (sm3-04) to 7.4 ms (sm2-07) per drawn frame; sm3-04 15 -> 8.5 ms, sm2-07 20.5 -> 13 ms |
| 2 | Nebula missions: stop creating and freeing 48-80 textures every frame. Find why the neb2 bitmaps get new sink ids each frame (tex_free count equals tex_create count), cache the converted RGBA by bitmap id (gr_host `tex_get`) and reuse the RenderingServer texture with in-place update in the sink | 1 for the gr_host cache (the conversion `to_rgba8` is game-side), 2 for sink texture reuse | 1.0-1.8 ms tex_create plus about 1.5-2.5 ms conversion in the game step (to_rgba8 17-24 % of the step): about 3-4 ms in sm3-04 / sm2-07. Cause of the per-frame churn is not yet established |
| 3 | Pool the per-batch RenderingServer objects: reuse mesh and instance RIDs across frames and update them, instead of creating and freeing them every frame (frame_end frees 1.2 ms, layer_add creates 1.3 ms, mesh_build 0.6 ms) | 2 | 1.5-2.5 ms of 4.4 ms sink in md-01 (30-40 % of sink); about 3 ms in sm2-04a. Biggest gain for normal missions |
| 4 | Pre-warm shader / pipeline variants during load (draw one dummy canvas mesh and one depth-layer mesh per material/state behind the loading screen) | 2 | removes the 49-77 ms first-drawn-frame hitch and 1-2 mid-mission hitches of 29-35 ms per mission; no change to mean |
| 5 | Find the 207 ms frame_end hitch (sm3-10 #781, 561 meshes) with a sampled rerun, then fix (likely a bulk free, so folds into #3) | 2 | removes a 219 ms single-frame stall; rare (1 in 3500 frames) |
| 6 | Cut the per-call cost of `batcher.add` (817 us for 1438 draws, 0.57 us each): avoid vector growth and per-vertex copies, keep the texture-state compare cheap | 2 | up to about 0.3 ms per drawn frame in md-01; low |
| 7 | Fewer RenderingServer objects by merging batches that share texture and state across non-adjacent draws (retail's own state changes make 1438 draws into 196 batches; going well below needs depth-sorted opaque/blended passes or atlas-based material batching) | 3: reordering changes retail's draw order and so the look; only a real depth-tested 3D pipeline can reorder safely, and group 1 or 2 cannot take it without a fidelity change. Not proposed for now | would cut sink by the batch count (about 2x fewer RS calls), 1.5-2.5 ms |
| 8 | Game-side mission load (frames #0-#2, 0.5-1.0 s, bitmap decode and file reads) and mission-end frame (32-51 ms) | 1 | none in the 120 Hz play path; only shortens load. Not recommended unless load time matters |

Order of value for normal missions: #3 first (30-40 % of sink), then #4 (hitches), #6. For nebula missions: #1 and #2 together are about 10 ms of 15 ms.

## 8. Results (performance group 2)

Each fix is its own commit on GodotPerf with a tag `GodotPerf-<fix>-<date>-L1green`. For every one: L1 green, and the gate C shot hashes (tools/perf_shots.ps1, a shown replay saved every 300 frames) identical to the run before the fix. Debug wrapper, Release game DLL, 120 Hz pace.

| # | Fix | Commit | Switch | Before | After |
|---|---|---|---|---|---|
| 1 | Skip the nebula fog-colour read (32x32 at 0,0 only; larger reads such as Autotest screenshots still served) | e959b7c | `kSkipNebFogRead` | sm2-07 read_region 9.5 ms per drawn frame | 5 us. R2 must set the switch false or make the read asynchronous |
| 2 | Texture pool (aged-out textures reused with texture_2d_update) | 03528f2 | `kTexturePool` | sm2-07 tex_create 31.5 us per call, 2.4 ms per drawn frame | 12.9 us, 1.0 ms |
| 3 | Reuse layer mesh instances | d638f17 | `kInstancePool` | sm3-06 frame_end 1.62 ms per drawn frame, layer_add 15.0 us per call | 1.21 ms, 14.5 us. mesh_build unchanged: meshes were already pooled |
| 4 | Pre-warm canvas materials and the seven depth-layer shaders the missions use | 3a39a4f | `kPrewarm` | first-use hitches of 36 ms (md-01 #45, sm3-06 #182) | none. Cost: Godot frame 1 (load) 85-105 ms -> about 190 ms |
| 5 | Bulk append in `batcher.add` | reverted, not landed | | standalone sink_check: 44.5 -> 24 ns per triangle (affine), 60.5 -> 28.7 (perspective) | in the game draw_tris is 3.17 us per call before and after (3.168-3.190, run to run 0.01): below noise, so reverted. The per-call cost is not the batcher's vertex copy |
| 6 | Sampled rerun of the sm3-10 219 ms frame_end hitch | measurement only | | | Not reproduced in two runs on the fixed build (one sampled at 1000 Hz, one clean): worst frame_end 3.8 ms. Cause unexplained; fix 3 may have removed it, not shown |

Remaining hitches:
- sm3-10, game frame 782 (first frame with 563 layer meshes): 30 ms, layer_add 15.4 ms against 6.7 ms on the frames around it. Looks like first use of a shader or material not in the pre-warm list; not confirmed.
- md-01, Godot frame 76: 31 ms, of which the sink is 13 ms. It is not shader first use (pre-warm did not move it); the rest is game side or Godot.
- Shader keys outside the seven pre-warmed ones still build on first use.
- Mission load and last-frame hitches (section 3) are untouched.

## 7. Not measured / limits

- Godot-internal time (shader compile, RenderingServer work) is attributed by module only; the Godot exe has no symbols.
- The "about 1 % of game frames at 8-9 ms" figure is not reproduced: at this pace 11-12 % of md-01 body frames are at or above 8 ms; they are scene weight, not random spikes.
- GPU time of the depth-layer sub-viewport is not counted.
- Debug (`d`) game DLL and sampler passes on sm3-10 / sm1-02 were not run.
