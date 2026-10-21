# Fix review: D3D bitmap_ex reads from the wrong source address when sx or sy is above zero

**Recommendation: TAKE.** `gr_d3d_bitmap_ex_internal` computes its source pointer in pixels but adds it to a byte address, so any draw that starts inside the bitmap (sx or sy above zero) reads the wrong pixels. Drawing side only, no gameplay state. Found by the FS1 barracks pilot picture on FS1Demo (a85511f). Taken by the steward (D-20261021-100829).

## The bug
`gr_d3d_bitmap_ex_internal` (`code/Graphics/GrD3DRender.cpp`) locks the bitmap at 16 bits and sets

```
sptr = (ushort *)( bmp->data + (sy*bmp->w + sx) );
```

`bmp->data` is a byte address (`ptr_u`). `sy*bmp->w + sx` is a pixel count, and each pixel is two bytes, so the offset is half what it should be: the draw starts at the wrong row and column, and an odd offset is not even pixel-aligned. The destination pointer a few lines above has the `*2`: `(x+gr_screen.offset_x)*2`.

## Developers' intent
The Glide version of the same function computes the source with the bytes-per-pixel factor. D3D's was copied without it. The intent is a draw of the source rectangle starting at `(sx, sy)`.

## Proposed fix
`(sy*bmp->w + sx)` becomes `(sy*bmp->w + sx)*2`, with a `// Claude:` comment. Nothing else changes.

## Who reaches it
Two callers on this branch: `gr_d3d_bitmap_ex` (via `gr_bitmap_ex`) and `gr_d3d_bitmap` (via `gr_bitmap`, when `Gr_bitmap_poly` is 0).

- Reference configuration (FS2 retail, 1024, 32 bit): `Gr_bitmap_poly` is set to `D3D_32bit` in `GrD3D.cpp`, so `gr_bitmap` draws textured polys and never reaches this function. No game code calls `gr_bitmap_ex` under D3D (its only caller outside `2d.cpp` is the OpenGL renderer). So no screen in the reference configuration changes; no before/after capture is needed (D-20261021-100829 condition 3).
- 16 bit D3D: `gr_d3d_bitmap` passes a non-zero `sx`/`sy` only when the bitmap is clipped on the left or top. Those draws were reading shifted pixels and now read the right ones.

## What was checked
- Glide's source pointer (`GrGlide.cpp`): `bmp->data + (sy*bmp->w*2) + (sx*2)`.
- The same change on FS1Demo (a85511f), found through the FS1 barracks pilot picture, which is drawn a row at a time with `gr_bitmap_ex`. FS1 demo autotest r/d clean.
- Cherry-picked onto FS1Compat now as the identical hunk (D-20261021-100829), so the merge chain is a no-op there.
- Recordings: drawing side only, no gameplay state, so no trace can move (D-079, steward's call). Quick suite per D-042 at each layer the chain passes: pending; autotests are paused by Tom on 2026-10-21. A trace move anywhere stops the chain.

## Not changed
- The function still writes 16 bit pixels into the back buffer; it is wrong on a 32 bit buffer. The reference configuration never reaches it (above).

## Effect on mission flow
None. Drawing only.
