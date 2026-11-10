# The texture-origin switch: what `D3d_rendition_uvs = 1` really does

Status (Tom, 2026-11-08): **the value stays 1.** The two-texel clamp described under point 4 is
a suspected cause of later visual work and is to be investigated separately; nothing changes
until that investigation is done. Companion to fix review 67 (`fix_reviews/67_GrD3D_texture_origin_startup_test.md`, issue #274).

## One switch, four effects

`D3d_rendition_uvs` reads as one setting, the "texture origin", but the Direct3D renderer uses it
in four separate places. Only the first two are the half-texel origin correction its name
suggests. The other two are a workaround for one 1999 chipset that now runs on every machine.

| | Where (`code/Graphics/GrD3DRender.cpp`) | What 1 does | Verdict |
|---|---|---|---|
| 1 | `d3d_aabitmap_ex_internal`: fonts, cursor, interface buttons, ani frames | Adds half a texel to all four texture coordinates, so screen pixel *n* samples the centre of texel *n* | Needed. With 0 these bitmaps land one pixel right and one down of their background (review 67's screenshots, Tom's observation 2026-11-08) |
| 2 | `d3d_tmap_internal`, bitmap sections: interface backgrounds and other art split into tiles | Adds half a texel, as above | Needed, same reason |
| 3 | `d3d_tmap_internal`, tiled textures: ship hulls | Nothing. Both paths use `u * u_scale` | No effect |
| 4 | `d3d_tmap_internal`, "all else": untiled model faces, glows, thruster flames, beams, shockwaves, explosions, star and nebula bitmaps | Clamps each vertex's *u* and *v* to **two texels** inside the bitmap's edge (`2/w` to `1 - 2/w`) | Not an origin correction. This is the Rendition Vérité edge-bleed workaround |

A fifth use prints "D3D rendition" in the Debug main hall and does nothing else.

## Why 1 is right for points 1 and 2

Direct3D 7 puts a pixel's centre on its whole-number screen position and texel *n*'s centre at
*(n + 0.5) / w*. A 2D bitmap drawn 1:1 at a whole-pixel position therefore samples the boundary
between two texels unless half a texel is added, and which texel wins is down to rounding. In 1999
most consumer cards sampled half a texel off already, so 0 landed on texel centres by accident and
the Rendition cards, which followed the rule exactly, needed the switch. Every modern driver and
the GOG wrapper follow the rule exactly, so now every machine needs 1. The start-up test and the
launcher's registry value cannot measure this any more (review 67), which is why the value is fixed.

## Why point 4 is suspect

The two-texel clamp was written for a card whose bilinear filter pulled in neighbouring texels at
the edge of a polygon. It is not an origin correction: it is a different, larger number (two texels,
not half), and it applies to untiled 3D draws, not to 2D bitmaps. With the switch at 1 on every
machine, the clamp now runs everywhere, and:

- an untiled face mapped to a full texture loses its outer two texels on each side, so the image is
  very slightly zoomed. On a 256-texel hull detail that is under one percent; on a 32-texel glow,
  flame or spark it is six percent of the image;
- the clamp uses *u* and *v* before `u_scale` and `v_scale` are applied (noted as original code in
  review 67, "Not changed"), so on a texture padded to a power of two the clamp range and the
  coordinates are not in the same units;
- on the Godot side, the host renderer (`fs2game/gr_host.cpp`) and the Godot render sink
  (`godot/src/render_sink_godot.cpp`, two shaders marked "LEGACY PIXEL COMPATIBILITY (#133)")
  reproduce this exact clamp so that GPU output matches the Direct3D captures. The workaround for
  a 1999 chipset has therefore been carried into the port as part of the picture to match. Issue
  #134 already plans to delete it, together with the one-texel upload shift of
  `d3d_create_texture_sub`, when GodotEnhanced is made. That issue treats the clamp as a Godot
  compatibility item; this note says it is a retail flaw first, and the fix belongs on the lowest
  layer that has it.

Tom's view (2026-11-08) is that this clamp has been in effect throughout and has caused visual
issues that were then addressed by further code changes elsewhere. Candidates to check when the
investigation starts: the glyph bleed in the large HUD (#213), the bitmap-sections work
(`docs/BitmapSections_review.md` on GodotPlus), and the starfield bitmap edge handling in Render3D
row 7 (D-20261106-080353, "legacy uv clamp only at on-screen vertices"). None of these has been
traced to the clamp yet; this list is where to look, not a finding.

## The investigation, when it happens

1. **Measure.** Release build with the clamp disabled (point 4 only; points 1 and 2 keep the half
   texel), a screenshot pair and diff on a ship close-up, a thruster flame, an explosion and a
   beam, windowed at 1024x768. The question is whether the two-texel inset is visible and whether
   removing it brings back any edge bleed on modern filtering.
2. **Decide.** If the inset is visible and no bleed returns, split the switch: the half-texel
   shift stays unconditional, the clamp goes. That is a fix on `OriginalSourceBugFixes` with its
   own review and issue (a retail visual flaw that applies on all current hardware), and it moves
   no recording: nothing in game state reads the switch.
3. **Follow through.** The Godot legacy-compatibility shaders and the host renderer match retail
   D3D output, so they change with it, and the Godot pixel baselines are re-taken against the new
   captures. That is the expensive part, and the reason to decide once with evidence rather than
   piecemeal. Issue #134's clamp half is then done by this fix climbing the layers; its upload
   shift half is a separate retail flaw and gets the same treatment: review it on
   `OriginalSourceBugFixes` rather than remove it only in GodotEnhanced.

## Not in scope here
- `D3DLineOffset` (review 60) is a separate switch with its own test and is unaffected.
- The start-up detection routines stay in the source unused (review 67).
