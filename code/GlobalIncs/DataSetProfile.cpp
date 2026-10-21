// Claude: the data set profile table, see DataSetProfile.h.  The FS2 rows hold exactly the values of the shipped
// retail and demo builds (the old #if FS2_DEMO arms), so neither the retail nor the demo recordings can move.

#include "pstypes.h"
#include "DataSetInstall.h"
#include "DataSetProfile.h"
#include "FreeSpace.h"

// the built-in mission lists, in FreeSpace.cpp (the tools stub them)
extern fs_builtin_mission Game_builtin_missions_fs2[];
extern int Game_builtin_missions_fs2_count;
extern fs_builtin_mission Game_builtin_missions_fs2_demo[];
extern int Game_builtin_missions_fs2_demo_count;

static const data_set_profile Data_set_profiles[NUM_DATA_SETS] = {
	{	DATA_SET_FS2_RETAIL, 1,
		"FreeSpace2", "Freespace 2",
		1, 20, 0, "", "http://www.pxo.net/files/fs2/version.nfo", "http://www.pxo.net/files/fs2/motd.txt",
		"FreeSpace2", "The Main Freespace2 Campaign",
		{ NULL, NULL }, "hud_3.hcf",
		Game_builtin_missions_fs2, &Game_builtin_missions_fs2_count,
		FVS_ALL, 0, 0, 0, 0 },
	{	DATA_SET_FS2_DEMO, 1,
		"FreeSpace2Demo", "Freespace 2 Demo",
		1, 10, 0, " D", "http://www.pxo.net/files/fs2demo/version.nfo", "http://www.pxo.net/files/fs2demo/motd.txt",
		"demo", "DEMO Campaign",
		{ "PreLoad", "2_PreLoad" }, "hud_1.hcf",
		Game_builtin_missions_fs2_demo, &Game_builtin_missions_fs2_demo_count,
		0, 1, 1, 1, 1 },
	// FS1 rows: FS2 retail values until the FS1 work fills them in (D-20261020-053500)
	{	DATA_SET_FS1_RETAIL, 0,
		"FreeSpace2", "Freespace 2",
		1, 20, 0, "", "http://www.pxo.net/files/fs2/version.nfo", "http://www.pxo.net/files/fs2/motd.txt",
		"FreeSpace2", "The Main Freespace2 Campaign",
		{ NULL, NULL }, "hud_3.hcf",
		Game_builtin_missions_fs2, &Game_builtin_missions_fs2_count,
		FVS_ALL, 0, 0, 0, 0 },
	{	DATA_SET_FS1_DEMO, 0,
		"FreeSpace2", "Freespace 2",
		1, 20, 0, "", "http://www.pxo.net/files/fs2/version.nfo", "http://www.pxo.net/files/fs2/motd.txt",
		"FreeSpace2", "The Main Freespace2 Campaign",
		{ NULL, NULL }, "hud_3.hcf",
		Game_builtin_missions_fs2, &Game_builtin_missions_fs2_count,
		FVS_ALL, 0, 0, 0, 0 },
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
