# Fix review: two-texel clamp on 3D textures and the one-texel upload shift

Issue #134. **Recommendation: FIX (Tom, 2026-11-10).** Two Direct3D renderer flaws move every game texture a little:

- the "rendition uvs" switch clamps the texture coordinates of untiled 3D draws two texels inside each edge;
- the texture upload resamples normal textures with a step of (w-1)/w, so texel i holds source texel i-1.

Both are removed. The half-texel origin correction the same switch applies to fonts, interface bitmaps and bitmap sections stays. That correction is the UI pixel fix Tom asked for (fix review 67, `docs/TEXTURE_ORIGIN.md` effects 1 and 2). No recording moves.

## The bugs
**1. The two-texel clamp** (`code/Graphics/GrD3DRender.cpp`, `d3d_tmap_internal`). With `D3d_rendition_uvs` on, the "all else" branch caps u and v to `[2/w, 1-2/w]`:

```
				// all else.
				else {
					src_v->tu = flCAP(va->u, minu, maxu);
					src_v->tv = flCAP(va->v, minv, maxv);
				}
```

This was a workaround for the Rendition Vérité, whose bilinear filter bled neighbouring texels in at polygon edges. Since review 67 fixed the switch at 1 for the half-texel origin, the clamp runs on every machine. An untiled face mapped to its whole texture loses two texels on each side, which is about 6 % of a 32-texel glow, flame or spark. The branch also drops `u_scale`/`v_scale`, though they are 1 for normal textures.

**2. The upload shift** (`code/Graphics/GrD3DTexture.cpp`, `d3d_create_texture_sub`, default case):

```
			du = ( (bmap_w-1)*F1_0 ) / tex_w;
			dv = ( (bmap_h-1)*F1_0 ) / tex_h;
```

At full size, `f2i(i*(w-1)/w)` is i-1 for every i from 1 up. Texels 0 and 1 both hold source texel 0, every later texel holds its left (or upper) neighbour, and the last column and row are lost. Every normal texture sits one texel down-right of where it is mapped. In nebula missions this shows as the poofs sitting about 6 px off (#133).

## Fix
1. "All else" now uses `va->u*u_scale`, `va->v*v_scale`, as the switch-off path always did. The tiled and bitmap-section branches are unchanged, so the half texel stays for interface art. `d3d_aabitmap_ex_internal` (fonts, cursor, buttons) is not touched.
2. The step is `w/tex_w` (`bmap_w*F1_0/tex_w`). At full size texel i is source texel i. When the texture is smaller than the bitmap, the last index read is `f2i((tex_w-1)*w/tex_w)`, which is at most w-1, so nothing reads past the row.

Every changed line carries a `// Claude:` comment.

## What was checked
Before/after frame compare on Enhanced with the fix applied (Release, `tools/frame_compare.ps1`, all seven cases, 19 frames). Every replay said REPLAY OK, trace MATCH, on both builds. Results are in `PerfG2_out\texfix_1110` on DESKTOP-CLAUDE (not in git): 10 frames within threshold, 9 changed.
- The changes are the one-texel move of textures: planet rim, nebula backdrop, ship hulls, glows and flares, and the subspace tunnel (a large magnified texture, so the move covers many pixels).
- HUD text and gauges are pixel-identical.
- No edge bleed came back on any face, glow, flare or beam in the zoomed old/fixed/diff crops (planet edge, HUD, AutotestWeapons 1500).

## Not changed
- `D3d_rendition_uvs` stays 1. Effects 1 and 2 (the half texel on 2D bitmaps and bitmap sections) are needed, and effect 3 (tiled) does nothing.
- `D3DLineOffset` (review 60) is a separate switch.
- The Godot host and sink copy both flaws as "LEGACY PIXEL COMPATIBILITY (#133)". They follow as this fix climbs, with the Godot pixel baselines re-taken once.

## Effect on mission flow
None: nothing in game state reads texture coordinates or texels. Pictures change and recordings do not. No missions were re-recorded.
