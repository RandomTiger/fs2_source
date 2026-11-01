# Fix review: windowed, the pause screen saves the desktop instead of the game frame

**Recommendation: TAKE.** Display only: in a window (`-window`), every screen that saves the game frame behind it (the pause screen, and the other popups that call `gr_save_screen`) shows the top-left of the desktop instead. Gameplay never reads the saved picture, so no recording moves. Found by the FS1 compatibility work (2026-10-20). GitHub issue #76. Taken by the steward (D-20261020-070917).

## The bug
`gr_d3d_save_screen()` (`code/Graphics/GrD3D.cpp`) copies the last frame from `lpFrontBuffer`:

```
ddrval = lpFrontBuffer->Lock( NULL, &ddsd, DDLOCK_WAIT, NULL );
```

Full-screen, the front buffer is the game's flipping primary surface, so that is the frame just shown. Windowed, `gr_d3d_init` makes the front buffer the desktop's primary surface (with a clipper for the window) and draws into a separate off-screen back buffer; `gr_d3d_flip` blits the back buffer into the window's client area at its screen position. Locking the front buffer with no rectangle and copying `max_w` x `max_h` from its start therefore copies the desktop from (0,0), not the game. `gr_d3d_restore_screen` then draws that copy behind the pause box.

Seen: with the window at the desktop origin the paused frame is the game pushed down by the window's title bar, title bar included; with the window elsewhere it is whatever is at the desktop's top-left (magenta on the FS1 test machine's desktop).

## Developers' intent
Show the frame the player was looking at behind the pause box. Windowed mode was a developer mode in 1999 and was not checked here.

## Proposed fix
Windowed, save from the back buffer: the flip blits it to the window and leaves it as it was, so it still holds the last frame, exactly the pixels the window shows (and none of any window overlapping it). Full-screen is unchanged. The unlock uses the same surface. The 16-bit mouse-cursor patch after the copy works the same, since the cursor is drawn into the back buffer before the flip in both modes.

## What was checked
- Other front-buffer readers: `gr_d3d_print_screen` already reads the back buffer. `d3d_get_pixel` (the start-up texture-origin and line-offset tests) has the same windowed mistake: it reads desktop pixels (0,0) and (1,1). The texture origin no longer runs the test (`D3DTextureOrigin` defaults to 1, an earlier Claude change); the line-offset test still does. Not changed here: fixing it could change `D3D_line_offset` in a window, a visible change that needs its own look (see Not changed).
- Windowed pause screenshots, before and after, one-off (D-20261020-070919): sm1-01 under autotest in a Release build of `RecordPlayback` with this change applied, the game window at the desktop origin and moved to (300,200); a capture of the window's client area in play, then the Pause key, then a capture of the paused screen. Before: the paused background is the desktop's corner, with the game window's own title bar inside it when moved. After: the paused background is the frame just before pausing, with no title bar and no offset, in both positions. Full-screen is unchanged by the code (the other arm of the same line). Any later change to `gr_d3d_save_screen` or `gr_d3d_flip` repeats this check by hand.
- Recordings: the quick suite on `RecordPlayback` with this change (12 test missions, sm1-01 and sm3-06, Release and Debug) replays and matches frame for frame; AutotestAI Debug exits 2 as it did before (its two known Warnings). Release and Debug build with no warning; the tools check passes.

## Not changed
- `d3d_get_pixel`'s windowed read: same root cause, issue #77; a separate review with captures of what the line offset governs (D-20261020-070918).
- Restoring into the back buffer (`gr_d3d_restore_screen`) is already right in both modes.

## Effect on mission flow
None. The saved picture is only drawn; no gameplay code reads it.
