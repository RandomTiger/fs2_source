# Fix review: windowed page flip spins forever when the surface is lost

**Recommendation: TAKE.** Low risk.

Found during the campaign autotest: the game froze at full CPU in the middle of a mission (sm2-08 recording, Release; sm1-05 Debug replay; an earlier SM1-04 replay). A stack of the stuck game showed the main thread in `gr_d3d_flip`.

## The bug
In a window, `gr_d3d_flip` copies the back buffer to the screen and waits for the copy:
```cpp
lpFrontBuffer->Blt(&rect, lpBackBuffer, &view_rect, 0, NULL );
while(lpFrontBuffer->GetBltStatus(DDGBS_ISBLTDONE) != DD_OK){}
```
`GetBltStatus` returns `DD_OK` when the copy is done and `DDERR_WASSTILLDRAWING` while it is still going. If the surface has been lost (another program took the display, the screen was locked, the display mode changed), the copy fails and `GetBltStatus` returns `DDERR_SURFACELOST` every time, so the loop never ends.

The fullscreen path a few lines below already restores lost surfaces before flipping; the windowed path does not.

## Proposed fix
- Wait only while `GetBltStatus` says `DDERR_WASSTILLDRAWING`.
- Before the blit, restore the front and back buffers if they are lost, as the fullscreen path does.
- After a restore, flush the texture cache. The textures in video memory were lost with the surfaces; without the flush the game carries on but draws the HUD and menus with garbage textures. The fullscreen path gets this from the flush on reactivation; in a window the surface can be lost without the game losing focus.

## Effect on mission flow
None. The flip only puts the frame on screen. With the fix, a frame drawn while the surface is lost is not shown and the game carries on; before, the game hung.
