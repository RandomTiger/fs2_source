// Claude: GrVirtual.cpp, the virtual canvas and the HUD anchor table. See GrVirtual.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GrVirtual.h"
#include "2d.h"
#include "osregistry.h"
#include "HUDgauges.h"
#include "Autotest.h"
#include "Replay.h"

// The HUD anchor table, indexed by the HUDgauges.h gauge index. Chosen from where each gauge sits on the retail
// 1024x768 / 640x480 layout; docs/STATUS.md lists it. Gauges drawn by projection into the 3D view (lead indicator,
// brackets, triangles, offscreen indicators) are drawn inside the wide clip, where the anchor is not used; they are
// tagged centre for completeness.
static const int Hud_gauge_anchor[NUM_HUD_GAUGES] = {
	HUD_ANCHOR_CENTRE,			// HUD_LEAD_INDICATOR
	HUD_ANCHOR_CENTRE,			// HUD_ORIENTATION_TEE
	HUD_ANCHOR_CENTRE,			// HUD_HOSTILE_TRIANGLE
	HUD_ANCHOR_CENTRE,			// HUD_TARGET_TRIANGLE
	HUD_ANCHOR_BOTTOM_RIGHT,	// HUD_MISSION_TIME
	HUD_ANCHOR_CENTRE,			// HUD_RETICLE_CIRCLE
	HUD_ANCHOR_CENTRE,			// HUD_THROTTLE_GAUGE
	HUD_ANCHOR_BOTTOM_CENTRE,	// HUD_RADAR
	HUD_ANCHOR_BOTTOM_LEFT,		// HUD_TARGET_MONITOR
	HUD_ANCHOR_CENTRE,			// HUD_CENTER_RETICLE
	HUD_ANCHOR_BOTTOM_LEFT,		// HUD_TARGET_MONITOR_EXTRA_DATA
	HUD_ANCHOR_BOTTOM_CENTRE,	// HUD_TARGET_SHIELD_ICON // Claude: GodotResolution, by the radar (Tom, 2026-11-03)
	HUD_ANCHOR_BOTTOM_CENTRE,	// HUD_PLAYER_SHIELD_ICON (beside the radar)
	HUD_ANCHOR_BOTTOM_RIGHT,	// HUD_ETS_GAUGE
	HUD_ANCHOR_BOTTOM_RIGHT,	// HUD_AUTO_TARGET (beside the player shield icon)
	HUD_ANCHOR_BOTTOM_RIGHT,	// HUD_AUTO_SPEED
	HUD_ANCHOR_BOTTOM_RIGHT,	// HUD_WEAPONS_GAUGE
	HUD_ANCHOR_MIDDLE_RIGHT,	// HUD_ESCORT_VIEW
	HUD_ANCHOR_MIDDLE_LEFT,		// HUD_DIRECTIVES_VIEW
	HUD_ANCHOR_CENTRE,			// HUD_THREAT_GAUGE
	HUD_ANCHOR_CENTRE,			// HUD_AFTERBURNER_ENERGY
	HUD_ANCHOR_CENTRE,			// HUD_WEAPONS_ENERGY
	HUD_ANCHOR_CENTRE,			// HUD_WEAPON_LINKING_GAUGE
	HUD_ANCHOR_CENTRE,			// HUD_TARGET_MINI_ICON
	HUD_ANCHOR_CENTRE,			// HUD_OFFSCREEN_INDICATOR
	HUD_ANCHOR_TOP_LEFT,		// HUD_TALKING_HEAD
	HUD_ANCHOR_TOP_CENTRE,		// HUD_DAMAGE_GAUGE
	HUD_ANCHOR_TOP_LEFT,		// HUD_MESSAGE_LINES
	HUD_ANCHOR_CENTRE,			// HUD_MISSILE_WARNING_ARROW
	HUD_ANCHOR_BOTTOM_RIGHT,	// HUD_CMEASURE_GAUGE
	HUD_ANCHOR_TOP_CENTRE,		// HUD_OBJECTIVES_NOTIFY_GAUGE
	HUD_ANCHOR_TOP_RIGHT,		// HUD_WINGMEN_STATUS
	HUD_ANCHOR_CENTRE,			// HUD_OFFSCREEN_RANGE
	HUD_ANCHOR_BOTTOM_RIGHT,	// HUD_KILLS_GAUGE
	HUD_ANCHOR_CENTRE,			// HUD_ATTACKING_TARGET_COUNT
	HUD_ANCHOR_TOP_CENTRE,		// HUD_TEXT_FLASH
	HUD_ANCHOR_TOP_RIGHT,		// HUD_MESSAGE_BOX (the squad message menu)
	HUD_ANCHOR_TOP_CENTRE,		// HUD_SUPPORT_GAUGE
	HUD_ANCHOR_CENTRE,			// HUD_LAG_GAUGE
};

static int Display_mode = GR_DISPLAY_CLASSIC;
static int Display_w = 0, Display_h = 0;		// GR_DISPLAY_SIZE only
static int Window_w = 0, Window_h = 0;		// from the host; 0 = none
static int Hud_on = 0;
static int Anchor = HUD_ANCHOR_CENTRE;
static int Hud_gauge_now = -1;		// the gauge the anchor came from, -1 when it was set directly
static void (*Clip_listener)() = NULL;
static int Fog_read = 0;

// neb2_pre_render's draws and read: at the canvas origin, where the backend's fog read path expects them (#138)
void gr_virtual_fog_read_begin()
{
	Fog_read = 1;
	if (Clip_listener != NULL) {
		Clip_listener();
	}
}

void gr_virtual_fog_read_end()
{
	Fog_read = 0;
	if (Clip_listener != NULL) {
		Clip_listener();
	}
}

int gr_virtual_fog_read() { return Fog_read; }

void gr_virtual_set_clip_listener(void (*fn)())
{
	Clip_listener = fn;
}

static int compared_run()
{
	return Autotest_harness || (Replay_mode != REPLAY_OFF);
}

void gr_virtual_read_config()
{
	Display_mode = GR_DISPLAY_CLASSIC;
	Display_w = Display_h = 0;
	char *s = os_config_read_string(NULL, "DisplayResolution", NULL);
	if (s == NULL) {
		return;
	}
	if (!_stricmp(s, "desktop")) {
		Display_mode = GR_DISPLAY_DESKTOP;
	} else {
		int w = 0, h = 0;
		char x = 0;
		if ((sscanf(s, "%d%c%d", &w, &x, &h) == 3) && ((x == 'x') || (x == 'X')) && (w >= 320) && (h >= 240) && (w <= 16384) && (h <= 16384)) {
			Display_mode = GR_DISPLAY_SIZE;
			Display_w = w;
			Display_h = h;
		}
	}
}

int gr_virtual_display_request(int *w, int *h)
{
	int mode = compared_run() ? GR_DISPLAY_CLASSIC : Display_mode;
	*w = (mode == GR_DISPLAY_SIZE) ? Display_w : 0;
	*h = (mode == GR_DISPLAY_SIZE) ? Display_h : 0;
	return mode;
}

void gr_virtual_set_window(int w, int h)
{
	Window_w = (w > 0) ? w : 0;
	Window_h = (h > 0) ? h : 0;
}

// Claude: GodotOptions
void gr_virtual_get_window(int *w, int *h)
{
	*w = Window_w;
	*h = Window_h;
}

// the virtual size for the current base, window and mode
static void virtual_size(int *vw, int *vh)
{
	int bw = gr_screen.max_w, bh = gr_screen.max_h;
	*vw = bw;
	*vh = bh;
	if ((bw <= 0) || (bh <= 0) || (Window_w <= 0) || (Window_h <= 0) || (Display_mode == GR_DISPLAY_CLASSIC) || compared_run()) {
		return;
	}
	// same aspect test in integers: window wider than the base when win_w * bh > bw * win_h
	if ((long long)Window_w * bh > (long long)bw * Window_h) {
		*vw = (int)(((long long)bh * Window_w + Window_h / 2) / Window_h);
	} else if ((long long)Window_w * bh < (long long)bw * Window_h) {
		*vh = (int)(((long long)bw * Window_h + Window_w / 2) / Window_w);
	}
	if (*vw < bw) { *vw = bw; }
	if (*vh < bh) { *vh = bh; }
}

int gr_virtual_w() { int w, h; virtual_size(&w, &h); return w; }
int gr_virtual_h() { int w, h; virtual_size(&w, &h); return h; }
int gr_virtual_centre_x() { return (gr_virtual_w() - gr_screen.max_w) / 2; }
int gr_virtual_centre_y() { return (gr_virtual_h() - gr_screen.max_h) / 2; }
int gr_virtual_active() { int w, h; virtual_size(&w, &h); return (w != gr_screen.max_w) || (h != gr_screen.max_h); }

int gr_virtual_in_wide_clip()
{
	return (gr_screen.clip_width > gr_screen.max_w) || (gr_screen.clip_height > gr_screen.max_h);
}

void gr_virtual_offset(int *dx, int *dy)
{
	if (Fog_read) {
		*dx = *dy = 0;
		return;
	}
	int vw, vh;
	virtual_size(&vw, &vh);
	int ex = vw - gr_screen.max_w, ey = vh - gr_screen.max_h;
	int a = (Hud_on && !gr_virtual_in_wide_clip()) ? Anchor : HUD_ANCHOR_CENTRE;
	int col = a % 3, row = a / 3;
	*dx = (col == 0) ? 0 : (col == 1) ? ex / 2 : ex;
	*dy = (row == 0) ? 0 : (row == 1) ? ey / 2 : ey;
}

void gr_virtual_hud_begin()
{
	Hud_on = 1;
	Anchor = HUD_ANCHOR_TOP_LEFT;		// a draw with no gauge check before it: top-left
}

void gr_virtual_hud_end()
{
	Hud_on = 0;
	Anchor = HUD_ANCHOR_CENTRE;
}

int gr_virtual_gauge_anchor(int gauge_index)
{
	if ((gauge_index < 0) || (gauge_index >= NUM_HUD_GAUGES)) {
		return HUD_ANCHOR_TOP_LEFT;
	}
	return Hud_gauge_anchor[gauge_index];
}

void gr_virtual_hud_gauge(int gauge_index)
{
	if (Hud_on) {
		Anchor = gr_virtual_gauge_anchor(gauge_index);
		Hud_gauge_now = gauge_index;
	}
}

void gr_virtual_set_anchor(int anchor)
{
	if (Hud_on && (anchor >= 0) && (anchor < HUD_NUM_ANCHORS)) {
		Anchor = anchor;
		Hud_gauge_now = -1;
	}
}

void gr_virtual_wide_clip()
{
	int vw, vh;
	virtual_size(&vw, &vh);
	if ((vw == gr_screen.max_w) && (vh == gr_screen.max_h)) {
		return;
	}
	// set directly: gr_set_clip clamps to the base screen
	gr_screen.offset_x = -((vw - gr_screen.max_w) / 2);
	gr_screen.offset_y = -((vh - gr_screen.max_h) / 2);
	gr_screen.clip_left = 0;
	gr_screen.clip_top = 0;
	gr_screen.clip_right = vw - 1;
	gr_screen.clip_bottom = vh - 1;
	gr_screen.clip_width = vw;
	gr_screen.clip_height = vh;
	if (Clip_listener != NULL) {
		Clip_listener();
	}
}

int gr_virtual_wide_shift_x() { return gr_virtual_in_wide_clip() ? -gr_screen.offset_x : 0; }
int gr_virtual_wide_shift_y() { return gr_virtual_in_wide_clip() ? -gr_screen.offset_y : 0; }

static int Base_clip_depth = 0;
static int Base_clip_saved = 0;
static int Saved_clip[8];
static int Saved_anchor = HUD_ANCHOR_CENTRE;

void gr_virtual_base_clip_begin()
{
	if (Base_clip_depth++ > 0) {
		return;
	}
	Base_clip_saved = gr_virtual_in_wide_clip();
	if (!Base_clip_saved) {
		return;
	}
	Saved_clip[0] = gr_screen.offset_x;		Saved_clip[1] = gr_screen.offset_y;
	Saved_clip[2] = gr_screen.clip_left;	Saved_clip[3] = gr_screen.clip_top;
	Saved_clip[4] = gr_screen.clip_right;	Saved_clip[5] = gr_screen.clip_bottom;
	Saved_clip[6] = gr_screen.clip_width;	Saved_clip[7] = gr_screen.clip_height;
	Saved_anchor = Anchor;
	gr_virtual_set_anchor(HUD_ANCHOR_CENTRE);	// the reticle's; a gauge check inside sets its own
	gr_screen.offset_x = 0;
	gr_screen.offset_y = 0;
	gr_screen.clip_left = 0;
	gr_screen.clip_top = 0;
	gr_screen.clip_right = gr_screen.max_w - 1;
	gr_screen.clip_bottom = gr_screen.max_h - 1;
	gr_screen.clip_width = gr_screen.max_w;
	gr_screen.clip_height = gr_screen.max_h;
	if (Clip_listener != NULL) {
		Clip_listener();
	}
}

void gr_virtual_base_clip_end()
{
	if ((Base_clip_depth <= 0) || (--Base_clip_depth > 0) || !Base_clip_saved) {
		return;
	}
	Base_clip_saved = 0;
	gr_screen.offset_x = Saved_clip[0];		gr_screen.offset_y = Saved_clip[1];
	gr_screen.clip_left = Saved_clip[2];	gr_screen.clip_top = Saved_clip[3];
	gr_screen.clip_right = Saved_clip[4];	gr_screen.clip_bottom = Saved_clip[5];
	gr_screen.clip_width = Saved_clip[6];	gr_screen.clip_height = Saved_clip[7];
	Anchor = Saved_anchor;
	if (Clip_listener != NULL) {
		Clip_listener();
	}
}

// GodotOptions: the double size HUD (GrVirtual.h)
static int Hud_double = 0;
static int Hud_double_depth = 0;

void gr_virtual_set_hud_double(int on) { Hud_double = on ? 1 : 0; }
void gr_virtual_hud_double_begin() { Hud_double_depth++; }
void gr_virtual_hud_double_end() { if (Hud_double_depth > 0) { Hud_double_depth--; } }

float gr_virtual_hud_scale(int *pivot_x, int *pivot_y)
{
	*pivot_x = *pivot_y = 0;
	if (!Hud_double || !Hud_on || Fog_read || gr_virtual_in_wide_clip() || ((Hud_double_depth <= 0) && (Base_clip_depth <= 0))) {
		return 1;
	}
	// the centre group and the radar stay their normal size (Tom, 2026-11-04)
	if ((Anchor == HUD_ANCHOR_CENTRE) || (Hud_gauge_now == HUD_RADAR)) {
		return 1;
	}
	*pivot_x = (Anchor % 3) * gr_screen.max_w / 2;
	*pivot_y = (Anchor / 3) * gr_screen.max_h / 2;
	// three gauges share a column or corner with a doubled neighbour, so they double about a point that keeps them clear
	if (Hud_gauge_now == HUD_WINGMEN_STATUS) {
		*pivot_y = gr_screen.max_h * 37 / 100;		// to the top of the right column
	} else if (Hud_gauge_now == HUD_ESCORT_VIEW) {
		*pivot_y = gr_screen.max_h * 70 / 100;		// under the wingmen, above the weapons
	} else if (Hud_gauge_now == HUD_PLAYER_SHIELD_ICON) {
		*pivot_x += gr_screen.max_w * 132 / 1000;	// between the radar and the energy bars
	} else if (Hud_gauge_now == HUD_DIRECTIVES_VIEW) {
		// Claude: the left column has no room for them at 1.5 between the head and the target box, so they sit beside the head, tops level (#189)
		*pivot_x = -gr_screen.max_w * 764 / 1000;
		*pivot_y = gr_screen.max_h * 894 / 1000;
	}
	return 1.5f;	// Claude: 1.5 rather than 2 (Tom, 2026-11-04)
}
