# Fix review: background nebulas and planets jump as they cross a screen edge in Direct3D

**Recommendation: TAKE (asked for by Tom, who confirmed the fix by playing it).** Background nebulas and planets take a small jump now and then as the view pitches and yaws, then carry on moving correctly. Turning off the clip against the screen edges for these bitmaps stops the jump. The original game has the same jump.

## The bug
Background nebulas and planets are starfield bitmaps: a fixed mesh on a sphere centred on the eye, drawn triangle by triangle by `g3_draw_perspective_bitmap` in `code/Render/3ddraw.cpp`:

```
	g3_draw_poly(3, verts, tmap_flags);
```

`g3_draw_poly` clips each triangle against the screen edges when any of its points is off screen, so the clip starts and stops as points cross an edge. Hardware can trim a triangle to the screen by itself, so Direct3D does not need that clip.

**The mechanism is not explained.** Tom's test shows the clip causes the jump: with the clip off the jump goes, and the only change in that build was the clip. A model of the clip written during the investigation moved the image by under 0.1 pixel, far too little to see, so the model is missing something. This review does not claim why the clip moves the image.

## Fix
`code/Render/3ddraw.cpp`: a new `perspective_bitmap_draw_tri`, called by `g3_draw_perspective_bitmap` in place of its two `g3_draw_poly` calls.

- If `gr_screen.mode` is not `GR_DIRECT3D`, it calls `g3_draw_poly` as before. The software and Glide renderers are not known to cope with points far off screen.
- In Direct3D it cuts the triangle only at a near plane (`z = 10`), because a point behind the eye cannot be projected, then projects the points and calls `gr_tmapper` directly.

## What was checked
- Tom played a native Direct3D test build with the screen clip off and the near-plane cut kept: the jump is gone.
- Builds: Release and Debug, no new warnings.
- Gate on the merge into RecordPlayback, Release and Debug, in Direct3D: `VisualSpace` under autotest for 60 s exits clean with no assert, with the nebulas and planets drawn; the `sm1-01` replay is `REPLAY OK`, TraceDiff 0, and the two builds give identical traces.
- No test forces a triangle across the near plane, so the turning view in `sm1-01` probably runs that cut but does not prove it.

## Not changed
- Godot is not fixed. It draws through its own renderer mode, not `GR_DIRECT3D`, so it keeps the clip and still jumps. Tom has seen the jump there. Issue 152 tracks it.
- Suns (`g3_draw_bitmap`) and everything else that uses `g3_draw_poly` keep the clip.
- The software and Glide paths are unchanged.

## Effect on mission flow
None: only the drawing of background bitmaps changes, and nothing in game logic reads it. Re-recorded missions: none.
