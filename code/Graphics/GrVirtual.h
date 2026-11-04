// Claude: GrVirtual.h, the virtual canvas: play at any window size (GodotResolution, D-20261102-204801).
//
// The game keeps drawing in its base art coordinates (gr_screen.max_w x max_h: 1024x768 for GR_1024, 640x480 for
// GR_640). The virtual canvas has the base height and the window's aspect, so it is wider (or taller) than the base.
// A backend that supports it (the GR_HOST backend) shifts each draw by a present offset into the virtual canvas:
// the base screen centred for menus, or a HUD gauge pushed to its anchor (left, centre, right; top, middle, bottom).
// The full-view 3D clip of a mission frame covers the whole virtual canvas (gr_virtual_wide_clip), drawn Hor+.
//
// gr_screen.max_w/max_h never change, so replays, the autotest cursor and the bitmap log see the base canvas. Compared
// runs (-autotest, -record, -replay) pin the virtual canvas to the base canvas. With no window size from the host,
// or the display mode "classic" (the default), the virtual canvas is the base canvas and nothing changes.

#ifndef _GR_VIRTUAL_H
#define _GR_VIRTUAL_H

// Display modes, from the DisplayResolution key in fs2.ini [Settings]: "classic", "desktop" or "<W>x<H>".
#define GR_DISPLAY_CLASSIC		0		// 4:3 letterbox in any window (also when the key is absent or not understood)
#define GR_DISPLAY_DESKTOP		1		// fill the host window at its own aspect
#define GR_DISPLAY_SIZE			2		// fill a window of the given size

// HUD anchors: where a gauge goes when the canvas is wider or taller than the base. The column picks a factor
// 0, 1/2 or 1 of (virtual_w - base_w), the row likewise of (virtual_h - base_h).
#define HUD_ANCHOR_TOP_LEFT			0
#define HUD_ANCHOR_TOP_CENTRE		1
#define HUD_ANCHOR_TOP_RIGHT		2
#define HUD_ANCHOR_MIDDLE_LEFT		3
#define HUD_ANCHOR_CENTRE			4
#define HUD_ANCHOR_MIDDLE_RIGHT		5
#define HUD_ANCHOR_BOTTOM_LEFT		6
#define HUD_ANCHOR_BOTTOM_CENTRE	7
#define HUD_ANCHOR_BOTTOM_RIGHT		8
#define HUD_NUM_ANCHORS				9

// Reads DisplayResolution from fs2.ini. Called at start-up before the host asks for the display request; call it
// again to apply a changed key (then gr_virtual_set_window with the new window size).
void gr_virtual_read_config();

// The display mode in force, and for GR_DISPLAY_SIZE the window size it asks for (0, 0 otherwise). Compared runs
// report GR_DISPLAY_CLASSIC.
int gr_virtual_display_request(int *w, int *h);

// The host window's client size in pixels. 0, 0 (the default) means no window: the virtual canvas is the base.
// Re-settable at any time (a window resize); the next frame draws at the new size.
void gr_virtual_set_window(int w, int h);
// Claude: GodotOptions, the window size last set (0 x 0 before the first)
void gr_virtual_get_window(int *w, int *h);

// The virtual canvas size (the base size when not active), and the base screen's top-left corner in it.
int gr_virtual_w();
int gr_virtual_h();
int gr_virtual_centre_x();
int gr_virtual_centre_y();
int gr_virtual_active();		// 1 when the virtual canvas is not the base canvas

// The present offset for the next draw: where the base screen's (0,0) lands in the virtual canvas. Inside the wide
// clip it is the centring only (gr_screen.offset_x/y cancel it); otherwise the current anchor's.
void gr_virtual_offset(int *dx, int *dy);

// HUD anchoring, on only while a mission frame draws its view and HUD (gr_virtual_hud_begin/end). While it is off,
// every draw is centred. gr_virtual_hud_gauge sets the anchor from the gauge table (HUDgauges.h index); it is called
// from hud_gauge_active and hud_set_gauge_color, so a gauge is anchored by the checks it already makes.
void gr_virtual_hud_begin();
void gr_virtual_hud_end();
void gr_virtual_hud_gauge(int gauge_index);
void gr_virtual_set_anchor(int anchor);

// GodotOptions: the double size HUD. Inside begin/end, and in a base clip, gauge draws double around their anchor point
// (the screen corner, edge or centre they hold to), so a gauge grows into the screen. Projected gauges stay where they are:
// they mark places in the 3D view. gr_virtual_hud_scale gives the scale for the next draw and that point, in base pixels.
void gr_virtual_set_hud_double(int on);
void gr_virtual_hud_double_begin();
void gr_virtual_hud_double_end();
float gr_virtual_hud_scale(int *pivot_x, int *pivot_y);
int gr_virtual_gauge_anchor(int gauge_index);	// the table entry, HUD_ANCHOR_TOP_LEFT for an unknown gauge

// Mission frame, full view: widen the clip to the whole virtual canvas. gr_screen.offset_x/y go negative by the
// centring so base-coordinate code still lands centred. No-op when not active.
void gr_virtual_wide_clip();
int gr_virtual_in_wide_clip();	// 1 while the clip is wider or taller than the base screen
void gr_virtual_set_clip_listener(void (*fn)());	// the backend's "clip changed" call, made by gr_virtual_wide_clip

// HUD_render_3d runs inside the wide clip, where base-coordinate gauges (the reticle triangles, the target monitor,
// the mini target icon) would land left of the base screen by the centring. Between base_clip_begin and end the clip
// is the base screen again, so those draws take their gauge anchors; outside the wide clip both are no-ops. Nests.
// gr_virtual_wide_shift_x/y: what to add to a base coordinate to draw it inside the wide clip (0 outside it), and
// so what to take off a projected coordinate to compare it with a base one (Hud_reticle_center).
void gr_virtual_base_clip_begin();
void gr_virtual_base_clip_end();
int gr_virtual_wide_shift_x();
int gr_virtual_wide_shift_y();

// The nebula fog read (#138): neb2_pre_render draws its ESIZE square and reads it back between begin and end. In
// between the present offset is 0, so the square sits at the canvas origin at any aspect, and the backend's
// get_region reads it there (the Godot sink's fog paths render and read the canvas origin). Screenshots read the
// base screen where it is presented, as before.
void gr_virtual_fog_read_begin();
void gr_virtual_fog_read_end();
int gr_virtual_fog_read();	// 1 between begin and end

#endif
