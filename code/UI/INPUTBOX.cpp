/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/

/*
 * $Logfile: /Freespace2/code/Ui/INPUTBOX.cpp $
 * $Revision: 8 $
 * $Date: 9/07/99 4:01p $
 * $Author: Dave $
 *
 * Code to implement input boxes
 *
 * $Log: /Freespace2/code/Ui/INPUTBOX.cpp $
 * 
 * 8     9/07/99 4:01p Dave
 * Fixed up a string.tbl paroblem (self destruct message). Make sure IPX
 * does everything properly (setting up address when binding). Remove
 * black rectangle background from UI_INPUTBOX.
 * 
 * 7     7/15/99 7:14p Jefff
 * 
 * 6     6/25/99 11:59a Dave
 * Multi options screen.
 * 
 * 5     2/11/99 3:08p Dave
 * PXO refresh button. Very preliminary squad war support.
 * 
 * 4     12/02/98 5:47p Dave
 * Put in interface xstr code. Converted barracks screen to new format.
 * 
 * 3     10/13/98 9:29a Dave
 * Started neatening up freespace.h. Many variables renamed and
 * reorganized. Added AlphaColors.[h,cpp]
 * 
 * 2     10/07/98 10:54a Dave
 * Initial checkin.
 * 
 * 1     10/07/98 10:51a Dave
 * 
 * 43    5/14/98 6:29p Hoffoss
 * Fixed some warnings a release rebuild all turned up.
 * 
 * 42    4/14/98 5:06p Dave
 * Don't load or send invalid pilot pics. Fixed chatbox graphic errors.
 * Made chatbox display team icons in a team vs. team game. Fixed up pause
 * and endgame sequencing issues.
 * 
 * 41    4/12/98 5:31p Lawrance
 * use timer_get_milliseconds() instead of gettime()
 * 
 * 40    4/10/98 5:36p Dave
 * Put in user notification of illegal values in multi host options
 * screen. Fixed server respawn ship class problem.
 * 
 * 39    4/09/98 3:10p Mike
 * Fix shockingly stupid typo.
 * 
 * 38    4/09/98 2:46p Mike
 * Fix bug preventing first letter in pilot name being capitalized.
 * 
 * 37    4/09/98 12:12p Mike
 * Separate versioning for demo and full versions.
 * Fix inputbox bugs.
 * 
 * 36    4/06/98 5:14p Hoffoss
 * Added some needed asserts.
 * 
 * 35    4/06/98 4:25p Allender
 * Fix strncpy bug in inputbox
 * 
 * 34    4/06/98 3:58p Frank
 * added initialization to try and fix a bug.
 * 
 * 33    4/01/98 10:26a Hoffoss
 * Changed input box code to not draw a cursor if the input box is
 * disabled.
 * 
 * 32    3/27/98 4:01p Hoffoss
 * Fixed bug where input box text wsn't being null terminated!  Bad!
 * 
 * 31    3/27/98 3:04p Mitri
 * Fixed bug in set_text().  'length' is the number of characters allowed,
 * EXCLUDING the null terminator.
 * 
 * 30    3/27/98 11:20a Hoffoss
 * Changed input to use bright white text instead of bright green.
 * 
 * 29    3/10/98 11:29a Hoffoss
 * Make first_time ignored and text not disappear after a return is
 * pressed.
 * 
 * 28    2/26/98 4:21p Dave
 * More robust multiplayer voice.
 * 
 * 27    1/23/98 5:43p Dave
 * Finished bringing standalone up to speed. Coded in new host options
 * screen.
 * 
 * 26    1/20/98 10:36a Hoffoss
 * Fixed optimized warnings.
 * 
 * 25    1/17/98 5:51p Dave
 * Bug fixes for bugs generated by multiplayer testing.
 * 
 * 24    1/16/98 7:57p Lawrance
 * support animating input box cursor
 * 
 * 23    1/15/98 5:12p Hoffoss
 * Fixed inputbox so clicking on it gives it focus.
 * 
 * 22    1/15/98 5:10p Allender
 * ton of interface changes.  chatbox in multiplayer now behaves
 * differently than before.  It's always active in any screen that uses
 * it.  Only non-printatble characters will get passed back out from
 * chatbox
 * 
 * 21    1/14/98 6:43p Hoffoss
 * Massive changes to UI code.  A lot cleaner and better now.  Did all
 * this to get the new UI_DOT_SLIDER to work properly, which the old code
 * wasn't flexible enough to handle.
 * 
 * 20    1/05/98 1:15p John
 * Made inputbox skip the initial "selected" phase when you first tab to
 * it.
 * 
 * 19    12/22/97 5:08p Hoffoss
 * Changed inputbox class to be able to accept only certain keys, changed
 * pilot screens to utilize this feature.  Added to assert with pilot file
 * saving.
 * 
 * 18    12/11/97 8:15p Dave
 * Put in network options screen. Xed out olf protocol selection screen.
 * 
 * 17    12/08/97 6:22p Lawrance
 * blink cursor on inputbox
 * 
 * 16    12/06/97 4:27p Dave
 * Another load of interface and multiplayer bug fixes.
 * 
 * 15    11/25/97 3:51p Hoffoss
 * Changed edit background rect position slightly.
 * 
 * 14    10/24/97 10:58p Hoffoss
 * Made some changes to the UI code to do some things I need it to do.
 * 
 * 13    10/09/97 11:05a Allender
 * ignore controled and alted keys in input box
 * 
 * 12    10/01/97 4:39p Lawrance
 * null out text when free'ed
 * 
 * 11    9/09/97 3:39p Sandeep
 * warning level 4 bugs
 * 
 * 10    8/21/97 12:13p Dave
 * Made it possible for input box to ignore esc to lose focus.
 * 
 * 9     8/19/97 1:28p Dave
 * Made it possible to limit characters by pixel width.
 * 
 * 8     8/15/97 8:21p Dave
 * Modified UI_INPUTBOX so that it is possible to draw it invisibly. That
 * is, only the text is displayed.
 * 
 * 7     8/11/97 9:48p Lawrance
 * reset clip after drawing
 * 
 * 6     7/01/97 2:10p Dave
 * Fixed bug which caused the box to stop responding after user called
 * set_text(...) with a 0 length string.
 * 
 * 5     6/12/97 12:39p John
 * made ui use freespace colors
 * 
 * 4     6/11/97 1:13p John
 * Started fixing all the text colors in the game.
 * 
 * 3     12/03/96 3:46p Lawrance
 * added ability to set contents of input box
 * 
 * 2     11/15/96 11:43a John
 * 
 * 1     11/15/96 8:21a John
 *
 * $NoKeywords: $
 */

#include <ctype.h>
#include "UiDefs.h"
#include "Ui.h"
#include "bmpman.h"
#include "timer.h"
#include "AlphaColors.h"


#define INPUTBOX_PASSWD_CHAR        '*'   // the password protected char

//	Retuen true if c is a letter, else return false.
int is_letter(char c)
{
	return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
}

// if the passed key is keypad number, return the ascii value, otherwise -1
int keypad_to_ascii(int c)
{
	switch(c){
	case KEY_PAD0:
		return key_to_ascii(KEY_0);
		break;
	case KEY_PAD1:
		return key_to_ascii(KEY_1);
		break;
	case KEY_PAD2:
		return key_to_ascii(KEY_2);
		break;
	case KEY_PAD3:
		return key_to_ascii(KEY_3);
		break;
	case KEY_PAD4:
		return key_to_ascii(KEY_4);
		break;
	case KEY_PAD5:
		return key_to_ascii(KEY_5);
		break;
	case KEY_PAD6:
		return key_to_ascii(KEY_6);
		break;
	case KEY_PAD7:
		return key_to_ascii(KEY_7);
		break;
	case KEY_PAD8:
		return key_to_ascii(KEY_8);
		break;
	case KEY_PAD9:
		return key_to_ascii(KEY_9);
		break;
	case KEY_PADPERIOD:
		return key_to_ascii(KEY_PERIOD);
		break;
	default :
		return -1;
		break;
	}
}

// insert character c into string s at position p.
void strcins(char *s, int p, char c)
{
	int n;
	for (n=strlen(s)-p; n>=0; n-- )
		*(s+p+n+1) = *(s+p+n);   // Move everything over	
	*(s+p) = c;         // then insert the character
}

// delete n character from string s starting at position p

void strndel(char *s, int p, int n)
{
	for (; (*(s+p) = *(s+p+n)) != '\0'; s++ )
		*(s+p+n) = '\0';    // Delete and zero fill
}

void UI_INPUTBOX::init_cursor()
{
	cursor_first_frame = bm_load_animation("cursor1", &cursor_nframes, &cursor_fps);
	if ( cursor_first_frame < 0 ) {
		Warning(LOCATION,"Cannot load input box cursor: cursor1.ani\n");
		return;
	}
	cursor_elapsed_time=0;
	cursor_current_frame=0;
}

void UI_INPUTBOX::create(UI_WINDOW *wnd, int _x, int _y, int _w, int _text_len, char *_text, int _flags, int pixel_lim, color *clr)
{
	int tw, th;

	Assert(_text_len >= 0);
	Assert((int) strlen(_text) <= _text_len);
	gr_set_font(wnd->f_id);
	gr_get_string_size( &tw, &th, "*" );

	// check to see if the user passed in a text color otherwise use the default green color
	if (clr){
		text_color = clr;
	} else {
		text_color = &CBRIGHT;
	}

	base_create( wnd, UI_KIND_INPUTBOX, _x, _y, _w, th+4 );
	text = (char *) malloc( _text_len + 1);

	// input boxes no longer use background
	_flags |= UI_INPUTBOX_FLAG_NO_BACK;
	
	// if its in "password" mode, allocate a second string
	// and copy it
	if (_flags & UI_INPUTBOX_FLAG_PASSWD) {
		passwd_text = (char *) malloc(_text_len + 1);
		memset(passwd_text, INPUTBOX_PASSWD_CHAR, strlen(_text));
		passwd_text[strlen(_text)] = 0;

	} else {
		passwd_text = NULL;
	}

	init_cursor();

	if ( _text_len > 0 ) {
		strncpy( text, _text, _text_len );
	}
	text[_text_len] = 0;
	position = strlen(_text);
	oldposition = position;
	length = _text_len;
	pressed_down = 0;
//	first_time = 1;
	changed_flag = 0;
	flags = _flags;
	pixel_limit = pixel_lim;
	locked = 0;
	valid_chars = NULL;
	invalid_chars = NULL;
};

void UI_INPUTBOX::set_valid_chars(char *vchars)
{
	// free up any existing string
	if(valid_chars != NULL){
		free(valid_chars);
		valid_chars = NULL;
	}

	valid_chars = strdup(vchars);
}

void UI_INPUTBOX::set_invalid_chars(char *ichars)
{
	// free up any existing string
	if(invalid_chars != NULL){
		free(invalid_chars);
		invalid_chars = NULL;
	}
	
	invalid_chars = strdup(ichars);
}

void UI_INPUTBOX::destroy()
{
	if (text) {
		free(text);
		text = NULL;
	}

	// free any valid chars
	if(valid_chars != NULL){
		free(valid_chars);
		valid_chars = NULL;
	}

	// free any invalid chars
	if(invalid_chars != NULL){
		free(invalid_chars);
		invalid_chars = NULL;
	}

	if ((flags & UI_INPUTBOX_FLAG_PASSWD) && passwd_text) {
		free(passwd_text);
		passwd_text = NULL;
	}

	UI_GADGET::destroy();
}

void UI_INPUTBOX::draw()
{
	int invis, w1, h1, tw, th;
	int text_x, text_y;

	if (hidden){
		return;
	}

	w1 = w;
	h1 = h;
	invis = flags & UI_INPUTBOX_FLAG_INVIS;

	gr_set_font(my_wnd->f_id);
	gr_reset_clip();
	if (!invis && !(flags & UI_INPUTBOX_FLAG_NO_BACK)) {
		// draw the entire text box region
		ui_draw_sunken_border( x-2, y-2, x+w+1, y+h+1 );
		gr_set_color_fast( &CBLACK );
		gr_rect( 0, 0, w, h );
		w1 -= 4;
		h1 -= 4;
		gr_set_clip( x + 1, y + 1, w1 + 1, h1 + 1 );
	} else {
		gr_set_clip( x - 1, y - 1, w1 + 1, h1 + 1 );
	}

	if (flags & UI_INPUTBOX_FLAG_PASSWD){
		gr_get_string_size(&tw, &th, passwd_text);
	} else {
		gr_get_string_size(&tw, &th, text);
	}

	// If first_time is set, that means this input box got
	// focus, but nothing is typed yet, so all the text is
	// selected, if you type a character it will replace the
	// text, if you type an arrow it will unselect it.
	// So it needs to be colored differently to show this.
	if (!disabled_flag && !(flags & UI_INPUTBOX_FLAG_NO_BACK)) {
//		if ( (my_wnd->selected_gadget == this) && first_time ) {
//			gr_set_color_fast( text_color );

//		} else {
			gr_set_color_fast( &CBLACK );
//		}

		// color the background behind the text	
		gr_rect( 0, 0, tw + 1, th );
	}

	if	( (my_wnd->selected_gadget == this) || disabled_flag ) {		
		gr_set_color_fast(text_color);
	} else {
		gr_set_color_fast(&CWHITE);
	}

	// coords of where to draw the text
	text_x = 1;
	text_y = 1;
	if(flags & UI_INPUTBOX_FLAG_TEXT_CEN){
		// if we fit within the text area, draw it centered
		if(tw <= w1 - 5){
			text_x += (w1 - tw)/2;
		}		
	}

	// draw the text
	if (flags & UI_INPUTBOX_FLAG_PASSWD){
		gr_string(text_x, text_y, passwd_text);
	} else {
		gr_string(text_x, text_y, text);
	}

	// draw the "cursor"
	if (!disabled_flag) {
		if (my_wnd->selected_gadget == this) {
			if (cursor_first_frame == -1) {
				gr_set_color_fast(text_color);
				ui_vline(1, h1, text_x + tw + 4);
				ui_vline(1, h1, text_x + tw + 5);
			} else {
				// draw animating cursor
				int time_delta = timer_get_milliseconds() - cursor_elapsed_time;

				if ( (time_delta / 1000.0f) > (1.0f / cursor_fps) ) {
					// advance frame
					cursor_elapsed_time += time_delta;
					cursor_current_frame++;
					if (cursor_current_frame >= cursor_nframes) {
						cursor_current_frame = 0;
					}
				}

				// draw current frame
				gr_set_bitmap(cursor_first_frame + cursor_current_frame);
				gr_bitmap(text_x + tw + 4, 1);
			}
		}
	}

	gr_reset_clip();
}

int UI_INPUTBOX::validate_input(int chr)
{
	if (chr < 32) {  // weed out control characters
		return 0;
	}

	// if we're disallowing letters altogether
	if((flags & UI_INPUTBOX_FLAG_NO_LETTERS) && isalpha(chr)){
		return 0;
	}

	// if we're disallowing numbers altogether
	if((flags & UI_INPUTBOX_FLAG_NO_NUMERALS) && isdigit(chr)){
		return 0;
	}

	// otherwise allow numbers and alpha chars by
	if (isdigit(chr) || isalpha(chr)){
		return chr;
	}

	// if we have specified no valid or invalid chars, accept everything
	if(!valid_chars && !invalid_chars){
		return chr;
	}

	// otherwise compare against the valid chars list
	if((valid_chars) && strchr(valid_chars, chr)){
		return chr;
	}

	// otherwise compare against the invalid chars list0
	if((invalid_chars) && !strchr(invalid_chars,chr)){
		return chr;
	}

	return 0;
}

void UI_INPUTBOX::process(int focus)
{
	int ascii, clear_lastkey, key, key_used, key_check;	

	// check if mouse is pressed
	if (B1_PRESSED && is_mouse_on()) {
		set_focus();
//		first_time = 1;
	}

	if (disabled_flag)
		return;

	if (my_wnd->selected_gadget == this)
		focus = 1;
//	else
//		first_time = 0;

	key_used = 0;
	changed_flag = 0;
	oldposition = position;
	pressed_down = 0;
	clear_lastkey = (flags & UI_INPUTBOX_FLAG_KEYTHRU) ? 0 : 1;

	if (focus) {
		key = my_wnd->keypress;
		switch (key) {
			case 0:
				break;

			//case KEY_LEFT:
			case KEY_BACKSP:
				if (position > 0)
					position--;

				text[position] = 0;
				if (flags & UI_INPUTBOX_FLAG_PASSWD) {
					passwd_text[position] = 0;
				}

				changed_flag = 1;
				key_used = 1;
//				if (first_time)
//					first_time = 0;

				break;

			case KEY_ENTER:
				pressed_down = 1;
				locked = 0;
				changed_flag = 1;
				key_used = 1;
//				if (first_time)
//					first_time = 0;

//				should_reset = 1;
				break;

			case KEY_ESC:
				if (flags & UI_INPUTBOX_FLAG_ESC_CLR){
					if (position > 0) {
						set_text("");
						key_used = 1;						

					} else {
						key_used = 0;
						clear_lastkey = 0;
					}
				}

				if (flags & UI_INPUTBOX_FLAG_ESC_FOC) {
					clear_focus();
				}

				break;

			default:
				if (!locked) {
					// MWA -- determine if alt or ctrl held down on this key and don't process if it is.  We
					// need to be able to pass these keys back to the top level.  (And anyway -- ctrl-a shouldn't
					// print out an A in the input window
					if ( key & (KEY_ALTED | KEY_CTRLED) ) {
						clear_lastkey = 0;
						break;
					}

					// get an ascii char from the input if possible
					key_check = keypad_to_ascii(key);
					if(key_check == -1){
						key_check = key_to_ascii(key);
					}

					ascii = validate_input(key_check);
					if ((ascii > 0) && (ascii < 255)) {
						if (flags & UI_INPUTBOX_FLAG_LETTER_FIRST) {
							if ((position == 0) && !is_letter((char) ascii))
								break;
						}

						key_used = 1;
//						if (should_reset) {
//							should_reset = 0;
//							position = 0;
//						}

//						if (first_time) {
//							first_time = 0;
//							position = 0;
//						}

						if ( position < length ) {
							text[position] = (char) ascii;
							text[position + 1] = 0;

							if (flags & UI_INPUTBOX_FLAG_PASSWD) {
								passwd_text[position] = (char) INPUTBOX_PASSWD_CHAR;
								passwd_text[position + 1] = 0;
							}

							position++;

							// check to see if we should limit by pixel width
							if (pixel_limit > -1) {
								int w;

								if (flags & UI_INPUTBOX_FLAG_PASSWD) {
									gr_get_string_size(&w, NULL, passwd_text);									

								} else {
									gr_get_string_size(&w, NULL, text);								
								}

								if (w > pixel_limit) {
									position--;
									locked = 1;
									text[position] = 0;

									if (flags & UI_INPUTBOX_FLAG_PASSWD) {
										passwd_text[position] = 0;
									}
								}
							}
						}

						changed_flag = 1;
					}
				}

				break;
		}

		if (clear_lastkey || (key_used && (flags & UI_INPUTBOX_FLAG_EAT_USED)) )
			my_wnd->last_keypress=0;

//	} else {
//		first_time = 1;
	}	
}

int UI_INPUTBOX::changed()
{		
	return changed_flag;
}

int UI_INPUTBOX::pressed()
{	
	return pressed_down;
}

void UI_INPUTBOX::get_text(char *out)
{
	strncpy(out, text, length);
	out[length] = 0;
}

void UI_INPUTBOX::set_text(char *in)
{
	int in_length;
	
	in_length = strlen(in);
	if (in_length > length)
		Assert(0);	// tried to force text into an input box that won't fit into allocated memory

	strcpy(text, in);
	
	if (flags & UI_INPUTBOX_FLAG_PASSWD) {
		memset(passwd_text, INPUTBOX_PASSWD_CHAR, strlen(text));
		passwd_text[strlen(text)] = 0;
	}

	position = in_length;  // fixes the zero-length-I-don't-think-so bug
}


