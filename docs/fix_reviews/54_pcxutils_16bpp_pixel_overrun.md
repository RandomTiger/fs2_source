# Fix review: PCX 16bpp reader overruns its pixel temp on a 32 bit screen

**Recommendation: TAKE.** Stack overrun in `pcx_read_bitmap_16bpp` whenever D3D runs with a 32 bit back buffer: the pixel setter writes 4 bytes into a 2 byte local. Debug builds stop with a run-time check failure (seen entering the FS1 barracks on FS1Demo, 2026-10-21); Release silently writes past the variable. Drawing side only, no gameplay state. Taken by the steward (D-20261021-100829).

## The bug
`pcx_read_bitmap_16bpp` (`code/PcxUtils/pcxutils.cpp`) builds each pixel in a local

```
ushort bit_16;
...
bit_16 = 0;
...
bm_set_components((ubyte*)&bit_16, &r, &g, &b, &al);
```

`bm_set_components` is a function pointer. When D3D's screen pixel format is in use and `D3D_32bit` is set, `BmpMan.cpp` points it at `bm_set_components_argb_d3d_32_screen`, which writes through `*((uint*)pixel)`: four bytes into the two byte `bit_16`. The two bytes after it on the stack are overwritten.

`D3D_32bit` is reachable in the original code: `gr_init` (`2d.cpp`) sets `Cmdline_force_32bit` when the requested depth is 32, which the launcher's registry setting gives.

## Developers' intent
`BmpMan.cpp` has a 32 bit screen setter that writes a `uint`, so the developers meant 32 bit pixels to pass through this path. The local that receives them was left at the 16 bit size. The fix makes the local wide enough for the widest setter.

## Proposed fix
`ushort bit_16;` becomes `uint bit_16;`, with a `// Claude:` comment. Every setter ORs into the value after `bit_16 = 0`, so the 16 bit setters leave the upper half zero and `*((ushort*)pixdata) = bit_16` stores the same 16 bits as before. Nothing else changes.

## What was checked
- The same change on FS1Demo (2bb57a4): the Debug run-time check failure entering the FS1 barracks is gone; FS1 demo autotest r/d clean.
- The hunk is identical to FS1Demo's, so the merge chain up to FS1Compat is a no-op there (cherry-picked onto FS1Compat now, D-20261021-100829).
- Recordings: drawing side only, no gameplay state, so no trace can move (D-079, steward's call). Quick suite per D-042 at each layer the chain passes: pending; autotests are paused by Tom on 2026-10-21. A trace move anywhere stops the chain.

## Not changed
- `*((ushort*)pixdata) = bit_16` still stores 16 bits, so a 32 bit screen still gets the low half of the 32 bit value in this path. That is a colour fault, not an overrun; the 32 bit rendering work on Enhanced handles it.
- `pcx_read_bitmap_16bpp_nondark` in the same file has the same `ushort bit_16` and the same setter call. Not in this ruling; listed for a follow-up.

## Effect on mission flow
None. Bitmap loading only.
