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

static const pad_tree_category	*Pad_tree_categories = NULL;
static int							Pad_tree_num_categories = 0;
static const pad_tree_node		*Pad_tree_nodes = NULL;
static int							Pad_tree_num_nodes = 0;
static int							Pad_tree_category = -1;		// open category, -1 = closed
static int							Pad_tree_stack[PAD_TREE_MAX_DEPTH];
static int							Pad_tree_depth = 0;
static int							Pad_tree_open_ms = 0;

static const int					Pad_tree_dpad[PAD_TREE_NUM_DIRS] = { PAD_DPAD_UP, PAD_DPAD_RIGHT, PAD_DPAD_DOWN, PAD_DPAD_LEFT };

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

void pad_core_set_tree(const pad_tree_category *categories, int num_categories, const pad_tree_node *nodes, int num_nodes)
{
	Pad_tree_categories = categories;
	Pad_tree_num_categories = num_categories;
	Pad_tree_nodes = nodes;
	Pad_tree_num_nodes = num_nodes;
	Pad_tree_category = -1;
	Pad_tree_depth = 0;
}

const pad_tree_node *pad_core_tree_node(int index)
{
	if ((index < 0) || (index >= Pad_tree_num_nodes)) {
		return NULL;
	}
	return &Pad_tree_nodes[index];
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
	if (input == PAD_INPUT_RSTICK_DOWN) {
		return st->axis[PAD_RIGHT_Y] > PAD_STICK_BUTTON;
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

// opens, walks and closes the pad tree from this update's face button and D-pad edges
static void pad_tree_update(int now_ms, pad_output *out)
{
	const pad_tree_category *cat;
	int i;

	// release the face button: close at any depth
	if (Pad_tree_category >= 0) {
		cat = &Pad_tree_categories[Pad_tree_category];
		if (!Pad_track[cat->button].down) {
			if (cat->close_action >= 0) {
				pad_add(out->fired, &out->num_fired, cat->close_action);
			}
			Pad_tree_category = -1;
			Pad_tree_depth = 0;
		}
	}

	// press a face button: open its category
	if (Pad_tree_category < 0) {
		for (i = 0; i < Pad_tree_num_categories; i++) {
			cat = &Pad_tree_categories[i];
			if ((cat->button >= 0) && (cat->button < PAD_NUM_BUTTONS) && Pad_track[cat->button].pressed
				&& (cat->root >= 0) && (cat->root < Pad_tree_num_nodes)) {
				Pad_tree_category = i;
				Pad_tree_stack[0] = cat->root;
				Pad_tree_depth = 1;
				Pad_tree_open_ms = now_ms;
				if (cat->open_action >= 0) {
					pad_add(out->fired, &out->num_fired, cat->open_action);
				}
				break;
			}
		}
	}

	if (Pad_tree_category < 0) {
		return;
	}

	// D-pad: one direction per update
	for (i = 0; i < PAD_TREE_NUM_DIRS; i++) {
		const pad_tree_option *opt;

		if (!Pad_track[Pad_tree_dpad[i]].pressed) {
			continue;
		}

		if ((i == PAD_TREE_LEFT) && (Pad_tree_depth > 1)) {
			Pad_tree_depth--;
			break;
		}

		opt = &Pad_tree_nodes[Pad_tree_stack[Pad_tree_depth - 1]].option[i];
		if ((opt->child >= 0) && (opt->child < Pad_tree_num_nodes)) {
			if (Pad_tree_depth < PAD_TREE_MAX_DEPTH) {
				Pad_tree_stack[Pad_tree_depth++] = opt->child;
			}
		} else if (opt->action >= 0) {
			pad_add(out->fired, &out->num_fired, opt->action);
		}
		break;
	}

	out->tree_category = Pad_tree_category;
	out->tree_node = Pad_tree_stack[Pad_tree_depth - 1];
	out->tree_depth = Pad_tree_depth;
	out->tree_show = (now_ms - Pad_tree_open_ms) >= PAD_TREE_SHOW_MS;
}

void pad_core_update(const pad_state *st, int now_ms, pad_output *out)
{
	int i, stale, layer;
	float x, y;

	memset(out, 0, sizeof(*out));

	if (!Pad_settings_set) {
		pad_core_default_settings(&Pad_settings);
		Pad_settings_set = 1;
	}

	out->tree_category = -1;
	out->tree_node = -1;

	if (!st->connected) {
		memset(Pad_track, 0, sizeof(Pad_track));
		Pad_have_update = 0;
		Pad_tree_category = -1;
		Pad_tree_depth = 0;
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

	pad_tree_update(now_ms, out);
	layer = (Pad_tree_category >= 0) ? PAD_LAYER_TREE : Pad_layer;

	// bindings
	for (i = 0; i < Pad_num_bindings; i++) {
		const pad_binding *b = &Pad_bindings[i];
		const pad_input_track *t;

		if ((b->layer != PAD_LAYER_ANY) && (b->layer != layer)) {
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

	out->throttle_max_rate = Pad_settings.throttle_rate;
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
