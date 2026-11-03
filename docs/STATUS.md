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
