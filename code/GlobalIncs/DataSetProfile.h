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
	int	thruster_species;					// species whose thruster anims are loaded: FS1's demo build cut the Shivan ones (Ship.cpp)
	int	shields;							// 0: no shields at all, as FS1's demo build (Shield.cpp, D-20261020-161655)
	int	loadout;							// 0: no ship or weapon selection, their icons and anims not loaded, as FS1's demo build (MissionScreenCommon.cpp)
	int	brief_fade;							// 0: no BriefMap.ani, so briefing stage cuts draw no static (MissionBrief.cpp, D-20261020-163608)
	int	smoke2;								// 0: no ParticleSmoke02.ani, so those particles draw nothing (Particle.cpp, D-20261020-163608)
	int	attackers;							// 0: no attacker.pcx, so the target brackets show no '+' per extra attacker (HUDbrackets.cpp)
	int	unmarked_entries;					// 0: ships.tbl and weapons.tbl entries not marked '@' are skipped, as FS1's demo build (Ship.cpp, Weapons.cpp)
	int	talking_heads;						// 0: messages play no head anim, as FS1's demo build: the demo has none (MissionMessage.cpp)
	int	unknown_weapon_warning;				// 0: a ships.tbl weapon missing from weapons.tbl is logged, not a Warning; FS1's demo data names three (PARSELO.CPP, D-20261020-213146)
	int	full_version_screens;				// 1 = the full-version screens open; 0 = each site answers game_feature_not_in_demo_popup() (FS1 demo: freespace.vp carries no Campaign*, TechShip*, Hotkeys-M, Objectives-m, Medals-m, OptionsMulti*-M or multiplayer screen art; D-20261020-214042)

	struct fs_builtin_mission	*builtin_missions;	// the Volition missions and campaign files (FreeSpace.cpp)
	int	*builtin_mission_count;
} data_set_profile;

extern const data_set_profile *Game_profile;

// makes the data set's row the active one; an FS1 row not filled in yet logs a Warning and uses FS2 retail values
void data_set_profile_apply(int data_set);

#endif
