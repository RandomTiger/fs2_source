// PadMenuGame.cpp
// Control pad glue for the menus. See PadMenuGame.h, and docs/PAD_CONTROL.md.
//
// Recording: every read here is live and unrecorded, between replay_push and replay_pop. All this
// does to the game is make key_mark and mouse_mark_button events, which the recorder keeps, and
// move the real cursor, whose position the recorder keeps where the game reads it. A replay does
// none of this: it gets the same events from the stream.

#include <string.h>

#include "pstypes.h"
#include "PadMenuGame.h"
#include "PadMenu.h"
#include "PadCore.h"
#include "PadGame.h"
#include "PadXInput.h"

#include "2d.h"
#include "GameSequence.h"
#include "key.h"
#include "Mouse.h"
#include "OsApi.h"
#include "Popup.h"
#include "PopupDead.h"
#include "Replay.h"
#include "Timer.h"
#include "UI.H"

extern int joy_num_sticks;			// Joy.cpp: 1 if the legacy joystick code is reading a device

#define PAD_MENU_STALE_POLLS	2		// what a window or a region reported is good for this many polls
#define PAD_MENU_MAX_STEP_MS	100	// the cursor does not jump after a load

static const int Pad_menu_keys[PAD_MENU_NUM] = { KEY_ENTER, KEY_ESC, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN };

static int Pad_menu_polls = 0;
static UI_WINDOW *Pad_menu_window = NULL;
static int Pad_menu_window_poll = 0;
static int Pad_menu_region = -1;
static int Pad_menu_region_poll = 0;
static int Pad_menu_event_count = 0;
static int Pad_menu_enter_down = 0;		// Confirm sent Enter as well as the click
static int Pad_menu_last_ms = 0;
static float Pad_menu_rem_x = 0.0f;		// the part of a pixel not moved yet
static float Pad_menu_rem_y = 0.0f;

void pad_menu_note_window(UI_WINDOW *w)
{
	Pad_menu_window = w;
	Pad_menu_window_poll = Pad_menu_polls;
}

void pad_menu_forget_window(UI_WINDOW *w)
{
	if (Pad_menu_window == w) {
		Pad_menu_window = NULL;
	}
}

void pad_menu_note_region(int region)
{
	Pad_menu_region = region;
	Pad_menu_region_poll = Pad_menu_polls;
}

int pad_menu_region_under_cursor()
{
	return ((Pad_menu_polls - Pad_menu_region_poll) <= PAD_MENU_STALE_POLLS) ? Pad_menu_region : -1;
}

int pad_menu_under_cursor()
{
	if (pad_menu_region_under_cursor() >= 0) {
		return 1;
	}
	return Pad_menu_window && ((Pad_menu_polls - Pad_menu_window_poll) <= PAD_MENU_STALE_POLLS) && Pad_menu_window->mouse_over_gadget();
}

int pad_menu_events()
{
	return Pad_menu_event_count;
}

// the menus: every state but flight and the two death views, and popups whichever state is under them
static int pad_menu_on_a_menu()
{
	int state;

	if (popup_active() || popupdead_is_active()) {
		return 1;
	}
	state = gameseq_get_state();
	return (state != GS_STATE_GAME_PLAY) && (state != GS_STATE_DEATH_DIED) && (state != GS_STATE_DEATH_BLEW_UP);
}

static int pad_menu_read(pad_state *st)
{
	if (pad_menu_test_on()) {
		pad_menu_test_state(st);
		return st->connected;
	}

	// a legacy joystick took input at startup (no pad then): the pad stays off, never both
	if (joy_num_sticks > 0) {
		memset(st, 0, sizeof(*st));
		return 0;
	}
	return pad_backend_poll(st);
}

int pad_menu_read_for_flight(pad_state *st)
{
	int connected = pad_menu_read(st);

	pad_menu_mask(st);
	return connected;
}

// adds the stick to wherever the cursor is, so a real mouse works alongside it
static void pad_menu_move_cursor(float speed_x, float speed_y, int ms)
{
	float dx, dy;
	int mx, my, ix, iy;

	if ((speed_x == 0.0f) && (speed_y == 0.0f)) {
		Pad_menu_rem_x = 0.0f;
		Pad_menu_rem_y = 0.0f;
		return;
	}

	dx = speed_x * gr_screen.max_w * ms / 1000.0f + Pad_menu_rem_x;
	dy = speed_y * gr_screen.max_w * ms / 1000.0f + Pad_menu_rem_y;
	ix = (int) dx;
	iy = (int) dy;
	Pad_menu_rem_x = dx - ix;
	Pad_menu_rem_y = dy - iy;
	if (!ix && !iy) {
		return;
	}

	mouse_get_real_pos(&mx, &my);
	mx += ix;
	my += iy;
	if (mx < 0) mx = 0;
	if (my < 0) my = 0;
	if (mx >= gr_screen.max_w) mx = gr_screen.max_w - 1;
	if (my >= gr_screen.max_h) my = gr_screen.max_h - 1;

	if (pad_menu_test_on()) {
		pad_menu_test_move_cursor(mx, my);
	} else {
		mouse_set_pos(mx, my);
	}
}

static void pad_menu_key(int key, int down)
{
	key_mark(key, down, 0);
	Pad_menu_event_count++;
}

static void pad_menu_click(int down)
{
	mouse_mark_button(MOUSE_LEFT_BUTTON, down);
	Pad_menu_event_count++;
}

void pad_menu_poll()
{
	pad_state st;
	pad_menu_output out;
	int active, now, ms, under, test_key, i;

	// a replay gets all of this from the recording
	if (Replay_mode == REPLAY_PLAY) {
		return;
	}

	Pad_menu_polls++;

	replay_push();

	pad_game_settings_read();
	pad_menu_test_frame();
	if (!pad_menu_read(&st)) {
		memset(&st, 0, sizeof(st));
	}

	active = pad_menu_on_a_menu() && (pad_menu_test_on() || os_foreground());

	now = timer_get_milliseconds();
	ms = now - Pad_menu_last_ms;
	Pad_menu_last_ms = now;
	if ((ms < 0) || (ms > PAD_MENU_MAX_STEP_MS)) {
		ms = PAD_MENU_MAX_STEP_MS;
	}

	pad_menu_update(&st, active, &out);

	if (mouse_is_visible()) {
		pad_menu_move_cursor(out.cursor_x, out.cursor_y, ms);
	}

	under = out.pressed[PAD_MENU_CONFIRM] ? pad_menu_under_cursor() : 0;
	test_key = pad_menu_test_take_key();

	replay_pop();

	if (test_key) {
		key_mark(test_key, 1, 0);
		key_mark(test_key, 0, 0);
	}

	// Confirm is a click, always, so whatever reads the mouse sees it (ship icons, map icons). With
	// no gadget or region under the cursor it is also Enter, the game's own key for a screen's
	// default action. Never Ctrl+Enter: a commit is only ever a click on Commit.
	if (out.pressed[PAD_MENU_CONFIRM]) {
		pad_menu_click(1);
		if (!under) {
			pad_menu_key(KEY_ENTER, 1);
			Pad_menu_enter_down = 1;
		}
	}
	if (out.released[PAD_MENU_CONFIRM]) {
		pad_menu_click(0);
		if (Pad_menu_enter_down) {
			pad_menu_key(KEY_ENTER, 0);
			Pad_menu_enter_down = 0;
		}
	}

	for (i = 0; i < PAD_MENU_NUM; i++) {
		if (i == PAD_MENU_CONFIRM) {
			continue;
		}
		if (out.pressed[i]) {
			pad_menu_key(Pad_menu_keys[i], 1);
		}
		if (out.released[i]) {
			pad_menu_key(Pad_menu_keys[i], 0);
		}
	}
}
