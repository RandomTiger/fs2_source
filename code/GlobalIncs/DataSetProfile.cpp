// Claude: the data set profile table, see DataSetProfile.h.  The FS2 rows hold exactly the values of the shipped
// retail and demo builds (the old #if FS2_DEMO arms), so neither the retail nor the demo recordings can move.

#include "pstypes.h"
#include "DataSetInstall.h"
#include "DataSetProfile.h"
#include "FreeSpace.h"
#include "Object.h"

// the built-in mission lists, in FreeSpace.cpp (the tools stub them)
extern fs_builtin_mission Game_builtin_missions_fs2[];
extern int Game_builtin_missions_fs2_count;
extern fs_builtin_mission Game_builtin_missions_fs2_demo[];
extern int Game_builtin_missions_fs2_demo_count;

// Claude: FS1 retail's built-in list, read from its FS.exe (124 entries of a 32-byte name and an int).  FS1's flag bits
// are FS2's in another order (training 8, campaign 16, campaign file 32), plus 4 for Silent Threat's missions, which
// FS2 has no flag for; they map to FS2's FSB_ bits here.  Silent Threat's mdu missions, flagged 4 alone, were not
// Volition's.  The list is longer than MAX_BUILTIN_MISSIONS, so it is sized by its entries.
static fs_builtin_mission Game_builtin_missions_fs1[] = {
	{ "sm1-01.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-02a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-03a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-04a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-05a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-06a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-07a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-08a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-09a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm1-10a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-01a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-02a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-03a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-04a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-05a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-06a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-07a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-08a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-09a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm2-10a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-01a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-02a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-03a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-04a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-05a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-06a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-07a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-08a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "sm3-09a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "t-gauntlet.fsm", (FSB_FROM_VOLITION), "" },
	{ "v-gauntlet.fsm", (FSB_FROM_VOLITION), "" },
	{ "s-gauntlet.fsm", (FSB_FROM_VOLITION), "" },
	{ "btm-01.fsm", (FSB_FROM_VOLITION | FSB_TRAINING), "" },
	{ "btm-02.fsm", (FSB_FROM_VOLITION | FSB_TRAINING), "" },
	{ "btm-03.fsm", (FSB_FROM_VOLITION | FSB_TRAINING), "" },
	{ "btm-04.fsm", (FSB_FROM_VOLITION | FSB_TRAINING), "" },
	{ "btm-05.fsm", (FSB_FROM_VOLITION | FSB_TRAINING), "" },
	{ "m-pain.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "m-v-gauntlet.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "m-va.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "m-unstoppable.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "m-t-gauntlet.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "m-s-gauntlet.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "m-rescue.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "m-orecovery.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },
	{ "mm3-01a.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "mm3-02a.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "mm3-03a.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "mm3-04a.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "mm3-05a.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "mm3-06a.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "m-guardduty.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "m-gate.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "m-duel.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "m-convoyassault.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },
	{ "md-01.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-02.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-03.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-04.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-05.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-06.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-07.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-08.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-09.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-10.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-11.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "md-12.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "mdmm-01.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },	// Silent Threat
	{ "mdmm-02.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },	// Silent Threat
	{ "mdmm-03.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },	// Silent Threat
	{ "mdmm-04.fsm", (FSB_FROM_VOLITION | FSB_MULTI), "" },	// Silent Threat
	{ "stmm-01.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-02.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-03.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-04.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-05.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-06.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-07.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-08.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-09.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-10.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-11.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "stmm-12.fsm", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN), "" },	// Silent Threat
	{ "mdu-01.fsm", 0, "" },	// Silent Threat
	{ "mdu-02.fsm", 0, "" },	// Silent Threat
	{ "mdu-03.fsm", 0, "" },	// Silent Threat
	{ "mdu-04.fsm", 0, "" },	// Silent Threat
	{ "mdu-05.fsm", 0, "" },	// Silent Threat
	{ "mdu-06.fsm", 0, "" },	// Silent Threat
	{ "mdu-07.fsm", 0, "" },	// Silent Threat
	{ "mdu-08.fsm", 0, "" },	// Silent Threat
	{ "mdu-09.fsm", 0, "" },	// Silent Threat
	{ "mdu-10.fsm", 0, "" },	// Silent Threat
	{ "mdu-11.fsm", 0, "" },	// Silent Threat
	{ "mdu-12.fsm", 0, "" },	// Silent Threat
	{ "mdu-13.fsm", 0, "" },	// Silent Threat
	{ "mdu-14.fsm", 0, "" },	// Silent Threat
	{ "mdu-15.fsm", 0, "" },	// Silent Threat
	{ "mdu-16.fsm", 0, "" },	// Silent Threat
	{ "mdu-17.fsm", 0, "" },	// Silent Threat
	{ "mdu-18.fsm", 0, "" },	// Silent Threat
	{ "mdu-19.fsm", 0, "" },	// Silent Threat
	{ "mdu-20.fsm", 0, "" },	// Silent Threat
	{ "mdu-21.fsm", 0, "" },	// Silent Threat
	{ "mdu-22.fsm", 0, "" },	// Silent Threat
	{ "mdu-23.fsm", 0, "" },	// Silent Threat
	{ "mdu-24.fsm", 0, "" },	// Silent Threat
	{ "mdu-25.fsm", 0, "" },	// Silent Threat
	{ "mdu-26.fsm", 0, "" },	// Silent Threat
	{ "mdu-27.fsm", 0, "" },	// Silent Threat
	{ "mdu-28.fsm", 0, "" },	// Silent Threat
	{ "mdu-29.fsm", 0, "" },	// Silent Threat
	{ "mdu-30.fsm", 0, "" },	// Silent Threat
	{ "mdu-31.fsm", 0, "" },	// Silent Threat
	{ "mdumm-01.fsm", (FSB_MULTI), "" },	// Silent Threat
	{ "mdumm-02.fsm", (FSB_MULTI), "" },	// Silent Threat
	{ "freespace.fsc", (FSB_FROM_VOLITION | FSB_CAMPAIGN_FILE), "" },
	{ "m-altair.fsc", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN_FILE), "" },
	{ "m-hope.fsc", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN_FILE), "" },
	{ "silentthreat.fsc", (FSB_FROM_VOLITION | FSB_CAMPAIGN_FILE), "" },	// Silent Threat
	{ "st-part1.fsc", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN_FILE), "" },	// Silent Threat
	{ "st-part2.fsc", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN_FILE), "" },	// Silent Threat
	{ "st-part3.fsc", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN_FILE), "" },	// Silent Threat
	{ "st-part4.fsc", (FSB_FROM_VOLITION | FSB_MULTI | FSB_CAMPAIGN_FILE), "" },	// Silent Threat
};
static int Game_builtin_missions_fs1_count = sizeof(Game_builtin_missions_fs1) / sizeof(Game_builtin_missions_fs1[0]);

// Claude: the FS1 demo's FS.exe has no built-in list (none of its mission names are in it), so this is the demo's
// own campaign, in campaign order (demo.fsc runs btmdemo, demo01, then demo02a or demo02b; docs/FS1_DEMO.md), with
// retail's flags for the same kinds of mission.
static fs_builtin_mission Game_builtin_missions_fs1_demo[] = {
	{ "btmdemo.fsm", (FSB_FROM_VOLITION | FSB_TRAINING | FSB_CAMPAIGN), "" },
	{ "demo01.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "demo02a.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "demo02b.fsm", (FSB_FROM_VOLITION | FSB_CAMPAIGN), "" },
	{ "demo.fsc", (FSB_FROM_VOLITION | FSB_CAMPAIGN_FILE), "" },
};
static int Game_builtin_missions_fs1_demo_count = sizeof(Game_builtin_missions_fs1_demo) / sizeof(Game_builtin_missions_fs1_demo[0]);

static const data_set_profile Data_set_profiles[NUM_DATA_SETS] = {
	{	DATA_SET_FS2_RETAIL, 1,
		"FreeSpace2", "Freespace 2",
		1, 20, 0, "", "http://www.pxo.net/files/fs2/version.nfo", "http://www.pxo.net/files/fs2/motd.txt",
		"FreeSpace2", "The Main Freespace2 Campaign",
		{ NULL, NULL }, "hud_3.hcf",
		Game_builtin_missions_fs2, &Game_builtin_missions_fs2_count,
		FVS_ALL, 0, 0, 0, 0,
		1, 1, 1, 1, 1,
		1, 1, 2, 3, 18, 0, MAX_OBJECTS,
		3, 1, 1, 1, 1, 1, 1, 1, 1, 1, { { 63, 316 }, { 101, 505 } }, { NULL, NULL }, { { 461, 23 }, { 782, 58 } }, 0, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, NULL },
	{	DATA_SET_FS2_DEMO, 1,
		"FreeSpace2Demo", "Freespace 2 Demo",
		1, 10, 0, " D", "http://www.pxo.net/files/fs2demo/version.nfo", "http://www.pxo.net/files/fs2demo/motd.txt",
		"demo", "DEMO Campaign",
		{ "PreLoad", "2_PreLoad" }, "hud_1.hcf",
		Game_builtin_missions_fs2_demo, &Game_builtin_missions_fs2_demo_count,
		FVS_MULTIPLAYER | FVS_OBJECTIVES | FVS_OPTIONS_MULTI | FVS_DEBRIEF_MEDALS, 1, 1, 1, 1,	// the FS2_DEMO build walled none of FS1's four (D-20261021-142001)
		0, 0, 0, 0, 0,
		0, 0, 1, 1, 16, 1, 300,
		3, 1, 1, 0, 1, 1, 1, 1, 1, 1, { { 63, 316 }, { 101, 505 } }, { NULL, NULL }, { { 461, 23 }, { 782, 58 } }, 0, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, NULL },	// brief_fade_anim 0: unread, brief_fade is 0 (D-20261021-142002)
	// FS1 rows (docs/FS1_COMPAT.md): the names, campaign and title screen from FS1's FS.exe and data.  Retail is v1.06,
	// the demo v1.00 (its exe printed "Dv1.00", which a suffix cannot give).  FS1 checked no version or MOTD on the web,
	// so those URLs are empty, and has no .hcf files: a new pilot keeps the one HUD colour hud_set_default_hud_config gives.
	{	DATA_SET_FS1_RETAIL, 1,
		"FreeSpace", "FreeSpace",
		1, 6, 0, "", "", "",
		"freespace", "The Main Freespace Campaign",
		{ NULL, NULL }, NULL,
		Game_builtin_missions_fs1, &Game_builtin_missions_fs1_count,
		FVS_ALL, 0, 0, 0, 0,
		1, 1, 1, 1, 1,
		1, 1, 2, 3, 18, 0, MAX_OBJECTS,
		3, 1, 1, 1, 1, 1, 1, 1, 1, 1, { { 117, 317 }, { 187, 507 } }, { NULL, NULL }, { { 460, 33 }, { 736, 53 } }, 0, { 36, 152 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, "AlertFlash" },
	{	DATA_SET_FS1_DEMO, 1,
		"FreeSpaceDemo", "FreeSpace Demo",
		1, 0, 0, "", "", "",
		"demo", "Freespace Demo Campaign",
		{ "DemoTitle1", NULL }, NULL,
		Game_builtin_missions_fs1_demo, &Game_builtin_missions_fs1_demo_count,
		FVS_LOAD_MISSION | FVS_QUICK_START | FVS_HUD_CONFIG | FVS_IPX | FVS_CUTSCENES, 0, 0, 0, 0,	// FS1's demo walls (D-20261020-214042, D-20261021-142001); FVS_CUTSCENES set: an FS2Demo-only wall, FS1 ran retail code there (D-20261022-161946)
		1, 1, 1, 1, 1,	// brief_fade 1 with brief_fade_anim 0 below: the cut's static sound plays and nothing is drawn (D-20261021-142002)
		1, 1, 2, 3, 18, 0, MAX_OBJECTS,
		2, 0, 0, 0, 0, 0, 0, 0, 0, 0, { { 133, 337 }, { 213, 539 } }, { "Briefing_Help_Overlay_Demo", "Main_Help_Overlay_Demo" }, { { 452, 37 }, { 723, 59 } }, 1, { 27, 168 }, { -66, 25 }, { 4, 14 }, { 0, 16 }, NULL },
};

const data_set_profile *Game_profile = &Data_set_profiles[DATA_SET_FS2_RETAIL];

void data_set_profile_apply(int data_set)
{
	if ((data_set < 0) || (data_set >= NUM_DATA_SETS)) {
		data_set = DATA_SET_FS2_RETAIL;
	}
	Game_profile = &Data_set_profiles[data_set];
	if (!Game_profile->filled_in) {
		Warning(LOCATION, "Data set profile: the %s row is not filled in, FS2 retail values used", data_set_name(data_set));
	}
}
