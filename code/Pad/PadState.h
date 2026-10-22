// PadState.h
// Normalised control pad state, in the SDL / Godot "standard gamepad" layout.
// Backends fill it; the core reads it. Nothing here knows about Windows or FS2.

#ifndef __PAD_STATE_H__
#define __PAD_STATE_H__

// Buttons, by position. Same order as Godot's JoyButton.
enum {
	PAD_SOUTH,			// 360: A
	PAD_EAST,			// 360: B
	PAD_WEST,			// 360: X
	PAD_NORTH,			// 360: Y
	PAD_BACK,
	PAD_GUIDE,
	PAD_START,
	PAD_LSTICK,
	PAD_RSTICK,
	PAD_LSHOULDER,
	PAD_RSHOULDER,
	PAD_DPAD_UP,
	PAD_DPAD_DOWN,
	PAD_DPAD_LEFT,
	PAD_DPAD_RIGHT,
	PAD_NUM_BUTTONS
};

// Axes. Same order as Godot's JoyAxis.
// Sticks are -1..1 with +x right and +y down. Triggers are 0..1.
enum {
	PAD_LEFT_X,
	PAD_LEFT_Y,
	PAD_RIGHT_X,
	PAD_RIGHT_Y,
	PAD_TRIGGER_LEFT,
	PAD_TRIGGER_RIGHT,
	PAD_NUM_AXES
};

typedef struct pad_state {
	int	connected;
	float	axis[PAD_NUM_AXES];
	int	button[PAD_NUM_BUTTONS];
} pad_state;

#endif
