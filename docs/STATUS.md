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
| 2 | gr_host present offset, clear, restore, region | todo |
| 3 | Godot host: window size, resize, mouse mapping, viewport size | todo |
| 4 | Wide 3D clip + Hor+ | todo |
| 5 | HUD anchors wired | todo |
| 6 | GR mode switch + live flag + test hook | todo |
| 7 | Builds, screenshots, one replay r/d | todo |
