/*
 * Claude: the Game Options screen and its two fs2.ini keys.  See GameOptions.h.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

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
#define GO_MAX_ITEMS		16

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
	char text[24];
} go_res;

#define GO_MAX_RES			7
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
	if (text) {
		strcpy(r->text, text);
	} else {
		sprintf(r->text, "%dx%d", w, h);
	}
}

// Classic, Desktop, up to GO_MAX_SIZES common sizes that fit the monitor (the largest, in ascending order), the monitor's own
// size, and the size in force if it is none of those
static void game_options_res_list()
{
	int mw, mh, cw, ch, cur, i, first, n;
	int num_common = (int)(sizeof(Go_common_sizes) / sizeof(Go_common_sizes[0]));

	Go_num_res = 0;
	game_options_res_add(GR_DISPLAY_CLASSIC, 0, 0, "Classic (4:3)");
	game_options_res_add(GR_DISPLAY_DESKTOP, 0, 0, "Desktop");
	game_options_monitor_size(&mw, &mh);
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
		if ((Go_res[i].mode == cur) && ((cur != GR_DISPLAY_SIZE) || ((Go_res[i].w == cw) && (Go_res[i].h == ch)))) {
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
static void game_options_apply_res(int i)
{
	go_res *r = &Go_res[i];
	char text[24];

	Go_res_shown = i;
	if (!game_options_keys_active()) {
		return;
	}
	if (r->mode == GR_DISPLAY_CLASSIC) {
		strcpy(text, "classic");
	} else if (r->mode == GR_DISPLAY_DESKTOP) {
		strcpy(text, "desktop");
	} else {
		sprintf(text, "%dx%d", r->w, r->h);
	}
	os_config_write_string(NULL, GAME_OPTIONS_KEY_DISPLAY, text);
	if (gr_live_mode_change()) {
		gr_virtual_read_config();
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
	return 0;
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
		int gr = it->value;

		Go_gr_shown = gr;
		if (game_options_keys_active()) {
			os_config_write_string(NULL, GAME_OPTIONS_KEY_GR, (gr == GR_1024) ? "GR_1024" : "GR_640");
			game_options_load();
			Game_options_gr = gr;	// the key as read: the screen opened again lights the new choice
		}
		// live where the backend can (D-20261102-204802, 204806); the screen is never in a mission. The screen is laid out again
		// in the new mode: its coordinates are fractions of gr_screen, its font is FONT1 in both modes. 'it' is stale after this.
		if (gr_live_mode_change() && (gr != gr_screen.res)) {
			game_options_dropdown_hide();	// the host's drop-down goes while the mode switches, and comes back on the new layout
			if (game_switch_gr_mode(gr)) {
				game_options_layout();
				Go_mouse_was_down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
			}
			game_options_dropdown_show();
		}
	} else if (it->kind == GO_ITEM_RES) {
		game_options_apply_res(it->value);
	}
}

void game_options_screen_back()
{
	gamesnd_play_iface(SND_USER_SELECT);
	gameseq_post_event(GS_EVENT_MAIN_MENU);
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
// the main hall's OPTIONS label: bottom left, level with the version number in the bottom right

static void game_options_hall_label_pos(int *x, int *y, int *w, int *h)
{
	gr_set_font(FONT1);
	gr_get_string_size(w, h, "OPTIONS");
	*x = Cfile_fs1_data ? 10 : 12;
	*y = gr_screen.max_h - (Cfile_fs1_data ? 13 : 12);
}

int game_options_hall_label_hit(int x, int y)
{
	int lx, ly, lw, lh;

	game_options_hall_label_pos(&lx, &ly, &lw, &lh);
	return (x >= lx - 6) && (x < lx + lw + 6) && (y >= ly - 6) && (y < gr_screen.max_h);
}

void game_options_hall_label_draw(int hover)
{
	int x, y, w, h;

	game_options_hall_label_pos(&x, &y, &w, &h);
	gr_set_color_fast(hover ? &Color_bright_white : game_options_text_color());
	gr_string(x, y, "OPTIONS");
}
