// PadMenu.cpp
// Control pad on the menus. See PadMenu.h, and docs/PAD_CONTROL.md for the layout.

#include <string.h>

#include "PadMenu.h"
#include "PadCore.h"

typedef struct pad_menu_source {
	int	button;		// PAD_*
	int	action;		// PAD_MENU_*
} pad_menu_source;

static const pad_menu_source Pad_menu_sources[] = {
	{ PAD_SOUTH,		PAD_MENU_CONFIRM },
	{ PAD_EAST,			PAD_MENU_BACK },
	{ PAD_LSHOULDER,	PAD_MENU_LEFT },
	{ PAD_DPAD_LEFT,	PAD_MENU_LEFT },
	{ PAD_RSHOULDER,	PAD_MENU_RIGHT },
	{ PAD_DPAD_RIGHT,	PAD_MENU_RIGHT },
	{ PAD_DPAD_UP,		PAD_MENU_UP },
	{ PAD_DPAD_DOWN,	PAD_MENU_DOWN },
};
#define PAD_MENU_SOURCES	((int)(sizeof(Pad_menu_sources) / sizeof(Pad_menu_sources[0])))

static int Pad_menu_was_down[PAD_NUM_BUTTONS];	// physically down at the last update
static int Pad_menu_taken[PAD_NUM_BUTTONS];		// went down on a menu and has not been released since
static int Pad_menu_live[PAD_NUM_BUTTONS];		// taken, and still on a menu
static int Pad_menu_action_down[PAD_MENU_NUM];

void pad_menu_update(const pad_state *st, int active, pad_menu_output *out)
{
	int now_down[PAD_MENU_NUM];
	int i;

	memset(out, 0, sizeof(*out));
	memset(now_down, 0, sizeof(now_down));

	for (i = 0; i < PAD_MENU_SOURCES; i++) {
		int b = Pad_menu_sources[i].button;
		int down = st->connected && st->button[b];

		if (!down) {
			Pad_menu_taken[b] = 0;
			Pad_menu_live[b] = 0;
		} else if (active && !Pad_menu_was_down[b]) {
			Pad_menu_taken[b] = 1;
			Pad_menu_live[b] = 1;
		}
		if (!active) {
			Pad_menu_live[b] = 0;
		}
		Pad_menu_was_down[b] = down;

		if (Pad_menu_live[b]) {
			now_down[Pad_menu_sources[i].action] = 1;
		}
	}

	// North has no menu action, but its flight action is Fire Secondary: a press that goes down on a
	// menu is held back from flight as well. Down before the menu opened, it keeps acting (D-20261105-211449).
	{
		int down = st->connected && st->button[PAD_NORTH];

		if (!down) {
			Pad_menu_taken[PAD_NORTH] = 0;
		} else if (active && !Pad_menu_was_down[PAD_NORTH]) {
			Pad_menu_taken[PAD_NORTH] = 1;
		}
		Pad_menu_was_down[PAD_NORTH] = down;
	}

	for (i = 0; i < PAD_MENU_NUM; i++) {
		out->pressed[i] = now_down[i] && !Pad_menu_action_down[i];
		out->released[i] = !now_down[i] && Pad_menu_action_down[i];
		Pad_menu_action_down[i] = now_down[i];
	}

	if (active && st->connected) {
		pad_core_menu_cursor(st, &out->cursor_x, &out->cursor_y);
	}
}

void pad_menu_mask(pad_state *st)
{
	int i;

	for (i = 0; i < PAD_MENU_SOURCES; i++) {
		int b = Pad_menu_sources[i].button;

		if (Pad_menu_taken[b]) {
			st->button[b] = 0;
		}
	}
	if (Pad_menu_taken[PAD_NORTH]) {
		st->button[PAD_NORTH] = 0;
	}
}
