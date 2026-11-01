# P8: animations as texture layers (design draft, GodotRender3D)

Order: Tom 2026-11-01. "We want the particle anis to completely load into textures. Texture layers, a system that allows many games to exist in the same texture setup." Also the done-definition: no lazy loading of fixed data (memory note render3d-no-lazy-fixed-data).

Status: scope decided by Tom; no code yet.

## Goal
Every animation (.ani: particle fire and smoke, fireballs, shockwaves, explosions, muzzle flash, beam particles, shield, debris, thruster, weapon impact) is uploaded whole at load, one frame per layer of an array texture. A draw names (array, layer). No texture upload at draw time. Draws that use the same array batch together whatever frame they show.

## What happens today (read of gr_host.cpp, BmpMan.cpp)
- `bm_load_animation` returns the first of `n` consecutive bitmap handles, one per frame. Frame pixels are not read at that point; `bm_lock` reads them on first use.
- The host uploads a frame at its first draw (`tex_get` in `h_tcache_set`, only inside an accepted frame): `bm_lock`, convert to RGBA8, `texture_create`. One texture per frame, keyed (handle, signature, kind).
- So a first-time explosion costs a file read and a texture upload mid-frame, and every frame is a separate texture, so draws of different frames never batch.
- `can_drop_frames` (retail drops frames on low texture detail) changes the frame count a game sees.

## Design
1. **Sink**: new optional entries `texture_array_create(user, id, w, h, layers, format, flags, pixels)` and a draw field `layer` (float or int) in `fs2_sink_draw`/vertex. Missing entries: the host keeps today's per-frame textures (fallback, same pixels).
2. **Host (gr_host.cpp)**: a preload pass, `bm_load_animation` hook (like `Bm_release_hook`): when an animation is loaded, lock each frame, convert, and build one array of w x h x n. Map (frame handle) -> (array id, layer). `h_tcache_set` for a mapped handle returns the array and layer instead of uploading.
3. **Keying for many games**: array key = (animation file name, frame count, frame size, signature). FS1, FS2 and demo data load into the same cache. An identical animation (same key) is shared. Different games with different sizes get their own arrays; nothing assumes one data set. Bounded by a layer budget per array (GPU max layers) and arrays per size class.
4. **Frame size mismatch**: frames in one .ani share a size, so no padding is needed. Different animations stay in separate arrays (not one atlas), which keeps UVs as retail.
5. **Shader**: sampler2DArray variant of the draw shader; layer per draw (uniform) or per vertex. Filtering and alpha as the current per-frame path (bilinear, retail blend).
6. **Order and fidelity**: draw order is unchanged (alpha and additive keep submission order). Pixel result must equal the per-frame path; this is the acceptance test (pixel tolerance per D-20261031-192444).

## Where this touches other layers
- Texture loading is the Godot-layer agent's. The load pass lives in `gr_host.cpp` (PrePort64/Godot layer): lower than my branch. It goes to them as a brief; I do not edit below GodotRender3D.
- The sink entries and shader are mine (GodotRender3D).
- Original source: only the load hook in BmpMan if the existing `Bm_release_hook` pattern is not enough (minimal, byte-safe, replay-verified).

## Decisions (Tom 2026-11-01)
1. Scope: effects only (particles, fireballs, shockwaves, explosions, muzzle flash, beams, debris, shield, thrusters). UI, briefing and icon animations stay as they are.
2. Load time: mission load (animations the mission uses), via a hook after mission parse.
3. Frame dropping: `can_drop_frames` is the retail low-memory option (`bm_set_low_mem`, LowMem setting): mode 0 all frames (our reference, D-046), mode 1 every other frame at half size and half fps, mode 2 first frame only (debug). It saves RAM on small machines, not frame rate. The layers load whatever frame count BmpMan reports, so the setting keeps working and nothing changes for the game or trace. (Tom asked what it is; answer given, default kept.)
4. Memory budget: open. All frames of the mission's effect animations stay resident; size to be measured on the largest missions before the budget is fixed.

## Order of work if approved
1. Brief to Godot agent: load-side hook and preload pass.
2. Sink: array texture entries, fake sink, `sink_check`.
3. Godot sink: array textures, layer in the shader.
4. Switch-on gates and pixel numbers per D-20261031-192444; frame-cost number.
