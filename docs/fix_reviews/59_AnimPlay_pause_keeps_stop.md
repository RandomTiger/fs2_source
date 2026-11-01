# Fix review: a paused anim that reaches its last frame is ended and freed while its owner still holds it

**Recommendation: TAKE.** `anim_show_next_frame` undoes the frame advance of a paused anim, but not the stop that its one-shot clamp sets on the same call. The next call ends the anim and `anim_render_all` frees the instance, while ship select (`Ss_icons[].anim_instance`) or weapon select (`Wl_icons[].anim_instance`) still points at it. Leaving the screen frees it a second time and the Debug build asserts `instance->frame` in `anim_release_render_instance`; in Release the ship picture disappears. The fix makes the pause undo the stop too. Found by the FS2 demo Debug menu tour (#93). Classified and ruled by the steward: D-20261022-224915. The fix changes what ship select shows (the picture keeps animating after F1 instead of vanishing); Tom chose "Fix it" for that change (asked directly, about 23:10 on 2026-10-22).

## The bug
`code/Anim/AnimPlay.cpp`, `anim_show_next_frame`:

```
	frame_save = instance->frame_num;
	... advance frame_num from the elapsed time ...
	if ( instance->stop_now == TRUE ) {
		return -1;
	}
	if(instance->direction == ANIM_DIRECT_FORWARD){
		if (instance->frame_num >= instance->stop_at ) {
			...
			} else {															// one-shot animations
				instance->frame_num = instance->stop_at;
				instance->last_frame_num = instance->frame_num;
				instance->stop_now = TRUE;
			}
	...
	// if the anim is paused, ignore all the above changes and still display this frame
	if(instance->paused || Anim_paused){
		instance->frame_num = frame_save;
		instance->time_elapsed -= frametime;
		frame_diff = 0;
	}
```

The comment says a pause ignores all the above changes, but only `frame_num` is restored. If the anim reaches `stop_at` on a paused call, `stop_now` and `last_frame_num` keep the clamp's values. The next call returns -1 at the `stop_now` test, paused or not, and `anim_render_all` releases the instance.

## How ship select and weapon select hit it
`ship_select_do` pauses the ship anim while the F1 help overlay is up (`ss_anim_pause`, `MissionShipChoice.cpp`). Every frame it restarts the anim when `frame_num == stop_at`, before rendering. The paused call above leaves `frame_num` at the earlier frame, so the restart test misses. The next render frees the instance, but `Ss_icons[Selected_ss_class].anim_instance` still points at it. On leaving the screen, `stop_ship_animation` (or `unload_ship_anim_instances`, in `ship_select_close`) releases it again: `Assert(instance->frame)`.

Weapon select has the same shape: `weapon_select_do` pauses `Wl_icons[Weapon_anim_class].anim_instance` under its help overlay and restarts it on `frame_num == stop_at`, and the anim is drawn through `anim_render_all(ON_WEAPON_SELECT)`. It runs through the same function, so this one change covers it; no change in `MissionWeaponChoice.cpp`.

A diagnostic log in `anim_release_render_instance` caught the sequence in the demo: a release from `anim_render_all` of the ship anim with `paused=1 stop_now=1 frame_num=0 last_frame_num=1 stop_at=1`, then a release of the same instance from `stop_ship_animation` with `frame` already NULL, then the assert.

## Why the demo shows it and retail rarely does
The demo's ship select and weapon select anims (`2_ssfighter2t-02.ani`, `2_SD4.ani`) are two frames long (`stop_at` 1), so they reach `stop_at` about every 0.1 s, and almost any pause catches one. Retail anims are long and loop from frame 51, so the pause rarely lands on the frame that reaches the end. The code path is the same in both, and a player pressing F1 hits it. Attribution (D-20261022-224842): FS2 demo Debug tours fired 7 of 10 on the Enhanced 39f4881 exe and 5 of 10 with the #92 hunk; retail FS2 Debug tours 0 of 10.

## Proposed fix
Save `last_frame_num` and `stop_now` next to `frame_save`, and restore them in the paused branch with `frame_num`. A paused call then changes nothing but `time_elapsed`, as the comment says it should. `// Claude:` comments naming #93 on the added lines. The Assert stays and the screen is not walled (D-029).

## What was checked
Results per layer on #93: every recording set replays identical r/d at OSBF, PrePort32 and Enhanced, and 10 serialised FS2 demo Debug tours plus 10 retail FS2 Debug tours run with 0 asserts.

## Not changed
- An anim that was already stopping before the pause (`stop_now` set on an earlier call) still ends at the `stop_now` test; the pause does not revive it.
- The looped and ping-pong branches, which also change the instance on a paused call (`time_elapsed = 0`, `anim_reverse_direction`), are not touched; they free nothing.
- The possible double release when `bm_create` fails (`anim_show_next_frame` and then `anim_render_all` both release) is a separate path and was not seen.

## Effect on mission flow
None on missions. On the ship select and weapon select screens, an anim paused under the help overlay keeps playing after the overlay closes, instead of vanishing (Release) or asserting (Debug).
