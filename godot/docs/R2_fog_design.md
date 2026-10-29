# R2 fog design (step 1, no code), branch GodotR2 off Godot 5fe0769, 2026-10-29

Scope: nebula (MISSION_FLAG_FULLNEB) missions look as retail: FS2 sm2-07, sm2-09, sm3-04 and the FS1 equivalents (found at implementation by the mission flag, same code path). Basis: D-20261028-135516 (fog is R2; `gr_fog_set` a no-op in the sink, sink v2 fog fields reserved until now).

## 1. How retail produces fog (D3D path; Glide is gone from this tree)

Fog exists only for models in a fullneb mission. Four pieces:

1. **Setup per object** (`ObjectSort.cpp` 308-333): for each sorted object, `neb2_get_fog_values` gives near/far from `Neb_ship_fog_vals_d3d[ship type]` (fighters/bombers by default). If they differ from `gr_screen.fog_near/far`, `gr_fog_set(GR_FOGMODE_FOG, current_fog_color, near, far)`. After the objects, and after each ship/model render (`Ship.cpp` 2588, `ModelInterp.cpp` 1972), `gr_fog_set(NONE)`. Thrusters, shockwaves, fireballs, weapons, the starfield switch it off explicitly.
2. **Per-poly colour** (`GrD3DRender.cpp` `gr_d3d_tmapper_internal` 962-980): a poly with `TMAP_FLAG_PIXEL_FOG` (set on every model poly in fullneb, `ModelInterp.cpp` 2512, 2821) averages `neb2_get_pixel(sx, sy)` over its vertices and calls `gr_fog_set(GR_FOGMODE_FOG, ra, ga, ba)` with no planes (-1, -1: planes unchanged). This turns fog back on for the poly, and the colour stays until the next `gr_fog_set` colour change. So fog colour is one flat colour per poly, taken from the nebula background behind it.
3. **Per-vertex factor** (`gr_d3d_stuff_fog_value`, D3D_fog_mode 1 = vertex fog, the default on any card with FOGVERTEX): `f = clamp((far - z) / (far - near), 0, 1)` with z = the vertex's view depth (`va->z`, world units), written to the specular alpha. D3D then outputs `rgb = f * texture*lit rgb + (1 - f) * fogcolour`, alpha untouched, per pixel interpolating f linearly. Applies to every tmapper poly drawn while `current_fog_mode != NONE`, whatever its blend mode. Table fog (D3D_fog_mode 2: ForceWFOG or no vertex-fog cap) is not reproduced; it is not the default.
4. **The background colour** (`Neb.cpp` `neb2_pre_render`): once per frame (`game_render_frame` -> `neb2_render_setup`), the game clips to 32x32 at (0,0), draws `stars_draw_background()` there with nebula mode off, `gr_flush`, `gr_get_region(0, 32, 32, tpixels)`, `g3_end_frame`, `gr_clear`. `neb2_get_pixel` then looks up `tpixels` with a 3x3-ish neighbour average, scaled from screen to 32x32. In D3D the read is the back buffer, so it holds the pre-render. `Neb2_render_mode` is NEB2_RENDER_POF on hardware (also for the dummy/host: `gr_screen.hardware`).

## 2. What the Godot host has today

- `h_fog_set` (gr_host.cpp 873) stores mode/colour/near/far in the host (`H.fog_*`), writes nothing to `gr_screen` (dummy-equal, so ObjectSort re-calls it for every object). `plain_draw` copies that state into `fs2_sink_draw.fog_mode / fog_rgb / fog_near / fog_far` on every draw. The sink ignores them.
- Gaps in the host: (a) `tmapper_internal` ignores `TMAP_FLAG_PIXEL_FOG`, so fog never turns on from the tmapper path and its colour is never set; (b) `h_fog_set` stores near/far unconditionally, so the tmapper call (-1, -1) would clobber the planes; (c) `h_fog_set` with ObjectSort's colour argument (always the unwritten `gr_screen.current_fog_color` = 0) would reset the colour to black; D3D keeps the stored colour because it compares against what it holds.
- `fs2_sink_vertex.z` is `va->z / 30000` capped at 0.98 (D3D z_mult), set for zbuffered polys. So the sink can recover world depth as `z * 30000` (fog distances beyond 29400 do not occur; the Neb table is hundreds to low thousands).
- The sink's `read_region` is wrong for this read as well as slow: it returns the *last shown frame* (`force_draw`, `get_image` of the main viewport, 6.5-7.4 ms in the nebula profile), i.e. the previous frame's top-left corner with whatever the HUD put there, not the 32x32 pre-render. perf fix 1 (`kSkipNebFogRead`) skips it, so `tpixels` stays zero/stale today; no visible loss because fog is off. In dummy mode `gr_get_region` writes nothing; `tpixels` is zero.

## 3. What the sink v2 reserved fields carry, and the Godot-side approach

The reserved per-draw fields are enough; **no struct or version change**:

| field | meaning | R2 use |
|---|---|---|
| `fog_mode` | NONE / LINEAR | LINEAR = fog on for this draw |
| `fog_rgb` | colour r g b | the poly's pixel-fog colour (host computes it, see below) |
| `fog_near/far` | planes, world units | the object's planes; the sink converts vertex z by x30000 |

### 3.1 Fog on the draw (wrapper, `godot/src` + `godot/render_sink`)
- Fogged draws are 3D model polys, which are in depth runs, so they go through the layer path (`add_to_layer`). Add to the spatial shader key a `fog` bit and to the depth batch a per-vertex `CUSTOM1` (fog colour rgb, factor f); batch key gets `fog` (on/off only, not the colour, so batches do not split per poly). The batcher computes `f` per vertex from `z * 30000` and the draw's near/far (plain C++ in `batcher.h`, covered by sink_check against a software reference), the colour is copied to all vertices of the draw.
- Shader: `ALBEDO = mix(fogc, c.rgb, f)` on the textured, lit colour, before blend; ALPHA untouched. That is D3D's vertex fog, applied to every blend mode including ADD, as D3D does. Layers are HDR 2D, so the maths runs on the game's sRGB values like the blends do.
- Draws with fog on that reach the canvas path (no zbuffer): a counter in the sink; expected zero. If non-zero, apply the same maths on the CPU at batch time.
- Unfogged draws use the existing shaders: no cost outside nebula missions, and gate C shot hashes for non-nebula missions must stay identical to the pre-R2 run (proof that nothing leaks).

### 3.2 The background colour (the part that costs)
Keep retail's exact data path: the game's own `neb2_pre_render` / `tpixels` / `neb2_get_pixel` smoothing, called from `gr_host` for fogged polys exactly as `gr_d3d_tmapper_internal` calls it. What changes is where `tpixels` comes from and when:
- **Probe**: the draws between `set_clip(0,0,<=32,<=32)` after frame start/clear and the 32x32 `read_region` are the pre-render. At `read_region(0,0,w,h<=32x32)` the sink renders those items into a dedicated 32x32 sub-viewport (reparent the pending clip item with `canvas_item_set_parent`; the clear that follows covers them in the main frame as today), and reads it back with `RenderingDevice.texture_get_data_async` (exists in the pinned godot-cpp API 4.7; main device, via `call_on_render_thread`). No `force_draw`, no wait.
- The call returns the newest finished probe (one to two Godot frames old; the background changes slowly, and the game frames at 120 Hz are 8 ms apart). The very first request of a mission is answered synchronously once. Until a result exists the call returns 0 and `tpixels` is left as it is (zero).
- `kSkipNebFogRead` is reconsidered as asked: it stays only as the off-state of a new `kNebFog` switch (false = today's behaviour, no fog, read skipped). With fog on, the synchronous read must not return; the probe replaces it. The read size/position gate (32x32 at 0,0) stays, so larger reads (Autotest screenshots) are still served synchronously.
- Fallback if async readback is not usable on the machine (driver, render thread): read the probe every Nth frame synchronously on the 32x32 viewport only (a tiny texture, no `force_draw` of the whole frame). Measured before choosing.
- Rejected: CPU rasteriser of the 32x32 probe (needs CPU copies of every texture, a second renderer to keep equal); fragment-side sampling of the probe texture (cross-viewport ordering within a frame is not guaranteed, and it replaces retail's flat per-poly colour with a per-pixel one).

## 4. Host fidelity and the change list

**Game-visible returns.** No `gf_*` return changes; `h_fog_set` still writes no `gr_screen` field, so ObjectSort behaves as with the dummy. `neb2_get_pixel` / `tpixels` are read only by the D3D renderer (grep: the only callers are `GrD3DRender.cpp` 969 and `Neb.cpp`; the tree note at `Freespace.cpp` 3463 says the same). The fog-colour read therefore does **not** feed game logic: `neb2_skip_render`, `neb2_get_fog_intensity` and the detail-level choice use eye distance and ship type only. No trace field depends on `tpixels` or on fog. Host mode stays off clock, rand and os_poll (the wrapper times nothing; the async callback only stores bytes). One thing to prove rather than assume: `neb2_get_pixel` runs `BM_SELECT_SCREEN_FORMAT()`, which sets bmpman pixel-format globals that the dummy never sets; every bmpman caller re-selects its own format, and TraceDiff 0 on the nebula recordings in host mode is the check.

**What needs a steward entry before any code** (manager brief: gr_host, sink/fs2_api contract, code/):
1. `fs2game/gr_host.cpp` (yes, required): `h_fog_set` keeps colour when the planes are -1 and the colour argument is ObjectSort's (colour stays host state, set by the tmapper), ignores planes < 0; `tmapper_internal` handles `TMAP_FLAG_PIXEL_FOG` (average `neb2_get_pixel` at the vertices, set fog on with that colour) and fills the draw's fog fields. Host-only, dummy untouched, no game-visible return changes.
2. `fs2game/render_sink.h`: comment text only (fog fields "honoured" instead of "reserved"; document `z = depth / 30000`). Struct layout, `FS2_RENDER_SINK_VERSION` and `fs2_api.h` unchanged; an older sink that ignores the fields is still valid. Steward to rule whether comment-only needs a log line.
3. `code/`: no change. `fs2_api`: no change.
4. Wrapper (`godot/src/render_sink_godot.*`, `godot/render_sink/batcher.h`, `sink_check`): all of 3.1 and 3.2. No steward entry by itself.

Because item 1 touches gr_host, I stop here as ordered and implement nothing until the steward entry exists.

## 5. Proof and cost plan (for the implementation step)

- L0: x64 Release and Debug, 0 warnings. TraceDiff 0 r/d (native referee), L1 green, then gate C (shown, host renderer) on sm2-07, sm2-09, sm3-04 and the FS1 fullneb missions: before/after own-check shots (`tools\perf_shots.ps1`), no images committed, none sent to Tom. Non-nebula shot hashes identical before/after.
- Perf, sm2-07 with `perf_profile.ps1`: baseline after the perf fixes (read_region 5 us, drawn-frame total) against R2. Budget: R2 adds at most 0.5 ms per drawn frame (probe viewport, layer attribute, shader) and the per-frame stall of 6.5-7.4 ms does not return; the three nebula missions are already over budget at 120 Hz for other reasons (texture churn, group 2 fix 2), so R2 is judged on its delta, not the absolute figure.
- Switches: `kNebFog` (false = pre-R2), kept for A/B in the gate C hashes.
- Risks: (a) async readback latency/availability (fallback above); (b) probe reparenting touches the clip-item order in `s_clear`/`open_clip_item`, which the Autotest screenshot path also uses; (c) fog on ADD-blend polys follows D3D and may look hotter than expected, check against the glow draws in sm2-07; (d) FS1 data: `Nebula_fs1_ramp` only affects the NEBULA-flag colour ramp, the fog path is shared, to be confirmed in gate C.

## 6. Steward rulings and the background buffer (added at implementation)

Rulings D-20261029-010643/-010644/-010645: gr_host fix allowed (Q1), `render_sink.h` comment-only needs a commit line (Q2), `kNebFog` off/same/lag with a 0.5 ms bar for the same-frame read (Q3).

**Who reads the background buffer.** Searched over `code/` (`neb2_get_pixel`, `tpixels`):
- `code/Graphics/GrD3DRender.cpp` ~969, `gr_d3d_tmapper_internal`, PIXEL_FOG block: the only caller of `neb2_get_pixel`. Result goes only to `gr_fog_set` (renderer state).
- `code/Nebula/Neb.cpp`: `tpixels` is written by `neb2_pre_render` (~1190, `gr_get_region`) and read only by `neb2_get_pixel` (~1272) and the `NEB2_THUMBNAIL` debug bitmap (not defined). No game logic, AI, HUD or trace field reads it.
- `Freespace2/Freespace.cpp` ~3463: comment only.
So the buffer feeds the renderer only. With the dummy `gr_get_region` leaves `tpixels` zero; gr_host now fills it with real pixels through `h_get_region` (once the sink returns them; with `kNebFog` off the sink still returns 0 and the buffer stays zero as with the dummy). Nothing game-visible changes; TraceDiff stays the check.

## 7. Interim state and measurements (D-20261029-052041)

**State.** `kNebFog` is `kNebFogOff` by default: nebula missions draw as before R2 (no fog), and are reported "fog-missing, kNebFog off (D-20261029-052041)". `kNebFogSame` and `kNebFogLag` stay as switch states for measurement and are never a default without a ruling. Base: GodotR2 = Godot 5fe0769 + the gr_host commits (b0329a8, afad074) + the wrapper fog commit. R2 is not claimed done. Every trace-compared and gate run uses off.

**Measurements, sm2-07** (autotest recording replayed 30 s of wall time in a shown window at 120 Hz game pace, `perf_profile.ps1`, post-perf baseline of 5fe0769):

| state | read_region / fog read per drawn frame | note |
|---|---|---|
| baseline (off, 5fe0769) | ~5 us | the read is skipped |
| same (same-frame probe) | ~4.3-4.5 ms | over the +0.5 ms bar of D-20261029-010645 |
| lag L=1 | ~3.5 ms | over the bar; cost is the synchronous `texture_2d_get` stall, capture 68 us, no waits (fog_wait 0) |
| async readback | result arrives ~3 frames late | over the L <= 2 limit |

Same and lag miss the +0.5 ms bar; async misses the lag limit; so none ships (D-20261029-010645 e).

**Open, pending Tom (escalated, no work before his ruling):** 2. a host-side CPU rasteriser for the 32x32 buffer; 3. relax the bar (fog on at ~3.5-4.5 ms on nebula missions); 4. allow lag up to 3 frames with async readback. GodotR2 is parked after the merge into Godot and is reopened only by Tom's ruling.

**Call-site analysis (D-20261029-010643 b).** Searched over `code/` for `neb2_get_pixel` and `tpixels`:
- `neb2_get_pixel` has one caller, the PIXEL_FOG block of `gr_d3d_tmapper_internal` (`GrD3DRender.cpp` ~969), whose result goes only to `gr_fog_set` (renderer state). In the host, the same call is made only from `tmapper_internal` in `gr_host.cpp` (draw path).
- The 32x32 buffer `tpixels` is written by `neb2_pre_render` (`Neb.cpp` ~1190, via `gr_get_region`) and read only by `neb2_get_pixel` (~1272) and the undefined `NEB2_THUMBNAIL` debug bitmap. No game logic, AI, HUD or trace field reads it.
- Does `gr_host` fill the buffer where dummy left it? Yes in principle: `h_get_region` writes the sink's pixels into it where dummy writes nothing. With `kNebFog` off the sink answers the 32x32 read with "no data" (returns 0), so the buffer stays zero exactly as with dummy. With same or lag it holds the real pre-render; the only reader is `neb2_get_pixel`, i.e. the renderer, and TraceDiff 0 on nebula recordings in host mode is the proof that nothing else reads it.
- gr_host does not read clock, os_poll, host_hooks, rand or cosmetic_rand for fog; `neb2_get_pixel` is called only from the draw path; no `gf_*` return and no `gr_screen.fog_*` changes.

**Off leak found and fixed (proof run).** The first gate C run with `kNebFog` off moved on sm2-01, sm2-07, sm2-09, sm3-04 (sm2-01 draws fogged polys too). Cause: the sink still honoured the per-draw fog fields the host sets for PIXEL_FOG polys, so with no fog colour (buffer zero) models were mixed towards black. Fix: with `kNebFogOff`, `s_draw_tris` clears `fog_mode` before the batcher. After it, gate C hashes equal 5fe0769 on sm1-01, sm2-01, sm2-07 (twice), sm2-09, sm3-04 and FS1 sm1-01, sm1-08a, sm2-07a (FS1 has no fullneb missions, only old-style `+Nebula:` backdrops). Runs are deterministic (tip twice on sm2-01 equal).
Off cost on sm2-07 (same 30 s replay): read_region 4.8 us per read (1461 reads) against 4.9 us baseline; fog zones 0 calls. TraceDiff MATCH (native and recorded) r and d in host mode (-NoDraw and -Shown) on sm2-07, sm2-09, sm3-04, sm2-01; L0 x64 Release and Debug 0 warnings; L1 green.

## 8. Option 5, kNebFog gpu prototype (result: condition b missed, not merged)

State `kNebFogGpu` (fourth state, default stays off): the pre-render goes into a 32x32 GPU slot texture; a per-draw table (xs, ys per vertex, the `ys>=31 -> ys--` quirk and clamp done in `batcher.h`) is read by the fog vertex shader, which does the `neb2_get_pixel` 3x3 box average (integer truncation, edge counts, bottom bound ys<30) and the per-polygon integer average (GrD3DRender.cpp 962-980), no CPU readback. The host still calls `neb2_get_pixel`; every value callers branch on is unchanged.

**(b) Result: not identical.** Gate C on sm2-07, gpu vs same, same frames: 53 of 59 shots differ, R channel only, max channel diff 60 (e.g. shot_001 (656,238): gpu 115,137,64 vs same 74,137,64). G and B are equal on the differing pixels.

Cause, checked offline: a log of every fogged poly (vertex ints, colour the host gave) against the slot dump of the read before it, recomputed with the Neb.cpp algorithm, matches in G and B for 99.2% of 84121 polys (the rest are read-pairing at frame edges of the log), and the R the host gives is always 0. In host mode `D3D_32bit` is 0 (GrD3D.cpp:248, set only by D3D init), so `bm_get_components` takes the 16-bit path and reads only the low 16 bits of the 32-bit `tpixels` pixel, which loses R (bits 16-23). Same mode inherits that; gpu computes the retail algorithm on a 32-bit buffer and so has R. Matching same exactly would need either gpu to copy the R=0 bug or `D3D_32bit` set by the host (fs2game/ or code/ change, out of scope here). Steward brief need: decide which is the reference (retail x86 D3D has R).

(c) cost and (e) off/gpu repeat gates were not run: (b) already decides the merge. Off is unchanged by this work (`kNebFog` = off; builds clean, 0 warnings).
