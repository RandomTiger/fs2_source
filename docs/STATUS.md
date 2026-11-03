# GodotOptions status

Branch `GodotOptions` (WIP, no gates; never merged into `Godot`, Tom's order). Game Options screen, live switching per
D-20261102-204801..808. `origin/GodotResolution` is merged in (c1c5a68); its own status follows below this section.

## Done
- Merge of GodotResolution: conflict in FreeSpace.cpp's host init kept both (`gr_virtual_read_config` + GraphicsMode key).
- Resolution row: Classic (4:3) / Desktop / up to 3 common sizes that fit the monitor / the monitor's size (and the size in
  force if none of those). Writes `DisplayResolution`; live where `gr_live_mode_change()` (Godot host: the window follows
  `fs2_display_request`); else "Takes effect next time the game starts" (native D3D).
- GR row (FS2 retail): live via `game_switch_gr_mode` where the backend says live; the next-start notice only where not.
  The screen lays itself out again in the new mode; reopening it lights the switched mode (9cf65dc).
- Flight row: `joy_reassign()` (Joy.cpp) re-runs `joy_init` on a changed choice, no restart. On native backends it
  releases and re-opens the WinMM stick; on the Godot host joy_init opens nothing (host reader), but the re-run is still
  needed: with Pad chosen at start `Cur_joystick` is -1 and Joystick must take it back.
- Menu tour (D-20261102-204807): the Game options Back stop has six live views (GR 640, device, device back, resolution,
  resolution back, GR 1024) with checks: mode reached, device reached, late-load counters 0. No key is written (compared
  run). GR views SKIP on native D3D and other data sets. Godot host r, FS2: 70/70 PASS.
- Builds: Win32 r/d, x64 r/d, fs2game x64 r/d, GDExtension: 0 warnings.
- Resolution drop-down (Tom): fs2_api v9 adds the optional `fs2_set_dropdown_hooks` (show at a base-pixel rect with labels
  and the selected index, hide) and `fs2_input_dropdown` (the pick, from inside poll_events); `host_hooks` gains
  `dropdown_show/hide` (null on Windows and headless) and `Host_dropdown_chosen`. GameOptions.cpp uses it only when the
  hooks are set and `game_options_keys_active()` (no replay, no autotest): the drawn sizes row is then left out, the
  drop-down is shown on screen init, hidden on close, hidden/re-shown around a live GR switch; a pick goes through
  `game_options_res_choose` (same apply + ini key as the drawn row). The tour's RES steps call `game_options_res_choose`
  directly, so the tour and replays see no change. Godot host (host_hooks_godot.cpp): an `OptionButton` child of the
  Fs2Screen, themed in code (near-black, green edge, grey/white text), placed every poll through the canvas + keep-aspect
  fit; a click on it (or anything but motion while its list is open) is not forwarded to the game; the OS cursor shows
  over it. Checked at 2560x1080: closed/open at 1024, after a live switch to 640, and a pick of 1920x1080 (ini written,
  window resized, button followed). Godot host r menu tour, FS2: 70/70 PASS.
- Cursor after a live GR switch (Tom): `game_switch_gr_mode` no longer warps the pointer to the centre (`// Claude:` in
  FreeSpace.cpp). The host keeps the game mouse in base pixels and only remapped it on a mouse event, so after the mode
  changed (or a warp that did not reach the real pointer) the drawn cursor sat away from the pointer until the mouse moved.
  host_hooks_godot.cpp now remaps the real pointer through the new canvas/fit whenever the mapping or window size changes
  (`remap_mouse_on_change`, once per poll). Checked with real OS clicks, 1024->640 and 640->1024, fullscreen classic and a
  2560x1080 window: drawn cursor on the pointer in the first frame after the switch.
- Drop-down font (Tom): was 55% of the box height in base pixels (about 14px at 1024); now 2% of the picture height in
  window pixels (22px at 1080, 29px at 1440), same in both modes, box grown and centred on the row when the font needs more.
  Godot host r/d menu tour, FS2: 68 PASS, 2 SKIP, 0 FAIL.

## For GodotResolution (flag, not done there)
- `godot/src/fs2_node.cpp` (d773c72): the host now acts on `fs2_display_request` at start and on each step (a size
  request resizes the window). That is Resolution-side behaviour; GodotResolution should take it or say no.

## Open
- InputTest's six device transitions (204804) need a device switch between injected inputs inside one run; not small,
  not done.
- Native Win32 tour (`tools\menu_tour_all.ps1`) not run this round (no gates on WIP).
- At GR_640 the drawn Resolution sizes row (native D3D and compared runs; the Godot host now has the drop-down) is tight (step w/8 at 640 wide); readable, no overlap.

# GodotResolution status

Branch `GodotResolution` (off `Godot` 8703e2d). Never merged into `Godot` (Tom's order). Rulings D-20261102-200801..809
and 204801..808. Goal: any window size, full screen on an ultrawide, 4:3 menus centred, Hor+ 3D, HUD gauges anchored.

## Design
- `code/Graphics/GrVirtual.h/.cpp`: the virtual canvas. Height = base height (768 GR_1024, 480 GR_640), width = base
  height x window aspect (or the reverse for a window taller than 4:3). `gr_screen.max_w/max_h` stay at base.
- Compared runs (`Autotest_harness || Replay_mode != REPLAY_OFF`) pin the virtual canvas to the base canvas.
- fs2.ini `[Settings] DisplayResolution = classic | desktop | <W>x<H>`; absent or not understood = classic
  (today's letterbox). (`Resolution` already names the GR art mode, hence the new name.)
- fs2_api v8: `fs2_set_window_size`, `fs2_canvas_info`, `fs2_display_request`.

## Steps
| # | Step | State |
|---|---|---|
| 1 | Virtual-size seam, fs2_api v8, ini key read | done |
| 2 | gr_host present offset, clear, restore, region | done |
| 3 | Godot host: window size, resize, mouse mapping, viewport size | done |
| 4 | Wide 3D clip + Hor+ | done |
| 5 | HUD anchors wired | done |
| 6 | GR mode switch + live flag + test hook | done |
| 7 | Builds, screenshots, one replay r/d | done |

## Drawing
- Menus and anything outside a mission frame: the base screen centred, black bars round it (a black full-canvas
  quad at frame open). The mouse is mapped window -> canvas -> base pixels; deltas stay in base pixels.
- Mission frame, full view: `game_set_view_clip` widens the clip to the virtual canvas (`gr_virtual_wide_clip`).
  3D is Hor+: `3dSetup.cpp` sets `Window_scale.z = W*bh / (H*bw)` when the canvas is wider than 4:3, so the
  vertical field of view stays the retail one.
- HUD: anchoring is on between `game_drawing_begin/end` only. Each gauge picks its anchor through the checks it
  already makes (`hud_gauge_active`, `hud_set_gauge_color`). Present offset = (vw-bw) x {0, 1/2, 1} by anchor
  column, (vh-bh) likewise by row. Inside the wide clip the anchor is ignored.
- Native D3D build: never sets a window size, so the canvas is always the base (no change).

## HUD anchor table (`Hud_gauge_anchor` in GrVirtual.cpp)
| Anchor | Gauges |
|---|---|
| top-left | talking head, message lines |
| top-centre | damage, objectives notify, text flash, support |
| top-right | wingmen status, squad message menu |
| middle-left | directives |
| middle-right | escort list |
| centre | lead indicator, orientation tee, hostile/target triangles, reticle circle (unused), throttle, centre reticle, threat, afterburner/weapon energy, weapon linking, target mini icon, offscreen indicator/range, missile warning arrow, attacking count, lag |
| bottom-left | target monitor, its extra data, target shield icon |
| bottom-centre | radar, player shield icon |
| bottom-right | mission time, auto target/speed, ETS, weapons, countermeasures, kills |

## Untagged draws (default top-left)
Drawn with no gauge check, so they get `HUD_ANCHOR_TOP_LEFT` set explicitly in `HUD_render_2d`:
multiplayer text. Tom, 2026-11-02: training messages and red alert text top-centre, pad HUD top-right (pad is WIP).
Subspace notify takes the objectives-notify gauge's top-centre anchor. The scoring debug text (debug
builds) is drawn after the HUD resets, also top-left. Supernova text follows the threat gauge (centre).

## GR mode switch
`game_switch_gr_mode(res)` (FreeSpace.cpp; `fs2_switch_gr_mode` in fs2_api v8) re-runs `gr_init` on the same backend
and reloads the radar art. Outside a mission only: Assert in Debug, ignored in Release. Only when
`gr_live_mode_change()`: the backend's `live_mode_change` flag, 1 for the Godot host, 0 headless; native D3D is not
GR_HOST, so never. Only the test hook calls it: `-gr_switch_test` switches to the other mode and back on the first
main hall entry and logs two checks to autotest.log (switch done; late-load counters 0).

## Results (2026-11-02)
- Builds, 0 warnings: Win32 r/d, x64 r/d, fs2game x64 r/d, GDExtension Debug + Release.
- Godot host, DisplayResolution=2560x1080: main hall centred with bars, flight Hor+ with anchored HUD; classic
  (key absent) unchanged. Screenshots are not committed (game data).
- `-autotest -at_menus -gr_switch_test` in the Godot host: both switch checks passed.
- sm1-01 replayed in the Godot host with DisplayResolution=2560x1080, Release and Debug: REPLAY OK, TraceDiff MATCH
  (3544 frames); the replay pins the base canvas.

## Nebula fog read (#138)
The Godot sink tells the fog read (neb2_pre_render's 32x32 read) from a screenshot by origin (0,0), and its fog paths
render the canvas origin. With the base screen centred, the pre-render landed at the centring and the read came in at
(398,0) in a 2560x1080 window: every live nebula frame took the full-frame screenshot path. Fix: `neb2_pre_render`
brackets its draws and read with `gr_virtual_fog_read_begin/end`; in between the present offset is 0, and `h_get_region`
reads at (0,0). Screenshots still read the base screen where it is shown. Checked 2026-11-03: sm1-05 live in the Godot
host at 2560x1080, every read on the fog path (GPU), fog as in classic; menu tour fs2 r 62/62, 0 black.

## 3D-phase HUD gauges (2026-11-03)
`HUD_render_3d` draws inside the wide clip, where the anchor is ignored, so its base-coordinate draws landed left by
the centring (398 px at 2560x1080): the target mini-icon hull number, the target triangle, the hostile and missile
triangles, the orientation tee and the lock indicator. Fix: the in-cockpit block of `hud_show_targeting_gauges`
(triangle, tee, target monitor, target shield icon, mini icon) runs between `gr_virtual_base_clip_begin/end`, so
each gauge takes its anchor; `hud_render_triangle` and the lock indicator add `gr_virtual_wide_shift_x/y` to their
base coordinates; the two in-reticle tests take it off the projected point. Compared runs pin the base canvas
(shift 0), so no trace change. Checked live at 2560x1080: FS2 sm1-04 (capital, escort list), SM1-05 (nebula), FS1
SM1-01; FS1 main hall at GR_640 centred, nothing cut off.
- Design question: the target shield icon is BOTTOM_LEFT, so it floats between the target monitor and the radar;
  the player shield (its mirror) is by the radar.

## Open
- The canvas renders at the virtual size (height 768 or 480) and Godot scales it to the window; drawing at the
  window's native pixels is not done.
