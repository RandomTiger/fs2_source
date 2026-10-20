/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/

#ifndef _OSAPI_H
#define _OSAPI_H

// --------------------------------------------------------------------------------------------------
// OSAPI DEFINES/VARS
//

// set if running under MsDev - done after os_init(...) has returned
extern int Os_debugger_running;

// game-wide
// #define THREADED

	#define ENTER_CRITICAL_SECTION(csc)		do { } while(0);
	#define LEAVE_CRITICAL_SECTION(csc)		do { } while(0);

// --------------------------------------------------------------------------------------------------
// OSAPI FUNCTIONS
//

// initialization/shutdown functions -----------------------------------------------

// If app_name is NULL or ommited, then TITLE is used
// for the app name, which is where registry keys are stored.
void os_init(char * wclass, char * title, char *app_name=NULL, char *version_string=NULL );

// set the main window title
void os_set_title( char * title );

// call at program end
void os_cleanup();


// window management ---------------------------------------------------------------

// toggle window size between full screen and windowed
void os_toggle_fullscreen();

// Returns 1 if app is not the foreground app.
int os_foreground();

// Returns the handle to the main window
// Claude: x64, HWNDs only use the low 32 bits, but uintptr_t keeps the cast quiet; uint on Win32 (D-20261020-194406 batch 3)
uintptr_t os_get_window();


// process management --------------------------------------------------------------

// call to process windows messages. only does something in non THREADED mode
void os_poll();

// Sleeps for n milliseconds or until app becomes active.
void os_sleep(int ms);

// Claude: a message box, so game code needs no windows.h. Returns 1 for OK, 0 for Cancel.
#define OS_MB_OKCANCEL		(1<<0)	// OK and Cancel buttons; OK alone otherwise
#define OS_MB_FOREGROUND	(1<<1)	// task modal, brought to the foreground
#define OS_MB_OWNED			(1<<2)	// owned by the game window
int os_message_box(const char *text, const char *caption, int flags);

// Claude: a 1 ms timer and scheduler resolution while the game runs (1), or back to the default (0)
void os_timer_precise(int on);

// Claude: directory listing, so game code needs no io.h. Same use as the C runtime's _findfirst, _findnext and
// _findclose; files come in the order the platform lists them.
#include <time.h>
typedef struct os_find_data {
	char				name[260];
	int				is_dir;
	int				read_only;
	unsigned int	size;
	time_t			write_time;
} os_find_data;
intptr_t os_find_first(const char *filespec, os_find_data *fd);	// a handle, or -1 if nothing matches
int os_find_next(intptr_t handle, os_find_data *fd);				// 0 if fd holds the next file, -1 at the end
void os_find_close(intptr_t handle);	// intptr_t: a pointer-sized handle on x64, int on x86 (D-057)

// Used to stop message processing
void os_suspend();

// resume message processing
void os_resume();

#endif
