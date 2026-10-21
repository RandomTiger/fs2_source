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
	char	*default_hcf;						// a new pilot's HUD colours, NULL for none (FS1 has no .hcf files)

	int	loading_ani_coords[GR_NUM_RESOLUTIONS][2];	// where the loading screen draws Loading.ani on LoadingBG, per art mode (FreeSpace.cpp, D-20261020-222222): each data set's art places its bar differently; the FS1 rows' 1024 pair is the 640 pair x 1.6, derived, not measured (FS1 has 640 art only)
	int	barracks_image_coords[GR_NUM_RESOLUTIONS][2];	// where the barracks draws the pilot picture, per art mode (Barracks.cpp, D-20261020-231901); the FS1 rows' 1024 pair is the 640 pair x 1.6, derived and unreachable (FS1 has 640 art only); the FS1 demo's y 49 is retail's 33 moved down with its window's bottom edge (152 to 168), derived, not measured
	int	barracks_image_window[2];		// 0, 0: the picture is drawn whole; else the first and last screen rows of FS1's pilot picture window on BARRACKS.PCX, and the picture is clipped to its shape (Barracks.cpp, D-20261020-231901)

	struct fs_builtin_mission	*builtin_missions;	// the Volition missions and campaign files (FreeSpace.cpp)
	int	*builtin_mission_count;
} data_set_profile;

extern const data_set_profile *Game_profile;

// makes the data set's row the active one; an FS1 row not filled in yet logs a Warning and uses FS2 retail values
void data_set_profile_apply(int data_set);

#endif
