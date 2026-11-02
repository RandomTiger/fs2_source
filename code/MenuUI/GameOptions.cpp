/*
 * Claude: the Game Options screen and its two fs2.ini keys.  See GameOptions.h.
 */

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
#include "GameSnd.h"
#include "OsRegistry.h"
#include "cfile.h"
#include "DataSetProfile.h"
#include "Replay.h"
#include "Autotest.h"
#include "../Pad/PadGame.h"

#define GAME_OPTIONS_KEY_FLIGHT		"FlightControl"
#define GAME_OPTIONS_KEY_GR			"GraphicsMode"

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
	Game_options_flight = device;
	game_options_apply_flight();
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
#define GO_MAX_ITEMS		8

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

// lays the screen out for this art mode: fractions of the screen, so GR_640 and GR_1024 match
static void game_options_layout()
{
	int w = gr_screen.max_w, h = gr_screen.max_h;
	int cx = w / 2, step = w / 8;

	gr_set_font(FONT1);
	Go_num_items = 0;
	game_options_add(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_PAD, cx, h * 30 / 100, "Pad");
	game_options_add(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_JOYSTICK, cx + step, h * 30 / 100, "Joystick");
	game_options_add(GO_ITEM_FLIGHT, GAME_OPTIONS_FLIGHT_MOUSE, cx + step * 2, h * 30 / 100, "Mouse");
	if (Game_profile->gr_mode_choice) {
		game_options_add(GO_ITEM_GR, GR_1024, cx, h * 42 / 100, "1024 x 768");
		game_options_add(GO_ITEM_GR, GR_640, cx + step * 3 / 2, h * 42 / 100, "640 x 480");
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
	return 0;
}

static void game_options_pick(go_item *it)
{
	gamesnd_play_iface(SND_USER_SELECT);
	if (it->kind == GO_ITEM_FLIGHT) {
		game_options_load();
		Game_options_flight = it->value;
		game_options_apply_flight();
		game_options_write_flight();
	} else if (it->kind == GO_ITEM_GR) {
		Go_gr_shown = it->value;
		if (game_options_keys_active()) {
			os_config_write_string(NULL, GAME_OPTIONS_KEY_GR, (it->value == GR_1024) ? "GR_1024" : "GR_640");
		}
	}
}

void game_options_screen_back()
{
	gamesnd_play_iface(SND_USER_SELECT);
	gameseq_post_event(GS_EVENT_MAIN_MENU);
}

void game_options_screen_init()
{
	int gr = game_options_gr_key();

	Go_gr_shown = (gr >= 0) ? gr : gr_screen.res;
	Go_mouse_was_down = mouse_down(MOUSE_LEFT_BUTTON) ? 1 : 0;
	game_options_layout();
}

void game_options_screen_close()
{
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
		gr_string(gr_screen.max_w / 2, gr_screen.max_h * 42 / 100 + gr_get_font_height() + 6, "Takes effect next time the game starts");
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
