/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/Cmdline/cmdline.cpp $
 * $Revision: 8 $
 * $Date: 8/26/99 8:51p $
 * $Author: Dave $
 *
 * $Log: /Freespace2/code/Cmdline/cmdline.cpp $
 * 
 * 8     8/26/99 8:51p Dave
 * Gave multiplayer TvT messaging a heavy dose of sanity. Cheat codes.
 * 
 * 7     7/15/99 3:07p Dave
 * 32 bit detection support. Mouse coord commandline.
 * 
 * 6     7/13/99 1:15p Dave
 * 32 bit support. Whee!
 * 
 * 5     6/22/99 9:37p Dave
 * Put in pof spewing.
 * 
 * 4     1/12/99 5:45p Dave
 * Moved weapon pipeline in multiplayer to almost exclusively client side.
 * Very good results. Bandwidth goes down, playability goes up for crappy
 * connections. Fixed object update problem for ship subsystems.
 * 
 * 3     11/17/98 11:12a Dave
 * Removed player identification by address. Now assign explicit id #'s.
 * 
 * 2     10/07/98 10:52a Dave
 * Initial checkin.
 * 
 * 1     10/07/98 10:48a Dave
 * 
 * 38    10/02/98 3:22p Allender
 * fix up the -connect option and fix the -port option
 * 
 * 37    9/15/98 4:04p Allender
 * added back in the -ip_addr command line switch because it needs to be
 * in the standalone server only executable
 * 
 * 36    9/14/98 11:52a Allender
 * don't use cfile
 * 
 * 35    9/14/98 11:28a Allender
 * support for server bashing of address when received from client.  Added
 * a cmdline.cfg file to process command line arguments from a file
 * 
 * 34    9/08/98 2:20p Allender
 * temporary code to force IP address to a specific value.
 * 
 * 33    8/20/98 5:30p Dave
 * Put in handy multiplayer logfile system. Now need to put in useful
 * applications of it all over the code.
 * 
 * 32    8/07/98 10:39a Allender
 * fixed debug standalone problem where stats would continually get sent
 * to tracker.  more debug code to help find stats problem
 * 
 * 31    7/24/98 11:14a Allender
 * start of new command line options for version 1.04
 * 
 * 30    5/21/98 1:50a Dave
 * Remove obsolete command line functions. Reduce shield explosion packets
 * drastically. Tweak PXO screen even more. Fix file xfer system so that
 * we can guarantee file uniqueness.
 * 
 * 29    5/18/98 9:10p Dave
 * Put in many new PXO features. Fixed skill level bashing in multiplayer.
 * Removed several old command line options. Put in network config files.
 * 
 * 28    5/09/98 7:16p Dave
 * Put in CD checking. Put in standalone host password. Made pilot into
 * popup scrollable.
 * 
 * 27    4/23/98 8:27p Allender
 * basic support for cutscene playback.  Into movie code in place.  Tech
 * room can view cutscenes stored in CDROM_dir variable
 * 
 * 26    4/09/98 5:43p Dave
 * Remove all command line processing from the demo. Began work fixing up
 * the new multi host options screen.
 * 
 * 25    4/02/98 11:40a Lawrance
 * check for #ifdef DEMO instead of #ifdef DEMO_RELEASE
 * 
 * 24    4/01/98 5:56p Dave
 * Fixed a messaging bug which caused msg_all mode in multiplayer not to
 * work. Compile out a host of multiplayer options not available in the
 * demo.
 * 
 * 23    3/14/98 2:48p Dave
 * Cleaned up observer joining code. Put in support for file xfers to
 * ingame joiners (observers or not). Revamped and reinstalled pseudo
 * lag/loss system.
 * 
 * 22    2/22/98 12:19p John
 * Externalized some strings
 * 
 * 21    1/31/98 4:32p Dave
 * Put in new support for VMT player validation, game logging in and game
 * logging out.
 * 
 * 20    12/10/97 4:45p Dave
 * Added in more detailed support for multiplayer packet lag/loss. Fixed
 * some multiplayer stuff. Added some controls to the standalone.
 * 
 * 19    12/09/97 6:14p Lawrance
 * add -nomusic flag
 * 
 * 18    12/01/97 5:10p Dave
 * Fixed a syntax bug.
 * 
 * 17    12/01/97 4:59p Dave
 * Synchronized multiplayer debris objects. Put in pilot popup in main
 * hall. Optimized simulated multiplayer lag module. Fixed a potential
 * file_xfer bug.
 * 
 * 16    11/28/97 7:04p Dave
 * Emergency checkin due to big system crash.
 * 
 * 15    11/28/97 5:06p Dave
 * Put in facilities for simulating multiplayer lag.
 * 
 * 14    11/24/97 5:42p Dave
 * Fixed a file xfer buffer free/malloc problem. Lengthened command line
 * switch string parse length.
 * 
 * 13    11/12/97 4:39p Dave
 * Put in multiplayer campaign support parsing, loading and saving. Made
 * command-line variables better named. Changed some things on the initial
 * pilot select screen.
 * 
 * 12    11/11/97 4:54p Dave
 * Put in support for single vs. multiplayer pilots. Put in initial player
 * selection screen (no command line option yet). Started work on
 * multiplayer campaign file save gaming.
 * 
 * 11    11/11/97 11:55a Allender
 * initialize network at beginning of application.  create new call to set
 * which network protocol to use
 * 
 * 10    9/18/97 10:12p Dave
 * Added -gimmemedals, which gives the current pilot all the medals in the
 * game (debug)
 * 
 * 9     9/18/97 9:20a Dave
 * Minor modifications
 * 
 * 8     9/15/97 11:40p Lawrance
 * remove demo granularity switch
 * 
 * 7     9/09/97 3:39p Sandeep
 * warning level 4 bugs
 * 
 * 6     9/03/97 5:03p Lawrance
 * add support for -nosound command line parm
 * 
 * 5     8/22/97 8:52a Dave
 * Removed a return statement which would have broken the parser out too
 * early.
 * 
 * 4     8/21/97 4:55p Dave
 * Added a switch for multiplayer chat streaming. Added a section for
 * global command line vars.
 * 
 * 3     8/06/97 2:26p Dave
 * Made the command line parse more robust. Made it easier to add and
 * process new command-line switches.
 * 
 * 2     8/04/97 3:13p Dave
 * Added command line functions. See cmdline.cpp for directions on adding
 * new switches
 * 
 * 1     8/04/97 9:58a Dave
 * 
 * $NoKeywords: $
 */

#include <string.h>
#include <stdlib.h>
#include "cmdline.h"
#include "LinkList.h"
#include "systemvars.h"
#include "multi.h"
#include "cfile.h"
#include "Replay.h"
#include "Autotest.h"
#include "BitmapLog.h"	// Claude
#include "InputTest.h"	// Claude
#include "OsRegistry.h"	// Claude
#include "OsApi.h"	// Claude: Os_no_window

// variables
class cmdline_parm {
public:
	cmdline_parm *next, *prev;
	char *name;						// name of parameter, must start with '-' char
	char *help;						// help text for this parameter
	char *args;						// string value for parameter arguements (NULL if no arguements)
	int name_found;				// true if parameter on command line, otherwise false

	cmdline_parm(char *name, char *help);
	~cmdline_parm();
	int found();
	int get_int();
	float get_float();
	char *str();
};

// here are the command line parameters that we will be using for FreeSpace
cmdline_parm standalone_arg("-standalone", NULL);
cmdline_parm nosound_arg("-nosound", NULL);
cmdline_parm nomusic_arg("-nomusic", NULL);
cmdline_parm startgame_arg("-startgame", NULL);
cmdline_parm gamename_arg("-gamename", NULL);
cmdline_parm gamepassword_arg("-password", NULL);
cmdline_parm gameclosed_arg("-closed", NULL);
cmdline_parm gamerestricted_arg("-restricted", NULL);
cmdline_parm allowabove_arg("-allowabove", NULL);
cmdline_parm allowbelow_arg("-allowbelow", NULL);
cmdline_parm port_arg("-port", NULL);
cmdline_parm connect_arg("-connect", NULL);
cmdline_parm multilog_arg("-multilog", NULL);
cmdline_parm server_firing_arg("-oldfire", NULL);
cmdline_parm client_dodamage("-clientdamage", NULL);
cmdline_parm pof_spew("-pofspew", NULL);
cmdline_parm force_16bit("-force_16bit", NULL);	// Claude: was -32bit, now that 32 bit is the default
cmdline_parm headless_arg("-headless", NULL);	// Claude: no window, graphics or sound device
cmdline_parm nowindow_arg("-nowindow", NULL);	// Claude: test stand-in for a host-set Os_no_window; honoured only with -nographics
cmdline_parm nographics_arg("-nographics", NULL);	// Claude: no graphics device: the null renderer, and no drawing
cmdline_parm nosounddevice_arg("-nosounddevice", NULL);	// Claude: the sound system with no device (Sound.cpp)
cmdline_parm nodraw_arg("-nodraw", NULL);	// Claude: test: skip drawing the mission, to check that gameplay does not depend on drawing
cmdline_parm mouse_coords("-coords", NULL);
cmdline_parm timeout("-timeout", NULL);
cmdline_parm d3d_window("-window", NULL);
cmdline_parm all_cutscenes_arg("-allcutscenes", NULL);		// Claude: list every cutscene in the tech room
cmdline_parm record_arg("-record", NULL);
cmdline_parm replay_arg("-replay", NULL);
cmdline_parm trace_arg("-trace", NULL);
cmdline_parm fulltrace_arg("-fulltrace", NULL);
cmdline_parm replay_pace_arg("-replay_pace", NULL);		// Claude: replay at the recorded speed, see Replay.h
cmdline_parm autorecord_arg("-autorecord", NULL);		// Claude: record every mission played, see Replay.h
cmdline_parm mission_arg("-mission", NULL);		// Claude: start a mission from the main hall, see MainHallMenu.cpp
cmdline_parm autotest_arg("-autotest", NULL);			// Claude: autotest, see Autotest.h
cmdline_parm autotest_mission_arg("-at_mission", NULL);
cmdline_parm autotest_time_arg("-at_time", NULL);
cmdline_parm autotest_speed_arg("-at_speed", NULL);
cmdline_parm autotest_pilot_arg("-at_pilot", NULL);
cmdline_parm no_ship_glow_arg("-noshipglow", NULL);	// Claude: turn off the D3D ship glow
cmdline_parm no_force_glow_arg("-noforceglow", NULL);	// Claude: turn off forced red ship glow
cmdline_parm autotest_skill_arg("-at_skill", NULL);
cmdline_parm autotest_menus_arg("-at_menus", NULL);	// Claude: a tour of every screen, see Autotest.h
cmdline_parm autotest_shots_arg("-at_shots", NULL);	// Claude: a screenshot of each screen of the tour, see Autotest.h
cmdline_parm autotest_input_arg("-at_input", NULL);	// Claude: scripted joystick, see InputTest.h
cmdline_parm ini_arg("-ini", NULL);				// Claude: settings file instead of fs2.ini, see OsRegistry.h
cmdline_parm dataset_arg("-dataset", NULL);		// Claude: fs2demo, fs1demo or retail, see DataSetInstall.h
char *Cmdline_dataset = NULL;						// Claude
cmdline_parm playernpc_arg("-playernpc", NULL);	// Claude: the AI flies the player ship, see Autotest.h
cmdline_parm bmlog_arg("-bmlog", NULL);			// Claude: bitmap section evidence log, see BitmapLog.h
cmdline_parm bmshots_arg("-bmshots", NULL);		// Claude
cmdline_parm bmframes_arg("-bmframes", NULL);		// Claude
cmdline_parm loadtimes_arg("-loadtimes", NULL);	// Claude: mission load times log, see LoadTimes.h
int Cmdline_loadtimes = 0;						// Claude: -loadtimes

int Cmdline_multi_stream_chat_to_file = 0;
int Cmdline_freespace_no_sound = 0;
int Cmdline_freespace_no_music = 0;
int Cmdline_gimme_all_medals = 0;
int Cmdline_use_last_pilot = 0;
int Cmdline_multi_protocol = -1;
int Cmdline_cd_check = 1;
int Cmdline_start_netgame = 0;
int Cmdline_closed_game = 0;
int Cmdline_restricted_game = 0;
int Cmdline_network_port = -1;
char *Cmdline_game_name = NULL;
char *Cmdline_game_password = NULL;
char *Cmdline_rank_above= NULL;
char *Cmdline_rank_below = NULL;
char *Cmdline_connect_addr = NULL;
int Cmdline_multi_log = 0;
int Cmdline_server_firing = 0;
int Cmdline_client_dodamage = 0;
int Cmdline_spew_pof_info = 0;
int Cmdline_force_32bit = 0;
int Cmdline_force_16bit = 0;	// Claude: 16 bit screen and textures instead of the 32 bit default
int Cmdline_headless = 0;	// Claude: -nographics, -nosounddevice and a message-only window that is never shown
int Cmdline_nographics = 0;	// Claude: no graphics device: the null renderer (GrDummy.cpp), and -nodraw
int Cmdline_nosounddevice = 0;	// Claude: the sound system with no device: sounds load their length, nothing plays
int Cmdline_nodraw = 0;	// Claude: test: skip drawing the mission, to check that gameplay does not depend on drawing
int Cmdline_mouse_coords = 0;
int Cmdline_timeout = -1;

int Cmdline_window = 0;
int Cmdline_no_ship_glow = 0;	// Claude: -noshipglow
int Cmdline_no_force_glow = 0;	// Claude: -noforceglow
int Cmdline_all_cutscenes = 0;		// Claude
char *Cmdline_mission = NULL;	// Claude: -mission, or NULL

static cmdline_parm Parm_list(NULL, NULL);
static int Parm_list_inited = 0;


//	Return true if this character is an extra char (white space and quotes)
int is_extra_space(char ch)
{
	return ((ch == ' ') || (ch == '\t') || (ch == 0x0a) || (ch == '\'') || (ch == '\"'));
}


// eliminates all leading and trailing extra chars from a string.  Returns pointer passed in.
char *drop_extra_chars(char *str)
{
	int s, e;

	s = 0;
	while (str[s] && is_extra_space(str[s]))
		s++;

	// Claude: x64, cast to int, its width on Win32 (D-20261020-194406 batch 2)
	e = (int)strlen(str) - 1;
	while (e > s) {
		if (!is_extra_space(str[e])){
			break;
		}

		e--;
	}

	if (e >= s){	// Claude: was e > s, which left a one-character value unmoved (" 3" became " ")
		memmove(str, str + s, e - s + 1);
	}

	str[e - s + 1] = 0;
	return str;
}


// internal function - copy the value for a parameter agruement into the cmdline_parm arg field
void parm_stuff_args(cmdline_parm *parm, char *cmdline)
{
	char buffer[1024];
	memset(buffer, 0, 1024);
	char *dest = buffer;
	int in_quote = 0;

	cmdline += strlen(parm->name);

	// Claude: Stop at the next parameter, not at any '-', so values such as "sm1-03.fs2" are kept whole.
	// A parameter is a '-' that starts a token outside quotes.  Also stop before the buffer overflows.
	while ((*cmdline != 0) && (in_quote || (*cmdline != '-') || !is_extra_space(cmdline[-1])) && (dest < buffer + sizeof(buffer) - 1)) {
		if (*cmdline == '"') {
			in_quote = !in_quote;
		}
		*dest++ = *cmdline++;
	}

	drop_extra_chars(buffer);

	// mwa 9/14/98 -- made it so that newer command line arguments found will overwrite
	// the old arguments
//	Assert(parm->args == NULL);
	if ( parm->args != NULL ) {
		delete [] parm->args;	// Claude: allocated with new[]; was scalar delete (#7)
		parm->args = NULL;
	}

	// Claude: x64, cast to int, its width on Win32 (D-20261020-194406 batch 2)
	int size = (int)strlen(buffer) + 1;
	if (size > 0) {
		parm->args = new char[size];
		memset(parm->args, 0, size);
		strcpy(parm->args, buffer);
	}
}


// Claude: Find a parameter only as a whole token outside quotes.  strstr also matched it inside a longer
// parameter ("-cat" in "-catfish") or inside a value or path ("-replay" in "c:\my-replay.fs2rec").
static char *parm_find(char *cmdline, char *name)
{
	// Claude: x64, cast to int, its width on Win32 (D-20261020-194406 batch 2)
	int len = (int)strlen(name);
	int in_quote = 0;
	char *p;

	for (p = cmdline; *p; p++) {
		if (*p == '"') {
			in_quote = !in_quote;
		} else if (!in_quote && ((p == cmdline) || is_extra_space(p[-1]) || (p[-1] == ',')) && !strncmp(p, name, len) && ((p[len] == 0) || is_extra_space(p[len]) || (p[len] == ','))) {	// Claude: ',' separates parameters too, as in os_validate_parms
			return p;
		}
	}
	return NULL;
}

// internal function - parse the command line, extracting parameter arguements if they exist
// cmdline - command line string passed to the application
void os_parse_parms(char *cmdline)
{
	// locate command line parameters
	cmdline_parm *parmp;
	char *cmdline_offset;

	for (parmp = GET_FIRST(&Parm_list); parmp !=END_OF_LIST(&Parm_list); parmp = GET_NEXT(parmp) ) {
		cmdline_offset = parm_find(cmdline, parmp->name);	// Claude: was strstr
		if (cmdline_offset) {
			parmp->name_found = 1;
			parm_stuff_args(parmp, cmdline_offset);
		}
	}
}


// validate the command line parameters.  Display an error if an unrecognized parameter is located.
void os_validate_parms(char *cmdline)
{
	cmdline_parm *parmp;
	char seps[] = " ,\t\n";
	char *token;
	int parm_found;

   token = strtok(cmdline, seps);
   while(token != NULL) {
	
		if (token[0] == '-') {
			parm_found = 0;
			for (parmp = GET_FIRST(&Parm_list); parmp !=END_OF_LIST(&Parm_list); parmp = GET_NEXT(parmp) ) {
				if (!stricmp(parmp->name, token)) {
					parm_found = 1;
					break;
				}
			}

			if (parm_found == 0) {
				Error(LOCATION,"Unrecogzined command line parameter %s", token);
			}
		}

		token = strtok(NULL, seps);
	}
}


// Call once to initialize the command line system
//
// cmdline - command line string passed to the application
void os_init_cmdline(char *cmdline)
{
	FILE *fp;

	// Claude: a restarted game (game_host_init again in this process, D-055) parses a new command line: forget what the last one had
	for (cmdline_parm *parmp = GET_FIRST(&Parm_list); parmp != END_OF_LIST(&Parm_list); parmp = GET_NEXT(parmp)) {
		parmp->name_found = 0;
	}

	// read the cmdline.cfg file from the data folder, and pass the command line arguments to
	// the the parse_parms and validate_parms line.  Read these first so anything actually on
	// the command line will take precedence
	fp = fopen("data\\cmdline.cfg", "rt");

	// if the file exists, get a single line, and deal with it
	if ( fp ) {
		char buf[1024], *p;

		fgets(buf, 1024, fp);

		// replace the newline character with a NUL:
		if ( (p = strrchr(buf, '\n')) != NULL ) {
			*p = '\0';
		}

		os_parse_parms(buf);
		os_validate_parms(buf);
		fclose(fp);
	}



	os_parse_parms(cmdline);
	os_validate_parms(cmdline);

}


// arg constructor
// name_ - name of the parameter, must start with '-' character
// help_ - help text for this parameter
cmdline_parm::cmdline_parm(char *name_, char *help_)
{
	name = name_;
	help = help_;
	args = NULL;
	name_found = 0;

	if (Parm_list_inited == 0) {
		list_init(&Parm_list);
		Parm_list_inited = 1;
	}

	if (name != NULL) {
		list_append(&Parm_list, this);
	}
}


// destructor - frees any allocated memory
cmdline_parm::~cmdline_parm()
{
	if (args) {
		delete [] args;
		args = NULL;
	}
}


// returns - true if the parameter exists on the command line, otherwise false
int cmdline_parm::found()
{
	return name_found;
}


// returns - the interger representation for the parameter arguement
int cmdline_parm::get_int()
{
	Assert(args);
	return atoi(args);
}


// returns - the float representation for the parameter arguement
float cmdline_parm::get_float()
{
	Assert(args);
	return (float)atof(args);
}


// returns - the string value for the parameter arguement
char *cmdline_parm::str()
{
	Assert(args);
	return args;
}

// external entry point into this modules
int parse_cmdline(char *cmdline)
{
	os_init_cmdline(cmdline);

	// is this a standalone server??
	if (standalone_arg.found()) {
		Is_standalone = 1;
	}

	// run with no sound
	if ( nosound_arg.found() ) {
		Cmdline_freespace_no_sound = 1;
	}

	// run with no music
	if ( nomusic_arg.found() ) {
		Cmdline_freespace_no_music = 1;
	}

	// should we start a network game
	if ( startgame_arg.found() ) {
		Cmdline_use_last_pilot = 1;
		Cmdline_start_netgame = 1;
	}

	// closed network game
	if ( gameclosed_arg.found() ) {
		Cmdline_closed_game = 1;
	}

	// restircted network game
	if ( gamerestricted_arg.found() ) {
		Cmdline_restricted_game = 1;
	}

	// get the name of the network game
	if ( gamename_arg.found() ) {
		Cmdline_game_name = gamename_arg.str();

		// be sure that this string fits in our limits
		if ( strlen(Cmdline_game_name) > MAX_GAMENAME_LEN ) {
			Cmdline_game_name[MAX_GAMENAME_LEN-1] = '\0';
		}
	}

	// get the password for a pssword game
	if ( gamepassword_arg.found() ) {
		Cmdline_game_password = gamepassword_arg.str();

		// be sure that this string fits in our limits
		if ( strlen(Cmdline_game_name) > MAX_PASSWD_LEN ) {
			Cmdline_game_name[MAX_PASSWD_LEN-1] = '\0';
		}
	}

	// set the rank above/below arguments
	if ( allowabove_arg.found() ) {
		Cmdline_rank_above = allowabove_arg.str();
	}
	if ( allowbelow_arg.found() ) {
		Cmdline_rank_below = allowbelow_arg.str();
	}

	// get the port number for games
	if ( port_arg.found() ) {
		Cmdline_network_port = port_arg.get_int();
	}

	// the connect argument specifies to join a game at this particular address
	if ( connect_arg.found() ) {
		Cmdline_use_last_pilot = 1;
		Cmdline_connect_addr = connect_arg.str();
	}

	// see if the multilog flag was set
	if ( multilog_arg.found() ){
		Cmdline_multi_log = 1;
	}	

	// maybe use old-school server-side firing
	if (server_firing_arg.found() ){
		Cmdline_server_firing = 1;
	}

	// maybe use old-school client damage
	if(client_dodamage.found()){
		Cmdline_client_dodamage = 1;
	}	

	// spew pof info
	if(pof_spew.found()){
		Cmdline_spew_pof_info = 1;
	}

	// Claude: 16 bit screen and textures (32 bit is the default)
	if(force_16bit.found()){
		Cmdline_force_16bit = 1;
	}

	// Claude: test: skip drawing the mission, to check that gameplay does not depend on drawing
	if(nodraw_arg.found()){
		Cmdline_nodraw = 1;
	}

	// Claude: no window, graphics or sound device
	if(headless_arg.found()){
		Cmdline_headless = 1;
		Cmdline_nographics = 1;
		Cmdline_nosounddevice = 1;
		Cmdline_nodraw = 1;
	}

	// Claude: no sound device
	if(nosounddevice_arg.found()){
		Cmdline_nosounddevice = 1;
	}

	// Claude: no graphics device
	if(nographics_arg.found()){
		Cmdline_nographics = 1;
		Cmdline_nodraw = 1;
	}

	// Claude: no window handle at all; D3D needs one, so only with no graphics device (-nographics or -headless)
	if(nowindow_arg.found() && Cmdline_nographics){
		Os_no_window = 1;
	}

	// mouse coords
	if(mouse_coords.found()){
		Cmdline_mouse_coords = 1;
	}

	// net timeout
	if(timeout.found()){
		Cmdline_timeout = timeout.get_int();
	}

	// d3d windowed
	if(d3d_window.found()){
		Cmdline_window = 1;
	}

	// Claude: list every cutscene in the tech room, seen or not
	if(all_cutscenes_arg.found()){
		Cmdline_all_cutscenes = 1;
	}

	// gameplay recorder
	if(record_arg.found() || replay_arg.found()){
		replay_set_cmdline(record_arg.found() ? record_arg.str() : NULL, replay_arg.found() ? replay_arg.str() : NULL,
			trace_arg.found() ? trace_arg.str() : NULL, fulltrace_arg.found());
	}
	if(replay_pace_arg.found()){	// Claude
		replay_set_pace(1);
	}

	// Claude: -autorecord.  -record and -replay take precedence.
	if(autorecord_arg.found() && !record_arg.found() && !replay_arg.found()){
		replay_set_autorecord(autorecord_arg.str());
	}

	// Claude: -ini, before the settings are first read
	if(ini_arg.found()){
		os_config_set_ini(ini_arg.str());
	}
	if(dataset_arg.found()){	// Claude
		Cmdline_dataset = dataset_arg.str();	// Claude
	}	// Claude

	// Claude: -bmlog, -bmshots, -bmframes
	if(bmlog_arg.found() || bmshots_arg.found()){
		bmlog_set_cmdline(bmlog_arg.found() ? bmlog_arg.str() : NULL, bmshots_arg.found() ? bmshots_arg.str() : NULL, bmframes_arg.found() ? bmframes_arg.str() : NULL);
	}

	// Claude: -loadtimes
	if(loadtimes_arg.found()){
		Cmdline_loadtimes = 1;
	}

	// Claude: -mission.  Autotest and replay pick their own mission.
	if(mission_arg.found() && !autotest_arg.found() && !replay_arg.found()){
		Cmdline_mission = mission_arg.str();
	}

	// Claude: autotest.  The -at_ flags do nothing without -autotest.
	if(autotest_arg.found()){
		autotest_set_cmdline(autotest_mission_arg.found() ? autotest_mission_arg.str() : NULL, autotest_time_arg.found() ? autotest_time_arg.str() : NULL,
			autotest_speed_arg.found() ? autotest_speed_arg.str() : NULL, autotest_pilot_arg.found() ? autotest_pilot_arg.str() : NULL,
			autotest_skill_arg.found() ? autotest_skill_arg.str() : NULL, playernpc_arg.found());
		if(autotest_menus_arg.found()){	// Claude: -at_menus
			autotest_set_menus();
		}
		if(autotest_shots_arg.found()){	// Claude: -at_shots
			autotest_set_shots(autotest_shots_arg.str());
		}
		if(autotest_input_arg.found()){	// Claude: -at_input
			input_test_set_cmdline(autotest_input_arg.str());
		}
	}

	// Claude: -noshipglow turns off the D3D ship glow (Graphics/GrD3DGlow.cpp)
	if(no_ship_glow_arg.found()){
		Cmdline_no_ship_glow = 1;
	}

	// Claude: -noforceglow turns off forced red ship glow (Graphics/GrD3DGlow.cpp)
	if(no_force_glow_arg.found()){
		Cmdline_no_force_glow = 1;
	}

	return 1;
}
