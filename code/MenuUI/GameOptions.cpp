/*
 * Claude: the Game Options screen and its two fs2.ini keys.  See GameOptions.h.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

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
#include "PcxUtils.h"

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
	char *choices;		// TextureFiltering: NULL for on/off, else the choices' labels split by |
} go_option;

#define GO_OPTION(id, name, key, label, dflt, host_only, choices) { name, key, label, dflt, host_only, choices },
static go_option Go_options[GO_OPT_COUNT] = {
#include "GameOptionList.h"
};
#undef GO_OPTION

static int Go_option_value[GO_OPT_COUNT];

// TextureFiltering (#315): a choice option's labels (the screen's) and names (the console's and command line's: the label in lower
// case without spaces), split out of its choices string once
#define GO_MAX_CHOICES		8
#define GO_CHOICE_LEN		24
static char Go_choice_label[GO_OPT_COUNT][GO_MAX_CHOICES][GO_CHOICE_LEN];
static char Go_choice_name[GO_OPT_COUNT][GO_MAX_CHOICES][GO_CHOICE_LEN];
static int Go_choice_count[GO_OPT_COUNT];	// 0 for an on/off option
static int Go_choices_split = 0;

static void go_choices_split()
{
	int i, n, k, j;
	const char *s;

	if (Go_choices_split) {
		return;
	}
	Go_choices_split = 1;
	for (i = 0; i < GO_OPT_COUNT; i++) {
		Go_choice_count[i] = 0;
		for (s = Go_options[i].choices; s && *s && (Go_choice_count[i] < GO_MAX_CHOICES); ) {
			n = Go_choice_count[i]++;
			for (k = 0; *s && (*s != '|'); s++) {
				if (k < GO_CHOICE_LEN - 1) {
					Go_choice_label[i][n][k++] = *s;
				}
			}
			Go_choice_label[i][n][k] = 0;
			for (k = 0, j = 0; Go_choice_label[i][n][k]; k++) {
				if (Go_choice_label[i][n][k] != ' ') {
					Go_choice_name[i][n][j++] = (char)tolower((unsigned char)Go_choice_label[i][n][k]);
				}
			}
			Go_choice_name[i][n][j] = 0;
			if (*s == '|') {
				s++;
			}
		}
	}
}

// the number of values the option takes: 2 for on/off
static int go_option_values(int id)
{
	go_choices_split();
	return Go_choice_count[id] ? Go_choice_count[id] : 2;
}

static int go_option_clamp(int id, int value)
{
	if (!Go_options[id].choices) {
		return value ? 1 : 0;
	}
	return ((value < 0) || (value >= go_option_values(id))) ? Go_options[id].dflt : value;
}

int game_option_parse(int id, const char *arg, int empty)
{
	int i;

	if ((id < 0) || (id >= GO_OPT_COUNT) || !arg) {
		return -1;
	}
	if (arg[0] == 0) {
		return empty;
	}
	if (!Go_options[id].choices) {
		if (!stricmp(arg, "on")) {
			return 1;
		}
		if (!stricmp(arg, "off")) {
			return 0;
		}
		return isdigit((unsigned char)arg[0]) ? (atoi(arg) != 0) : -1;
	}
	go_choices_split();
	for (i = 0; i < Go_choice_count[id]; i++) {
		if (!stricmp(arg, Go_choice_name[id][i])) {
			return i;
		}
	}
	if (isdigit((unsigned char)arg[0]) && (atoi(arg) < Go_choice_count[id])) {
		return atoi(arg);
	}
	return -1;
}

// what a change does beyond the value: only what cannot read game_option as it goes
static void go_option_apply(int id)
{
	if (id == GO_OPT_BIG_HUD) {
		gr_virtual_set_hud_double(Go_option_value[id]);
	}
	if ((id == GO_OPT_TEXTURE_FILTER) || (id == GO_OPT_MIP_BIAS)) {	// TextureFiltering: the host's sampling; a backend without it ignores it
		gr_host_texture_filter(Go_option_value[GO_OPT_TEXTURE_FILTER], Go_option_value[GO_OPT_MIP_BIAS] - 2);
	}
}

void game_options_settings_init()
{
	int i;

	for (i = 0; i < GO_OPT_COUNT; i++) {
		Go_option_value[i] = Go_options[i].dflt;
		if (game_options_keys_active()) {
			Go_option_value[i] = go_option_clamp(i, (int)os_config_read_uint(NULL, Go_options[i].key, Go_options[i].dflt));
		}
		if (Cmdline_game_option[i] >= 0) {
			Go_option_value[i] = go_option_clamp(i, Cmdline_game_option[i]);
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
	Go_option_value[id] = go_option_clamp(id, value);
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
#define GO_ITEM_OPTION		4		// Claude: GodotOptions, value: option id * GO_MAX_CHOICES + the value (TextureFiltering)
#define GO_MAX_ITEMS		48		// Claude: GodotOptions, was 16; TextureFiltering, was 24

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

// the Resolution row (DisplayResolution, D-20261102-200805 / 204803): Classic, Desktop, then window sizes that fit the monitor.
// Live play with the host's drop-down lists the monitor's own display modes (Tom, #198); the drawn row keeps the short common list.
typedef struct go_res {
	int mode, w, h;		// GR_DISPLAY_*; w x h for GR_DISPLAY_SIZE
	int gr;				// Claude: GodotOptions, a wide entry's GR mode (it also sets the height), else -1
	char text[24];
} go_res;

#define GO_MAX_SYS_SIZES	48		// the display modes listed, the largest if there are more
#define GO_MAX_RES			(GO_MAX_SYS_SIZES + 8)	// Claude: GodotOptions, room also for the 4:3 and wide entries, Desktop, the monitor's size and the size in force
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

// the sizes the monitor offers (each once, however many refresh rates or colour depths), smallest first; read again if the window
// moves to another monitor. 0 if Windows gave none.
static int game_options_system_sizes(int sizes[GO_MAX_SYS_SIZES][2])
{
	static char device[sizeof(((MONITORINFOEXA *)0)->szDevice)] = "";
	static int cached[GO_MAX_SYS_SIZES][2];
	static int num_cached = 0;
	MONITORINFOEXA mi;
	DEVMODEA dm;
	HMONITOR mon = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
	int i, j, k, w, h;

	mi.cbSize = sizeof(mi);
	if (!mon || !GetMonitorInfoA(mon, (MONITORINFO *)&mi)) {
		return 0;
	}
	if ((num_cached == 0) || strcmp(device, mi.szDevice)) {
		strcpy(device, mi.szDevice);
		num_cached = 0;
		memset(&dm, 0, sizeof(dm));
		dm.dmSize = sizeof(dm);
		for (i = 0; EnumDisplaySettingsA(mi.szDevice, i, &dm); i++) {
			w = (int)dm.dmPelsWidth;
			h = (int)dm.dmPelsHeight;
			if ((w < 640) || (h < 480)) {
				continue;
			}
			// in order by width, then height; at the limit the smallest goes
			for (j = 0; (j < num_cached) && ((cached[j][0] < w) || ((cached[j][0] == w) && (cached[j][1] < h))); j++) {
			}
			if ((j < num_cached) && (cached[j][0] == w) && (cached[j][1] == h)) {
				continue;
			}
			if (num_cached == GO_MAX_SYS_SIZES) {
				if (j == 0) {
					continue;
				}
				memmove(&cached[0], &cached[1], (j - 1) * sizeof(cached[0]));
				j--;
			} else {
				for (k = num_cached; k > j; k--) {
					cached[k][0] = cached[k - 1][0];
					cached[k][1] = cached[k - 1][1];
				}
				num_cached++;
			}
			cached[j][0] = w;
			cached[j][1] = h;
		}
	}
	memcpy(sizes, cached, num_cached * sizeof(cached[0]));
	return num_cached;
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

// Classic (both heights where the mode changes live), Desktop, the wide entries, then the sizes: the monitor's display modes that
// fit it with the host's drop-down, else up to GO_MAX_SIZES common sizes that fit (the largest, in ascending order). Then the
// monitor's own size, and the size in force if it is none of those.
static int game_options_dropdown_ok();

static void game_options_res_list()
{
	int mw, mh, cw, ch, cur, i, first, n, live, own_listed = 0;
	int sizes[GO_MAX_SYS_SIZES][2];
	char classic[24];	// Claude: GodotOptions, the Classic entry names the size the game draws at
	int num_common = (int)(sizeof(Go_common_sizes) / sizeof(Go_common_sizes[0]));

	Go_num_res = 0;
	sprintf(classic, "%s (4:3)", (Go_gr_shown == GR_640) ? "640x480" : "1024x768");
	live = game_options_keys_active() && gr_live_mode_change() && Game_profile->gr_mode_choice;
	if (live) {
		// Claude: GodotOptions, both 4:3 heights (Tom, #198)
		game_options_res_add(GR_DISPLAY_CLASSIC, 0, 0, "640x480 (4:3)");
		Go_res[Go_num_res - 1].gr = GR_640;
		game_options_res_add(GR_DISPLAY_CLASSIC, 0, 0, "1024x768 (4:3)");
		Go_res[Go_num_res - 1].gr = GR_1024;
	} else {
		game_options_res_add(GR_DISPLAY_CLASSIC, 0, 0, classic);
	}
	game_options_res_add(GR_DISPLAY_DESKTOP, 0, 0, "Desktop");
	game_options_monitor_size(&mw, &mh);
	// Claude: GodotOptions, the wide entries (Tom): only where the mode changes live, and never in a compared run, whose screen stays as it was
	cur = gr_virtual_display_request(&cw, &ch);
	if (live) {
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
	n = game_options_dropdown_ok() ? game_options_system_sizes(sizes) : 0;
	for (i = 0; i < n; i++) {
		if ((sizes[i][0] <= mw) && (sizes[i][1] <= mh) && ((sizes[i][0] != mw) || (sizes[i][1] != mh))) {
			game_options_res_add(GR_DISPLAY_SIZE, sizes[i][0], sizes[i][1], NULL);
			own_listed = 1;
		}
	}
	n = 0;
	first = num_common;
	for (i = num_common - 1; !own_listed && (i >= 0) && (n < GO_MAX_SIZES); i--) {
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
static int Go_res_x = 0, Go_res_y = 0, Go_res_w = 0;	// Claude: EnhancedOptions, the host drop-down's place, set by the layout in force

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
	Host_hooks.dropdown_show(Go_res_x, Go_res_y - 4, Go_res_w, h, labels, Go_num_res, Go_res_shown);	// Claude: EnhancedOptions, the place the layout in force gives
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
	return gr_screen.max_h * (66 + row * 6) / 100;	// TextureFiltering: was 72, room for four rows above Back
}

// TextureFiltering: a value's label on the screen: On / Off, or the choice's
static char *go_option_value_label(int id, int value)
{
	if (!Go_options[id].choices) {
		return value ? "On" : "Off";
	}
	go_choices_split();
	return Go_choice_label[id][value];
}

// the screen's items of an option's row: On then Off, or the choices in order, wrapping under x0 at the screen's right edge
static void go_option_row_items(int id, int x0, int y)
{
	int k, v, n = go_option_values(id), x = x0, w;
	char *text;

	for (k = 0; k < n; k++) {
		v = Go_options[id].choices ? k : (1 - k);
		text = go_option_value_label(id, v);
		gr_get_string_size(&w, NULL, text);
		if ((x > x0) && (x + w > gr_screen.max_w * 96 / 100)) {
			x = x0;
			y += gr_get_font_height() + 4;
		}
		game_options_add(GO_ITEM_OPTION, id * GO_MAX_CHOICES + v, x, y, text);
		x += (Go_options[id].choices ? (w + gr_screen.max_w / 40) : (gr_screen.max_w / 8));
	}
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
	Go_res_x = cx;	// Claude: EnhancedOptions, the drop-down's place on this screen
	Go_res_y = yr;
	Go_res_w = w * 30 / 100;
	for (i = 0; !Go_dropdown_on && (i < Go_num_res); i++) {
		if (i < 2) {
			game_options_add(GO_ITEM_RES, i, cx + i * step * 3 / 2, yr, Go_res[i].text);
		} else {
			game_options_add(GO_ITEM_RES, i, cx + (i - 2) * step, yr + lh, Go_res[i].text);
		}
	}
	for (i = 0, yr = 0; i < GO_OPT_COUNT; i++) {	// Claude: GodotOptions
		if (go_option_on_screen(i)) {
			go_option_row_items(i, cx, go_option_row_y(yr));	// Claude: TextureFiltering, choices
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
		return game_option(it->value / GO_MAX_CHOICES) == (it->value % GO_MAX_CHOICES);	// TextureFiltering
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
		game_option_set(it->value / GO_MAX_CHOICES, it->value % GO_MAX_CHOICES);	// TextureFiltering
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
static int Go_button_in_hall = 0;		// Claude: the main hall has the button too (Tom, #212): Back returns to the screen it came from
static int Go_button_clicked = 0;		// Claude: one click, one event, until the screen closes

static void game_options_pilot_button_click()
{
	Go_button_clicked = 1;
	Go_from_pilot_screen = Go_button_in_hall ? 0 : 1;
	gamesnd_play_iface(SND_IFACE_MOUSE_CLICK);
	gameseq_post_event(GS_EVENT_GAME_OPTIONS);
}

void game_options_pilot_button_init()
{
	Go_button_on = 0;	// Claude: EnhancedOptions, the button is gone: the Options screen's Enhanced page has its rows (was game_options_keys_active())
	Go_from_pilot_screen = 0;
	Go_button_in_hall = 0;	// Claude: #212
	Go_button_clicked = 0;	// Claude: #212
	Go_button_was_down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
	Go_button_host = 0;
	Go_button_last_do = timer_get_milliseconds();
	Host_side_button_pressed = 0;
	if (Go_button_on && (Host_hooks.side_button != NULL)) {
		Go_button_host = 1;
		Host_hooks.side_button("OPTIONS");
	}
}

// Claude: #212, the same button on the main hall
void game_options_hall_button_init()
{
	game_options_pilot_button_init();
	Go_button_in_hall = 1;
}

void game_options_pilot_button_do()
{
	int w = 0, h = 0, x, y, mx, my, over, down, now;

	if (!Go_button_in_hall) {
		game_version_corner_draw();	// Claude: EnhancedOptions, the version line on the pilot screen
	}
	if (!Go_button_on) {
		return;
	}
	if (Go_button_host) {
		now = timer_get_milliseconds();
		if (Host_side_button_pressed && ((now - Go_button_last_do) < GO_BUTTON_STALE_MS) && !Go_button_clicked) {	// Claude: #212
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

	if (over && !down && Go_button_was_down && !Go_button_clicked) {	// Claude: #212
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
			sprintf(line, "%c  %s%s\n", (i < 26) ? 'a' + i : ' ', Go_res[i].text, (i == Go_res_shown) ? "  *" : "");	// past z: resolution <w>x<h>
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
		for (i = 0; !game_options_dropdown_ok() && (i < num_common); i++) {	// Claude: GodotOptions, with the host's drop-down the list is the monitor's own modes
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
	char text[256];
	const char *arg;
	int i, v, k;

	for (i = 0; i < GO_OPT_COUNT; i++) {
		arg = go_console_word(line, Go_options[i].name);
		if (arg && Go_options[i].choices) {	// TextureFiltering: a choice option; no word shows the value and the choices
			go_choices_split();
			v = game_option_parse(i, arg, -2);
			if (v >= 0) {
				game_option_set(i, v);
			}
			sprintf(text, "%s%s %s", (v == -1) ? "unknown; " : "", Go_options[i].name, Go_choice_name[i][game_option(i)]);
			if (v < 0) {
				strcat(text, " (");
				for (k = 0; k < Go_choice_count[i]; k++) {
					strcat(text, k ? ", " : "");
					strcat(text, Go_choice_name[i][k]);
				}
				strcat(text, ")");
			}
			go_console_print(out, out_size, text);
			return 1;
		}
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

// ---------------------------------------------------------------------------------------------------
// Claude: EnhancedOptions (Tom, 2026-11-12, reworked 2026-11-13). The Options screen's Enhanced page. Its way in is the Options
// screen's Exit Game button, called Enhanced (the main hall keeps Exit). The page holds this screen's rows and the status that was in the Godot host's black column, and replaces the
// pilot screen's and main hall's Options button. Live play only (game_options_keys_active): a compared run sees the Options
// screen it always did. No art is added: the page is drawn with lines and text over the Options background.

#include "../Pad/PadState.h"
#include "../Voice/VoiceGame.h"

#define GO_PANEL_MAX_ROWS	8
#define GO_PANEL_MAX_HEADS	4

static int Go_panel_on = 0;
static int Go_panel_gr_request = -1;
static int Go_panel_rows = 0;
static int Go_panel_row_y[GO_PANEL_MAX_ROWS];
static char *Go_panel_row_label[GO_PANEL_MAX_ROWS];
static int Go_panel_heads = 0;
static int Go_panel_head_y[GO_PANEL_MAX_HEADS];
static char *Go_panel_head_text[GO_PANEL_MAX_HEADS];
static int Go_panel_gr_note_x = -1, Go_panel_gr_note_y = 0;

// the layout, in this art mode's pixels
static int Go_lay_px0, Go_lay_py0, Go_lay_px1, Go_lay_py1, Go_lay_cut;		// the page's panel and its corner cut
static int Go_lay_title_y, Go_lay_rule_y, Go_lay_top;
static int Go_lay_label_x, Go_lay_row_x, Go_lay_choice_x, Go_lay_right, Go_lay_step, Go_lay_line, Go_lay_gap, Go_lay_sect;
static int Go_lay_col_x, Go_lay_text_x, Go_lay_value_x;

// the status, read when the page opens (a pad plugged in later shows when the page is opened again)
static int Go_status_mouse = 0, Go_status_stick = 0, Go_status_pad = 0, Go_status_voice = 0;

static int Go_enh_bg = -1;				// the Options page's background (the screen's own handle)
#define GO_ICON_MAX		64
static int Go_icon_core = -1, Go_icon_glow = -1, Go_icon_n = 0;	// the Enhanced button's icon: the Options tab's four diamonds
static ubyte Go_icon_core_data[GO_ICON_MAX * GO_ICON_MAX], Go_icon_glow_data[GO_ICON_MAX * GO_ICON_MAX];
static color Go_c_label, Go_c_choice, Go_c_current, Go_c_over, Go_c_head, Go_c_box, Go_c_dim, Go_c_title;
static color Go_c_corner;

static int go_s(int v)
{
	return (gr_screen.res == GR_1024) ? v : (v * 5 + 4) / 8;	// 1024 x 768 coordinates to this art mode's
}

static void go_alpha(color *c, int r, int g, int b)
{
	gr_init_alphacolor(c, r, g, b, 255);
}

#pragma comment(lib, "version.lib")


// the exe or DLL holding this code (the exe natively, the game DLL under the Godot host)
static HMODULE go_own_module()
{
	HMODULE mod = NULL;

	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&go_own_module, &mod);
	return mod;
}

// the port's version the build stamped into the module's ProductVersion (#311): "<number> <layer> <hash>", or "unknown"
static void go_version_text(char *out, int size)
{
	char path[MAX_PATH];
	DWORD unused = 0, len;
	char *value = NULL;
	UINT value_len = 0;
	void *block;

	strncpy(out, "unknown", size);
	out[size - 1] = 0;
	if (!GetModuleFileNameA(go_own_module(), path, MAX_PATH)) {
		return;
	}
	len = GetFileVersionInfoSizeA(path, &unused);
	if (len == 0) {
		return;
	}
	block = malloc(len);
	if (block == NULL) {
		return;
	}
	if (GetFileVersionInfoA(path, 0, len, block) && VerQueryValueA(block, "\\StringFileInfo\\040904b0\\ProductVersion", (void **)&value, &value_len) && (value_len > 0)) {
		strncpy(out, value, size);
		out[size - 1] = 0;
	}
	free(block);
}

// "<number> <layer> <hash>" from the build's stamp
static void go_version_parts(char *num, char *layer, char *hash)
{
	char text[96];
	char *a, *b;

	go_version_text(text, sizeof(text));
	num[0] = layer[0] = hash[0] = 0;
	a = strchr(text, ' ');
	if (a == NULL) {
		strcpy(num, text);
		return;
	}
	*a++ = 0;
	strcpy(num, text);
	b = strchr(a, ' ');
	if (b != NULL) {
		*b++ = 0;
		strncpy(hash, b, 23);
		hash[23] = 0;
	}
	strncpy(layer, a, 31);
	layer[31] = 0;
}

// the modified time of the exe or DLL holding this code: the build's time, as the Godot host's status panel shows it
static void go_build_time(char *out)
{
	char path[MAX_PATH];
	WIN32_FILE_ATTRIBUTE_DATA info;
	FILETIME local;
	SYSTEMTIME st;

	strcpy(out, "unknown");
	if (!GetModuleFileNameA(go_own_module(), path, MAX_PATH) || !GetFileAttributesExA(path, GetFileExInfoStandard, &info) ||
		!FileTimeToLocalFileTime(&info.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &st)) {
		return;
	}
	sprintf(out, "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

// A remote desktop session always lists a stand-in mouse, which is not a mouse here (as the host's status panel).
static int go_mouse_found()
{
	UINT count = 0, got, i;
	int found = 0;
	RAWINPUTDEVICELIST *list;

	if ((GetRawInputDeviceList(NULL, &count, sizeof(RAWINPUTDEVICELIST)) != 0) || (count == 0)) {
		return 0;
	}
	list = (RAWINPUTDEVICELIST *)malloc(count * sizeof(RAWINPUTDEVICELIST));
	if (list == NULL) {
		return 0;
	}
	got = GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST));
	for (i = 0; (got != (UINT)-1) && (i < got) && !found; i++) {
		if (list[i].dwType == RIM_TYPEMOUSE) {
			char name[512];
			UINT name_len = sizeof(name);
			UINT copied = GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, name, &name_len);
			found = (copied == (UINT)-1) || (strstr(name, "RDP_MOU") == NULL);
		}
	}
	free(list);
	return found;
}

extern int joy_num_sticks;	// Joy.cpp: the sticks joy_init found

static void go_status_read()
{
	pad_state ps;

	Go_status_mouse = go_mouse_found();
	Go_status_stick = (joy_num_sticks > 0) ? 1 : 0;
	memset(&ps, 0, sizeof(ps));
	Go_status_pad = (Host_hooks.pad_poll != NULL) ? (Host_hooks.pad_poll(&ps) ? 1 : 0) : 0;
	Go_status_voice = voice_game_enabled();
}

// -status (or --status) on the command line: the Godot host draws the status top left, so the version here would clash with it.
// The process's command line has the host's and the user's arguments; FS2_CMD has the game's when a script starts it.
static int go_status_forced()
{
	static int forced = -1;
	char *lines[2];
	int i;

	if (forced >= 0) {
		return forced;
	}
	forced = 0;
	lines[0] = GetCommandLineA();
	lines[1] = getenv("FS2_CMD");
	for (i = 0; i < 2; i++) {
		char *p = lines[i];
		while ((p != NULL) && ((p = strstr(p, "-status")) != NULL)) {
			char before = (p == lines[i]) ? ' ' : p[-1];
			char after = p[7];
			if (before == '-') {
				before = (p - 1 == lines[i]) ? ' ' : p[-2];
			}
			if (((before == ' ') || (before == '\t') || (before == '"')) && ((after == 0) || (after == ' ') || (after == '\t') || (after == '"'))) {
				forced = 1;
			}
			p += 7;
		}
	}
	return forced;
}

// The version on the pilot screen and the loading screen, top left inside the picture: number, layer, build time, one per line.
void game_version_corner_draw()
{
	char num[32], layer[32], hash[24], built[32], text[128];
	int w = 0, h = 0, m;

	if (!game_options_keys_active() || go_status_forced()) {
		return;
	}
	go_version_parts(num, layer, hash);
	go_build_time(built);
	go_alpha(&Go_c_corner, 150, 150, 150);
	gr_set_font(FONT1);
	gr_set_color_fast(&Go_c_corner);
	// stacked, top left (Tom): the bottom of the pilot screen has the copyright lines, which run far to the right at 640 and on FS1
	{
		char *lines[3];
		int i, y;

		sprintf(text, "v%s", num);
		lines[0] = text;
		lines[1] = layer;
		lines[2] = built;
		gr_get_string_size(&w, &h, "0");
		m = go_s(10);
		y = m;
		for (i = 0; i < 3; i++) {
			gr_string(m, y, lines[i]);
			y += h + 2;
		}
	}
}

// ---- the page's layout ----

static void go_panel_place(int kind, int value, char *text, int *x, int *y)
{
	int w = 0, h = 0;

	gr_get_string_size(&w, &h, text);
	if ((*x + w > Go_lay_right) && (*x > Go_lay_choice_x)) {
		*x = Go_lay_choice_x;
		*y += Go_lay_line;
	}
	game_options_add(kind, value, *x, *y, text);
	*x += w + Go_lay_gap;
}

static void go_panel_row(char *label, int y)
{
	if (Go_panel_rows < GO_PANEL_MAX_ROWS) {
		Go_panel_row_label[Go_panel_rows] = label;
		Go_panel_row_y[Go_panel_rows] = y;
		Go_panel_rows++;
	}
}

// an option's row: On then Off, or the choices in order (TextureFiltering); returns the next row's line
static int go_panel_option_row(int id, int y)
{
	int k, v, x = Go_lay_choice_x;

	go_panel_row(Go_options[id].label, y);
	for (k = 0; k < go_option_values(id); k++) {
		v = Go_options[id].choices ? k : (1 - k);
		go_panel_place(GO_ITEM_OPTION, id * GO_MAX_CHOICES + v, go_option_value_label(id, v), &x, &y);
	}
	return y + Go_lay_step;
}

// a heading's height with the space under it
static int go_head_step()
{
	return gr_get_font_height() + (Cfile_fs1_data ? 7 : go_s(14));
}

// a section's heading at y; returns the y of its first row
static int go_panel_head(char *text, int y)
{
	if (Go_panel_heads < GO_PANEL_MAX_HEADS) {
		Go_panel_head_text[Go_panel_heads] = text;
		Go_panel_head_y[Go_panel_heads] = y;
		Go_panel_heads++;
	}
	return y + go_head_step();
}

static void go_panel_layout()
{
	int i, x, y, lh, margin, text_base;

	gr_set_font(FONT1);
	lh = gr_get_font_height();
	if (Cfile_fs1_data) {	// FS1's 640 layout, inside the Options frame under the tabs' plate, above the config buttons' plate
		Go_lay_px0 = 30;	Go_lay_py0 = 82;	Go_lay_px1 = 608;	Go_lay_py1 = 328;	Go_lay_cut = 10;
		Go_lay_title_y = Go_lay_py0 + 9;	Go_lay_rule_y = Go_lay_title_y + lh + 6;	Go_lay_top = Go_lay_rule_y + 12;
		Go_lay_label_x = 44;	Go_lay_row_x = 52;	Go_lay_choice_x = 150;	Go_lay_step = 20;	Go_lay_sect = 10;	Go_lay_gap = 16;
		text_base = 420;	margin = 14;
	} else {
		Go_lay_px0 = go_s(24);	Go_lay_py0 = go_s(82);	Go_lay_px1 = go_s(1000);	Go_lay_py1 = go_s(606);	Go_lay_cut = go_s(18);
		Go_lay_title_y = Go_lay_py0 + go_s(16);	Go_lay_rule_y = Go_lay_title_y + lh + go_s(10);	Go_lay_top = Go_lay_rule_y + go_s(24);
		Go_lay_label_x = go_s(48);	Go_lay_row_x = go_s(66);	Go_lay_choice_x = go_s(250);	Go_lay_step = go_s(36);	Go_lay_sect = go_s(20);	Go_lay_gap = go_s(30);
		text_base = go_s(680);	margin = go_s(28);
	}
	Go_lay_line = lh + 6;
	Go_num_items = 0;
	Go_panel_rows = 0;
	Go_panel_heads = 0;
	Go_panel_gr_note_x = -1;

	// the columns from the text itself: the font is the same size in both art modes, so fixed fractions crowd at 640
	{
		static char *labels[] = { "Flight Control", "Graphics Mode", "Resolution" };
		static char *names[] = { "Mouse", "Joystick", "Joypad", "Voice Control" };
		static char *values[] = { "Detected", "Undetected", "Enabled", "Disabled" };
		char num[32], layer[32], hash[24], built[32], text[96];
		int max_label = 0, max_name = 0, max_value = 0, inner, pad = Go_lay_gap / 2, w;

		for (i = 0; i < 3; i++) {
			gr_get_string_size(&w, NULL, labels[i]);
			max_label = (w > max_label) ? w : max_label;
		}
		for (i = 0; i < GO_OPT_COUNT; i++) {
			gr_get_string_size(&w, NULL, Go_options[i].label);
			max_label = (w > max_label) ? w : max_label;
		}
		for (i = 0; i < 4; i++) {
			gr_get_string_size(&w, NULL, names[i]);
			max_name = (w > max_name) ? w : max_name;
			gr_get_string_size(&w, NULL, values[i]);
			max_value = (w > max_value) ? w : max_value;
		}
		inner = max_name + pad + max_value;
		go_version_parts(num, layer, hash);
		go_build_time(built);
		sprintf(text, "%s  %s", layer, hash);
		gr_get_string_size(&w, NULL, text);
		inner = (w > inner) ? w : inner;
		sprintf(text, "Built %s", built);
		gr_get_string_size(&w, NULL, text);
		inner = (w > inner) ? w : inner;

		if (Go_lay_choice_x < Go_lay_row_x + max_label + Go_lay_gap) {
			Go_lay_choice_x = Go_lay_row_x + max_label + Go_lay_gap;
		}
		Go_lay_text_x = (Go_lay_px1 - margin - inner < text_base) ? Go_lay_px1 - margin - inner : text_base;
		Go_lay_value_x = Go_lay_text_x + max_name + pad;
		Go_lay_col_x = Go_lay_text_x - margin;
		Go_lay_right = Go_lay_col_x - margin;
	}

	y = go_panel_head("Controls", Go_lay_top);
	go_panel_row("Flight Control", y);
	x = Go_lay_choice_x;
	go_panel_place(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_PAD, "Pad", &x, &y);
	go_panel_place(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_JOYSTICK, "Joystick", &x, &y);
	go_panel_place(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_MOUSE, "Mouse", &x, &y);
	y += Go_lay_step + Go_lay_sect;

	y = go_panel_head("Display", y);
	if (Game_profile->gr_mode_choice) {
		go_panel_row("Graphics Mode", y);
		x = Go_lay_choice_x;
		go_panel_place(GO_ITEM_GR, GR_1024, "1024 x 768", &x, &y);
		go_panel_place(GO_ITEM_GR, GR_640, "640 x 480", &x, &y);
		if (!gr_live_mode_change() || (Game_mode & GM_IN_MISSION)) {
			Go_panel_gr_note_x = x;
			Go_panel_gr_note_y = y;
		}
		y += Go_lay_step;
	}

	go_panel_row("Resolution", y);
	if (Go_dropdown_on) {
		Go_res_x = Go_lay_choice_x;
		Go_res_y = y;
		Go_res_w = Go_lay_right - Go_lay_choice_x;
	} else {
		x = Go_lay_choice_x;
		for (i = 0; i < Go_num_res; i++) {
			go_panel_place(GO_ITEM_RES, i, Go_res[i].text, &x, &y);
		}
	}
	y += Go_lay_step;
	for (i = 0; i < GO_OPT_COUNT; i++) {	// TextureFiltering: the choice options (texture sampling) are Display rows
		if (go_option_on_screen(i) && Go_options[i].choices) {
			y = go_panel_option_row(i, y);
		}
	}
	y += Go_lay_sect;

	for (i = 0; i < GO_OPT_COUNT; i++) {
		if (go_option_on_screen(i) && !Go_options[i].choices) {
			if ((Go_panel_heads < 3)) {
				y = go_panel_head("Interface", y);
			}
			y = go_panel_option_row(i, y);
		}
	}
}

static void go_panel_colors()
{
	if (Cfile_fs1_data) {
		go_alpha(&Go_c_title, 230, 50, 50);
		go_alpha(&Go_c_head, 200, 30, 30);
		go_alpha(&Go_c_label, 175, 175, 182);
		go_alpha(&Go_c_choice, 135, 135, 142);
		go_alpha(&Go_c_current, 245, 245, 250);
		go_alpha(&Go_c_over, 255, 210, 210);
		go_alpha(&Go_c_box, 96, 102, 112);
		go_alpha(&Go_c_dim, 135, 135, 142);
	} else {
		go_alpha(&Go_c_title, 205, 240, 195);
		go_alpha(&Go_c_head, 170, 220, 160);
		go_alpha(&Go_c_label, 140, 160, 140);
		go_alpha(&Go_c_choice, 115, 135, 118);
		go_alpha(&Go_c_current, 220, 245, 215);
		go_alpha(&Go_c_over, 255, 255, 255);
		go_alpha(&Go_c_box, 72, 98, 88);
		go_alpha(&Go_c_dim, 115, 135, 118);
	}
}

// ---- the Enhanced button's icon: the Options tab's four diamonds, drawn as two alpha bitmaps (shape and glow) ----

// spacing: from the icon's middle to each diamond's; radius: each diamond's half diagonal (pixels)
static void go_icon_build(float spacing, float radius)
{
	static const float dx[4] = { 0.0f, -1.0f, 1.0f, 0.0f }, dy[4] = { -1.0f, 0.0f, 0.0f, 1.0f };
	int n = (int)(2.0f * (spacing + radius)) + 8, x, y, i;
	float c, d, best, a;

	n = (n > GO_ICON_MAX) ? GO_ICON_MAX : n;
	c = (n - 1) * 0.5f;
	for (y = 0; y < n; y++) {
		for (x = 0; x < n; x++) {
			best = 1.0e9f;
			for (i = 0; i < 4; i++) {
				d = fl_abs(x - c - dx[i] * spacing) + fl_abs(y - c - dy[i] * spacing);
				d = (d - radius) * 0.7071f;	// to a pixel distance from the diamond's edge
				best = (d < best) ? d : best;
			}
			a = 0.5f - best;	// one pixel of soft edge
			a = (a < 0.0f) ? 0.0f : ((a > 1.0f) ? 1.0f : a);
			Go_icon_core_data[y * n + x] = (ubyte)(a * 14.0f + 0.5f);	// 15 draws nearly clear (gr_host.cpp aa_to_a8)
			a = 1.0f - best / 3.5f;
			a = (a < 0.0f) ? 0.0f : ((a > 1.0f) ? 1.0f : a);
			Go_icon_glow_data[y * n + x] = (ubyte)(a * a * 11.0f + 0.5f);
		}
	}
	Go_icon_n = n;
	Go_icon_core = bm_create(8, n, n, Go_icon_core_data, BMP_AABITMAP);
	Go_icon_glow = bm_create(8, n, n, Go_icon_glow_data, BMP_AABITMAP);
}

// lit: 2 the page is open or the button is held (lit, with its glow), 1 the mouse is over it (lit, dimmer, no glow), 0 cut into the
// metal like the X was
static void go_icon_draw(int cx, int cy, int lit)
{
	color c;
	int x = cx - Go_icon_n / 2, y = cy - Go_icon_n / 2;

	if (Go_icon_core < 0) {
		return;
	}
	if (lit == 1) {
		if (Cfile_fs1_data) go_alpha(&c, 225, 110, 110); else go_alpha(&c, 140, 190, 128);
		gr_set_color_fast(&c);
		gr_set_bitmap(Go_icon_core);
		gr_aabitmap(x, y);
		return;
	}
	if (lit) {
		if (Cfile_fs1_data) go_alpha(&c, 230, 50, 50); else go_alpha(&c, 110, 190, 100);
		gr_set_color_fast(&c);
		gr_set_bitmap(Go_icon_glow);
		gr_aabitmap(x, y);
		if (Cfile_fs1_data) go_alpha(&c, 255, 235, 235); else go_alpha(&c, 210, 245, 190);
		gr_set_color_fast(&c);
		gr_set_bitmap(Go_icon_core);
		gr_aabitmap(x, y);
		return;
	}
	gr_set_bitmap(Go_icon_core);
	if (!Cfile_fs1_data) {	// FS2's are cut in, with a lit lower left edge
		go_alpha(&c, 74, 78, 62);
		gr_set_color_fast(&c);
		gr_aabitmap(x - 1, y + 1);
	}
	if (Cfile_fs1_data) go_alpha(&c, 125, 18, 18); else go_alpha(&c, 14, 16, 10);
	gr_set_color_fast(&c);
	gr_aabitmap(x, y);
}

// ---- the Options screen's calls ----

int game_options_enhanced_available()
{
	return game_options_keys_active();
}

// FS1's Exit Game icon and label are painted into the Options background. Only their own pixels are filled, each from the nearest
// art beside it (along the row or the column, whichever gap is shorter), so the plate and its bevels keep their grain and no
// patch edge shows. Built once from the background file, drawn as runs of one colour.
#define GO_ERASE_MAX	2048
typedef struct { short x, y, w; ubyte r, g, b; } go_erase_run;
static go_erase_run Go_erase[GO_ERASE_MAX];
static int Go_erase_n = 0;
static int Go_erase_page = 0;	// the runs from here on are the page's (Brightness), the ones before it the button's

static int go_erase_red(ubyte *p)
{
	return (p[0] >= 40) && (p[0] >= p[1] + 16) && (p[0] >= p[2] + 16);
}

static void go_erase_build_area(ubyte *data, ubyte *pal, int bw, int bh, int x0, int y0, int x1, int y1)
{
	static ubyte mask[64 * 128], rgb[64 * 128][3];
	int w = x1 - x0, h = y1 - y0, x, y, i, k;

	if ((w <= 0) || (h <= 0) || (w > 128) || (h > 64) || (x1 > bw) || (y1 > bh)) {
		return;
	}
	for (y = 0; y < h; y++) {	// the red pixels and those next to them (their soft edges)
		for (x = 0; x < w; x++) {
			int dx, dy, m = 0;
			for (dy = -1; (dy <= 1) && !m; dy++) {
				for (dx = -1; (dx <= 1) && !m; dx++) {
					int xx = x + dx, yy = y + dy;
					if ((xx >= 0) && (yy >= 0) && (xx < w) && (yy < h) && go_erase_red(&pal[3 * data[(y0 + yy) * bw + x0 + xx]])) {
						m = 1;
					}
				}
			}
			mask[y * w + x] = (ubyte)m;
		}
	}
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int l = x, r = x, u = y, d = y, hx, vy;
			ubyte *a, *b;
			float t;

			if (!mask[y * w + x]) {
				continue;
			}
			while ((l >= 0) && mask[y * w + l]) l--;
			while ((r < w) && mask[y * w + r]) r++;
			while ((u >= 0) && mask[u * w + x]) u--;
			while ((d < h) && mask[d * w + x]) d++;
			hx = (l >= 0) && (r < w);
			vy = (u >= 0) && (d < h);
			if (hx && (!vy || (r - l <= d - u))) {
				a = &pal[3 * data[(y0 + y) * bw + x0 + l]];
				b = &pal[3 * data[(y0 + y) * bw + x0 + r]];
				t = (float)(x - l) / (float)(r - l);
			} else if (vy) {
				a = &pal[3 * data[(y0 + u) * bw + x0 + x]];
				b = &pal[3 * data[(y0 + d) * bw + x0 + x]];
				t = (float)(y - u) / (float)(d - u);
			} else {
				a = b = &pal[3 * data[(y0 + y) * bw + x0 + ((l >= 0) ? l : r)]];
				t = 0.0f;
			}
			for (k = 0; k < 3; k++) {
				rgb[y * w + x][k] = (ubyte)(a[k] + (b[k] - a[k]) * t + 0.5f);
			}
		}
	}
	for (y = 0; y < h; y++) {	// runs of one colour along each row
		for (x = 0; x < w; x = i) {
			i = x + 1;
			if (!mask[y * w + x]) {
				continue;
			}
			while ((i < w) && mask[y * w + i] && !memcmp(rgb[y * w + i], rgb[y * w + x], 3)) i++;
			if (Go_erase_n < GO_ERASE_MAX) {
				go_erase_run *e = &Go_erase[Go_erase_n++];
				e->x = (short)(x0 + x);
				e->y = (short)(y0 + y);
				e->w = (short)(i - x);
				e->r = rgb[y * w + x][0];
				e->g = rgb[y * w + x][1];
				e->b = rgb[y * w + x][2];
			}
		}
	}
}

static void go_fs1_erase_build()
{
	char *name;
	int w, h;
	ubyte pal[768], *data;

	Go_erase_n = 0;
	Go_erase_page = 0;
	name = (Go_enh_bg >= 0) ? bm_get_filename(Go_enh_bg) : NULL;
	if (!name || (pcx_read_header(name, &w, &h, pal) != PCX_ERROR_NONE) || (w != 640) || (h != 480)) {
		return;
	}
	data = (ubyte *)malloc(w * h);
	if (!data) {
		return;
	}
	if (pcx_read_bitmap_8bpp(name, data, pal) == PCX_ERROR_NONE) {
		go_erase_build_area(data, pal, w, h, 26, 380, 68, 424);		// the X on the button's plate
		go_erase_build_area(data, pal, w, h, 0, 427, 104, 448);		// "Exit Game" under it
		Go_erase_page = Go_erase_n;
		go_erase_build_area(data, pal, w, h, 374, 50, 498, 72);		// the Brightness label, when the Enhanced page is up
	}
	free(data);
}

extern float Gr_gamma;	// 2d.cpp

// the value that comes out as v once a drawn colour has its gamma (Gr_gamma) applied, which the art does not
static int go_ungamma(int v)
{
	if (Gr_gamma <= 0.0f) {
		return v;
	}
	return (int)(pow((float)v / 255.0f, Gr_gamma) * 255.0f + 0.5f);
}

static void go_fs1_erase_draw(int from, int to)
{
	int i;
	color c;

	for (i = from; i < to; i++) {
		go_erase_run *e = &Go_erase[i];
		go_alpha(&c, go_ungamma(e->r), go_ungamma(e->g), go_ungamma(e->b));
		gr_set_color_fast(&c);
		gr_rect(e->x, e->y, e->w, 1);
	}
}

// FS2's Exit Game X is covered with plain plate from the Control Config button's plate, further right on the same row. The plate's
// shade changes across the screen, so the copy is shaded to match the art around it (the difference just outside the square on its
// left, right and top, carried in by distance) and blended into the art over its last few pixels on those sides, so no edge shows.
// Built once from the background file into a bitmap, which keeps the art's own colours (a drawn colour has the gamma applied).
static int Go_fs2_patch = -1, Go_fs2_patch_x = 0, Go_fs2_patch_y = 0;
static void *Go_fs2_patch_data = NULL;

static void go_fs2_patch_build()
{
	char *name;
	int w, h, x0 = go_s(19), y0 = go_s(712), x1 = go_s(74), y1, dx = go_s(693), r = go_s(4), f = go_s(6);
	int pw, ph, x, y, c, k, bpp = (gr_screen.texture_bpp == 32) ? 32 : 16;
	ubyte pal[768], *data;
	float *side[3], v;

	Go_fs2_patch = -1;
	name = (Go_enh_bg >= 0) ? bm_get_filename(Go_enh_bg) : NULL;
	if (!name || (pcx_read_header(name, &w, &h, pal) != PCX_ERROR_NONE) || (w != gr_screen.max_w) || (h != gr_screen.max_h)) {
		return;
	}
	y1 = h;
	pw = x1 - x0;
	ph = y1 - y0;
	if ((x0 < 4) || (x1 + dx + 4 > w) || (y0 < 4) || (pw <= 0) || (ph <= 0)) {
		return;
	}
	data = (ubyte *)malloc(w * h);
	side[0] = (float *)malloc(sizeof(float) * 3 * ph);	// left, per row and channel
	side[1] = (float *)malloc(sizeof(float) * 3 * ph);	// right
	side[2] = (float *)malloc(sizeof(float) * 3 * pw);	// top, per column and channel
	Go_fs2_patch_data = malloc(pw * ph * (bpp / 8));
	if (!data || !side[0] || !side[1] || !side[2] || !Go_fs2_patch_data || (pcx_read_bitmap_8bpp(name, data, pal) != PCX_ERROR_NONE)) {
		free(data); free(side[0]); free(side[1]); free(side[2]);
		free(Go_fs2_patch_data);
		Go_fs2_patch_data = NULL;
		return;
	}
#define GO_PX(px, py, ch)	((float)pal[3 * data[(py) * w + (px)] + (ch)])
	// the art minus the copy's source, just outside the square (three pixels deep), smoothed along each side
	{
		float *raw = (float *)malloc(sizeof(float) * 3 * ((ph > pw) ? ph : pw));
		int s, n, i, j;
		for (s = 0; s < 3; s++) {
			n = (s < 2) ? ph : pw;
			for (i = 0; i < n; i++) {
				for (c = 0; c < 3; c++) {
					v = 0.0f;
					for (k = 1; k <= 3; k++) {
						int ax = (s == 0) ? x0 - k : ((s == 1) ? x1 - 1 + k : x0 + i), ay = (s < 2) ? y0 + i : y0 - k;
						v += GO_PX(ax, ay, c) - GO_PX(ax + dx, ay, c);
					}
					raw[i * 3 + c] = v / 3.0f;
				}
			}
			for (i = 0; i < n; i++) {
				for (c = 0; c < 3; c++) {
					float sum = 0.0f;
					int cnt = 0;
					for (j = i - r; j <= i + r; j++) {
						if ((j >= 0) && (j < n)) { sum += raw[j * 3 + c]; cnt++; }
					}
					side[s][i * 3 + c] = sum / cnt;
				}
			}
		}
		free(raw);
	}
	if (bpp == 16) {
		if (Gr_bitmap_poly) {
			BM_SELECT_TEX_FORMAT();
		} else {
			BM_SELECT_SCREEN_FORMAT();
		}
	}
	for (y = 0; y < ph; y++) {
		for (x = 0; x < pw; x++) {
			float wl = 1.0f / (x + 1), wr = 1.0f / (pw - x), wt = 1.0f / (y + 1), a, out[3];
			int edge = x + 1;
			ubyte rgb[3], al = 255;

			edge = (pw - x < edge) ? pw - x : edge;
			edge = (y + 1 < edge) ? y + 1 : edge;
			a = (float)edge / (float)f;
			a = (a > 1.0f) ? 1.0f : a;
			for (c = 0; c < 3; c++) {
				float corr = (wl * side[0][y * 3 + c] + wr * side[1][y * 3 + c] + wt * side[2][x * 3 + c]) / (wl + wr + wt);
				float copy = GO_PX(x0 + x + dx, y0 + y, c) + corr;
				out[c] = GO_PX(x0 + x, y0 + y, c) * (1.0f - a) + copy * a;
				out[c] = (out[c] < 0.0f) ? 0.0f : ((out[c] > 255.0f) ? 255.0f : out[c]);
				rgb[c] = (ubyte)(out[c] + 0.5f);
			}
			if (bpp == 32) {
				((uint *)Go_fs2_patch_data)[y * pw + x] = 0xff000000 | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
			} else {
				uint px = 0;
				bm_set_components((ubyte *)&px, &rgb[0], &rgb[1], &rgb[2], &al);
				((ushort *)Go_fs2_patch_data)[y * pw + x] = (ushort)px;
			}
		}
	}
#undef GO_PX
	if (bpp == 16) {
		BM_SELECT_SCREEN_FORMAT();
	}
	free(data); free(side[0]); free(side[1]); free(side[2]);
	Go_fs2_patch = bm_create(bpp, pw, ph, Go_fs2_patch_data, 0);
	Go_fs2_patch_x = x0;
	Go_fs2_patch_y = y0;
	if (Go_fs2_patch < 0) {
		free(Go_fs2_patch_data);
		Go_fs2_patch_data = NULL;
	}
}

void game_options_enhanced_init(int options_bg)
{
	Go_enh_bg = options_bg;
	Go_panel_on = 0;
	Go_panel_gr_request = -1;
	if (game_options_enhanced_available()) {
		go_panel_colors();
		if (Cfile_fs1_data) {
			go_icon_build(8.0f, 4.8f);
			go_fs1_erase_build();
		} else {
			go_icon_build((float)go_s(13), (float)go_s(72) / 10.0f);
			go_fs2_patch_build();
		}
	}
}

void game_options_enhanced_close()
{
	if (Go_panel_on) {
		game_options_panel_close();
	}
	if (Go_icon_core >= 0) {
		bm_release(Go_icon_core);
		Go_icon_core = -1;
	}
	if (Go_icon_glow >= 0) {
		bm_release(Go_icon_glow);
		Go_icon_glow = -1;
	}
	if (Go_fs2_patch >= 0) {
		bm_release(Go_fs2_patch);
		Go_fs2_patch = -1;
	}
	if (Go_fs2_patch_data) {
		free(Go_fs2_patch_data);
		Go_fs2_patch_data = NULL;
	}
	Go_enh_bg = -1;
}

// The way in: the Options screen's Exit Game button, called Enhanced. FS2's label is the screen's own text (OptionsMenu.cpp); FS1's
// is painted into its art, so here its pixels are filled from the art around them and it is drawn: lit when selected (the page is open) or the
// mouse is over the button. The button's own press opens the page (OptionsMenu.cpp).
void game_options_enhanced_entry_do(int selected, int over)
{
	int lit = (selected || (over && mouse_down(MOUSE_LEFT_BUTTON))) ? 2 : (over ? 1 : 0);

	if (!game_options_enhanced_available() || (Go_enh_bg < 0)) {
		return;
	}
	// the old icon gives way (FS2: plain plate from the Control Config plate, shaded to match; FS1: its own pixels filled from the
	// art beside them), the four diamonds over it
	gr_set_bitmap(Go_enh_bg);
	if (Cfile_fs1_data) {
		go_fs1_erase_draw(0, Go_erase_page);
		go_icon_draw(46, 401, lit);
	} else {
		if (Go_fs2_patch >= 0) {
			gr_set_bitmap(Go_fs2_patch);
			gr_bitmap(Go_fs2_patch_x, Go_fs2_patch_y);
		} else {
			gr_bitmap_ex(go_s(19), go_s(712), go_s(55), go_s(56), go_s(712), go_s(712));
		}
		go_icon_draw(go_s(46), go_s(739), lit);
	}
	if (Cfile_fs1_data) {
		color c;

		gr_set_font(FONT1);
		if (selected) {
			go_alpha(&c, 255, 70, 70);
		} else if (over) {
			go_alpha(&c, 230, 120, 120);
		} else {
			go_alpha(&c, 160, 24, 24);
		}
		gr_set_color_fast(&c);
		{
			int w = 0;
			gr_get_string_size(&w, NULL, "Enhanced");
			gr_string(47 - w / 2, 432, "Enhanced");	// under the button's middle
		}
	}
}

// the page's panel: a filled shape with the UI's cut corners (top left, bottom right), its title and the line under it
static void go_panel_frame()
{
	int i, c = Go_lay_cut, x0 = Go_lay_px0, y0 = Go_lay_py0, x1 = Go_lay_px1, y1 = Go_lay_py1;

	if (Cfile_fs1_data) {
		gr_set_color(0, 0, 0);
	} else {
		gr_set_color(5, 10, 12);
	}
	for (i = 0; i < c; i++) {
		gr_rect(x0 + c - i, y0 + i, x1 - x0 + 1 - (c - i), 1);			// top band, cut on the left
		gr_rect(x0, y1 - i, x1 - x0 + 1 - (c - i), 1);					// bottom band, cut on the right
	}
	gr_rect(x0, y0 + c, x1 - x0 + 1, y1 - y0 + 1 - 2 * c);

	gr_set_color_fast(&Go_c_box);
	gr_line(x0 + c, y0, x1, y0);
	gr_line(x1, y0, x1, y1 - c);
	gr_line(x1, y1 - c, x1 - c, y1);
	gr_line(x1 - c, y1, x0, y1);
	gr_line(x0, y1, x0, y0 + c);
	gr_line(x0, y0 + c, x0 + c, y0);

	gr_set_font(FONT1);
	gr_set_color_fast(&Go_c_title);
	gr_string(Go_lay_label_x, Go_lay_title_y, "Enhanced Options");
	gr_set_color_fast(&Go_c_box);
	gr_line(Go_lay_label_x, Go_lay_rule_y, x1 - (Go_lay_label_x - x0), Go_lay_rule_y);
	gr_line(Go_lay_col_x, Go_lay_top, Go_lay_col_x, y1 - (Go_lay_top - Go_lay_rule_y));
}

void game_options_panel_open()
{
	int gr = game_options_gr_key();

	Go_panel_on = 1;
	Go_gr_shown = (gr >= 0) ? gr : gr_screen.res;
	Go_mouse_was_down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
	game_options_res_list();
	Go_dropdown_on = game_options_dropdown_ok();
	Host_dropdown_chosen = -1;
	go_status_read();
	go_panel_layout();
	game_options_dropdown_show();
}

void game_options_panel_close()
{
	game_options_dropdown_hide();
	Go_dropdown_on = 0;
	Host_dropdown_chosen = -1;
	Go_num_items = 0;
	Go_panel_on = 0;
}

int game_options_panel_gr_request()
{
	int gr = Go_panel_gr_request;

	Go_panel_gr_request = -1;
	return gr;
}

static void go_panel_status_line(int y, char *name, int on, char *yes, char *no)
{
	gr_set_color_fast(&Go_c_dim);
	gr_string(Go_lay_text_x, y, name);
	gr_set_color_fast(on ? &Go_c_current : &Go_c_dim);
	gr_string(Go_lay_value_x, y, on ? yes : no);
}

void game_options_panel_do()
{
	int mx, my, over, down, i, y, lh, row;
	go_item *it;
	char num[32], layer[32], hash[24], built[32], text[96];

	if (!Go_panel_on) {
		return;
	}
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
	if (!down && Go_mouse_was_down && (over >= 0)) {
		it = &Go_items[over];
		if (it->kind == GO_ITEM_GR) {
			int gr = it->value;
			game_options_pick(it);	// writes the key; 'it' is stale after this
			if (gr_live_mode_change() && !(Game_mode & GM_IN_MISSION) && (gr != gr_screen.res)) {
				Go_panel_gr_request = gr;	// the Options screen switches the mode and opens again on this page
			}
		} else {
			game_options_pick(it);
		}
		go_panel_layout();	// the resolution list may have changed with the mode
		over = -1;
	}
	Go_mouse_was_down = down;

	if (Cfile_fs1_data) {
		go_fs1_erase_draw(Go_erase_page, Go_erase_n);
	}
	go_panel_frame();	// over the Options screen as it is: its tabs stay and work
	gr_set_font(FONT1);
	lh = gr_get_font_height();
	for (i = 0; i < Go_panel_heads; i++) {
		gr_set_color_fast(&Go_c_head);
		gr_string(Go_lay_label_x, Go_panel_head_y[i], Go_panel_head_text[i]);
	}
	for (i = 0; i < Go_panel_rows; i++) {
		gr_set_color_fast(&Go_c_label);
		gr_string(Go_lay_row_x, Go_panel_row_y[i], Go_panel_row_label[i]);
	}
	for (i = 0; i < Go_num_items; i++) {
		it = &Go_items[i];
		if (game_options_item_current(it)) {
			gr_set_color_fast(&Go_c_current);
			gr_string(it->x, it->y, it->text);
			gr_line(it->x, it->y + it->h + 1, it->x + it->w - 1, it->y + it->h + 1);
		} else {
			gr_set_color_fast((i == over) ? &Go_c_over : &Go_c_choice);
			gr_string(it->x, it->y, it->text);
		}
	}
	if (Go_panel_gr_note_x >= 0) {
		gr_set_color_fast(&Go_c_dim);
		gr_string(Go_panel_gr_note_x, Go_panel_gr_note_y, "(next start)");
	}

	// the build and the devices, in the right column
	row = Cfile_fs1_data ? 4 : go_s(6);
	go_version_parts(num, layer, hash);
	go_build_time(built);
	y = Go_lay_top + go_head_step();
	gr_set_color_fast(&Go_c_head);
	gr_string(Go_lay_text_x, Go_lay_top, "Version");
	gr_set_color_fast(&Go_c_current);
	sprintf(text, "v%s", num);
	gr_string(Go_lay_text_x, y, text);
	y += lh + row;
	gr_set_color_fast(&Go_c_dim);
	sprintf(text, "%s  %s", layer, hash);
	gr_string(Go_lay_text_x, y, text);
	y += lh + row;
	sprintf(text, "Built %s", built);
	gr_string(Go_lay_text_x, y, text);
	y += lh + row + Go_lay_step;
	gr_set_color_fast(&Go_c_head);
	gr_string(Go_lay_text_x, y, "Status");
	y += go_head_step();
	go_panel_status_line(y, "Mouse", Go_status_mouse, "Detected", "Undetected");
	y += lh + row;
	go_panel_status_line(y, "Joystick", Go_status_stick, "Detected", "Undetected");
	y += lh + row;
	go_panel_status_line(y, "Joypad", Go_status_pad, "Detected", "Undetected");
	y += lh + row;
	go_panel_status_line(y, "Voice Control", Go_status_voice, "Enabled", "Disabled");
}
