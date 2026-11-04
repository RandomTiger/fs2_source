// PadMenu.h
// Control pad on the menus: turns a normalised pad_state into menu buttons and a cursor speed.
//
// Platform-free and game-free, like PadCore: meant to port to Godot as it is. The glue
// (PadMenuGame.cpp) turns the output into the game's own mouse and key events.

#ifndef __PAD_MENU_H__
#define __PAD_MENU_H__

#include "PadState.h"

enum {
	PAD_MENU_CONFIRM,		// South
	PAD_MENU_BACK,			// East
	PAD_MENU_LEFT,			// LB, D-pad left
	PAD_MENU_RIGHT,		// RB, D-pad right
	PAD_MENU_UP,			// D-pad up
	PAD_MENU_DOWN,			// D-pad down
	PAD_MENU_NUM
};

typedef struct pad_menu_output {
	int	pressed[PAD_MENU_NUM];		// went down in this update
	int	released[PAD_MENU_NUM];		// came up in this update
	float	cursor_x;						// screen widths per second, +right
	float	cursor_y;						// screen widths per second, +down
} pad_menu_output;

// Call every time the game polls, on the menus and in flight alike, so the layer sees every button.
// active: the game is on a menu. A button only counts if it goes down while active, so one held
// into a menu does nothing; a menu that closes releases what it had down.
void	pad_menu_update(const pad_state *st, int active, pad_menu_output *out);

// Clears from *st each button whose press the menu took, until that button is physically
// released, so a press on a menu does not go on to do its flight action.
void	pad_menu_mask(pad_state *st);

#endif
