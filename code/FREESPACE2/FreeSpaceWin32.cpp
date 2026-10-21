// Claude: the Windows host (decision D-045). WinMain, the exe directory and memory checks, launcher update and
// launch, and CD detection, moved verbatim from FreeSpace.cpp so the game code needs no windows.h. Another host
// (Godot) supplies its own entry point and these hooks, or their absence, and leaves this file out.

#include <windows.h>
#include <stdlib.h>
#include <process.h>
#include <direct.h>
#include <io.h>

#include "pstypes.h"
#include "SystemVars.h"
#include "OsApi.h"
#include "HostHooks.h"
#include "FreeSpace.h"
#include "GameSequence.h"
#include "cmdline.h"
#include "CFile.h"
#include "popup.h"
#include "localize.h"
#include "Replay.h"
#include "Autotest.h"
#include "ExceptionHandler.h"
#include "Demo.h"

#define EXE_FNAME			("fs2.exe")
#define LAUNCHER_FNAME	("freespace2.exe")

// Check if there is a freespace.exe in the /update directory (relative to where fs.exe is installed).
// If so, copy it over and remove the update directory.
void game_maybe_update_launcher(char *exe_dir)
{
	char src_filename[MAX_PATH];
	char dest_filename[MAX_PATH];

	strcpy(src_filename, exe_dir);
	strcat(src_filename, NOX("\\update\\freespace.exe"));

	strcpy(dest_filename, exe_dir);
	strcat(dest_filename, NOX("\\freespace.exe"));

	// see if src_filename exists
	FILE *fp;
	fp = fopen(src_filename, "rb");
	if ( !fp ) {
		return;
	}
	fclose(fp);

	SetFileAttributes(dest_filename, FILE_ATTRIBUTE_NORMAL);

	// copy updated freespace.exe to freespace exe dir
	if ( CopyFile(src_filename, dest_filename, 0) == 0 ) {
		os_message_box(XSTR("Unable to copy freespace.exe from update directory to installed directory.  You should copy freespace.exe from the update directory (located in your FreeSpace install directory) to your install directory", 988), NULL, OS_MB_FOREGROUND);
		return;
	}

	// delete the file in the update directory
	DeleteFile(src_filename);

	// safe to assume directory is empty, since freespace.exe should only be the file ever in the update dir
	char update_dir[MAX_PATH];
	strcpy(update_dir, exe_dir);
	strcat(update_dir, NOX("\\update"));
	RemoveDirectory(update_dir);
}

// Claude: whether the command line has this option, before parse_cmdline runs (it runs in game_host_init)
static int host_cmdline_has(const char *cmdline, const char *option)
{
	int len = (int)strlen(option);
	const char *p = cmdline;
	while ( (p = strstr(p, option)) != NULL ) {
		if ( ((p == cmdline) || (p[-1] == ' ') || (p[-1] == '\t')) && ((p[len] == 0) || (p[len] == ' ') || (p[len] == '\t')) ) {
			return 1;
		}
		p += len;
	}
	return 0;
}

// Claude: find option as a whole word in cmdline; returns where its value starts and how long it is (quotes
// included), or NULL
static char *host_cmdline_find(char *cmdline, const char *option, int *value_len, char **option_start)
{
	int len = (int)strlen(option);
	char *p = cmdline;
	while ( (p = strstr(p, option)) != NULL ) {
		if ( ((p == cmdline) || (p[-1] == ' ') || (p[-1] == '\t')) && ((p[len] == ' ') || (p[len] == '\t')) ) {
			*option_start = p;
			char *v = p + len;
			while ( (*v == ' ') || (*v == '\t') ) {
				v++;
			}
			char *e = v;
			if ( *e == '"' ) {
				e = strchr(e + 1, '"');
				e = e ? (e + 1) : (v + strlen(v));
			} else {
				while ( *e && (*e != ' ') && (*e != '\t') ) {
					e++;
				}
			}
			*value_len = (int)(e - v);
			return v;
		}
		p += len;
	}
	return NULL;
}

// Claude: take "option value" out of cmdline, copying the value (with any quotes) to value; 1 if it was there
static int host_cmdline_take(char *cmdline, const char *option, char *value, int value_size)
{
	int n;
	char *start;
	char *v = host_cmdline_find(cmdline, option, &n, &start);
	if ( v == NULL ) {
		return 0;
	}
	if ( n > value_size - 1 ) {
		n = value_size - 1;
	}
	memcpy(value, v, n);
	value[n] = 0;
	memmove(start, v + n, strlen(v + n) + 1);
	return 1;
}

// Claude: give an option already in cmdline a new value (cmdline has room for cmdline_size bytes)
static void host_cmdline_set(char *cmdline, int cmdline_size, const char *option, const char *value)
{
	int n;
	char *start;
	char *v = host_cmdline_find(cmdline, option, &n, &start);
	if ( (v == NULL) || ((int)(strlen(cmdline) - n + strlen(value)) >= cmdline_size) ) {
		return;
	}
	memmove(v + strlen(value), v + n, strlen(v + n) + 1);
	memcpy(v, value, strlen(value));
}

int PASCAL WinMainSub(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int nCmdShow)
{
	// Claude: the platform seam, filled before anything reads the machine (D-051). -headless has its own.
	host_hooks_set(host_cmdline_has(szCmdLine, "-headless") ? &host_hooks_headless : &host_hooks_windows);

	replay_wait_for_restart();	// Claude: -autorecord restarts the game; let the old instance close first

	// Don't let more than one instance of Freespace run.
	HWND hwnd = FindWindow( NOX( "FreeSpaceClass" ), NULL );
	if ( hwnd )	{
		SetForegroundWindow(hwnd);
		return 0;
	}

	// Find out how much RAM is on this machine
	MEMORYSTATUS ms;
	ms.dwLength = sizeof(MEMORYSTATUS);
	GlobalMemoryStatus(&ms);
	// Claude: x64, cast to uint, its width on Win32; above 4 GB it truncates, but nothing reads it (game_do_ram_check has no caller) (D-20261020-194406 batch 2)
	Freespace_total_ram = (uint)ms.dwTotalPhys;

	if ( ms.dwTotalVirtual < 1024 )	{
		os_message_box(XSTR( "FreeSpace requires virtual memory to run.\r\n", 196), XSTR( "No Virtual Memory", 197), 0);
		return 0;
	}

	if (!vm_init(24*1024*1024)) {
		os_message_box(XSTR( "Not enough memory to run Freespace.\r\nTry closing down some other applications.\r\n", 198), XSTR( "Not Enough Memory", 199), 0);
		return 0;
	}
		
	char *tmp_mem = (char *) malloc(16 * 1024 * 1024);
	if (!tmp_mem) {
		os_message_box(XSTR( "Not enough memory to run Freespace.\r\nTry closing down some other applications.\r\n", 198), XSTR( "Not Enough Memory", 199), 0);
		return 0;
	}

	free(tmp_mem);
	tmp_mem = NULL;

/* this code doesn't work, and we will hit an error about being unable to load the direct draw
	dll before we get here anyway if it's not installed (unless we load it manually, which doesn't
	seem worth bothering with.

	LONG lResult;

	lResult = RegOpenKeyEx(
		HKEY_LOCAL_MACHINE,					// Where it is
		"Software\\Microsoft\\DirectX",	// name of key
		NULL,										// DWORD reserved
		KEY_QUERY_VALUE,						// Allows all changes
		&hKey										// Location to store key
	);

	if (lResult == ERROR_SUCCESS) {
		char version[32];
		DWORD dwType, dwLen;

		dwLen = 32;
		lResult = RegQueryValueEx(
			hKey,									// Handle to key
			"Version",							// The values name
			NULL,									// DWORD reserved
			&dwType,								// What kind it is
			(ubyte *) version, 				// value to set
			&dwLen								// How many bytes to set
		);

		if (lResult == ERROR_SUCCESS) {
			dx_version = atoi(strstr(version, ".") + 1);

		} else {
			int val;
			DWORD dwType, dwLen;

			dwLen = 4;
			lResult = RegQueryValueEx(
				hKey,									// Handle to key
				"InstalledVersion",				// The values name
				NULL,									// DWORD reserved
				&dwType,								// What kind it is
				(ubyte *) &val,					// value to set
				&dwLen								// How many bytes to set
			);

			if (lResult == ERROR_SUCCESS) {
				dx_version = val;
			}
		}

		RegCloseKey(hKey);
	}

	if (dx_version < 3) {
		MessageBox(NULL, "DirectX 3.0 or higher is required and wasn't detected.  You can get the\n"
			"latest version of DirectX at:\n\n"
			"http://www.microsoft.com/msdownload/directx/dxf/enduser5.0/default.htm", "DirectX required", MB_OK);

		MessageBox(NULL, "DirectX 3.0 or higher is required and wasn't detected.  You can install\n"
			"DirectX 5.2 by pressing the 'Install DirectX' button on the FreeSpace Launcher", "DirectX required", MB_OK);

		return 0;
	}
*/
	//=====================================================
	// Make sure we're running in the right directory.
	char exe_dir[1024];

	if ( GetModuleFileName( hInst, exe_dir, 1023 ) > 0 )	{
		char *p = exe_dir + strlen(exe_dir);

		// chop off the filename
		while( (p>exe_dir) && (*p!='\\') && (*p!='/') && (*p!=':') )	{
			p--;
		}
		*p = 0;

		// Set directory
		if ( strlen(exe_dir) > 0 )	{
			SetCurrentDirectory(exe_dir);
		}

		// check for updated freespace.exe
		game_maybe_update_launcher(exe_dir);
	}

	
	
	// Claude: the game's start-up, frames and shutdown are game_host_init, game_host_frame and game_host_shutdown (D-050)
	char root_dir[1024];
	_getcwd(root_dir, 1024);	// the exe directory, set above

	// Claude: -replay_next <recording> -trace_next <trace>: after the first run, init, replay and shut down again in
	// this process with that recording and trace, to test that the game restarts cleanly (D-055)
	char cmdline[4096], first_cmdline[4096], next_rec[MAX_PATH], next_trace[MAX_PATH];
	strncpy(first_cmdline, szCmdLine, sizeof(first_cmdline) - 1);
	first_cmdline[sizeof(first_cmdline) - 1] = 0;
	int run_next = host_cmdline_take(first_cmdline, "-replay_next", next_rec, sizeof(next_rec));
	int trace_next = host_cmdline_take(first_cmdline, "-trace_next", next_trace, sizeof(next_trace));
	strcpy(cmdline, first_cmdline);

	int code;
	for (;;) {
		if ( !game_host_init(cmdline, root_dir) ) {
			return 1;
		}

		while ( game_host_frame() ) {
		}

		code = game_host_shutdown();
		if ( !run_next || code ) {
			break;
		}
		run_next = 0;
		strcpy(cmdline, first_cmdline);
		host_cmdline_set(cmdline, sizeof(cmdline), "-replay", next_rec);
		if ( trace_next ) {
			host_cmdline_set(cmdline, sizeof(cmdline), "-trace", next_trace);
		}
	}

	return code;
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int nCmdShow)
{
	int result = -1;

	__try
	{
		result = WinMainSub(hInst, hPrev, szCmdLine, nCmdShow);
	}
	__except(RecordExceptionInfo(GetExceptionInformation(), "Freespace 2 Main Thread"))
	{
		// Do nothing here - RecordExceptionInfo() has already done
		// everything that is needed. Actually this code won't even
		// get called unless you return EXCEPTION_EXECUTE_HANDLER from
		// the __except clause.
	}
	return result;
}

// launcher the fslauncher program on exit
void game_launch_launcher_on_exit()
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	char cmd_line[2048];
	char original_path[1024] = "";
	
	memset( &si, 0, sizeof(STARTUPINFO) );
	si.cb = sizeof(si);

	// directory
	_getcwd(original_path, 1023);

	// set up command line
	strcpy(cmd_line, original_path);
	strcat(cmd_line, "\\");
	strcat(cmd_line, LAUNCHER_FNAME);
	strcat(cmd_line, " -straight_to_update");		

	BOOL ret = CreateProcess(	NULL,									// pointer to name of executable module 
										cmd_line,							// pointer to command line string
										NULL,									// pointer to process security attributes 
										NULL,									// pointer to thread security attributes 
										FALSE,								// handle inheritance flag 
										CREATE_DEFAULT_ERROR_MODE,		// creation flags 
										NULL,									// pointer to new environment block 
										NULL,									// pointer to current directory name 
										&si,									// pointer to STARTUPINFO 
										&pi									// pointer to PROCESS_INFORMATION  
										);			
	// to eliminate build warnings
	ret;
}

// ----------------------------------------------------------------
//
// CDROM detection code START
//
// ----------------------------------------------------------------

#define CD_SIZE_72_MINUTE_MAX			(697000000)

uint game_get_cd_used_space(char *path)
{
	uint total = 0;
	char use_path[512] = "";
	char sub_path[512] = "";
	WIN32_FIND_DATA	find;
	HANDLE find_handle;

	// recurse through all files and directories
	strcpy(use_path, path);
	strcat(use_path, "*.*");
	find_handle = FindFirstFile(use_path, &find);

	// bogus
	if(find_handle == INVALID_HANDLE_VALUE){
		return 0;
	}	

	// whee
	do {
		// subdirectory. make sure to ignore . and ..
		if((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && stricmp(find.cFileName, ".") && stricmp(find.cFileName, "..")){
			// subsearch
			strcpy(sub_path, path);
			strcat(sub_path, find.cFileName);
			strcat(sub_path, "\\");
			total += game_get_cd_used_space(sub_path);	
		} else {
			total += (uint)find.nFileSizeLow;
		}				
	} while(FindNextFile(find_handle, &find));	

	// close
	FindClose(find_handle);

	// total
	return total;
}


// if volume_name is non-null, the CD name must match that
int find_freespace_cd(char *volume_name)
{
	char oldpath[MAX_PATH];
	char volume[256];
	int i;
	int cdrom_drive=-1;
	int volume_match = 0;
	_finddata_t find;
	intptr_t find_handle;	// Claude: intptr_t, _findfirst returns one; int on x86 (D-057)

	GetCurrentDirectory(MAX_PATH, oldpath);

	for (i = 0; i < 26; i++) 
	{
//XSTR:OFF
		char path[]="d:\\";
//XSTR:ON

		path[0] = (char)('A'+i);
		if (GetDriveType(path) == DRIVE_CDROM) {
			cdrom_drive = -3;
			if ( GetVolumeInformation(path, volume, 256, NULL, NULL, NULL, NULL, 0) == TRUE ) {
				nprintf(("CD", "CD volume: %s\n", volume));
			
				// check for any CD volume
				int volume1_present = 0;
				int volume2_present = 0;
				int volume3_present = 0;		

				char full_check[512] = "";

				// look for setup.exe
				strcpy(full_check, path);
				strcat(full_check, "setup.exe");				
				find_handle = _findfirst(full_check, &find);
				if(find_handle != -1){
					volume1_present = 1;				
					_findclose(find_handle);				
				}

				// look for intro.mve
				strcpy(full_check, path);
				strcat(full_check, "intro.mve");				
				find_handle = _findfirst(full_check, &find);
				if(find_handle != -1){
					volume2_present = 1;
					_findclose(find_handle);						
				}				

				// look for endpart1.mve
				strcpy(full_check, path);
				strcat(full_check, "endpart1.mve");				
				find_handle = _findfirst(full_check, &find);
				if(find_handle != -1){
					volume3_present = 1;
					_findclose(find_handle);				
				}				
			
				// see if we have the specific CD we're looking for
				if ( volume_name ) {
					// volume 1
					if ( !stricmp(volume_name, FS_CDROM_VOLUME_1) && volume1_present) {
						volume_match = 1;
					}
					// volume 2
					if ( !stricmp(volume_name, FS_CDROM_VOLUME_2) && volume2_present) {
						volume_match = 1;
					}
					// volume 3
					if ( !stricmp(volume_name, FS_CDROM_VOLUME_3) && volume3_present) {
						volume_match = 1;
					}
				} else {										
					if ( volume1_present || volume2_present || volume3_present ) {
						volume_match = 1;
					}
				}
				
				// here's where we make sure that CD's 2 and 3 are not just ripped - check to make sure its capacity is > 697,000,000 bytes				
				if ( volume_match ){
#ifdef RELEASE_REAL					
					// we don't care about CD1 though. let it be whatever size it wants, since the game will demand CD's 2 and 3 at the proper time
					if(volume2_present || volume3_present) {
						// first step - check to make sure its a cdrom
						if(GetDriveType(path) != DRIVE_CDROM){							
							break;
						}

#if !defined(OEM_BUILD)
						// oem not on 80 min cds, so dont check tha size
						// check its size
						uint used_space = game_get_cd_used_space(path);											
						if(used_space < CD_SIZE_72_MINUTE_MAX){							
							break;
						}
#endif // !defined(OEM_BUILD)
					}					

					cdrom_drive = i;
					break;
#else
					cdrom_drive = i;
					break;
#endif // RELEASE_REAL
				}
			}
		}
	}	

	SetCurrentDirectory(oldpath);
	return cdrom_drive;
}

int set_cdrom_path(int drive_num)
{
	int rval;

	if (drive_num < 0) {			//no CD
//		#ifndef NDEBUG
//		strcpy(CDROM_dir,"j:\\FreeSpaceCD\\");				//set directory
//		rval = 1;
//		#else
		strcpy(Game_CDROM_dir,"");				//set directory
		rval = 0;
//		#endif
	} else {
		sprintf(Game_CDROM_dir,NOX("%c:\\"), 'a' + drive_num );			//set directory
		rval = 1;
	}

	return rval;
}

int init_cdrom()
{
	int i, rval;

	//scan for CD, etc.

	rval = 1;

	#ifndef DEMO
	i = find_freespace_cd();

	rval = set_cdrom_path(i);

	/*
	if ( rval ) {
		nprintf(("CD", "Using %s for FreeSpace CD\n", CDROM_dir));
	} else {
		nprintf(("CD", "FreeSpace CD not found\n"));
	}
	*/
	#endif

	return rval;
}

int Last_cd_label_found = 0;
char Last_cd_label[256];

int game_cd_changed()
{
	char label[256];
	int found;
	int changed = 0;
	
	if ( strlen(Game_CDROM_dir) == 0 ) {
		init_cdrom();
	}

	found = GetVolumeInformation(Game_CDROM_dir, label, 256, NULL, NULL, NULL, NULL, 0);

	if ( found != Last_cd_label_found )	{
		Last_cd_label_found = found;
		if ( found )	{
			mprintf(( "CD '%s' was inserted\n", label ));
			changed = 1;
		} else {
			mprintf(( "CD '%s' was removed\n", Last_cd_label ));
			changed = 1;
		}
	} else {
		if ( Last_cd_label_found )	{
			if ( !stricmp( Last_cd_label, label ))	{
				//mprintf(( "CD didn't change\n" ));
			} else {
				mprintf(( "CD was changed from '%s' to '%s'\n", Last_cd_label, label ));
				changed = 1;
			}
		} else {
			// none found before, none found now.
			//mprintf(( "still no CD...\n" ));
		}
	}
	
	Last_cd_label_found = found;
	if ( found )	{
		strcpy( Last_cd_label, label );
	} else {
		strcpy( Last_cd_label, "" );
	}

	return changed;
}

// check if _any_ FreeSpace2 CDs are in the drive
// return: 1	=> CD now in drive
//			  0	=>	Could not find CD, they refuse to put it in the drive
int game_do_cd_check(char *volume_name)
{	
#if !defined(GAME_CD_CHECK)
	return 1;
#else
	int cd_present = 0;
	int cd_drive_num;

	int num_attempts = 0;
	int refresh_files = 0;
	while(1) {
		int path_set_ok, popup_rval;

		cd_drive_num = find_freespace_cd(volume_name);
		path_set_ok = set_cdrom_path(cd_drive_num);
		if ( path_set_ok ) {
			cd_present = 1;
			if ( refresh_files ) {
				cfile_refresh();
				refresh_files = 0;
			}
			break;
		}

		// standalone mode
		if(Is_standalone){
			cd_present = 0;
			break;
		} else {
			// no CD found, so prompt user
			popup_rval = popup(PF_BODY_BIG, 1, POPUP_OK, XSTR( "FreeSpace 2 CD not found\n\nInsert a FreeSpace 2 CD to continue", 202));
			refresh_files = 1;
			if ( popup_rval != 1 ) {
				cd_present = 0;
				break;
			}

			if ( num_attempts++ > 5 ) {
				cd_present = 0;
				break;
			}
		}
	}

	return cd_present;
#endif
}

// check if _any_ FreeSpace2 CDs are in the drive
// return: 1	=> CD now in drive
//			  0	=>	Could not find CD, they refuse to put it in the drive
int game_do_cd_check_specific(char *volume_name, int cdnum)
{	
	int cd_present = 0;
	int cd_drive_num;

	int num_attempts = 0;
	int refresh_files = 0;
	while(1) {
		int path_set_ok, popup_rval;

		cd_drive_num = find_freespace_cd(volume_name);
		path_set_ok = set_cdrom_path(cd_drive_num);
		if ( path_set_ok ) {
			cd_present = 1;
			if ( refresh_files ) {
				cfile_refresh();
				refresh_files = 0;
			}
			break;
		}

		if(Is_standalone){
			cd_present = 0;
			break;
		} else {
			// no CD found, so prompt user
#if defined(DVD_MESSAGE_HACK)
			popup_rval = popup(PF_BODY_BIG, 1, POPUP_OK, XSTR("Please insert DVD", 1468));
#else
			popup_rval = popup(PF_BODY_BIG, 1, POPUP_OK, XSTR("Please insert CD %d", 1468), cdnum);
#endif
			refresh_files = 1;
			if ( popup_rval != 1 ) {
				cd_present = 0;
				break;
			}

			if ( num_attempts++ > 5 ) {
				cd_present = 0;
				break;
			}
		}
	}

	return cd_present;
}

// only need to do this in RELEASE_REAL
int game_do_cd_mission_check(char *filename)
{	
#ifdef RELEASE_REAL
	int cd_num;
	int cd_present = 0;
	int cd_drive_num;
	fs_builtin_mission *m = game_find_builtin_mission(filename);

	// check for changed CD
	if(game_cd_changed()){
		cfile_refresh();
	}

	// multiplayer
	if((Game_mode & GM_MULTIPLAYER) || Is_standalone){
		return 1;
	}

	// not builtin, so do a general check (any FS2 CD will do)
	if(m == NULL){
		return game_do_cd_check();
	}

	// does not have any CD requirement, do a general check
	if(strlen(m->cd_volume) <= 0){
		return game_do_cd_check();
	}

	// get the volume
	if(!stricmp(m->cd_volume, FS_CDROM_VOLUME_1)){
		cd_num = 1;
	} else if(!stricmp(m->cd_volume, FS_CDROM_VOLUME_2)){
		cd_num = 2;
	} else if(!stricmp(m->cd_volume, FS_CDROM_VOLUME_3)){
		cd_num = 3; 
	} else {
		return game_do_cd_check();
	}

	// did we find the cd?
	if(find_freespace_cd(m->cd_volume) >= 0){
		return 1;
	}

	// make sure the volume exists
	int num_attempts = 0;
	int refresh_files = 0;
	while(1){
		int path_set_ok, popup_rval;

		cd_drive_num = find_freespace_cd(m->cd_volume);
		path_set_ok = set_cdrom_path(cd_drive_num);
		if ( path_set_ok ) {
			cd_present = 1;
			if ( refresh_files ) {
				cfile_refresh();
				refresh_files = 0;
			}
			break;
		}

		// no CD found, so prompt user
#if defined(DVD_MESSAGE_HACK)
		popup_rval = popup(PF_BODY_BIG, 1, POPUP_OK, XSTR("Please insert DVD", 1468));
#else
		popup_rval = popup(PF_BODY_BIG, 1, POPUP_OK, XSTR("Please insert CD %d", 1468), cd_num);
#endif

		refresh_files = 1;
		if ( popup_rval != 1 ) {
			cd_present = 0;
			break;
		}

		if ( num_attempts++ > 5 ) {
			cd_present = 0;
			break;
		}
	}	

	return cd_present;
#else
	return 1;
#endif
}

// ----------------------------------------------------------------
//
// CDROM detection code END
//
// ----------------------------------------------------------------

// Claude: no video card configured yet: tell the player, start the launcher and exit. Was inline in game_init (D-045).
void game_run_launcher()
{
	os_message_box(XSTR("Please configure your system in the Launcher before running FS2. YOU MUST RUN AS ADMIN! \n\n The Launcher will now be started!", 1446), XSTR("Attention!", 1447), OS_MB_OWNED);

	// fire up the UpdateLauncher executable
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	memset( &si, 0, sizeof(STARTUPINFO) );
	si.cb = sizeof(si);

	BOOL ret = CreateProcess(	LAUNCHER_FNAME,	// pointer to name of executable module 
								NULL,							// pointer to command line string
								NULL,							// pointer to process security attributes 
								NULL,							// pointer to thread security attributes 
								FALSE,							// handle inheritance flag 
								CREATE_DEFAULT_ERROR_MODE,		// creation flags 
								NULL,							// pointer to new environment block 
								NULL,	// pointer to current directory name 
								&si,	// pointer to STARTUPINFO 
								&pi 	// pointer to PROCESS_INFORMATION  
							);			

	// If the Launcher could not be started up, let the user know
	if (!ret) {
		os_message_box(XSTR("The Launcher could not be restarted.", 1450), XSTR("Error", 1451), OS_MB_OWNED);
	}
	exit(1);
}

// Claude: the Win32 side of the title screen hold in FreeSpace.cpp (Enhanced's title_screen_hold; PrePort keeps
// Win32 calls out of FreeSpace.cpp)
uint game_title_ticks()
{
	return GetTickCount();
}

int game_title_skip_pressed()
{
	return ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_ESCAPE) | GetAsyncKeyState(VK_SPACE) | GetAsyncKeyState(VK_RETURN)) & 0x8000) ? 1 : 0;
}
