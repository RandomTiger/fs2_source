/*
 * Claude: the Game Options screen and its two fs2.ini keys.  See GameOptions.h.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pstypes.h"
#include "GameOptions.h"
#include "GameSequence.h"
#include "FreeSpace.h"
#include "2d.h"
#include "font.h"
#include "AlphaColors.h"
#include "Key.h"
#include "Mouse.h"
#include "Joy.h"
#include "GameSnd.h"
#include "OsRegistry.h"
#include "cfile.h"
#include "DataSetProfile.h"
#include "Replay.h"
#include "Autotest.h"
#include "../Pad/PadGame.h"
#include "GrVirtual.h"
#include "LoadTimes.h"
#include "HostHooks.h"
#include "cfile.h"
#include "bmpman.h"
#include "player.h"
#include "MissionCampaign.h"
#include "Scoring.h"
#include "cmdline.h"
#include "Timer.h"

#define GAME_OPTIONS_KEY_FLIGHT		"FlightControl"
#define GAME_OPTIONS_KEY_GR			"GraphicsMode"
#define GAME_OPTIONS_KEY_DISPLAY	"DisplayResolution"	// GrVirtual.cpp reads it (GodotResolution, D-20261102-200805)

int game_switch_gr_mode(int res);	// FreeSpace.cpp (GodotResolution): the live GR mode switch, outside a mission

static char *Game_options_flight_names[] = { "", "Pad", "Joystick", "Mouse" };

static int Game_options_loaded = 0;
static int Game_options_flight = GAME_OPTIONS_FLIGHT_AUTO;	// the choice in force
static int Game_options_gr = -1;									// GR_640 / GR_1024 from the key, -1 for none

// a replay, a recording and autotest neither read nor write the keys (D-20261102-200105)
static int game_options_keys_active()
{
	return (Replay_mode == REPLAY_OFF) && !Autotest_harness;
}

static void game_options_load()
{
	char *s;
	int i;

	if (Game_options_loaded) {
		return;
	}
	Game_options_loaded = 1;
	if (!game_options_keys_active()) {
		return;
	}

	s = os_config_read_string(NULL, GAME_OPTIONS_KEY_FLIGHT, NULL);
	if (s) {
		for (i = GAME_OPTIONS_FLIGHT_PAD; i <= GAME_OPTIONS_FLIGHT_MOUSE; i++) {
			if (!stricmp(s, Game_options_flight_names[i])) {
				Game_options_flight = i;
			}
		}
	}

	s = os_config_read_string(NULL, GAME_OPTIONS_KEY_GR, NULL);
	if (s) {
		if (!stricmp(s, "GR_1024")) {
			Game_options_gr = GR_1024;
		} else if (!stricmp(s, "GR_640")) {
			Game_options_gr = GR_640;
		}
	}
}

// ---------------------------------------------------------------------------------------------------
// GodotOptions: the options of GameOptionList.h. One table: the command line (cmdline.cpp), the screen's rows and the console's
// commands all go through game_option_set.

typedef struct go_option {
	char *name, *key, *label;
	int dflt, host_only;
} go_option;

#define GO_OPTION(id, name, key, label, dflt, host_only) { name, key, label, dflt, host_only },
static go_option Go_options[GO_OPT_COUNT] = {
#include "GameOptionList.h"
};
#undef GO_OPTION

static int Go_option_value[GO_OPT_COUNT];

// what a change does beyond the value: only what cannot read game_option as it goes
static void go_option_apply(int id)
{
	if (id == GO_OPT_BIG_HUD) {
		gr_virtual_set_hud_double(Go_option_value[id]);
	}
}

void game_options_settings_init()
{
	int i;

	for (i = 0; i < GO_OPT_COUNT; i++) {
		Go_option_value[i] = Go_options[i].dflt;
		if (game_options_keys_active()) {
			Go_option_value[i] = os_config_read_uint(NULL, Go_options[i].key, Go_options[i].dflt) ? 1 : 0;
		}
		if (Cmdline_game_option[i] >= 0) {
			Go_option_value[i] = Cmdline_game_option[i];
		}
		go_option_apply(i);
	}
}

int game_option(int id)
{
	return Go_option_value[id];
}

int game_option_voiced_text_shown()
{
	return Go_option_value[GO_OPT_SUBTITLES] || !game_options_keys_active();
}

// Claude: GodotOptions, with subtitles off the head box and directives move up into the empty space, and the lines
// left at the top start beside the head box (Tom, 2026-11-05). Compared runs always show the line, so stay classic.
int game_option_hud_lift()
{
	return game_option_voiced_text_shown() ? 0 : ((gr_screen.res == GR_640) ? 30 : 48);
}

int game_option_hud_lines_x()
{
	return game_option_voiced_text_shown() ? 0 : 170;
}

void game_option_set(int id, int value)
{
	Go_option_value[id] = value ? 1 : 0;
	if (game_options_keys_active()) {
		os_config_write_uint(NULL, Go_options[id].key, Go_option_value[id]);
	}
	go_option_apply(id);
}

// a row on the screen: live play only, so a compared run sees the screen it always did; a host only option needs the host
static int go_option_on_screen(int id)
{
	return game_options_keys_active() && (!Go_options[id].host_only || gr_live_mode_change());
}

static void game_options_write_flight()
{
	if (!game_options_keys_active() || (Game_options_flight == GAME_OPTIONS_FLIGHT_AUTO)) {
		return;
	}
	os_config_write_string(NULL, GAME_OPTIONS_KEY_FLIGHT, Game_options_flight_names[Game_options_flight]);
}

int game_options_flight_device()
{
	game_options_load();
	return Game_options_flight;
}

void game_options_set_flight_device(int device)
{
	game_options_load();
	if ((device < GAME_OPTIONS_FLIGHT_AUTO) || (device > GAME_OPTIONS_FLIGHT_MOUSE)) {
		return;
	}
	int was = Game_options_flight;

	Game_options_flight = device;
	game_options_apply_flight();
	if (device != was) {
		joy_reassign();	// live: the pad/joystick ownership follows the choice at once (D-20261102-204804)
	}
}

int game_options_pad_flies()
{
	int d = game_options_flight_device();

	return (d == GAME_OPTIONS_FLIGHT_AUTO) || (d == GAME_OPTIONS_FLIGHT_PAD);
}

int game_options_joystick_flies()
{
	int d = game_options_flight_device();

	return (d == GAME_OPTIONS_FLIGHT_AUTO) || (d == GAME_OPTIONS_FLIGHT_JOYSTICK);
}

int game_options_joystick_yields_to_pad()
{
	return game_options_flight_device() != GAME_OPTIONS_FLIGHT_JOYSTICK;
}

void game_options_apply_flight()
{
	switch (game_options_flight_device()) {
	case GAME_OPTIONS_FLIGHT_MOUSE:
		Use_mouse_to_fly = 1;
		break;
	case GAME_OPTIONS_FLIGHT_PAD:
	case GAME_OPTIONS_FLIGHT_JOYSTICK:
		Use_mouse_to_fly = 0;
		break;
	}
}

void game_options_mouse_changed(int on)
{
	game_options_load();
	if (!game_options_keys_active()) {
		return;
	}
	if (on) {
		Game_options_flight = GAME_OPTIONS_FLIGHT_MOUSE;
	} else if (Game_options_flight == GAME_OPTIONS_FLIGHT_MOUSE) {
		Game_options_flight = pad_owns_joystick() ? GAME_OPTIONS_FLIGHT_PAD : GAME_OPTIONS_FLIGHT_JOYSTICK;
	} else {
		return;		// already a non-mouse choice, or none
	}
	game_options_write_flight();
}

// the GR key counts only on a data set that offers the choice (D-20261102-200103)
static int game_options_gr_key()
{
	game_options_load();
	if (!game_options_keys_active() || !Game_profile->gr_mode_choice) {
		return -1;
	}
	return Game_options_gr;
}

int game_options_gr_hi(int hi)
{
	int gr = game_options_gr_key();

	if (gr < 0) {
		return hi;
	}
	return (gr == GR_1024) ? 1 : 0;
}

int game_options_gr_mode(int mode)
{
	int gr = game_options_gr_key();

	return (gr < 0) ? mode : gr;
}

// ---------------------------------------------------------------------------------------------------
// the screen: plain text in the main hall's font and colours on black

#define GO_ITEM_FLIGHT		0
#define GO_ITEM_GR			1
#define GO_ITEM_BACK		2
#define GO_ITEM_RES			3		// value: an index in Go_res
#define GO_ITEM_OPTION		4		// Claude: GodotOptions, value: option id * 2 + the choice
#define GO_MAX_ITEMS		24		// Claude: GodotOptions, was 16

typedef struct go_item {
	int kind, value;
	int x, y, w, h;
	char *text;
} go_item;

static go_item Go_items[GO_MAX_ITEMS];
static int Go_num_items = 0;
static int Go_mouse_was_down = 0;
static int Go_gr_shown = GR_1024;		// the GR row's lit choice: the key's, else the mode running now
static color Go_fs1_text;				// FS1's main hall grey
static int Go_fs1_text_inited = 0;

// the Resolution row (DisplayResolution, D-20261102-200805 / 204803): Classic, Desktop, then window sizes that fit the monitor
typedef struct go_res {
	int mode, w, h;		// GR_DISPLAY_*; w x h for GR_DISPLAY_SIZE
	int gr;				// Claude: GodotOptions, a wide entry's GR mode (it also sets the height), else -1
	char text[24];
} go_res;

#define GO_MAX_RES			9	// Claude: GodotOptions, 7 and the two wide entries
#define GO_MAX_SIZES		3		// common sizes below the monitor's own

static go_res Go_res[GO_MAX_RES];
static int Go_num_res = 0;
static int Go_res_shown = 0;			// the lit entry: the one in force
static const int Go_common_sizes[][2] = {
	{ 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1080 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 },
};

// the size of the monitor the game's window is on (the primary one if that cannot be told)
static void game_options_monitor_size(int *w, int *h)
{
	MONITORINFO mi;
	HMONITOR mon = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);

	mi.cbSize = sizeof(mi);
	if (mon && GetMonitorInfo(mon, &mi)) {
		*w = mi.rcMonitor.right - mi.rcMonitor.left;
		*h = mi.rcMonitor.bottom - mi.rcMonitor.top;
		return;
	}
	*w = GetSystemMetrics(SM_CXSCREEN);
	*h = GetSystemMetrics(SM_CYSCREEN);
}

static void game_options_res_add(int mode, int w, int h, char *text)
{
	go_res *r;

	if (Go_num_res >= GO_MAX_RES) {
		return;
	}
	r = &Go_res[Go_num_res++];
	r->mode = mode;
	r->w = w;
	r->h = h;
	r->gr = -1;
	if (text) {
		strcpy(r->text, text);
	} else {
		sprintf(r->text, "%dx%d", w, h);
	}
}

// Claude: GodotOptions, a wide entry: this GR mode's height, as wide as the window's shape (GrVirtual.cpp virtual_size: the
// same sum), so it says what Desktop gives in that mode. mw x mh is the shape to use.
static void game_options_res_add_wide(int gr, int base_h, int mw, int mh)
{
	char text[24];
	int was = Go_num_res;

	sprintf(text, "%dx%d (wide)", (int)(((long long)base_h * mw + mh / 2) / mh), base_h);
	game_options_res_add(GR_DISPLAY_DESKTOP, 0, 0, text);
	if (Go_num_res > was) {
		Go_res[was].gr = gr;
	}
}

// Classic, Desktop, up to GO_MAX_SIZES common sizes that fit the monitor (the largest, in ascending order), the monitor's own
// size, and the size in force if it is none of those
static void game_options_res_list()
{
	int mw, mh, cw, ch, cur, i, first, n;
	char classic[24];	// Claude: GodotOptions, the Classic entry names the size the game draws at
	int num_common = (int)(sizeof(Go_common_sizes) / sizeof(Go_common_sizes[0]));

	Go_num_res = 0;
	sprintf(classic, "%s (4:3)", (Go_gr_shown == GR_640) ? "640x480" : "1024x768");
	game_options_res_add(GR_DISPLAY_CLASSIC, 0, 0, classic);
	game_options_res_add(GR_DISPLAY_DESKTOP, 0, 0, "Desktop");
	game_options_monitor_size(&mw, &mh);
	// Claude: GodotOptions, the wide entries (Tom): only where the mode changes live, and never in a compared run, whose screen stays as it was
	cur = gr_virtual_display_request(&cw, &ch);
	if (game_options_keys_active() && gr_live_mode_change() && Game_profile->gr_mode_choice) {
		gr_virtual_get_window(&cw, &ch);
		if ((cur == GR_DISPLAY_SIZE) || (cw <= 0) || (ch <= 0)) {	// a chosen size's shape is not the shape Desktop would give
			cw = mw;
			ch = mh;
		}
		if ((ch > 0) && ((long long)cw * 3 > (long long)ch * 4)) {
			game_options_res_add_wide(GR_640, 480, cw, ch);
			game_options_res_add_wide(GR_1024, 768, cw, ch);
		}
	}
	n = 0;
	first = num_common;
	for (i = num_common - 1; (i >= 0) && (n < GO_MAX_SIZES); i--) {
		if ((Go_common_sizes[i][0] <= mw) && (Go_common_sizes[i][1] <= mh) && ((Go_common_sizes[i][0] != mw) || (Go_common_sizes[i][1] != mh))) {
			first = i;
			n++;
		}
	}
	for (i = first; i < num_common; i++) {
		if ((Go_common_sizes[i][0] <= mw) && (Go_common_sizes[i][1] <= mh) && ((Go_common_sizes[i][0] != mw) || (Go_common_sizes[i][1] != mh))) {
			game_options_res_add(GR_DISPLAY_SIZE, Go_common_sizes[i][0], Go_common_sizes[i][1], NULL);
		}
	}
	if ((mw >= 320) && (mh >= 240)) {
		game_options_res_add(GR_DISPLAY_SIZE, mw, mh, NULL);
	}

	cur = gr_virtual_display_request(&cw, &ch);
	Go_res_shown = -1;
	for (i = 0; i < Go_num_res; i++) {
		if ((Go_res[i].mode == cur) && ((cur != GR_DISPLAY_SIZE) || ((Go_res[i].w == cw) && (Go_res[i].h == ch))) && ((Go_res[i].gr < 0) || (Go_res[i].gr == Go_gr_shown))) {	// Claude: GodotOptions, gr: the wide entry of this mode wins over plain Desktop
			Go_res_shown = i;
		}
	}
	if (Go_res_shown < 0) {
		game_options_res_add(GR_DISPLAY_SIZE, cw, ch, NULL);
		Go_res_shown = Go_num_res - 1;
	}
}

// The Resolution row as the host's own drop-down (Tom, GodotOptions; HostHooks.h dropdown_show, the Godot host's OptionButton).
// Only for live play: a backend without the hook (native D3D, headless) and a compared run (autotest, record, replay) keep the
// drawn row, so the menu tour and the replays see the screen they always did.
static int Go_dropdown_on = 0;
static int Go_from_pilot_screen = 0;	// opened by the pilot screen's button: Back returns there

static int game_options_dropdown_ok()
{
	return (Host_hooks.dropdown_show != NULL) && (Host_hooks.dropdown_hide != NULL) && game_options_keys_active();
}

static int game_options_res_y();

// shows (or moves, or refreshes) the drop-down on the Resolution row, in this art mode's base pixels
static void game_options_dropdown_show()
{
	const char *labels[GO_MAX_RES];
	int i, h;

	if (!Go_dropdown_on) {
		return;
	}
	for (i = 0; i < Go_num_res; i++) {
		labels[i] = Go_res[i].text;
	}
	gr_set_font(FONT1);
	h = gr_get_font_height() + 8;
	Host_hooks.dropdown_show(gr_screen.max_w / 2, game_options_res_y() - 4, gr_screen.max_w * 30 / 100, h, labels, Go_num_res, Go_res_shown);
}

static void game_options_dropdown_hide()
{
	if (Go_dropdown_on) {
		Host_hooks.dropdown_hide();
	}
}

// the Resolution row's choice: the key, then, where the backend changes mode live, the canvas at once (GrVirtual: the host
// follows fs2_display_request); otherwise at the next start. A compared run (the tour) neither writes nor applies it.
static void game_options_write_display(int mode, int w, int h)
{
	char text[24];

	if (mode == GR_DISPLAY_CLASSIC) {
		strcpy(text, "classic");
	} else if (mode == GR_DISPLAY_DESKTOP) {
		strcpy(text, "desktop");
	} else {
		sprintf(text, "%dx%d", w, h);
	}
	os_config_write_string(NULL, GAME_OPTIONS_KEY_DISPLAY, text);
	if (gr_live_mode_change()) {
		gr_virtual_read_config();
	}
}

static void game_options_gr_choose(int gr);	// Claude: GodotOptions

static void game_options_apply_res(int i)
{
	go_res *r = &Go_res[i];

	Go_res_shown = i;
	if (!game_options_keys_active()) {
		return;
	}
	game_options_write_display(r->mode, r->w, r->h);
	if (r->gr >= 0) {
		game_options_gr_choose(r->gr);	// Claude: GodotOptions, a wide entry also picks the mode (rebuilds Go_res: r is stale after this)
	}
}

// a Resolution choice, from a click on the drawn row, the drop-down or the menu tour
static void game_options_res_choose(int i)
{
	if ((i < 0) || (i >= Go_num_res)) {
		return;
	}
	gamesnd_play_iface(SND_USER_SELECT);
	game_options_apply_res(i);
	game_options_dropdown_show();	// the drop-down lights the entry in force
}

static color *game_options_text_color()
{
	if (Cfile_fs1_data) {
		if (!Go_fs1_text_inited) {
			gr_init_alphacolor(&Go_fs1_text, 160, 160, 160, 255);
			Go_fs1_text_inited = 1;
		}
		return &Go_fs1_text;
	}
	return &Color_white;
}

static void game_options_add(int kind, int value, int x, int y, char *text)
{
	go_item *it;

	if (Go_num_items >= GO_MAX_ITEMS) {
		return;
	}
	it = &Go_items[Go_num_items++];
	it->kind = kind;
	it->value = value;
	it->x = x;
	it->y = y;
	it->text = text;
	gr_get_string_size(&it->w, &it->h, text);
}

// the flight choice the screen lights: the key's, else what flies now
static int game_options_flight_shown()
{
	int d = game_options_flight_device();

	if (d != GAME_OPTIONS_FLIGHT_AUTO) {
		return d;
	}
	if (Use_mouse_to_fly) {
		return GAME_OPTIONS_FLIGHT_MOUSE;
	}
	return pad_owns_joystick() ? GAME_OPTIONS_FLIGHT_PAD : GAME_OPTIONS_FLIGHT_JOYSTICK;
}

// the Resolution row's line: below the graphics mode row where there is one
static int game_options_res_y()
{
	return gr_screen.max_h * (Game_profile->gr_mode_choice ? 56 : 42) / 100;
}

// Claude: GodotOptions, the line of the nth option row
static int go_option_row_y(int row)
{
	return gr_screen.max_h * (72 + row * 6) / 100;
}

// lays the screen out for this art mode: fractions of the screen, so GR_640 and GR_1024 match
static void game_options_layout()
{
	int w = gr_screen.max_w, h = gr_screen.max_h;
	int cx = w / 2, step = w / 8;
	int i, yr, lh;

	gr_set_font(FONT1);
	lh = gr_get_font_height() + 6;
	Go_num_items = 0;
	game_options_add(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_PAD, cx, h * 30 / 100, "Pad");
	game_options_add(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_JOYSTICK, cx + step, h * 30 / 100, "Joystick");
	game_options_add(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_MOUSE, cx + step * 2, h * 30 / 100, "Mouse");
	if (Game_profile->gr_mode_choice) {
		game_options_add(GO_ITEM_GR, GR_1024, cx, h * 42 / 100, "1024 x 768");
		game_options_add(GO_ITEM_GR, GR_640, cx + step * 3 / 2, h * 42 / 100, "640 x 480");
	}
	// Resolution: Classic and Desktop on the first line, the sizes on the second; or the host's drop-down instead
	yr = game_options_res_y();
	for (i = 0; !Go_dropdown_on && (i < Go_num_res); i++) {
		if (i < 2) {
			game_options_add(GO_ITEM_RES, i, cx + i * step * 3 / 2, yr, Go_res[i].text);
		} else {
			game_options_add(GO_ITEM_RES, i, cx + (i - 2) * step, yr + lh, Go_res[i].text);
		}
	}
	for (i = 0, yr = 0; i < GO_OPT_COUNT; i++) {	// Claude: GodotOptions
		if (go_option_on_screen(i)) {
			game_options_add(GO_ITEM_OPTION, i * 2 + 1, cx, go_option_row_y(yr), "On");
			game_options_add(GO_ITEM_OPTION, i * 2, cx + step, go_option_row_y(yr), "Off");
			yr++;
		}
	}
	game_options_add(GO_ITEM_BACK, 0, w * 8 / 100, h * 90 / 100, "Back");
}

static int game_options_item_at(int mx, int my)
{
	int i;
	go_item *it;

	for (i = 0; i < Go_num_items; i++) {
		it = &Go_items[i];
		if ((mx >= it->x - 4) && (mx < it->x + it->w + 4) && (my >= it->y - 3) && (my < it->y + it->h + 3)) {
			return i;
		}
	}
	return -1;
}

static int game_options_item_current(go_item *it)
{
	if (it->kind == GO_ITEM_FLIGHT) {
		return it->value == game_options_flight_shown();
	}
	if (it->kind == GO_ITEM_GR) {
		return it->value == Go_gr_shown;
	}
	if (it->kind == GO_ITEM_RES) {
		return it->value == Go_res_shown;
	}
	if (it->kind == GO_ITEM_OPTION) {	// Claude: GodotOptions
		return game_option(it->value / 2) == (it->value & 1);
	}
	return 0;
}

// Claude: GodotOptions, the GR choice, moved here from game_options_pick for the wide entries too
static void game_options_gr_choose(int gr)
{
	Go_gr_shown = gr;
	game_options_res_list();	// Claude: GodotOptions, the Classic entry follows the mode
	if (game_options_keys_active()) {
		os_config_write_string(NULL, GAME_OPTIONS_KEY_GR, (gr == GR_1024) ? "GR_1024" : "GR_640");
		game_options_load();
		Game_options_gr = gr;	// the key as read: the screen opened again lights the new choice
	}
	// live where the backend can (D-20261102-204802, 204806); only on this screen, never in a mission (the console can ask from anywhere).
	// The screen is laid out again in the new mode: its coordinates are fractions of gr_screen, its font is FONT1 in both modes.
	if (gr_live_mode_change() && (gr != gr_screen.res) && (gameseq_get_state() == GS_STATE_GAME_OPTIONS)) {
		game_options_dropdown_hide();	// the host's drop-down goes while the mode switches, and comes back on the new layout
		if (game_switch_gr_mode(gr)) {
			game_options_layout();
			Go_mouse_was_down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
		}
		game_options_dropdown_show();
	}
}

static void game_options_pick(go_item *it)
{
	gamesnd_play_iface(SND_USER_SELECT);
	if (it->kind == GO_ITEM_FLIGHT) {
		int was;

		game_options_load();
		was = Game_options_flight;
		Game_options_flight = it->value;
		game_options_apply_flight();
		game_options_write_flight();
		if (it->value != was) {
			joy_reassign();	// live, no restart: the pad/joystick ownership is taken again (D-20261102-204804)
		}
	} else if (it->kind == GO_ITEM_GR) {
		game_options_gr_choose(it->value);	// Claude: GodotOptions. 'it' is stale after this
	} else if (it->kind == GO_ITEM_RES) {
		game_options_apply_res(it->value);
	} else if (it->kind == GO_ITEM_OPTION) {	// Claude: GodotOptions
		game_option_set(it->value / 2, it->value & 1);
	}
}

void game_options_screen_back()
{
	gamesnd_play_iface(SND_USER_SELECT);
	if (Go_from_pilot_screen) {
		Go_from_pilot_screen = 0;
		gameseq_post_event(GS_EVENT_INITIAL_PLAYER_SELECT);
	} else {
		gameseq_post_event(GS_EVENT_MAIN_MENU);
	}
}

// the screen's item of this kind and value, or NULL
static go_item *game_options_find(int kind, int value)
{
	int i;

	for (i = 0; i < Go_num_items; i++) {
		if ((Go_items[i].kind == kind) && (Go_items[i].value == value)) {
			return &Go_items[i];
		}
	}
	return NULL;
}

static int Go_tour_flight_was = -1, Go_tour_mouse_was = 0, Go_tour_res_was = -1;

int game_options_tour_step(int step)
{
	go_item *it;
	int gr, next, i;

	switch (step) {
	case GAME_OPTIONS_TOUR_GR_640:
	case GAME_OPTIONS_TOUR_GR_1024:
		gr = (step == GAME_OPTIONS_TOUR_GR_640) ? GR_640 : GR_1024;
		it = game_options_find(GO_ITEM_GR, gr);
		if (!it || !gr_live_mode_change()) {
			return 0;
		}
		game_options_pick(it);
		autotest_check(gr_screen.res == gr, (gr == GR_640) ? "Game options: live GR switch to 640" : "Game options: live GR switch to 1024");
		for (next = 0, i = 0; i < LT_LATE_NUM; i++) {
			next += Loadtime_late_count[i];
		}
		autotest_check(next == 0, "Game options: GR switch, late-load counters 0");
		return 1;

	case GAME_OPTIONS_TOUR_DEVICE:
		game_options_load();
		Go_tour_flight_was = Game_options_flight;
		Go_tour_mouse_was = Use_mouse_to_fly;
		next = game_options_flight_shown() % 3 + 1;	// Pad, Joystick, Mouse, Pad
		it = game_options_find(GO_ITEM_FLIGHT, next);
		if (!it) {
			return 0;
		}
		game_options_pick(it);
		autotest_check(game_options_flight_shown() == next, "Game options: live flight device switch");
		return 1;

	case GAME_OPTIONS_TOUR_DEVICE_BACK:
		if (Go_tour_flight_was < 0) {
			return 0;
		}
		next = (Game_options_flight != Go_tour_flight_was);
		Game_options_flight = Go_tour_flight_was;
		Use_mouse_to_fly = Go_tour_mouse_was;
		Go_tour_flight_was = -1;
		if (next) {
			joy_reassign();
		}
		return 1;

	case GAME_OPTIONS_TOUR_RES:
		if (Go_num_res < 2) {
			return 0;
		}
		// the game-side choice, not a click: the same with the drawn row and with the host's drop-down
		Go_tour_res_was = Go_res_shown;
		game_options_res_choose((Go_res_shown + 1) % Go_num_res);
		return 1;

	case GAME_OPTIONS_TOUR_RES_BACK:
		if ((Go_tour_res_was < 0) || (Go_tour_res_was >= Go_num_res)) {
			return 0;
		}
		next = Go_tour_res_was;
		Go_tour_res_was = -1;
		game_options_res_choose(next);
		return 1;
	}
	return 0;
}

void game_options_screen_init()
{
	int gr = game_options_gr_key();

	Go_gr_shown = (gr >= 0) ? gr : gr_screen.res;
	Go_mouse_was_down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
	game_options_res_list();
	Go_dropdown_on = game_options_dropdown_ok();
	Host_dropdown_chosen = -1;
	game_options_layout();
	game_options_dropdown_show();
}

void game_options_screen_close()
{
	game_options_dropdown_hide();
	Go_dropdown_on = 0;
	Host_dropdown_chosen = -1;
	Go_num_items = 0;
}

void game_options_screen_do_frame(float /*frametime*/)
{
	int k, mx, my, over, down, i, w;
	go_item *it;
	color *text = game_options_text_color();

	k = game_check_key();
	if (k == KEY_ESC) {
		game_options_screen_back();
	}

	// the host's drop-down: the player's pick, handed over from the host's events
	if (Host_dropdown_chosen >= 0) {
		i = Host_dropdown_chosen;
		Host_dropdown_chosen = -1;
		if (Go_dropdown_on && (i != Go_res_shown)) {
			game_options_res_choose(i);
		}
	}

	mouse_get_pos(&mx, &my);
	over = game_options_item_at(mx, my);
	down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
	if (!down && Go_mouse_was_down && (over >= 0)) {	// a click is a release over the choice, as the main hall's
		if (Go_items[over].kind == GO_ITEM_BACK) {
			game_options_screen_back();
		} else {
			game_options_pick(&Go_items[over]);
		}
	}
	Go_mouse_was_down = down;

	gr_reset_clip();
	gr_clear();
	gr_set_font(FONT1);

	gr_set_color_fast(&Color_bright_white);
	gr_get_string_size(&w, NULL, "GAME OPTIONS");
	gr_string((gr_screen.max_w - w) / 2, gr_screen.max_h * 12 / 100, "GAME OPTIONS");

	gr_set_color_fast(text);
	gr_string(gr_screen.max_w * 12 / 100, gr_screen.max_h * 30 / 100, "In-game flight control");
	if (Game_profile->gr_mode_choice) {
		gr_string(gr_screen.max_w * 12 / 100, gr_screen.max_h * 42 / 100, "Graphics mode");
		if (!gr_live_mode_change()) {	// native D3D and headless: at the next start (D-20261102-204806)
			gr_string(gr_screen.max_w / 2, gr_screen.max_h * 42 / 100 + gr_get_font_height() + 6, "Takes effect next time the game starts");
		}
	}
	gr_string(gr_screen.max_w * 12 / 100, game_options_res_y(), "Resolution");
	if (!gr_live_mode_change()) {
		gr_string(gr_screen.max_w / 2, game_options_res_y() + (gr_get_font_height() + 6) * 2, "Takes effect next time the game starts");
	}

	for (i = 0, w = 0; i < GO_OPT_COUNT; i++) {	// Claude: GodotOptions
		if (go_option_on_screen(i)) {
			gr_string(gr_screen.max_w * 12 / 100, go_option_row_y(w++), Go_options[i].label);
		}
	}

	for (i = 0; i < Go_num_items; i++) {
		it = &Go_items[i];
		if (game_options_item_current(it)) {
			gr_set_color_fast(&Color_bright_white);
			gr_string(it->x, it->y, it->text);
			gr_line(it->x, it->y + it->h + 1, it->x + it->w - 1, it->y + it->h + 1);
		} else {
			gr_set_color_fast((i == over) ? &Color_bright_white : text);
			gr_string(it->x, it->y, it->text);
		}
	}

	gr_flip();
}

// ---------------------------------------------------------------------------------------------------
// the pilot screen's Options button: the word, bottom left. The black column beside the picture is outside what the game can draw
// on or point at, so a host with a side button (HostHooks.h side_button, the Godot host) puts it there. A host without one
// (native D3D) gets the word drawn in the picture's bottom left corner.

#define GO_BUTTON_MARGIN	8
#define GO_BUTTON_STALE_MS	250		// a host click older than this came while a popup held the screen: not meant for the button

static int Go_button_on = 0;
static int Go_button_host = 0;			// the host's button is up
static int Go_button_was_down = 0;
static int Go_button_last_do = 0;

static void game_options_pilot_button_click()
{
	Go_from_pilot_screen = 1;
	gamesnd_play_iface(SND_IFACE_MOUSE_CLICK);
	gameseq_post_event(GS_EVENT_GAME_OPTIONS);
}

void game_options_pilot_button_init()
{
	Go_button_on = game_options_keys_active();
	Go_from_pilot_screen = 0;
	Go_button_was_down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
	Go_button_host = 0;
	Go_button_last_do = timer_get_milliseconds();
	Host_side_button_pressed = 0;
	if (Go_button_on && (Host_hooks.side_button != NULL)) {
		Go_button_host = 1;
		Host_hooks.side_button("OPTIONS");
	}
}

void game_options_pilot_button_do()
{
	int w = 0, h = 0, x, y, mx, my, over, down, now;

	if (!Go_button_on) {
		return;
	}
	if (Go_button_host) {
		now = timer_get_milliseconds();
		if (Host_side_button_pressed && ((now - Go_button_last_do) < GO_BUTTON_STALE_MS) && !Go_from_pilot_screen) {
			game_options_pilot_button_click();
		}
		Host_side_button_pressed = 0;
		Go_button_last_do = now;
		return;
	}
	gr_set_font(FONT1);
	gr_get_string_size(&w, &h, "OPTIONS");
	x = GO_BUTTON_MARGIN;
	y = gr_screen.max_h - h - GO_BUTTON_MARGIN;
	if (Cfile_fs1_data) {
		y -= (18 * gr_screen.max_h) / 480;	// FS1's copyright line runs this far out: sit above it
	}

	mouse_get_pos(&mx, &my);
	down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
	over = (mx >= x - 4) && (mx < x + w + 4) && (my >= y - 4) && (my < y + h + 4);

	gr_set_color_fast(over ? &Color_bright_white : game_options_text_color());
	gr_string(x, y, "OPTIONS");

	if (over && !down && Go_button_was_down && !Go_from_pilot_screen) {
		game_options_pilot_button_click();
	}
	Go_button_was_down = down;
}

void game_options_pilot_button_close()
{
	if (Go_button_host) {
		Host_hooks.side_button(NULL);
		Go_button_host = 0;
	}
}

int game_options_pilot_button_leaving()
{
	return Go_from_pilot_screen;
}

// ---------------------------------------------------------------------------------------------------
// the Godot host's console

static void go_console_print(char *out, int out_size, const char *text)
{
	int len = (int)strlen(out);

	if (len < out_size - 1) {
		strncat(out, text, out_size - 1 - len);
	}
}

static void go_console_apply(int mode, int w, int h, char *out, int out_size)
{
	char line[64];

	game_options_write_display(mode, w, h);
	game_options_res_list();
	game_options_dropdown_show();
	if (mode == GR_DISPLAY_SIZE) {
		sprintf(line, "resolution %dx%d", w, h);
	} else {
		sprintf(line, "resolution %s", (mode == GR_DISPLAY_CLASSIC) ? "classic" : "desktop");
	}
	go_console_print(out, out_size, line);
}

static void go_console_resolution(const char *arg, char *out, int out_size)
{
	char line[64];
	int i, w, h, mw, mh, end = 0;
	int num_common = (int)(sizeof(Go_common_sizes) / sizeof(Go_common_sizes[0]));

	if (gameseq_get_state() != GS_STATE_GAME_OPTIONS) {
		Go_gr_shown = gr_screen.res;	// Claude: GodotOptions, away from the screen the list follows the mode running
	}
	game_options_res_list();

	if (arg[0] == 0) {
		for (i = 0; i < Go_num_res; i++) {
			sprintf(line, "%c  %s%s\n", 'a' + i, Go_res[i].text, (i == Go_res_shown) ? "  *" : "");
			go_console_print(out, out_size, line);
		}
		return;
	}

	// a single letter: an index in the list
	if ((arg[1] == 0) && (tolower(arg[0]) >= 'a') && (tolower(arg[0]) <= 'z')) {
		i = tolower(arg[0]) - 'a';
		if (i >= Go_num_res) {
			sprintf(line, "no resolution %c: 'resolution' lists them", arg[0]);
			go_console_print(out, out_size, line);
			return;
		}
		{
			int gr = Go_res[i].gr;	// Claude: GodotOptions, a wide entry also picks the mode

			go_console_apply(Go_res[i].mode, Go_res[i].w, Go_res[i].h, out, out_size);
			if (gr >= 0) {
				game_options_gr_choose(gr);
				if (gr != gr_screen.res) {
					go_console_print(out, out_size, ", that height at the next start");
				}
			}
		}
		return;
	}

	// w x h
	if ((sscanf(arg, "%dx%d%n", &w, &h, &end) == 2) && (arg[end] == 0)) {
		if ((w < 320) || (h < 240) || (w > 16384) || (h > 16384)) {
			go_console_print(out, out_size, "resolution must be from 320x240 to 16384x16384");
			return;
		}
		go_console_apply(GR_DISPLAY_SIZE, w, h, out, out_size);
		return;
	}

	// a height: the list first, then the common sizes that fit the monitor, the shape nearest the monitor's
	if ((sscanf(arg, "%d%n", &h, &end) == 1) && (arg[end] == 0)) {
		int best_w = 0;
		float best = 0.0f, want;

		game_options_monitor_size(&mw, &mh);
		want = (mh > 0) ? (float)mw / (float)mh : 1.0f;
		for (i = 0; i < Go_num_res; i++) {
			if ((Go_res[i].mode == GR_DISPLAY_SIZE) && (Go_res[i].h == h)) {
				float d = (float)Go_res[i].w / (float)h - want;
				d = (d < 0.0f) ? -d : d;
				if (!best_w || (d < best)) {
					best_w = Go_res[i].w;
					best = d;
				}
			}
		}
		for (i = 0; i < num_common; i++) {
			if ((Go_common_sizes[i][1] == h) && (Go_common_sizes[i][0] <= mw) && (h <= mh)) {
				float d = (float)Go_common_sizes[i][0] / (float)h - want;
				d = (d < 0.0f) ? -d : d;
				if (!best_w || (d < best)) {
					best_w = Go_common_sizes[i][0];
					best = d;
				}
			}
		}
		if (best_w) {
			go_console_apply(GR_DISPLAY_SIZE, best_w, h, out, out_size);
		} else {
			sprintf(line, "no resolution of height %d: try resolution <w>x<h>", h);
			go_console_print(out, out_size, line);
		}
		return;
	}

	go_console_print(out, out_size, "usage: resolution, resolution <letter>, resolution <height>, resolution <w>x<h>");
}

extern int mission_campaign_savefile_save();	// MissionCampaign.cpp

static void go_console_unlock_missions(char *out, int out_size)
{
	char line[64];
	int i, count = 0;

	if ((Player == NULL) || (Player->callsign[0] == 0) || (gameseq_get_state() == GS_STATE_INITIAL_PLAYER_SELECT)) {
		go_console_print(out, out_size, "no pilot yet: select a pilot first");
		return;
	}
	// The game loads the campaign only on the way into the ready room, so in the main hall it may not be here yet.
	if ((Campaign.num_missions <= 0) && (mission_load_up_campaign() || (Campaign.num_missions <= 0))) {
		go_console_print(out, out_size, "this pilot's campaign would not load");
		return;
	}
	for (i = 0; i < Campaign.num_missions; i++) {
		cmission *cm = &Campaign.missions[i];

		if (!cm->completed) {
			cm->completed = 1;
			if (cm->num_goals < 0) {
				cm->num_goals = 0;
			}
			if (cm->num_events < 0) {
				cm->num_events = 0;
			}
			init_scoring_element(&cm->stats);
			count++;
		}
	}
	Campaign.num_missions_completed = Campaign.num_missions;
	mission_campaign_savefile_save();
	sprintf(line, "%d missions unlocked, %d in the campaign", count, Campaign.num_missions);
	go_console_print(out, out_size, line);
}

// Claude: GodotOptions, on, off, or empty to toggle; returns 0 or 1, or -1 for something else
static int go_console_on_off(const char *arg, int now)
{
	if (arg[0] == 0) {
		return !now;
	}
	if (!stricmp(arg, "on") || !strcmp(arg, "1")) {
		return 1;
	}
	if (!stricmp(arg, "off") || !strcmp(arg, "0")) {
		return 0;
	}
	return -1;
}

// the word at the start of line, whole: the rest of the line, or NULL
static const char *go_console_word(const char *line, const char *word)
{
	size_t n = strlen(word);

	if (strnicmp(line, word, n) || ((line[n] != 0) && (line[n] != ' '))) {
		return NULL;
	}
	line += n;
	while (*line == ' ') {
		line++;
	}
	return line;
}

// Claude: GodotOptions, an option of GameOptionList.h: 1 if the line was one
static int go_console_option(const char *line, char *out, int out_size)
{
	char text[64];
	const char *arg;
	int i, v;

	for (i = 0; i < GO_OPT_COUNT; i++) {
		arg = go_console_word(line, Go_options[i].name);
		if (arg) {
			v = go_console_on_off(arg, game_option(i));
			if (v < 0) {
				sprintf(text, "usage: %s, %s on, %s off", Go_options[i].name, Go_options[i].name, Go_options[i].name);
			} else {
				game_option_set(i, v);
				sprintf(text, "%s %s", Go_options[i].name, v ? "on" : "off");
			}
			go_console_print(out, out_size, text);
			return 1;
		}
	}
	return 0;
}

// Claude: GodotOptions, the show command (RenderShow.h)
#define RS_ITEM(id, name) name,
static char *Render_show_names[RS_COUNT] = {
#include "RenderShowList.h"
};
#undef RS_ITEM

static void go_console_show(const char *arg, char *out, int out_size)
{
	char text[64];
	const char *rest;
	int i, v;

	if (arg[0] == 0) {
		for (i = 0; i < RS_COUNT; i++) {
			sprintf(text, "%-16s%s\n", Render_show_names[i], Render_show[i] ? "on" : "OFF");
			go_console_print(out, out_size, text);
		}
		return;
	}
	rest = go_console_word(arg, "all");
	if (rest) {
		v = go_console_on_off(rest, 1);
		if ((rest[0] == 0) || (v < 0)) {
			go_console_print(out, out_size, "usage: show all on, show all off");
			return;
		}
		for (i = 0; i < RS_COUNT; i++) {
			Render_show[i] = v;
		}
		go_console_print(out, out_size, v ? "show all on" : "show all off");
		return;
	}
	for (i = 0; i < RS_COUNT; i++) {
		rest = go_console_word(arg, Render_show_names[i]);
		if (rest) {
			v = go_console_on_off(rest, Render_show[i]);
			if (v < 0) {
				break;
			}
			Render_show[i] = v;
			sprintf(text, "show %s %s", Render_show_names[i], v ? "on" : "off");
			go_console_print(out, out_size, text);
			return;
		}
	}
	go_console_print(out, out_size, "usage: show (lists them), show <name>, show <name> on, show <name> off, show all on, show all off");
}


int game_options_console_command(const char *line, char *out, int out_size)
{
	out[0] = 0;
	while (*line == ' ') {
		line++;
	}
	if (!game_options_keys_active()) {
		return 0;
	}
	if (!strnicmp(line, "resolution", 10) && ((line[10] == 0) || (line[10] == ' '))) {
		line += 10;
		while (*line == ' ') {
			line++;
		}
		go_console_resolution(line, out, out_size);
		return 1;
	}
	if (go_console_option(line, out, out_size)) {	// Claude: GodotOptions
		return 1;
	}
	if (go_console_word(line, "show")) {	// Claude: GodotOptions
		go_console_show(go_console_word(line, "show"), out, out_size);
		return 1;
	}
	if (!stricmp(line, "unlock missions")) {
		go_console_unlock_missions(out, out_size);
		return 1;
	}
	return 0;
}
