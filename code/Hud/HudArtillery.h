/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/Hud/HudArtillery.h $
 * $Revision: 2 $
 * $Date: 4/20/99 6:39p $
 * $Author: Dave $
 * 
 *
 * $Log: /Freespace2/code/Hud/HudArtillery.h $  
 * 
 * 2     4/20/99 6:39p Dave
 * Almost done with artillery targeting. Added support for downloading
 * images on the PXO screen.
 * 
 * 1     4/20/99 12:00a Dave
 * 
 * 
 * $NoKeywords: $
 */

#ifndef _FS2_HUD_ARTILLERY_HEADER_FILE
#define _FS2_HUD_ARTILLERY_HEADER_FILE

// -----------------------------------------------------------------------------------------------------------------------
// ARTILLERY DEFINES/VARS
//
#define MAX_SSM_COUNT			10

// creation info for the strike (useful for multiplayer)
typedef struct ssm_firing_info {
	int			delay_stamp[MAX_SSM_COUNT];	// timestamps
	vector		start_pos[MAX_SSM_COUNT];		// start positions
	
	int			ssm_index;							// index info ssm_info array
	vector		target;								// target for the strike	
} ssm_firing_info;


// -----------------------------------------------------------------------------------------------------------------------
// ARTILLERY FUNCTIONS
//

// level init
void hud_init_artillery();

// update all hud artillery related stuff
void hud_artillery_update();

// render all hud artillery related stuff
void hud_artillery_render();

void ssm_create(vector *target, vector *start, int ssm_index, ssm_firing_info *override);

#endif