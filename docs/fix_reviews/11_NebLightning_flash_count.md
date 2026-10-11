# Fix review: NebLightning.cpp counts lightning points from uninitialised memory

**Recommendation: TAKE.** Low risk.

Found because replays of the nebula mission loop2-1 desynced about one run in seven. The replay played a lightning sound that the recording had not.

## The bug
On the first frame of each lightning strike, `nebl_generate_section` counts the bolt's points that are on screen. The count sets the flash and, on a bolt's first strike, the thunder sound and the EMP:
```cpp
g3_rotate_vertex(&c->vex[idx], &pt);
g3_project_vertex(&c->vex[idx]);

// if first frame, keep track of the average screen pos
if((c->vex[idx].sx >= 0) && (c->vex[idx].sx < gr_screen.max_w) && (c->vex[idx].sy >= 0) && (c->vex[idx].sy < gr_screen.max_h)){
    Nebl_flash_x += c->vex[idx].sx;
    Nebl_flash_y += c->vex[idx].sy;
    Nebl_flash_count++;
}
```
When a point is behind the eye, `g3_project_vertex` sets `PF_OVERFLOW` and does not write `sx` and `sy`. The sections are locals in `nebl_render` (`l_section start, end, child_start, temp`), so the test reads whatever an earlier call left on the stack.

So the flash, and whether a strike makes a sound and an EMP, depend on stack contents. With sound on, the earlier sound calls leave different values each run, depending on whether DirectSound had a free channel. Logging the first strike of each bolt in a recording and in a replay of it gave different counts for the same frame, bolt, eye position and random state. For example, bolt 0 at frame 33 counted 532 points in the recording and 530 in the replay.

This changes gameplay: the count decides whether `frand_range` is called, which moves the random stream, and whether `emp_apply` runs on the player.

## Proposed fix
Count a point only if it projected:
```cpp
if(!(c->vex[idx].flags & PF_OVERFLOW) && (c->vex[idx].sx >= 0) && ...
```
The same test is added to the second check, for the bolt's end section.

Checked by recording loop2-1 in Release with the fix and replaying it 10 times. All 10 matched on every frame. Before the fix, 1 replay in 7 desynced.

## Not changed
The second check tests `c->vex` where `cap->vex` was meant, so a bolt's end section counts the start section's points again. That gives the same result every run, but fixing it changes the flash, sound and EMP decisions. A `Claude:` comment marks it.

## Effect on mission flow
None. Nebula missions now decide the flash, thunder and EMP from the points that are actually on screen, so lightning in those missions plays slightly differently. Recordings of nebula missions made before this fix no longer replay.
