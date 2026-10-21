/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/

#include <windows.h>
#include <string.h>
#include "PsTypes.h"
#include "OsRegistry.h"

// NEVER ADD ANY OTHER INCLUDES HERE - YOU'LL LIKELY BREAK THE LAUNCHER!!!!!!!!!!!!!

// ------------------------------------------------------------------------------------------------------------
// REGISTRY DEFINES/VARS
//

static char			szCompanyName[128];
static char			szAppName[128];
static char			szAppVersion[128];

char *Osreg_company_name = "Volition";
char *Osreg_class_name = "Freespace2Class";
#if defined(OEM_BUILD)	// Claude: the demo's names are in the data set profile, set in game_init (D-20261020-053500)
char *Osreg_app_name = "FreeSpace2OEM";
char *Osreg_title = "Freespace 2 OEM";
#else	// Claude: the data set profile's, once game_init sets them; the tools keep these
char *Osreg_app_name = "FreeSpace2";
char *Osreg_title = "Freespace 2";
#endif

int Os_reg_inited = 0;

// ------------------------------------------------------------------------------------------------------------
// Claude: SETTINGS FILE
//
// Settings live in an ini file (fs2.ini in the game folder, or the file given with -ini) instead of
// the registry, which the game never reads or writes.  Settings the game needs that the file lacks
// are written with defaults.  Settings with no section go in [Settings].  The launcher's Videocard
// string is kept as Renderer, Device, ColourDepth and Resolution, and put back together when the
// game reads Videocard.

#define OS_INI_DEFAULT_FILE	"fs2.ini"
#define OS_INI_MAIN_SECTION	"Settings"
#define OS_INI_MISSING			"<missing>"

static char Os_ini_file[MAX_PATH] = "";
static int Os_ini_from_cmdline = 0;	// Claude: -ini named the file; the root folder no longer chooses it
static int Os_ini_active = 0;

static char *os_ini_section( char *section )
{
	return section ? section : OS_INI_MAIN_SECTION;
}

// 1 and the value in buf if the ini has the key
static int os_ini_get( char *section, char *name, char *buf, int size )
{
	GetPrivateProfileString( os_ini_section(section), name, OS_INI_MISSING, buf, size, Os_ini_file );
	return strcmp( buf, OS_INI_MISSING ) != 0;
}

// the launcher's Videocard string from the ini keys, or NULL if there is no Renderer
static char *os_ini_join_videocard( char *buf )
{
	char renderer[64], device[256], depth[16], res[64];

	if ( !os_ini_get( NULL, "Renderer", renderer, sizeof(renderer) ) ) {
		return NULL;
	}
	if ( !stricmp( renderer, "Direct3D" ) ) {
		if ( !os_ini_get( NULL, "Device", device, sizeof(device) ) ) {
			strcpy( device, "Primary Display Driver" );
		}
		sprintf( buf, "Direct 3D - %s", device );
	} else if ( !stricmp( renderer, "Glide" ) ) {
		strcpy( buf, "3DFX Glide" );
	} else {
		strcpy( buf, renderer );
	}
	if ( os_ini_get( NULL, "ColourDepth", depth, sizeof(depth) ) ) {
		sprintf( buf + strlen(buf), " (%s bit)", depth );
	}
	if ( os_ini_get( NULL, "Resolution", res, sizeof(res) ) ) {
		sprintf( buf + strlen(buf), " (%s)", res );
	}
	return buf;
}

// first run: create the ini with a header; os_ini_write_defaults fills it in
static void os_ini_create()
{
	FILE *fp;

	fp = fopen( Os_ini_file, "wt" );
	if ( fp == NULL ) {
		return;
	}
	fprintf( fp, "; FreeSpace 2 settings.  The game reads only this file, never the registry.\n" );
	fprintf( fp, "; Renderer, Device, ColourDepth and Resolution replace the launcher's Videocard setting.\n" );
	fprintf( fp, "; [Detail] sets the detail levels for every pilot (0 lowest, 4 highest; the last three 0 or 1).\n\n" );
	fclose( fp );
}

// -ini <file>: called from parse_cmdline, before the game starts
void os_config_set_ini( char *file )
{
	GetFullPathName( file, MAX_PATH, Os_ini_file, NULL );
	Os_ini_from_cmdline = 1;
}

// Claude: game_host_init. fs2.ini in the root the host gives, instead of the current directory (D-056).
// -ini, read after this by parse_cmdline, still names another file.
void os_config_set_dir( char *dir )
{
	if ( Os_ini_from_cmdline ) {	// the data set folder, chosen after parse_cmdline, used to replace the -ini file
		return;
	}
	_snprintf( Os_ini_file, MAX_PATH - 1, "%s\\%s", dir, OS_INI_DEFAULT_FILE );
	Os_ini_file[MAX_PATH - 1] = 0;
}

char *os_config_ini_file()
{
	return Os_ini_file;
}

// settings the game needs that the launcher used to write.  Any that the ini does not have are
// written with these values, so the game needs neither the launcher nor the registry.
static char *Os_ini_defaults[][2] = {
	{ "Renderer",		"Direct3D" },
	{ "Device",			"Primary Display Driver" },
	{ "ColourDepth",	"32" },
	{ "Resolution",	"1024x768" },
	{ "Soundcard",		"DirectSound" },
	{ "Language",		"English" },
};
#define OS_INI_NUM_DEFAULTS	(sizeof(Os_ini_defaults) / sizeof(Os_ini_defaults[0]))

extern int Cfile_fs1_data;	// Claude: cfile.h, not included here (see the includes)

static void os_ini_write_defaults()
{
	char buf[1024];
	int i;

	for ( i = 0; i < OS_INI_NUM_DEFAULTS; i++ ) {
		if ( !os_ini_get( NULL, Os_ini_defaults[i][0], buf, sizeof(buf) ) ) {
			// a launcher string kept as Videocard (not in the usual form) still decides the renderer
			if ( (i < 4) && os_ini_get( NULL, "Videocard", buf, sizeof(buf) ) ) {
				continue;
			}
			// Claude: FS1 data has only 640x480 art (docs/FS1_COMPAT.md), so it starts there
			if ( Cfile_fs1_data && !stricmp( Os_ini_defaults[i][0], "Resolution" ) ) {
				WritePrivateProfileString( OS_INI_MAIN_SECTION, Os_ini_defaults[i][0], "640x480", Os_ini_file );
				continue;
			}
			WritePrivateProfileString( OS_INI_MAIN_SECTION, Os_ini_defaults[i][0], Os_ini_defaults[i][1], Os_ini_file );
		}
	}
}

static void os_ini_init()
{
	if ( !Os_ini_file[0] ) {
		GetCurrentDirectory( MAX_PATH - 16, Os_ini_file );
		strcat( Os_ini_file, "\\" OS_INI_DEFAULT_FILE );
	}
	if ( GetFileAttributes( Os_ini_file ) == INVALID_FILE_ATTRIBUTES ) {
		os_ini_create();
	}
	os_ini_write_defaults();
	Os_ini_active = 1;
}

static char *os_ini_read_string( char *section, char *name, char *default_value, char *buf )
{
	if ( !name ) {
		return default_value;
	}
	if ( os_ini_get( section, name, buf, 1024 ) ) {
		return buf;
	}
	if ( !section && !stricmp( name, "Videocard" ) && os_ini_join_videocard( buf ) ) {
		return buf;
	}
	return default_value;
}


// ------------------------------------------------------------------------------------------------------------
// REGISTRY FUNCTIONS
//

// os registry functions -------------------------------------------------------------

// initialize the registry. setup default keys to use
void os_init_registry_stuff(char *company, char *app, char *version)
{
	if(company){
		strcpy( szCompanyName, company );	
	} else {
		strcpy( szCompanyName, Osreg_company_name);
	}

	if(app){
		strcpy( szAppName, app );	
	} else {
		strcpy( szAppName, Osreg_app_name);
	}

	if(version){
		strcpy( szAppVersion, version);	
	} else {
		strcpy( szAppVersion, "1.0");
	}

	Os_reg_inited = 1;
	os_ini_init();	// Claude: settings come from the ini from now on
}

// Removes a value from to the INI file.  Passing
// name=NULL will delete the section.
void os_config_remove( char *section, char *name )
{
	// Claude: settings are only in the ini, never the registry
	if ( Os_ini_active && (section || name) ) {
		WritePrivateProfileString( os_ini_section(section), name, NULL, Os_ini_file );
	}
}

// Writes a string to the INI file.  If value is NULL,
// removes the string. Writing a NULL value to a NULL name will delete
// the section.
void os_config_write_string( char *section, char *name, char *value )
{
	// Claude: settings go only to the ini, never the registry
	if ( Os_ini_active && name ) {
		WritePrivateProfileString( os_ini_section(section), name, value, Os_ini_file );
	}
}

// Writes an unsigned int to the INI file.  
void os_config_write_uint( char *section, char *name, uint value )
{
	// Claude: settings go only to the ini, never the registry
	char num[16];

	if ( Os_ini_active && name ) {
		sprintf( num, "%u", value );
		WritePrivateProfileString( os_ini_section(section), name, num, Os_ini_file );
	}
}


// Reads a string from the INI file.  If default is passed,
// and the string isn't found, returns ptr to default otherwise
// returns NULL;    Copy the return value somewhere before
// calling os_read_string again, because it might reuse the
// same buffer.
static char tmp_string_data[1024];
char * os_config_read_string( char *section, char *name, char *default_value )
{
	// Claude: settings come only from the ini, never the registry
	if ( !Os_ini_active ) {
		return NULL;
	}
	return os_ini_read_string( section, name, default_value, tmp_string_data );
}

// Reads a string from the INI file.  Default_value must 
// be passed, and if 'name' isn't found, then returns default_value
uint  os_config_read_uint( char *section, char *name, uint default_value )
{
	// Claude: settings come only from the ini, never the registry
	char num[64];

	if ( !Os_ini_active ) {
		return NULL;
	}
	if ( name && os_ini_get( section, name, num, sizeof(num) ) ) {
		return (uint)strtoul( num, NULL, 10 );
	}
	return default_value;
}

