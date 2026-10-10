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
}

// Removes a value from to the INI file.  Passing
// name=NULL will delete the section.
void os_config_remove( char *section, char *name )
{
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
