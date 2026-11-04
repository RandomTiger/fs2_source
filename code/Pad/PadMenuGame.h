// PadMenuGame.h
// Control pad glue for the menus: the pad works the game's own UI through its mouse and key events.
// See docs/PAD_CONTROL.md.

#ifndef __PAD_MENU_GAME_H__
#define __PAD_MENU_GAME_H__

#include "PadState.h"

class UI_WINDOW;

// From the top of game_poll(), which every menu loop goes through. Before the first recorded read
// there, so that a replay has the events back before the key_inkey() of the same poll.
void	pad_menu_poll();

// From UI_WINDOW::process and UI_WINDOW::destroy: the window on screen, for what is under the cursor.
void	pad_menu_note_window(UI_WINDOW *w);
void	pad_menu_forget_window(UI_WINDOW *w);

// From snazzy_menu_do(): the region under the cursor, -1 if none.
void	pad_menu_note_region(int region);

// The pad as flight sees it: its state, without the buttons a menu took. Returns connected.
int	pad_menu_read_for_flight(pad_state *st);

// What the test script reads.
int	pad_menu_under_cursor();			// 1 if a gadget or a region is under the cursor
int	pad_menu_region_under_cursor();	// the region, -1 if none
int	pad_menu_events();					// key and mouse events the layer has sent

// Scripted fake pad, -at_padmenu (PadMenuTest.cpp): feeds pad_state below this layer. Never in a replay.
void	pad_menu_test_set();
int	pad_menu_test_on();
int	pad_menu_test_front_end();					// 1 while autotest must leave the main hall alone
int	pad_menu_test_holds_briefing(int state);	// 1 while autotest must leave this briefing screen alone
void	pad_menu_test_frame();						// once per pad_menu_poll, before the pad is read
void	pad_menu_test_state(pad_state *st);
void	pad_menu_test_move_cursor(int x, int y);
void	pad_menu_test_cursor(long *x, long *y);	// the script's cursor in place of the real one
int	pad_menu_test_take_key();					// a key the script wants pressed, 0 if none
void	pad_menu_test_check_finished();			// the run ends: the script must have got to its end
int	pad_menu_test_find_pixel(const unsigned char *mask, int w, int h, int value, int *x, int *y);

#endif
