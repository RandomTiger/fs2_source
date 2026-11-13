/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/MissionUI/MissionPause.h $
 * $Revision: 3 $
 * $Date: 6/09/99 2:17p $
 * $Author: Dave $
 * 
 *
 * $Log: /Freespace2/code/MissionUI/MissionPause.h $
 * 
 * 3     6/09/99 2:17p Dave
 * Fixed up pleasewait bitmap rendering.
 * 
 *
 * $NoKeywords: $
 */

#ifndef _MISSION_PAUSE_HEADER_FILE
#define _MISSION_PAUSE_HEADER_FILE

#include "2d.h"

// ----------------------------------------------------------------------------------------------------------------
// PAUSE DEFINES/VARS
//

// pause bitmap display stuff
extern int Please_wait_coords[GR_NUM_RESOLUTIONS][4];


// ----------------------------------------------------------------------------------------------------------------
// PAUSE FUNCTIONS
//

// initialize the pause screen
void pause_init(int multi);

// pause do frame - will handle running multiplayer operations if necessary
void pause_do(int multi);

// close the pause screen
void pause_close(int multi);

// debug pause init
void pause_debug_init();

// debug pause do frame
void pause_debug_do();

// debug pause close
void pause_debug_close();

// Claude: spectator mode (#313), see MissionPause.cpp
struct control_info;
int  spectator_active();
void spectator_level_init();						// a mission starts or ends: off, 1x, the option read again
void spectator_toggle();							// Alt+Pause
int  spectator_process_key(int k);				// game_process_keys: 1 if the key was the spectator's (while on, all but the pause keys)
void spectator_request(int on);					// the option changed: the next mission frame presses Alt+Pause if it differs
void spectator_mission_frame();					// each mission frame, before the keys are read
fix  spectator_time_scale();						// multiplies the frame time
void spectator_controls(control_info *ci);	// the ship's controls while on
void spectator_camera_frame();					// each mission frame, after the game view is set: moves the camera (unrecorded reads only)
void spectator_camera(vector *pos, matrix *orient);	// where the camera is
int  spectator_follow_object();					// the object the camera follows, or -1
void spectator_draw_text();						// drawing: the lines shown in place of the HUD

// Claude: set before GS_EVENT_PAUSE_GAME to pause with the debug camera (Shift+Pause, #125)
extern int Pause_debug_camera;

#endif