# Fix review: the start-up texture-origin test picks by window mode, not by what the card needs

GitHub issue #274 (companion of #77, review 60).

**Recommendation: FIX by always using 1 and no longer reading `D3DTextureOrigin` (Tom, D-20261107-210210; first landed as a default for an unset value, D-20261107-104004).** `d3d_detect_texture_origin_32/16` decide `D3d_rendition_uvs`, the half-texel offset on every textured draw. The test reads the front buffer. Windowed, that is the desktop, so the result depends on desktop pixels. Full-screen, it reads the game's own frame, but on this machine the test pixel lands in the same place under both settings, so the test cannot tell them apart and falls through to 0. Measured here: windowed picks 1, full-screen picks 0. 1 is the value the code's own mapping needs (see Fix). The default first landed on VS2022 (94ea4fa). It moves here because it changes original behaviour; VS2022 reverted it (b1d1c91). The steward ruled that this branch takes it (D-20261107-104004).

## The bug
`code/Graphics/GrD3D.cpp`, `gr_d3d_init`, as released:

```
	uint tmp = os_config_read_uint( NULL, "D3DTextureOrigin", 0xFFFF );

	if ( tmp != 0xFFFF )	{
		if ( tmp )	{
			D3d_rendition_uvs = 1;
		} else {
			D3d_rendition_uvs = 0;
		}
	} else {
		if(D3D_32bit){
			d3d_detect_texture_origin_32();
		} else {
			d3d_detect_texture_origin_16();
		}
	}
```

`d3d_detect_texture_origin_32` makes a 32x32 alpha bitmap with one lit texel at (15,15). It runs two trials, with `D3d_rendition_uvs` at 0 and then 1. Each trial draws the bitmap section starting at that texel to screen (0,0) with `gr_aabitmap_ex(0, 0, 32, 32, 15, 15)`, flips, and reads screen pixels (0,0) and (1,1) through `d3d_get_pixel`. The decision only looks at (1,1):

```
	if ( (pix1b!=0) || (pix2b!=0)  )	{
		D3d_rendition_uvs = 1;
	} else {
		D3d_rendition_uvs = 0;
	}
```

`d3d_get_pixel` locks `lpFrontBuffer`. Windowed (`D3D_window`), that is the desktop primary surface, not the game's frame (the same read as review 52 and review 60). So windowed, (1,1) is a desktop pixel, and any non-black desktop gives 1. Full-screen, the read is the game's frame, but the test only gives 1 if the texel bleeds into (1,1) in one of the trials. On a card that samples exactly it never does, so the test gives 0.

## What D3d_rendition_uvs governs
It is a draw-only switch. Nothing in game logic reads it, and it touches no `rand()` call.

- `GrD3DRender.cpp:830`, `d3d_tmap_internal`: on textured polygons, sets `minu/minv/maxu/maxv` two texels in from each edge of the bitmap.
- `GrD3DRender.cpp:916`, the same function, per vertex: tiled textures (ships) use `u*u_scale` either way. Bitmap sections add half a texel (`0.5f / sw`, `0.5f / sh`). Everything else is clamped to the range set at line 830.
- `GrD3DRender.cpp:1484`, `d3d_aabitmap_ex_internal` (fonts, cursor, interface buttons and ani frames): adds `+0.5f` texel to `u0/v0/u1/v1`.
- `MainHallMenu.cpp:1257`: Debug only (`#ifndef NDEBUG`), prints "D3D rendition" in the main hall.

## Fix
`gr_d3d_init`: the registry read and the test are replaced by `D3d_rendition_uvs = 1;`, with a `// Claude:` comment. The detect functions no longer run, and `D3DTextureOrigin` is no longer read, so a value left in the registry (0 from an old run of the test, for example) cannot bring back the wrong mapping. The detect functions stay in place, unused, to keep the diff small.

History: the first version of this fix (D-20261107-104004) only changed the default for an unset value to 1 and still honoured a value set in the registry, the same pattern as `D3DLineOffset` (review 60). Tom then ruled that the value is always 1 and the registry is ignored (D-20261107-210210). This change stays on this branch and climbs only with the next ordinary merge of it.

Why 1 is the intended value, from the code. The game draws 2D bitmaps as screen quads at whole-pixel positions (`x1 = i2fl(x+gr_screen.offset_x)`, just below line 1484). In Direct3D 7, a pixel's centre is at its whole-number screen position, and texel *n* covers *u* from *n/w* to *(n+1)/w*, with its centre at *(n+0.5)/w*. So without an offset, pixel *n* samples the texel corner *n/w*, the edge between texels *n-1* and *n*, and which texel it gets is down to rounding. The `+0.5f` in the rendition path moves the sample to the texel centre, so texel *n* lands on pixel *n* exactly. That is what the authors wrote the switch for. The test was meant to find cards where that offset was needed. It cannot tell, either windowed (it reads the desktop) or full-screen on a card that samples exactly.

## What was checked
Build: a temporary Release exe from this branch with two `// DIAG` lines, built into a scratch folder and run in `C:\dev\GameCopy\FS2_work` under its own name. The DIAG lines:
- forced the test to run even with a registry value set (`tmp = 0xFFFF`);
- appended `D3D_window`, the four pixel values and the result to a log file.

The machine runs DDrawCompat, in 32-bit, with no pilot files, so every run starts on the pilot screen. HKLM (WOW6432Node) `Software\Volition\Freespace2` holds `D3DTextureOrigin = 1`. That value cannot be removed without admin rights, so the unset case cannot be run on this machine. The forced test stands in for it.

| Run | Mode | (0,0) / (1,1), trial 0 | (0,0) / (1,1), trial 1 | Result |
|---|---|---|---|---|
| 1, 2 | windowed | e3e3e3 / ffffff | e3e3e3 / ffffff | 1 (desktop pixel at (1,1)) |
| 3, 4 | full-screen | eeeeee / 0 | eeeeee / 0 | 0 (fallback) |

- **Windowed.** The read returns the same desktop values in both trials, the ones review 60 saw for the line test. The result is 1 by accident.
- **Full-screen.** The read sees the game's frame. The lit texel lands on (0,0) in both trials, and (1,1) stays dark in both, so the test falls through to 0.
- So the original game uses different texture mapping windowed and full-screen on the same machine.

Screenshots: the game's own Print Screen (back buffer, 1024x768) on the pilot screen, from the VS2022 Release exe (the same code as this branch apart from the default), each run started fresh:
- `uv0k.png`: HKCU `D3DTextureOrigin = 0`;
- `uv1k.png`: HKCU `D3DTextureOrigin = 1`;
- `uvnone.png`: no HKCU value, so the HKLM 1 applies;
- `uvk_diff01.png`: the 0 / 1 difference, scaled x8;
- `uvk_diff1n.png`: the 1 / no-value difference;
- `uvbtn_0_vs_1.png`: the Single and Multi buttons at 4x, 0 on the left and 1 on the right.

What the screenshots show:
- **1 and no value** are identical, pixel for pixel. The registry value is read, and HKLM's 1 applies when HKCU has none.
- **0 and 1** differ in 1,584 pixels, all on bitmaps drawn over the background: the highlighted Single button, the mouse cursor, one glyph, and the list arrow. The full-screen background does not change. Matching the button region pixel by pixel, the image at 0 is the image at 1 moved one pixel right and one down. That is the ani frame offset the first VS2022 change reported.
- Which of the two looks right cannot be seen at this size. The case for 1 is the mapping argument under Fix.

Captures are kept outside the repo, in `C:\fs2t\review67\` on DESKTOP-CLAUDE: the PNGs, the raw TGAs and `startup_test.txt`, the DIAG log.

Registry: HKCU held no `D3DTextureOrigin` before the runs. Each run set and then removed it, and it is absent again. HKLM was not touched. The DIAG lines were removed (`git checkout`), and the scratch exe was deleted.

Code: `gr_d3d_init` on this branch now matches the previous tip (bc6a833) apart from the comment. This branch already defaulted to 1 through its VS2022 merge, so the merge of the revert and this re-land cancel out.

## Not changed
- `D3DLineOffset` (review 60) still honours a registry value. Only `D3DTextureOrigin` is ignored.
- The detect functions and `d3d_get_pixel` stay as they are. Correcting the read to the back buffer would not help full-screen, where the test still cannot tell the settings apart.
- In the rendition path of `d3d_tmap_internal`, the "all else" branch clamps *u,v* without multiplying by `u_scale`. That is original code, and it is outside this fix.
- The 16-bit detect path has the same read and the same decision. It was not run, because this machine runs 32-bit.

## Effect on mission flow
None. `D3d_rendition_uvs` is decided once in `gr_d3d_init`, before any mission or recording starts. It only moves texture coordinates by half a texel, and nothing in game state or `rand()` reads it. No recording can move: on this branch the code is unchanged from the previous tip, which already defaulted to 1, and this machine has HKLM `D3DTextureOrigin = 1` set as well. Ignoring the registry (D-20261107-210210) changes the picture only on a machine whose registry holds 0, and even there only 2D bitmaps move by one pixel. Its checks (D-20261107-210211): Release and Debug build with no new warnings, and one windowed Print Screen at the pilot screen compared with `uv1k.png`. Result: both builds clean. Two shots differed from `uv1k.png` only at the mouse cursor (23x24 pixels at its old spot, 512..534 x 384..407, and its new one, 320..342 x 240..263) and, in one shot, the blinking list arrow (5x7 at 187..191 x 186..192); with those masked, 0 pixels differ, and 0 in the Single/Multi buttons, where setting 0 shows first (D-20261107-212145). No gates: nothing in game state reads the value. Not tested: a machine with the value at 0 or unset, because this machine's HKLM holds 1 and that is a system setting.
