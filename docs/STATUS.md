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
| 6 | GR mode switch + live flag + test hook | todo |
| 7 | Builds, screenshots, one replay r/d | todo |

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
| centre | lead indicator, orientation tee, hostile/target triangles, reticle circle (unused), throttle, centre reticle, auto target/speed, threat, afterburner/weapon energy, weapon linking, target mini icon, offscreen indicator/range, missile warning arrow, attacking count, lag |
| bottom-left | target monitor, its extra data, target shield icon |
| bottom-centre | radar |
| bottom-right | mission time, player shield icon, ETS, weapons, countermeasures, kills |

## Untagged draws (default top-left)
Drawn with no gauge check, so they get `HUD_ANCHOR_TOP_LEFT` set explicitly in `HUD_render_2d`:
subspace notify, training messages, multiplayer text, red alert text, pad HUD. The scoring debug text (debug
builds) is drawn after the HUD resets, also top-left. Supernova text follows the threat gauge (centre).

## Open
- The canvas renders at the virtual size (height 768 or 480) and Godot scales it to the window; drawing at the
  window's native pixels is not done.
