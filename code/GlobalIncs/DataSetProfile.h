// Claude: the data set profile table (D-20261019-135406, D-20261020-053500..053503).  One row per data set holds
// everything that differs between the data sets and is not read from the data itself: the names the game
// shows and keeps its settings under, its version, the built-in campaign, the title screen and the default
// HUD colours.  Each is read from Game_profile where it is used, so nothing data-set dependent is fixed at
// start-up: data_set_profile_apply() picks the row once detection has run (game_detect_data_set), and a later
// run-time switcher can call it again.  Until then, and in the tools (FRED and the others never call it),
// Game_profile is the FS2 retail row.  Pools and limits are not here: they stay at the retail maximum.
#ifndef _DATASETPROFILE_H
#define _DATASETPROFILE_H

#include "2d.h"

typedef struct data_set_profile {
	int	data_set;							// DATA_SET_* (CFile/DataSetInstall.h)
	int	filled_in;							// 0: a placeholder row with FS2 retail values (FS1 rows, for the FS1 work)

	char	*app_name;							// settings are kept under it (os_init)
	char	*title;								// window title

	int	version_major, version_minor, version_build;
	char	*version_suffix;					// after the version string, " D" for the demo
	char	*version_url;						// multiplayer version check
	char	*motd_url;

	char	*builtin_campaign;				// campaign file name, without .fc2
	char	*builtin_campaign_name;

	char	*title_screen[GR_NUM_RESOLUTIONS];	// shown once at start-up, NULL for none
	char	*default_hcf;						// a new pilot's HUD colours

	struct fs_builtin_mission	*builtin_missions;	// the Volition missions and campaign files (FreeSpace.cpp)
	int	*builtin_mission_count;

	// menus and flow (batch 7, D-20261021-071013): each reproduces the FS2_DEMO build where that build read it
	int	full_version_screens;			// FVS_* bits: set = the screen opens as in the full game; clear = what the demo build did there
	int	always_pilot_select;			// 1: start-up always shows the pilot screen (a replay or autotest still loads its own pilot)
	int	demo_campaign_end;				// 1: the campaign's end posts GS_EVENT_END_DEMO and the ready room offers to play it again
	int	upsell_screens;					// 1: the sales screens at the end of the campaign and on exit
	int	trailer_timer;					// 1: input resets the trailer timer (the trailer itself is commented out in the shipped source)
	// mission UI, HUD, controls (batch 8, D-20261021-100301): 1 = the full game, 0 = what the FS2_DEMO build did
	int	brief_fade;						// the briefing static fade (briefmap.ani) and the ship select button flash; 0: no fade, no flash
	int	loadout_anim_loop;				// ship and weapon select animations loop from frame 51 / 52; 0: from 0 / 1
	int	hotkeys;							// F5-F12 target hotkeys and their help lines; 0: ignored and not listed
	int	artillery_lock;					// ship_update_artillery_lock runs; 0: it returns at once
	int	cheat_code;						// the full game's cheat codes; 0: the demo's one code, single player and multi alike
	int	breakup_fx;						// ship break-up particles at the full game's sizes; 0: the demo's (range x5, longer life, double scale); affects game logic (D-20261021-110833)
} data_set_profile;

// full_version_screens bits (shared with FS1Demo's field of the same name; the layouts are reconciled at the first common merge)
#define FVS_TECH_ROOM			(1<<0)	// main hall tech room; clear: not-in-demo popup
#define FVS_CAMPAIGN_ROOM		(1<<1)	// main hall campaign room; clear: the FS2 demo's restart-campaign popup
#define FVS_LOAD_MISSION		(1<<2)	// main hall load mission key; clear: nothing
#define FVS_QUICK_START			(1<<3)	// main hall quick start (Debug only); clear: nothing
#define FVS_MEDALS				(1<<4)	// barracks medals button; clear: not-in-demo popup
#define FVS_HUD_CONFIG			(1<<5)	// options HUD config button; clear: not-in-demo popup
#define FVS_IPX					(1<<6)	// multi options IPX button; clear: hidden and ignored
#define FVS_HOTKEYS				(1<<7)	// F3 hotkey screen and its help line; clear: F3 does nothing (batch 8, D-20261021-100301)
#define FVS_CUTSCENES			(1<<8)	// Simulator room, Credits and Tech room Cutscenes buttons; clear: not-in-demo popup (the FS2 demo has no ViewFootage art, #87, D-20261022-065950)
#define FVS_ALL					((1<<9) - 1)

extern const data_set_profile *Game_profile;

// makes the data set's row the active one; an FS1 row not filled in yet logs a Warning and uses FS2 retail values
void data_set_profile_apply(int data_set);

#endif
