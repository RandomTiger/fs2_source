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
#if defined(FS2_DEMO)
char *Osreg_app_name = "FreeSpace2Demo";
char *Osreg_title = "Freespace 2 Demo";
#elif defined(OEM_BUILD)
char *Osreg_app_name = "FreeSpace2OEM";
char *Osreg_title = "Freespace 2 OEM";
#else
char *Osreg_app_name = "FreeSpace2";
char *Osreg_title = "Freespace 2";
#endif

int Os_reg_inited = 0;

// Some installs (GOG) keep the launcher's settings under "Volition\Free" instead of
// "Volition\FreeSpace2", so reads fall back to that name.
static char *Osreg_launcher_app_name = "Free";

// open keyname under root if it holds the value 'name' (or just exists, if name is NULL)
static LONG os_reg_try_open( HKEY root, char *keyname, HKEY *hKey, char *name )
{
	LONG lResult;

	lResult = RegOpenKeyEx( root, keyname, NULL, KEY_QUERY_VALUE, hKey );
	if ( lResult != ERROR_SUCCESS )	{
		*hKey = NULL;
		return lResult;
	}
	if ( name && (RegQueryValueEx( *hKey, name, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS) )	{
		RegCloseKey( *hKey );
		*hKey = NULL;
		return ERROR_FILE_NOT_FOUND;
	}
	return ERROR_SUCCESS;
}

// Settings the game writes go to HKEY_CURRENT_USER, which needs no admin rights.  The launcher's
// settings are in HKEY_LOCAL_MACHINE.  A read takes the value from the first place that has it:
// HKEY_CURRENT_USER, then HKEY_LOCAL_MACHINE, then HKEY_LOCAL_MACHINE under the launcher's
// alternative key name.
static LONG os_reg_open_read( char *keyname, HKEY *hKey, char *name )
{
	char prefix[256], alt_keyname[1024];
	int len;

	if ( os_reg_try_open( HKEY_CURRENT_USER, keyname, hKey, name ) == ERROR_SUCCESS )	{
		return ERROR_SUCCESS;
	}
	if ( os_reg_try_open( HKEY_LOCAL_MACHINE, keyname, hKey, name ) == ERROR_SUCCESS )	{
		return ERROR_SUCCESS;
	}

	// same key with the application part of the name swapped for the launcher's
	sprintf( prefix, "Software\\%s\\%s", szCompanyName, szAppName );
	len = strlen( prefix );
	if ( strnicmp( keyname, prefix, len ) || ((keyname[len] != 0) && (keyname[len] != '\\')) )	{
		return ERROR_FILE_NOT_FOUND;
	}
	sprintf( alt_keyname, "Software\\%s\\%s%s", szCompanyName, Osreg_launcher_app_name, keyname + len );

	return os_reg_try_open( HKEY_LOCAL_MACHINE, alt_keyname, hKey, name );
}

// ------------------------------------------------------------------------------------------------------------
// Claude: SETTINGS FILE
//
// Settings live in an ini file (fs2.ini in the game folder, or the file given with -ini) instead of
// the registry.  The first run without the file creates it from the registry values; after that the
// registry is never read or written.  Settings with no section go in [Settings].  The launcher's
// Videocard string is kept as Renderer, Device, ColourDepth and Resolution, and put back together
// when the game reads Videocard.

#define OS_INI_DEFAULT_FILE	"fs2.ini"
#define OS_INI_MAIN_SECTION	"Settings"
#define OS_INI_MISSING			"<missing>"

static char Os_ini_file[MAX_PATH] = "";
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

// split "Direct 3D - <device> (32 bit) (1024x768)" into ini keys; 0 if it is not in that form
static int os_ini_split_videocard( char *videocard )
{
	char s[1024], *p, res[64];
	int len, w, h;

	strncpy( s, videocard, sizeof(s) - 1 );
	s[sizeof(s) - 1] = 0;

	res[0] = 0;
	len = strlen( s );
	p = strrchr( s, '(' );
	if ( p && (len > 0) && (s[len - 1] == ')') && (sscanf( p, "(%dx%d)", &w, &h ) == 2) ) {
		sprintf( res, "%dx%d", w, h );
		while ( (p > s) && (p[-1] == ' ') ) {
			p--;
		}
		*p = 0;
	}

	len = strlen( s );
	char *depth = NULL;
	if ( (len >= 9) && !strcmp( s + len - 9, " (32 bit)" ) ) {
		depth = "32";
		s[len - 9] = 0;
	} else if ( (len >= 9) && !strcmp( s + len - 9, " (16 bit)" ) ) {
		depth = "16";
		s[len - 9] = 0;
	}

	if ( !strncmp( s, "Direct 3D - ", 12 ) ) {
		WritePrivateProfileString( OS_INI_MAIN_SECTION, "Renderer", "Direct3D", Os_ini_file );
		WritePrivateProfileString( OS_INI_MAIN_SECTION, "Device", s + 12, Os_ini_file );
	} else if ( !strcmp( s, "3DFX Glide" ) ) {
		WritePrivateProfileString( OS_INI_MAIN_SECTION, "Renderer", "Glide", Os_ini_file );
	} else {
		return 0;
	}
	if ( depth ) {
		WritePrivateProfileString( OS_INI_MAIN_SECTION, "ColourDepth", depth, Os_ini_file );
	}
	if ( res[0] ) {
		WritePrivateProfileString( OS_INI_MAIN_SECTION, "Resolution", res, Os_ini_file );
	}
	return 1;
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

// copy one registry key's values into an ini section
static void os_ini_copy_key( HKEY root, char *keyname, char *section )
{
	HKEY hKey;
	char name[256], data[1024], text[1024];
	DWORD i, name_len, data_len, type;

	if ( RegOpenKeyEx( root, keyname, NULL, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		return;
	}
	for ( i = 0; ; i++ ) {
		name_len = sizeof(name);
		data_len = sizeof(data) - 1;
		if ( RegEnumValue( hKey, i, name, &name_len, NULL, &type, (ubyte *)data, &data_len ) != ERROR_SUCCESS ) {
			break;
		}
		if ( type == REG_DWORD ) {
			sprintf( text, "%u", *(uint *)data );
		} else if ( type == REG_SZ ) {
			data[data_len] = 0;
			strcpy( text, data );
		} else {
			continue;
		}
		if ( !stricmp( name, "Videocard" ) && !section && os_ini_split_videocard( text ) ) {
			WritePrivateProfileString( OS_INI_MAIN_SECTION, "Videocard", NULL, Os_ini_file );
			continue;
		}
		WritePrivateProfileString( os_ini_section(section), name, text, Os_ini_file );
	}
	RegCloseKey( hKey );
}

// copy a registry key and its subkeys (one level, as the game uses them) into the ini
static void os_ini_copy_tree( HKEY root, char *keyname )
{
	HKEY hKey;
	char sub[256], subkey[1024];
	DWORD i, len;

	os_ini_copy_key( root, keyname, NULL );
	if ( RegOpenKeyEx( root, keyname, NULL, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		return;
	}
	for ( i = 0; ; i++ ) {
		len = sizeof(sub);
		if ( RegEnumKeyEx( hKey, i, sub, &len, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS ) {
			break;
		}
		sprintf( subkey, "%s\\%s", keyname, sub );
		os_ini_copy_key( root, subkey, sub );
	}
	RegCloseKey( hKey );
}

// first run: create the ini from the registry, in the order a registry read would pick values
// (each later copy overwrites the earlier one)
static void os_ini_create()
{
	char keyname[1024];
	FILE *fp;

	fp = fopen( Os_ini_file, "wt" );
	if ( fp == NULL ) {
		return;
	}
	fprintf( fp, "; FreeSpace 2 settings.  Made from the registry on the first run; the game now reads only this file.\n" );
	fprintf( fp, "; Renderer, Device, ColourDepth and Resolution replace the launcher's Videocard setting.\n" );
	fprintf( fp, "; [Detail] sets the detail levels for every pilot (0 lowest, 4 highest; the last three 0 or 1).\n\n" );
	fclose( fp );

	sprintf( keyname, "Software\\%s\\%s", szCompanyName, Osreg_launcher_app_name );
	os_ini_copy_tree( HKEY_LOCAL_MACHINE, keyname );
	sprintf( keyname, "Software\\%s\\%s", szCompanyName, szAppName );
	os_ini_copy_tree( HKEY_LOCAL_MACHINE, keyname );
	os_ini_copy_tree( HKEY_CURRENT_USER, keyname );
}

// -ini <file>: called from parse_cmdline, before the game starts
void os_config_set_ini( char *file )
{
	GetFullPathName( file, MAX_PATH, Os_ini_file, NULL );
}

char *os_config_ini_file()
{
	return Os_ini_file;
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
	if ( Os_ini_active ) {	// Claude
		if ( section || name ) {
			WritePrivateProfileString( os_ini_section(section), name, NULL, Os_ini_file );
		}
		return;
	}
	HKEY hKey = NULL;
	DWORD dwDisposition;
	char keyname[1024];
	LONG lResult;	

	if(!Os_reg_inited){
		return;
	}

	if ( section )	{
		sprintf( keyname, "Software\\%s\\%s\\%s", szCompanyName, szAppName, section );
	} else {
		sprintf( keyname, "Software\\%s\\%s", szCompanyName, szAppName );
	}

	// remove the value
	if ( !name )	{
		if ( !section )	{			
			goto Cleanup;
		}
		lResult = RegDeleteKey( HKEY_CURRENT_USER, keyname );
		if ( lResult != ERROR_SUCCESS )	{			
			goto Cleanup;
		}
	} else	{
		lResult = RegCreateKeyEx( HKEY_CURRENT_USER,						// Where to add it
												 keyname,								// name of key
												 NULL,									// DWORD reserved
												 "",										// Object class
												 REG_OPTION_NON_VOLATILE,			// Save to disk
												 KEY_ALL_ACCESS,						// Allows all changes
												 NULL,									// Default security attributes
												 &hKey,							// Location to store key
												 &dwDisposition );					// Location to store status of key

		if ( lResult != ERROR_SUCCESS )	{			
			goto Cleanup;
		}

		lResult = RegDeleteValue( hKey, name );
		if ( lResult != ERROR_SUCCESS )	{			
			goto Cleanup;
		}
	}

Cleanup:
	if ( hKey )
		RegCloseKey(hKey);
}

// Writes a string to the INI file.  If value is NULL,
// removes the string. Writing a NULL value to a NULL name will delete
// the section.
void os_config_write_string( char *section, char *name, char *value )
{
	if ( Os_ini_active ) {	// Claude
		if ( name ) {
			WritePrivateProfileString( os_ini_section(section), name, value, Os_ini_file );
		}
		return;
	}
	HKEY hKey = NULL;
	DWORD dwDisposition;
	char keyname[1024];
	LONG lResult;	

	if(!Os_reg_inited){
		return;
	}

	if ( section )	{
		sprintf( keyname, "Software\\%s\\%s\\%s", szCompanyName, szAppName, section );
	} else {
		sprintf( keyname, "Software\\%s\\%s", szCompanyName, szAppName );
	}

	lResult = RegCreateKeyEx( HKEY_CURRENT_USER,					// Where to add it
											 keyname,							// name of key
											 NULL,								// DWORD reserved
											 "",									// Object class
											 REG_OPTION_NON_VOLATILE,		// Save to disk
											 KEY_ALL_ACCESS,					// Allows all changes
											 NULL,								// Default security attributes
											 &hKey,								// Location to store key
											 &dwDisposition );				// Location to store status of key

	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	if ( !name )	 {		
		goto Cleanup;
	}
		
	lResult = RegSetValueEx( hKey,									// Handle to key
									 name,									// The values name
									 NULL,									// DWORD reserved
									 REG_SZ,									// null terminated string
									 (CONST BYTE *)value,				// value to set
									 strlen(value) + 1 );				// How many bytes to set
																			
	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}


Cleanup:
	if ( hKey )
		RegCloseKey(hKey);
}

// same as previous function except we don't use the application name to build up the keyname
void os_config_write_string2( char *section, char *name, char *value )
{
	HKEY hKey = NULL;
	DWORD dwDisposition;
	char keyname[1024];
	LONG lResult;	

	if(!Os_reg_inited){
		return;
	}

	if ( section )	{
		sprintf( keyname, "Software\\%s\\%s", szCompanyName, section );
	} else {
		sprintf( keyname, "Software\\%s", szCompanyName );
	}

	lResult = RegCreateKeyEx( HKEY_CURRENT_USER,					// Where to add it
											 keyname,							// name of key
											 NULL,								// DWORD reserved
											 "",									// Object class
											 REG_OPTION_NON_VOLATILE,		// Save to disk
											 KEY_ALL_ACCESS,					// Allows all changes
											 NULL,								// Default security attributes
											 &hKey,								// Location to store key
											 &dwDisposition );				// Location to store status of key

	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	if ( !name )	 {		
		goto Cleanup;
	}
		
	lResult = RegSetValueEx( hKey,									// Handle to key
									 name,									// The values name
									 NULL,									// DWORD reserved
									 REG_SZ,									// null terminated string
									 (CONST BYTE *)value,				// value to set
									 strlen(value) + 1 );				// How many bytes to set
																			
	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}


Cleanup:
	if ( hKey )
		RegCloseKey(hKey);
}

// Writes an unsigned int to the INI file.  
void os_config_write_uint( char *section, char *name, uint value )
{
	if ( Os_ini_active ) {	// Claude
		char num[16];
		if ( name ) {
			sprintf( num, "%u", value );
			WritePrivateProfileString( os_ini_section(section), name, num, Os_ini_file );
		}
		return;
	}
	HKEY hKey = NULL;
	DWORD dwDisposition;
	char keyname[1024];
	LONG lResult;	

	if(!Os_reg_inited){
		return;
	}

	if ( section )	{
		sprintf( keyname, "Software\\%s\\%s\\%s", szCompanyName, szAppName, section );
	} else {
		sprintf( keyname, "Software\\%s\\%s", szCompanyName, szAppName );
	}

	lResult = RegCreateKeyEx( HKEY_CURRENT_USER,						// Where to add it
											 keyname,								// name of key
											 NULL,									// DWORD reserved
											 "",										// Object class
											 REG_OPTION_NON_VOLATILE,			// Save to disk
											 KEY_ALL_ACCESS,						// Allows all changes
											 NULL,									// Default security attributes
											 &hKey,							// Location to store key
											 &dwDisposition );					// Location to store status of key

	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	if ( !name )	 {		
		goto Cleanup;
	}
		
	lResult = RegSetValueEx( hKey,									// Handle to key
									 name,											// The values name
									 NULL,											// DWORD reserved
									 REG_DWORD,										// null terminated string
									 (CONST BYTE *)&value,						// value to set
									 4 );								// How many bytes to set
																				
	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

Cleanup:
	if ( hKey )
		RegCloseKey(hKey);

}


// Reads a string from the INI file.  If default is passed,
// and the string isn't found, returns ptr to default otherwise
// returns NULL;    Copy the return value somewhere before
// calling os_read_string again, because it might reuse the
// same buffer.
static char tmp_string_data[1024];
char * os_config_read_string( char *section, char *name, char *default_value )
{
	if ( Os_ini_active ) {	// Claude
		return os_ini_read_string( section, name, default_value, tmp_string_data );
	}
	HKEY hKey = NULL;
	DWORD dwType, dwLen;
	char keyname[1024];
	LONG lResult;	

	if(!Os_reg_inited){
		return NULL;
	}

	if ( section )	{
		sprintf( keyname, "Software\\%s\\%s\\%s", szCompanyName, szAppName, section );
	} else {
		sprintf( keyname, "Software\\%s\\%s", szCompanyName, szAppName );
	}

	lResult = os_reg_open_read( keyname, &hKey, name );

	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	if ( !name )	 {		
		goto Cleanup;
	}

	dwLen = 1024;
	lResult = RegQueryValueEx( hKey,									// Handle to key
									 name,											// The values name
									 NULL,											// DWORD reserved
	                         &dwType,										// What kind it is
									 (ubyte *)&tmp_string_data,						// value to set
									 &dwLen );								// How many bytes to set
																				
	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	default_value = tmp_string_data;

Cleanup:
	if ( hKey )
		RegCloseKey(hKey);

	return default_value;
}

// same as previous function except we don't use the application name to build up the keyname
char * os_config_read_string2( char *section, char *name, char *default_value )
{
	HKEY hKey = NULL;
	DWORD dwType, dwLen;
	char keyname[1024];
	LONG lResult;	

	if(!Os_reg_inited){
		return NULL;
	}

	if ( section )	{
		sprintf( keyname, "Software\\%s\\%s", szCompanyName, section );
	} else {
		sprintf( keyname, "Software\\%s", szCompanyName );
	}

	lResult = os_reg_open_read( keyname, &hKey, name );

	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	if ( !name )	 {		
		goto Cleanup;
	}

	dwLen = 1024;
	lResult = RegQueryValueEx( hKey,									// Handle to key
									 name,											// The values name
									 NULL,											// DWORD reserved
	                         &dwType,										// What kind it is
									 (ubyte *)&tmp_string_data,						// value to set
									 &dwLen );								// How many bytes to set
																				
	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	default_value = tmp_string_data;

Cleanup:
	if ( hKey )
		RegCloseKey(hKey);

	return default_value;
}

// Reads a string from the INI file.  Default_value must 
// be passed, and if 'name' isn't found, then returns default_value
uint  os_config_read_uint( char *section, char *name, uint default_value )
{
	if ( Os_ini_active ) {	// Claude
		char num[64];
		if ( name && os_ini_get( section, name, num, sizeof(num) ) ) {
			return (uint)strtoul( num, NULL, 10 );
		}
		return default_value;
	}
	HKEY hKey = NULL;
	DWORD dwType, dwLen;
	char keyname[1024];
	LONG lResult;
	uint tmp_val;	

	if(!Os_reg_inited){
		return NULL;
	}

	if ( section )	{
		sprintf( keyname, "Software\\%s\\%s\\%s", szCompanyName, szAppName, section );
	} else {
		sprintf( keyname, "Software\\%s\\%s", szCompanyName, szAppName );
	}

	lResult = os_reg_open_read( keyname, &hKey, name );

	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	if ( !name )	 {		
		goto Cleanup;
	}

	dwLen = 4;
	lResult = RegQueryValueEx( hKey,									// Handle to key
									 name,											// The values name
									 NULL,											// DWORD reserved
	                         &dwType,										// What kind it is
									 (ubyte *)&tmp_val,						// value to set
									 &dwLen );								// How many bytes to set
																				
	if ( lResult != ERROR_SUCCESS )	{		
		goto Cleanup;
	}

	default_value = tmp_val;

Cleanup:
	if ( hKey )
		RegCloseKey(hKey);

	return default_value;
}

// uses Ex versions of Windows registry functions
static char tmp_string_data_ex[1024];
char * os_config_read_string_ex( char *keyname, char *name, char *default_value )
{
	HKEY hKey = NULL;
	DWORD dwType, dwLen;
	LONG lResult;

	lResult = RegOpenKeyEx( HKEY_LOCAL_MACHINE,							// Where it is
											 keyname,								// name of key
											 NULL,									// DWORD reserved
											 KEY_QUERY_VALUE,						// Allows all changes
											 &hKey );								// Location to store key

	if ( lResult != ERROR_SUCCESS )	{
		//mprintf(( "Error opening registry key '%s'\n", keyname ));
		goto Cleanup;
	}

	if ( !name )	 {
		//mprintf(( "No variable name passed\n" ));
		goto Cleanup;
	}

	dwLen = 1024;
	lResult = RegQueryValueEx( hKey,									// Handle to key
									 name,											// The values name
									 NULL,											// DWORD reserved
	                         &dwType,										// What kind it is
									 (ubyte *)&tmp_string_data_ex,						// value to set
									 &dwLen );								// How many bytes to set
																				
	if ( lResult != ERROR_SUCCESS )	{
		//mprintf(( "Error reading registry key '%s'\n", name ));
		goto Cleanup;
	}

	default_value = tmp_string_data_ex;

Cleanup:
	if ( hKey )
		RegCloseKey(hKey);

	return default_value;
}
