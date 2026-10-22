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
	int	fs2_demo_walls;					// with an FVS_ bit clear, 1: the site does what the FS2_DEMO build did there (campaign room: offer to restart the campaign; F3: nothing); 0: the not-in-demo popup, as FS1's demo build (D-20261021-142001)
	// FS1 data sets (FS1Compat), appended after FS2Demo's fields at their merge (D-20261021-142003)
	int	thruster_species;					// species whose thruster anims are loaded: FS1's demo build cut the Shivan ones (Ship.cpp)
	int	shields;							// 0: no shields at all, as FS1's demo build (Shield.cpp, D-20261020-161655)
	int	loadout;							// 0: no ship or weapon selection, their icons and anims not loaded, as FS1's demo build (MissionScreenCommon.cpp)
	int	brief_fade_anim;						// Claude: was brief_fade, renamed at the FS2Demo merge (D-20261021-142002); read inside brief_fade's blocks.  0: no BriefMap.ani, so briefing stage cuts draw no static (MissionBrief.cpp, D-20261020-163608)
	int	smoke2;								// 0: no ParticleSmoke02.ani, so those particles draw nothing (Particle.cpp, D-20261020-163608)
	int	attackers;							// 0: no attacker.pcx, so the target brackets show no '+' per extra attacker (HUDbrackets.cpp)
	int	unmarked_entries;					// 0: ships.tbl and weapons.tbl entries not marked '@' are skipped, as FS1's demo build (Ship.cpp, Weapons.cpp)
	int	talking_heads;						// 0: messages play no head anim, as FS1's demo build: the demo has none (MissionMessage.cpp)
	int	unknown_weapon_warning;				// 0: a ships.tbl weapon missing from weapons.tbl is logged, not a Warning; FS1's demo data names three (PARSELO.CPP, D-20261020-213146)
	int	options_mouse_sliders;				// 1 = Options creates the mouse on/off buttons (OPa_62, OPa_63) and the three right-hand dot sliders (ids 60, 61, 64); 0 = not created, their settings keep their values (FS1 demo: no OPa_60-64 in freespace.vp, no place for them on OptionsMain.PCX; OptionsMenu.cpp, D-20261020-213842)

	int	loading_ani_coords[GR_NUM_RESOLUTIONS][2];	// where the loading screen draws Loading.ani on LoadingBG, per art mode (FreeSpace.cpp, D-20261020-222222): each data set's art places its bar differently; the FS1 rows' 1024 pair is the 640 pair x 1.6, derived, not measured (FS1 has 640 art only)
	const char	*help_overlay_brief_main[2];	// F1 overlay names for the briefing and the main hall; NULL = the table's (ContextHelp.cpp, D-20261020-231902): FS1's demo ships Briefing_Help_Overlay_Demo and Main_Help_Overlay_Demo, as its FS.exe names them
	int	barracks_image_coords[GR_NUM_RESOLUTIONS][2];	// where the barracks draws the pilot picture, per art mode (Barracks.cpp, D-20261020-231901); the FS1 rows' 1024 pair is the 640 pair x 1.6, derived and unreachable (FS1 has 640 art only); retail's 640 pair is its FS.exe's gr_bitmap(460, 33); the demo's FS.exe drew at 460, 32 leaving black rows in its window, 142 rows plus 3 for its top corners, so its picture is scaled up by half as much as filling that height would take (1.1), centred across and bottom aligned, its top row repeated above it, 452, 37 (Barracks.cpp)
	int	brief_map_window;				// 1 = the briefing draws MapWin01..04 around the map after the background (MissionBrief.cpp, D-20261021-082859): FS1's demo has no BriefTrans.ani, its FS.exe names these four edges instead
	int	barracks_image_window[2];		// 0, 0: the picture is drawn whole; else the first and last screen rows of FS1's pilot picture window on BARRACKS.PCX, and the picture is clipped to its shape (Barracks.cpp, D-20261020-231901)
	int	options_gamma_offset[2];		// rigid shift of the whole gamma group (down/up buttons OPa_56/57, gamma preview, gamma number) from retail FS1's positions (OptionsMenu.cpp, D-20261021-083325); FS1 demo measured from its own OptionsMain-m.PCX hotspots 56/57 and a template match of the number and preview boxes in OptionsMain.PCX, both exactly (-66,+25); the demo has no mouse/joystick panel and the group moved down into that space
	// Offset added once, in barracks_fs1_layout, to the two pilot-picture arrow
	// buttons (prev/next). FS1 demo measured from its own Barracks-m.pcx: hotspots
	// at x 579..599 / 603..623, y 155..178 against retail FS1's 575..595 /
	// 599..619, y 141..164, i.e. (+4,+14); every other hotspot is identical (D-20261021-095912).
	int	barracks_pic_arrows_offset[2];
	// Offset added once, in barracks_fs1_layout, to the "N of M" picture-number
	// print position. FS1 demo derived, not measured (no demo screenshot exists):
	// the frame strip under the picture window is 16 rows lower in the demo's
	// BARRACKS.PCX (retail y 165-167, demo y 181-184), so (0,+16) (D-20261021-095912).
	int	barracks_pic_number_offset[2];
} data_set_profile;

// full_version_screens bits (one field for every data set; FS1Compat's walls joined at the FS2Demo merge, D-20261021-142001)
#define FVS_TECH_ROOM			(1<<0)	// main hall tech room; clear: not-in-demo popup
#define FVS_CAMPAIGN_ROOM		(1<<1)	// main hall campaign room; clear: fs2_demo_walls picks the popup
#define FVS_LOAD_MISSION		(1<<2)	// main hall load mission key; clear: nothing
#define FVS_QUICK_START			(1<<3)	// main hall quick start (Debug only); clear: nothing
#define FVS_MEDALS				(1<<4)	// barracks medals button; clear: not-in-demo popup
#define FVS_HUD_CONFIG			(1<<5)	// options HUD config button; clear: not-in-demo popup
#define FVS_IPX					(1<<6)	// multi options IPX button; clear: hidden and ignored
#define FVS_HOTKEYS				(1<<7)	// F3 hotkey screen and its help line; clear: fs2_demo_walls picks nothing or the popup (batch 8, D-20261021-100301)
#define FVS_MULTIPLAYER			(1<<8)	// main hall multiplayer region; clear: not-in-demo popup (FS1 demo, D-20261021-142001)
#define FVS_OBJECTIVES			(1<<9)	// in-mission objectives screen; clear: not-in-demo popup (FS1 demo, D-20261021-142001)
#define FVS_OPTIONS_MULTI		(1<<10)	// options multiplayer tab; clear: not-in-demo popup (FS1 demo, D-20261021-142001)
#define FVS_DEBRIEF_MEDALS		(1<<11)	// debriefing medals button; clear: not-in-demo popup (FS1 demo; its own bit, the FS2_DEMO build left it open, D-20261021-142001)
#define FVS_CUTSCENES			(1<<12)	// Simulator room, Credits and Tech room Cutscenes buttons; clear: not-in-demo popup (the FS2 demo has no ViewFootage art, #87, D-20261022-065950); bit 12 above FS1's four (D-20261022-161946)
#define FVS_ALL					((1<<13) - 1)

extern const data_set_profile *Game_profile;

// makes the data set's row the active one; an FS1 row not filled in yet logs a Warning and uses FS2 retail values
void data_set_profile_apply(int data_set);

#endif
