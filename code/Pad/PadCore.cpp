// PadCore.cpp
// Control pad core: gestures, bindings, layers and stick shaping.
// Platform-free and game-free; see PadCore.h.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "PadCore.h"

typedef struct pad_input_track {
	int	down;
	int	pressed;			// went down in this update
	int	released;		// came up in this update
	int	press_ms;		// when the current or last press started
	int	release_ms;		// when the last press ended
	int	second;			// the current or last press is the second tap of a double tap
} pad_input_track;

static pad_settings				Pad_settings;
static int							Pad_settings_set = 0;
static const pad_binding		*Pad_bindings = NULL;
static int							Pad_num_bindings = 0;
static int							Pad_layer = PAD_LAYER_BASE;
static pad_input_track			Pad_track[PAD_NUM_INPUTS];
static int							Pad_last_update_ms = 0;
static int							Pad_have_update = 0;

void pad_core_default_settings(pad_settings *s)
{
	s->invert_pitch = 0;
	s->invert_yaw = 0;
	s->invert_roll = 0;
	s->left_sensitivity = 1.0f;
	s->right_sensitivity = 1.0f;
	s->left_deadzone = 0.15f;
	s->right_deadzone = 0.15f;
	s->curve = 2.0f;
	s->trigger_deadzone = 0.05f;
	s->trigger_threshold = 0.3f;
	s->double_tap_ms = 250;
	s->throttle_rate = 100.0f;
}

int pad_core_parse_settings(pad_settings *s, const char *text)
{
	char line[256], name[64];
	float value;
	int count = 0;

	while (*text) {
		int len = 0;
		char *hash;

		while (text[len] && (text[len] != '\n')) {
			len++;
		}

		if (len >= (int) sizeof(line)) {
			len = sizeof(line) - 1;
		}
		memcpy(line, text, len);
		line[len] = 0;

		text += len;
		while (*text && (*text != '\n')) {
			text++;
		}
		if (*text == '\n') {
			text++;
		}

		hash = strchr(line, '#');
		if (hash) {
			*hash = 0;
		}

		if (sscanf(line, " %63[A-Za-z_] = %f", name, &value) != 2) {
			continue;
		}

		count++;
		if (!stricmp(name, "invert_pitch"))					s->invert_pitch = (value != 0.0f);
		else if (!stricmp(name, "invert_yaw"))				s->invert_yaw = (value != 0.0f);
		else if (!stricmp(name, "invert_roll"))			s->invert_roll = (value != 0.0f);
		else if (!stricmp(name, "left_sensitivity"))		s->left_sensitivity = value;
		else if (!stricmp(name, "right_sensitivity"))	s->right_sensitivity = value;
		else if (!stricmp(name, "left_deadzone"))			s->left_deadzone = value;
		else if (!stricmp(name, "right_deadzone"))		s->right_deadzone = value;
		else if (!stricmp(name, "curve"))					s->curve = value;
		else if (!stricmp(name, "trigger_deadzone"))		s->trigger_deadzone = value;
		else if (!stricmp(name, "trigger_threshold"))	s->trigger_threshold = value;
		else if (!stricmp(name, "double_tap_ms"))			s->double_tap_ms = (int) value;
		else if (!stricmp(name, "throttle_rate"))			s->throttle_rate = value;
		else count--;
	}

	return count;
}

void pad_core_set_settings(const pad_settings *s)
{
	Pad_settings = *s;
	Pad_settings_set = 1;
}

void pad_core_set_bindings(const pad_binding *bindings, int count)
{
	Pad_bindings = bindings;
	Pad_num_bindings = count;
}

void pad_core_set_layer(int layer)
{
	Pad_layer = layer;
}

static float pad_clamp1(float v)
{
	if (v > 1.0f) return 1.0f;
	if (v < -1.0f) return -1.0f;
	return v;
}

// radial deadzone, rescaled so output starts at 0 at the deadzone edge, then curve and sensitivity
static void pad_shape_stick(float x, float y, float deadzone, float curve, float sensitivity, float *ox, float *oy)
{
	float mag = (float) sqrt(x * x + y * y);
	float m;

	if ((mag <= deadzone) || (deadzone >= 1.0f)) {
		*ox = *oy = 0.0f;
		return;
	}

	m = ((mag > 1.0f ? 1.0f : mag) - deadzone) / (1.0f - deadzone);
	if (curve > 0.0f) {
		m = (float) pow(m, curve);
	}
	m *= sensitivity;

	*ox = pad_clamp1(x / mag * m);
	*oy = pad_clamp1(y / mag * m);
}

static float pad_shape_trigger(float t, float deadzone)
{
	if ((t <= deadzone) || (deadzone >= 1.0f)) {
		return 0.0f;
	}
	return (t - deadzone) / (1.0f - deadzone);
}

static int pad_input_down(const pad_state *st, int input)
{
	if (input < PAD_NUM_BUTTONS) {
		return st->button[input];
	}
	if (input == PAD_INPUT_LTRIGGER) {
		return st->axis[PAD_TRIGGER_LEFT] > Pad_settings.trigger_threshold;
	}
	return st->axis[PAD_TRIGGER_RIGHT] > Pad_settings.trigger_threshold;
}

static void pad_add(int *list, int *num, int action)
{
	int i;

	for (i = 0; i < *num; i++) {
		if (list[i] == action) {
			return;
		}
	}
	if (*num < PAD_MAX_OUT) {
		list[(*num)++] = action;
	}
}

void pad_core_update(const pad_state *st, int now_ms, pad_output *out)
{
	int i, stale;
	float x, y;

	memset(out, 0, sizeof(*out));

	if (!Pad_settings_set) {
		pad_core_default_settings(&Pad_settings);
		Pad_settings_set = 1;
	}

	if (!st->connected) {
		memset(Pad_track, 0, sizeof(Pad_track));
		Pad_have_update = 0;
		return;
	}

	stale = !Pad_have_update || ((now_ms - Pad_last_update_ms) > PAD_CORE_STALE_MS);
	Pad_have_update = 1;
	Pad_last_update_ms = now_ms;

	// gestures
	for (i = 0; i < PAD_NUM_INPUTS; i++) {
		pad_input_track *t = &Pad_track[i];
		int down = pad_input_down(st, i);

		t->pressed = t->released = 0;

		if (stale) {
			// treat anything already held as held, without edges or taps
			if (down && !t->down) {
				t->press_ms = now_ms;
			}
			t->down = down;
			t->second = 0;
			t->release_ms = now_ms - Pad_settings.double_tap_ms - 1;
			continue;
		}

		if (down && !t->down) {
			// second tap: the last press was a short tap, ended recently, and was not itself a second tap
			int last_len = t->release_ms - t->press_ms;
			t->second = !t->second && (last_len <= Pad_settings.double_tap_ms) && ((now_ms - t->release_ms) <= Pad_settings.double_tap_ms);
			t->press_ms = now_ms;
			t->pressed = 1;
		} else if (!down && t->down) {
			t->release_ms = now_ms;
			t->released = 1;
		}

		t->down = down;
	}

	// bindings
	for (i = 0; i < Pad_num_bindings; i++) {
		const pad_binding *b = &Pad_bindings[i];
		const pad_input_track *t;

		if ((b->layer != PAD_LAYER_ANY) && (b->layer != Pad_layer)) {
			continue;
		}
		if ((b->input < 0) || (b->input >= PAD_NUM_INPUTS)) {
			continue;
		}

		t = &Pad_track[b->input];
		switch (b->gesture) {
		case PAD_G_PRESS:
			if (t->pressed) pad_add(out->fired, &out->num_fired, b->action);
			break;
		case PAD_G_RELEASE:
			if (t->released) pad_add(out->fired, &out->num_fired, b->action);
			break;
		case PAD_G_HOLD:
			if (t->down) pad_add(out->held, &out->num_held, b->action);
			break;
		case PAD_G_DOUBLE_TAP:
			if (t->released && t->second) pad_add(out->fired, &out->num_fired, b->action);
			break;
		case PAD_G_DOUBLE_TAP_HOLD:
			if (t->down && t->second) pad_add(out->held, &out->num_held, b->action);
			break;
		}
	}

	// flight: left stick yaw and pitch, right stick X roll, left trigger throttle up
	out->connected = 1;

	pad_shape_stick(st->axis[PAD_LEFT_X], st->axis[PAD_LEFT_Y], Pad_settings.left_deadzone, Pad_settings.curve, Pad_settings.left_sensitivity, &x, &y);
	out->yaw = Pad_settings.invert_yaw ? -x : x;
	out->pitch = Pad_settings.invert_pitch ? -y : y;		// stick pulled back (+y) = nose up

	pad_shape_stick(st->axis[PAD_RIGHT_X], st->axis[PAD_RIGHT_Y], Pad_settings.right_deadzone, Pad_settings.curve, Pad_settings.right_sensitivity, &x, &y);
	out->roll = Pad_settings.invert_roll ? -x : x;

	out->throttle_rate = pad_shape_trigger(st->axis[PAD_TRIGGER_LEFT], Pad_settings.trigger_deadzone) * Pad_settings.throttle_rate;
}

int pad_output_is_held(const pad_output *out, int action)
{
	int i;

	for (i = 0; i < out->num_held; i++) {
		if (out->held[i] == action) {
			return 1;
		}
	}
	return 0;
}
