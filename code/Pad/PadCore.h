// PadCore.h
// Control pad core: turns a normalised pad_state into game actions and flight values.
//
// Platform-free and game-free: no Windows, no FS2 types. Actions are plain ints that
// the game glue chooses. This file is meant to port to Godot as it is.

#ifndef __PAD_CORE_H__
#define __PAD_CORE_H__

#include "PadState.h"

// Inputs a binding can use: every button, plus each trigger used as a button, plus right stick down.
#define PAD_INPUT_LTRIGGER		(PAD_NUM_BUTTONS)
#define PAD_INPUT_RTRIGGER		(PAD_NUM_BUTTONS + 1)
#define PAD_INPUT_RSTICK_DOWN	(PAD_NUM_BUTTONS + 2)	// right stick pushed down past PAD_STICK_BUTTON
#define PAD_NUM_INPUTS			(PAD_NUM_BUTTONS + 3)

#define PAD_STICK_BUTTON		0.5f		// a stick direction counts as a pressed button past this

// Gestures. "Fired" gestures happen once; "held" gestures last while the input is down.
enum {
	PAD_G_PRESS,				// fired: input goes down
	PAD_G_RELEASE,				// fired: input comes up
	PAD_G_HOLD,					// held:  input is down
	PAD_G_DOUBLE_TAP,			// fired: second tap of a double tap is released, the tap itself short
	PAD_G_DOUBLE_TAP_HOLD,	// held:  second tap of a double tap is down
	PAD_G_HOLD_PAST_TAP,		// held:  input has been down longer than a tap (double_tap_ms), so a tap does nothing
	PAD_G_LONG_HOLD,			// fired: input held PAD_LONG_HOLD_MS in the row's layer; once per press, an early release cancels
};

#define PAD_LONG_HOLD_MS	1000

// A binding is live when its layer is the current layer, or PAD_LAYER_ANY. A press belongs to the
// layer that was current when it went down, and a row on a named layer ignores a press from another:
// RT pressed under a modifier does not start firing when the modifier comes up, it needs a new press.
// A press held from the base layer stops while a modifier is down and carries on when it comes up.
#define PAD_LAYER_ANY		-1
#define PAD_LAYER_BASE		0
#define PAD_LAYER_TREE		1			// set by the core while a pad tree category is open
#define PAD_LAYER_MOD		2			// a held modifier, see pad_modifier

// A chord on a row: the row also needs another input down (or not down) from the row's own layer.
#define PAD_CHORD_WITH(input)		((input) + 1)
#define PAD_CHORD_WITHOUT(input)	(-((input) + 1))

typedef struct pad_binding {
	int	input;		// PAD_* button or PAD_INPUT_*TRIGGER
	int	gesture;		// PAD_G_*
	int	layer;		// PAD_LAYER_*
	int	action;		// game action id, chosen by the glue
	int	chord;		// 0 = none, or PAD_CHORD_*
} pad_binding;

// While this input is down the base layer gives way to this layer.
typedef struct pad_modifier {
	int	input;
	int	layer;
} pad_modifier;

// A held action the core itself answers: the right stick stops rolling and its Y sets the throttle rate
// (up raises, down lowers, same rate as the left trigger).
#define PAD_CORE_ACTION_SPEED_MODE		2000

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
	float	menu_cursor_speed;	// on the menus, the cursor at full left stick, in screen widths per second
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
	float	throttle_rate;				// percent per second, negative lowers the throttle
	float	throttle_max_rate;		// the throttle_rate setting: percent per second for full-rate throttle actions

	// pad tree
	int	tree_category;				// open category, -1 = closed
	int	tree_node;					// node shown, -1 = closed
	int	tree_depth;					// 1 = category root
	int	tree_show;					// 1 once the category has been held for PAD_TREE_SHOW_MS
} pad_output;

// Pad tree: hold a category's face button and the D-pad picks one of four options. An
// option is a leaf (fires its action, the node stays open so it can be repeated) or a
// branch (opens a child node). Below the root, Left is always Back. Releasing the face
// button closes the tree. While open, bindings on PAD_LAYER_BASE are off, so the D-pad
// does not also do its base action.
enum {
	PAD_TREE_UP,
	PAD_TREE_RIGHT,
	PAD_TREE_DOWN,
	PAD_TREE_LEFT,
	PAD_TREE_NUM_DIRS
};

#define PAD_TREE_MAX_DEPTH		8
#define PAD_TREE_SHOW_MS		200		// hold this long before the options are shown

typedef struct pad_tree_option {
	const char	*label;		// NULL = empty slot
	int			action;		// fired by a leaf, -1 = none
	int			child;		// node index for a branch, -1 = leaf
} pad_tree_option;

typedef struct pad_tree_node {
	const char			*title;
	pad_tree_option	option[PAD_TREE_NUM_DIRS];		// indexed by PAD_TREE_*
} pad_tree_node;

typedef struct pad_tree_category {
	int	button;				// PAD_* face button that opens it
	int	root;					// node index
	int	open_action;		// fired when it opens, -1 = none
	int	close_action;		// fired when it closes, -1 = none
} pad_tree_category;

void	pad_core_default_settings(pad_settings *s);

// Reads "name = value" lines ('#' starts a comment) into *s. Unknown names are ignored.
// Returns the number of settings read.
int	pad_core_parse_settings(pad_settings *s, const char *text);

void	pad_core_set_settings(const pad_settings *s);
void	pad_core_set_bindings(const pad_binding *bindings, int count);	// table is not copied
void	pad_core_set_layer(int layer);
void	pad_core_set_modifiers(const pad_modifier *modifiers, int count);	// table is not copied

// Tables are not copied.
void	pad_core_set_tree(const pad_tree_category *categories, int num_categories, const pad_tree_node *nodes, int num_nodes);
const pad_tree_node	*pad_core_tree_node(int index);		// NULL if out of range

// Call once per game frame. now_ms is any millisecond clock.
// After a gap of more than PAD_CORE_STALE_MS, inputs already held count as held, not pressed,
// so a button held through a menu or a load does not fire when play resumes.
#define PAD_CORE_STALE_MS	250
void	pad_core_update(const pad_state *st, int now_ms, pad_output *out);

int	pad_output_is_held(const pad_output *out, int action);

// The menu cursor's speed from the left stick, in screen widths per second: left_deadzone and curve,
// the feel of flight, with menu_cursor_speed in place of left_sensitivity and no invert.
void	pad_core_menu_cursor(const pad_state *st, float *x, float *y);

#endif
