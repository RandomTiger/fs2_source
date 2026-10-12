// PadCore.h
// Control pad core: turns a normalised pad_state into game actions and flight values.
//
// Platform-free and game-free: no Windows, no FS2 types. Actions are plain ints that
// the game glue chooses. This file is meant to port to Godot as it is.

#ifndef __PAD_CORE_H__
#define __PAD_CORE_H__

#include "PadState.h"

// Inputs a binding can use: every button, plus each trigger used as a button.
#define PAD_INPUT_LTRIGGER		(PAD_NUM_BUTTONS)
#define PAD_INPUT_RTRIGGER		(PAD_NUM_BUTTONS + 1)
#define PAD_NUM_INPUTS			(PAD_NUM_BUTTONS + 2)

// Gestures. "Fired" gestures happen once; "held" gestures last while the input is down.
enum {
	PAD_G_PRESS,				// fired: input goes down
	PAD_G_RELEASE,				// fired: input comes up
	PAD_G_HOLD,					// held:  input is down
	PAD_G_DOUBLE_TAP,			// fired: second tap of a double tap is released
	PAD_G_DOUBLE_TAP_HOLD,	// held:  second tap of a double tap is down
};

// A binding is live when its layer is the current layer, or PAD_LAYER_ANY.
#define PAD_LAYER_ANY		-1
#define PAD_LAYER_BASE		0

typedef struct pad_binding {
	int	input;		// PAD_* button or PAD_INPUT_*TRIGGER
	int	gesture;		// PAD_G_*
	int	layer;		// PAD_LAYER_*
	int	action;		// game action id, chosen by the glue
} pad_binding;

typedef struct pad_settings {
	int	invert_pitch;
	int	invert_yaw;
	int	invert_roll;
	float	left_sensitivity;		// multiplier after the curve
	float	right_sensitivity;
	float	left_deadzone;			// 0..1, radial
	float	right_deadzone;
	float	curve;					// response exponent: 1 = linear, >1 = finer near centre
	float	trigger_deadzone;		// 0..1, below this a trigger reads 0
	float	trigger_threshold;	// 0..1, a trigger counts as a pressed button above this
	int	double_tap_ms;			// max gap between taps, and max length of the first tap
	float	throttle_rate;			// throttle change per second at full left trigger, in percent
} pad_settings;

#define PAD_MAX_OUT		32

typedef struct pad_output {
	int	connected;

	int	num_fired;					// one-shot actions from this update
	int	fired[PAD_MAX_OUT];

	int	num_held;					// actions held down in this update
	int	held[PAD_MAX_OUT];

	// flight, after deadzone, curve, sensitivity and invert
	float	yaw;							// +right,    -1..1
	float	pitch;						// +nose up,  -1..1
	float	roll;							// +right,    -1..1
	float	throttle_rate;				// percent per second, >= 0
} pad_output;

void	pad_core_default_settings(pad_settings *s);

// Reads "name = value" lines ('#' starts a comment) into *s. Unknown names are ignored.
// Returns the number of settings read.
int	pad_core_parse_settings(pad_settings *s, const char *text);

void	pad_core_set_settings(const pad_settings *s);
void	pad_core_set_bindings(const pad_binding *bindings, int count);	// table is not copied
void	pad_core_set_layer(int layer);

// Call once per game frame. now_ms is any millisecond clock.
// After a gap of more than PAD_CORE_STALE_MS, inputs already held count as held, not pressed,
// so a button held through a menu or a load does not fire when play resumes.
#define PAD_CORE_STALE_MS	250
void	pad_core_update(const pad_state *st, int now_ms, pad_output *out);

int	pad_output_is_held(const pad_output *out, int action);

#endif
