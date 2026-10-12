// PadGame.cpp
// Control pad glue for FS2. See PadGame.h, and docs/PAD_CONTROL.md for the layout.

#include <stdio.h>
#include <string.h>

#include "PadGame.h"
#include "PadCore.h"
#include "PadXInput.h"

#include "Physics.h"
#include "Player.h"
#include "ControlsConfig.h"
#include "KeyControl.h"
#include "Timer.h"

extern int joy_num_sticks;					// Joy.cpp: 1 if the legacy joystick code is reading a device

#define PAD_SETTINGS_FILE		"pad.cfg"		// in the game folder; optional
#define PAD_ACTION_PAUSE		1000				// not a Control_config id
#define PAD_ACTION_SLOW		1001				// held: lower the throttle

// Core layout. Everything else stays on the keyboard for now.
static const pad_binding Pad_game_bindings[] = {
	{ PAD_INPUT_RTRIGGER,	PAD_G_HOLD,					PAD_LAYER_BASE,	FIRE_PRIMARY },
	{ PAD_DPAD_UP,				PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_TOP },
	{ PAD_DPAD_DOWN,			PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_BOTTOM },
	{ PAD_DPAD_LEFT,			PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_LEFT },
	{ PAD_DPAD_RIGHT,			PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_RIGHT },
	{ PAD_INPUT_LTRIGGER,	PAD_G_DOUBLE_TAP_HOLD,	PAD_LAYER_BASE,	AFTERBURNER },
	{ PAD_RSHOULDER,			PAD_G_HOLD,					PAD_LAYER_BASE,	FIRE_SECONDARY },
	{ PAD_LSHOULDER,			PAD_G_HOLD,					PAD_LAYER_BASE,	PAD_ACTION_SLOW },
	{ PAD_LSHOULDER,			PAD_G_DOUBLE_TAP,			PAD_LAYER_BASE,	ZERO_THROTTLE },
	{ PAD_START,				PAD_G_PRESS,				PAD_LAYER_ANY,		PAD_ACTION_PAUSE },
};

static int				Pad_game_inited = 0;
static pad_output		Pad_game_out;
static pad_settings		Pad_game_settings;

static void pad_game_init()
{
	pad_settings s;
	FILE *fp;

	Pad_game_inited = 1;

	pad_core_default_settings(&s);

	fp = fopen(PAD_SETTINGS_FILE, "rb");
	if (fp) {
		static char text[8192];
		int len = fread(text, 1, sizeof(text) - 1, fp);
		fclose(fp);
		text[len > 0 ? len : 0] = 0;
		mprintf(("Pad: read %d settings from %s\n", pad_core_parse_settings(&s, text), PAD_SETTINGS_FILE));
	}

	Pad_game_settings = s;
	pad_core_set_settings(&s);
	pad_core_set_bindings(Pad_game_bindings, sizeof(Pad_game_bindings) / sizeof(Pad_game_bindings[0]));
}

static void pad_game_update()
{
	pad_state st;

	if (!Pad_game_inited) {
		pad_game_init();
	}

	// a legacy joystick took input at startup (no pad then): the pad stays off, never both
	if (joy_num_sticks > 0) {
		memset(&st, 0, sizeof(st));
	} else {
		pad_backend_poll(&st);
	}
	pad_core_update(&st, timer_get_milliseconds(), &Pad_game_out);
}

void pad_frame()
{
	int i;

	pad_game_update();

	for (i = 0; i < Pad_game_out.num_fired; i++) {
		int action = Pad_game_out.fired[i];

		if (action == PAD_ACTION_PAUSE) {
			game_process_pause_key();
		} else if ((action >= 0) && (action < CCFG_MAX)) {
			button_info_set(&Player->bi, action);
		}
	}
}

int pad_control_held(int id)
{
	return pad_output_is_held(&Pad_game_out, id);
}

void pad_read_flight(control_info *ci, float frame_time)
{
	if (!Pad_game_out.connected) {
		return;
	}

	// FS2 signs: heading +right, pitch +nose down, bank +left
	ci->heading += Pad_game_out.yaw;
	ci->pitch -= Pad_game_out.pitch;
	ci->bank -= Pad_game_out.roll;

	if (Pad_game_out.throttle_rate > 0.0f) {
		// raising the throttle by hand ends speed matching, or matching would undo it next frame
		Player->flags &= ~PLAYER_FLAGS_MATCH_TARGET;
		ci->forward_cruise_percent += Pad_game_out.throttle_rate * frame_time;
	}

	if (pad_output_is_held(&Pad_game_out, PAD_ACTION_SLOW)) {
		Player->flags &= ~PLAYER_FLAGS_MATCH_TARGET;
		ci->forward_cruise_percent -= Pad_game_settings.throttle_rate * frame_time;
	}
}

int pad_owns_joystick()
{
	pad_state st;

	return pad_backend_poll(&st);
}

int pad_pause_pressed()
{
	int i;

	pad_game_update();

	for (i = 0; i < Pad_game_out.num_fired; i++) {
		if (Pad_game_out.fired[i] == PAD_ACTION_PAUSE) {
			return 1;
		}
	}
	return 0;
}
