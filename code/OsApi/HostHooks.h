// HostHooks.h
// Claude: the platform seam (D-051). The live reads the game makes of the machine go through this table, which
// the host fills in. The recorder's wraps stay on the game side of it (Timer.cpp, ...), so a recording cannot tell
// which host filled it. No Windows or engine types here.

#ifndef _HOST_HOOKS_H
#define _HOST_HOOKS_H

#include "pstypes.h"
#include "../Pad/PadState.h"

// a joystick read: calibrated positions of all six axes (x, y, z, r, u, v), the buttons as bits, the POV hat
typedef struct host_joy_state {
	unsigned int	axes[6];
	unsigned int	buttons;
	unsigned int	pov;
} host_joy_state;

// a joystick's name and which axes past x and y it has
typedef struct host_joy_caps {
	char	name[32];
	int	has_z, has_r, has_u, has_v;
} host_joy_caps;

typedef struct host_hooks {
	// time: a counter that only goes up, and its ticks per second (less than 2^32)
	longlong	(*time_counter)();
	longlong	(*time_frequency)();

	// window and events: pump the host's events into the game's sinks (key_mark, mouse_mark, the focus), and
	// whether the game is the foreground application
	void		(*poll_events)();
	int		(*is_foreground)();

	// mouse: the cursor position in the game window's client coordinates, and moving the cursor there
	void		(*mouse_get_pos)(int *x, int *y);
	void		(*mouse_set_pos)(int x, int y);

	// joystick: how many sticks the driver has slots for, a stick's caps, and one read. joy_caps and joy_read
	// return 0 when the stick answered. The game keeps its own button timing (Joy.cpp).
	int		(*joy_count)();
	int		(*joy_caps)(int stick, host_joy_caps *caps);
	int		(*joy_read)(int stick, host_joy_state *state);

	// control pad: fill *out (pad_state, Pad/PadState.h) with the first connected pad and return out->connected;
	// with no pad, zero *out and return 0. Same bits as the pad backend gave: buttons and axes in the standard
	// gamepad order, sticks -1..1 with +y down, triggers 0..1. Safe to call every frame.
	int		(*pad_poll)(pad_state *out);
} host_hooks;

// The table in use. It starts as the Windows one (host_hooks_windows), so tools that never set it still run.
extern host_hooks Host_hooks;

// Fill the table before game_host_init. A null entry is an Error.
void host_hooks_set(const host_hooks *h);

// The Windows implementations (HostHooksWin32.cpp), and the headless host's: the same time counter, no
// events, always in the foreground, the mouse at rest at (0, 0), a joystick driver with no stick attached, and no pad
extern const host_hooks host_hooks_windows;
extern const host_hooks host_hooks_headless;

#endif
