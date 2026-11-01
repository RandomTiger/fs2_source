# Fix review: the start-up line-offset test reads the desktop instead of the game's test pattern when windowed

**Recommendation: FIX by defaulting `D3DLineOffset` to 0 when it is unset (steward D-20261023-235337, option 2; see Fix).** This review first recommended keeping the original. `d3d_get_pixel` reads the front buffer, which in windowed mode is the desktop primary surface, so `d3d_detect_line_offset_32/16` measure the desktop and not the white test pixel. The read is wrong, but it does not change the result. In every configuration measured (windowed at two window positions over two desktop backgrounds, full-screen, and windowed with the read corrected to the back buffer) the test ends at `D3D_line_offset = 0.0`. The corrected read and full-screen also reach 0.0, through the test's own fallback. A fix would change no pixel on this machine. A cheaper option, if the steward wants the wrong read gone: default `D3DLineOffset` to 0, as `D3DTextureOrigin` already defaults to 1 (94ea4fa). That skips the desktop read and the four start-up flips, with the same result. Issue #77. Review with captures ordered by D-20261020-070918, manager order step 2 D-20261023-202820. The fix-or-keep decision is the steward's.

## The bug
`code/Graphics/GrD3D.cpp`, `d3d_get_pixel`:

```
	ddrval = lpFrontBuffer->Lock( NULL, &ddsd, DDLOCK_WAIT, NULL );
	...
	ubyte *dptr = (ubyte *)((uint)ddsd.lpSurface + y*ddsd.lPitch + (x * gr_screen.bytes_per_pixel));
	memcpy(pixel, dptr, gr_screen.bytes_per_pixel);
	lpFrontBuffer->Unlock( NULL );
```

`gr_d3d_init`, run only when `D3DLineOffset` is not in the registry:

```
	tmp = os_config_read_uint( NULL, "D3DLineOffset", 0xFFFF );
	if ( tmp != 0xFFFF )	{ ... } else {
		if(D3D_32bit){ d3d_detect_line_offset_32(); } else { d3d_detect_line_offset_16(); }
	}
```

Each detect function runs two trials, with `D3D_line_offset` at 0.0 and then 0.5. Each trial does `gr_clear`, `gr_line(0,0,0,0)` in white, `gr_flip`, then `d3d_get_pixel` at (0,0) and (1,1). The decision is:

```
	if ( (pix1a!=0) && (pix2a==0)  )	{ D3D_line_offset = 0.0f; }
	else if ( (pix1a==0) && (pix2a!=0)  )	{ D3D_line_offset = 0.5f; }
	else { D3D_line_offset = 0.0f; }
```

Full-screen, the front buffer is the game's flipped surface. Windowed (`D3D_window`), `lpFrontBuffer` is the desktop primary with a clipper, and `gr_flip` blits the back buffer into the window. So (0,0) and (1,1) are desktop pixels, wherever the window is. This is the same front-buffer read that #76 fixed in `gr_d3d_save_screen` (review 52, `D3D_window ? lpBackBuffer : lpFrontBuffer`).

## What D3D_line_offset governs
`D3D_line_offset` (`GrD3DRender.cpp`) is used only in `d3d_make_rect`. That function adds it to the `sx`/`sy` of both vertices of every 2D line, pixel and gradient: `gr_d3d_line`, `gr_d3d_gradient`, `gr_d3d_pixel` (through `gr_line`) and `gr_d3d_aaline`. That covers HUD brackets and target box lines, star streaks and star pixels, `g3_draw_line`, briefing icon lines, and the line-drawn parts of the menus. It shifts those lines by half a pixel or not at all. It is a float added to vertex positions after `INT_CLIPLINE` has clipped in integers. Nothing reads it back, and it does not touch control flow or `rand()`.

## Proposed fix (if taken)
In `d3d_get_pixel`, lock `D3D_window ? lpBackBuffer : lpFrontBuffer`, as #76 did, with a `// Claude:` comment naming #77. Windowed, the test then reads the game's own frame and not the desktop.

## Fix
The steward chose option 2 (D-20261023-235337): in `gr_d3d_init`, `os_config_read_uint( NULL, "D3DLineOffset", 0 )` replaces the `0xFFFF` default, so an unset value gives `D3D_line_offset = 0.0` and `d3d_detect_line_offset_32/16` no longer run at start-up. A set value is still honoured exactly as before. Keeping the original was ruled out because, windowed, the test measures whatever the desktop shows, which is not deterministic in principle. Option 2 removes that read and the start-up mode flips with the least code, gives the value every measured case gave, and follows the `D3DTextureOrigin` precedent (94ea4fa). Correcting the read to the back buffer (the proposed fix above) was more code for the same result and keeps the flips. The detect functions are left in place, unused, to keep the diff small.

## What was checked
Build: a temporary Release exe with `// DIAG` logging, built from a scratch detached worktree of RecordPlayback bf4386c. Its graphics files are identical to OSBF cb67ed1, and OSBF has no autotest to take in-game captures. The exe ran under its own name beside the gate exes. The DIAG code:
- logged which surface was read, the lock result and the four pixel values;
- moved the window to a given position before the test;
- had an `I77_FIX` switch that reads the back buffer when windowed;
- had an `I77_FORCE` switch that overrides the measured offset.

Each run did the autotest menu tour plus `sm1-01.fs2` and saved the game's back-buffer shots. The desktop at (0,0) was covered by a magenta or a black window. The machine runs DDrawCompat, set to borderless full-screen. All runs were 32-bit, every lock returned DD_OK, and the registry value was unset (`0xFFFF`) for every run.

| Run | Mode, window position | Desktop at (0,0) | Surface read | (0,0) / (1,1), trial 0.0 and 0.5 | Offset |
|---|---|---|---|---|---|
| W1_pos0_magenta | windowed, 0,0 | magenta | front | e3e3e3 / ffffff, both trials | 0.0 (fallback) |
| W2_pos0_black | windowed, 0,0 | black | front | e3e3e3 / ffffff, both trials | 0.0 (fallback) |
| W3_pos300_magenta | windowed, 300,200 | magenta | front | e3e3e3 / ffffff, both trials | 0.0 (fallback) |
| W4_pos300_black | windowed, 300,200 | black | front | e3e3e3 / ffffff, both trials | 0.0 (fallback) |
| F1_fullscreen | full-screen | n/a | front | 0 / 0, both trials | 0.0 (fallback) |
| F2_fullscreen_black | full-screen | black | front | 0 / 0, both trials | 0.0 (fallback) |
| A1-A4 (after: back-buffer read) | windowed, as W1-W4 | as W1-W4 | back | 0 / 0, both trials | 0.0 (fallback) |
| X1_force05_pos300_magenta | windowed, 300,200 | magenta | front | e3e3e3 / ffffff, both trials | 0.0 measured, forced to 0.5 for comparison |

What the table shows:
- **Windowed, as shipped.** The read sees neither the test pixel nor the visible desktop. Under DDrawCompat the primary surface returned the same e3e3e3 / ffffff in all four position and background combinations, while the screen corner was magenta (`W3_pos300_magenta_corner_during.png`). Both trials read the same nonzero value, so the decision falls through to 0.0.
- **Full-screen and the corrected windowed read.** They see the game's frame, but the white pixel is not lit at either offset. `gr_line(0,0,0,0)` becomes a zero-length quad of half a pixel, which current rasterization does not cover. Both trials read 0, and the decision falls through to 0.0.
- **When the bug could matter.** The shipped windowed read can give 0.5 only if the desktop pixel at (0,0) reads 0 in the first trial and non-zero in the second, a few milliseconds later. That was not seen. If it did happen, the only effect would be lines drawn half a pixel off for that session.

Captures, kept outside the repo:
- the per-run `.diag.log` files;
- `<run>_corner_before.png` and `<run>_corner_during.png`, the desktop corner;
- the tour BMPs for each run;
- `W3_pos300_magenta_hud_in_flight.png` / `_hud_zoom.png` (0.0, as shipped);
- `X1_force05_pos300_magenta_hud_in_flight.png` / `_hud_zoom.png` (forced 0.5);
- `A3_fix_pos300_magenta_hud_in_flight.png` / `_hud_zoom.png` (after the fix);
- `F1_fullscreen_hud_in_flight.png` / `_hud_zoom.png`.

The zooms (bottom-left of the in-flight HUD, 2x) show star streaks and the offscreen indicator. The shots are taken at different moments, so the stars differ. At 0.5 the HUD and streak lines look the same at this scale: a half-pixel shift of one-pixel lines is not visible as a change in look.

Registry: `D3DLineOffset` was absent before the runs, in HKCU `Software\Volition\FreeSpace2`, HKLM (WOW6432Node) `Freespace` and `Freespace2`, and their VirtualStore copies. The game never writes it. Nothing was set, so nothing needed restoring, and it is still absent. The DIAG worktree and exe were removed afterwards, and the gate exes were never touched (md5 checked).

## Not changed
- The test itself stays in place. Its zero-length test line no longer lights a pixel on current drivers, so it always reaches its 0.0 fallback. Replacing it is out of scope.
- `gr_d3d_get_pixel` (the `gr_get_pixel` hook) is an empty stub and is not involved.
- The 16-bit detect path has the same read and the same fallback. It was not run, because this machine runs 32-bit.

## Effect on mission flow
None. The offset is decided once in `gr_d3d_init`, before any mission or recording starts. It only moves 2D line vertices by 0 or 0.5 pixels, with no effect on game state or `rand()`. None of the 48 recordings or the 3 demo recordings can move with or without the fix: the fix changes only which surface the start-up test reads, and on this machine the result is 0.0 either way.
