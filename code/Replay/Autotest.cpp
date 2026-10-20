/*
 * Autotest: plays missions with nobody at the controls.  See Autotest.h.
 *
 * Everything that changes how a mission plays (time compression, invulnerability, the time
 * limit, answering popups, not pausing when the window loses focus) is under Autotest_rules,
 * which a replay takes from the recording.  The time limit is measured in mission time, which a
 * replay reproduces exactly, so the replay ends on the same frame as the recording.
 *
 * Everything else (driving the menus, the log, the watchdog, no dialogs) is under
 * Autotest_harness and never changes a mission.  The watchdog runs on its own thread and only
 * reads the wall clock, which the recorder never sees.
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#include "pstypes.h"
#include "Autotest.h"
#include "Replay.h"
#include "FreeSpace.h"
#include "GameSequence.h"
#include "SystemVars.h"
#include "cfile.h"
#include "Player.h"
#include "ManagePilot.h"
#include "Object.h"
#include "Ship.h"
#include "MissionScreenCommon.h"
#include "MissionParse.h"
#include "MissionGoals.h"
#include "2d.h"
#include "MissionBrief.h"
#include "DataSetProfile.h"

#define AUTOTEST_LOG				"autotest.log"
#define AUTOTEST_DEFAULT_TIME		120		// seconds of mission time
#define AUTOTEST_DEFAULT_SPEED	4			// time compression
#define AUTOTEST_MAX_TIME			30000		// Missiontime is 16.16 fixed point
#define AUTOTEST_STUCK_MS			120000	// a menu state that lasts this long is stuck
#define AUTOTEST_GIVE_UP_MS		30000		// a menu the autotest drives that lasts this long fails the mission
#define AUTOTEST_EXIT_ERROR		1
#define AUTOTEST_EXIT_FAILED		2
#define AUTOTEST_EXIT_STUCK		5

// A test mission whose notes contain this decides the player's vulnerability itself (ship-vulnerable,
// ship-invulnerable), for example so that a supernova can end it.  Campaign missions do not, so
// they keep the player invulnerable however their events change it.
#define AUTOTEST_VULNERABILITY_NOTE	"autotest: mission controls vulnerability"

// A test mission whose notes contain this needs the player ship left alone (the unflown player scans
// cargo in an EMP storm), so the AI does not fly it even with -playernpc.
#define AUTOTEST_STILL_NOTE			"autotest: player stays still"


int Autotest_harness = 0;
int Autotest_rules = 0;
int Autotest_player_npc = 0;
static int Autotest_player_npc_asked = 0;	// -playernpc, or the recording; a mission can still refuse it

// the recording header's autotest field: bit 0 autotest rules, bit 1 -playernpc
#define AUTOTEST_HDR_RULES			1
#define AUTOTEST_HDR_PLAYER_NPC	2

static char Autotest_mission[MAX_FILENAME_LEN] = "";	// -at_mission, or "" for every campaign mission
static char Autotest_pilot[MAX_FILENAME_LEN] = "";
static int Autotest_time = AUTOTEST_DEFAULT_TIME;
static int Autotest_speed = AUTOTEST_DEFAULT_SPEED;
static int Autotest_skill = -1;			// -at_skill, or -1 for the pilot's own

static FILE *Autotest_log = NULL;
static CRITICAL_SECTION Autotest_log_lock;
static DWORD Autotest_start_ms;

// the mission being run
static int Autotest_list_index = -1;		// index into the data set's built-in mission list (Game_profile)
static int Autotest_started = 0;			// a mission was started from the main menu
static int Autotest_in_mission = 0;		// that mission reached gameplay
static int Autotest_ending = 0;			// the time limit has posted GS_EVENT_END_GAME
static int Autotest_gave_up = 0;			// a menu did not move on, so the mission was abandoned
static fix Autotest_mission_start;		// Missiontime on the first gameplay frame
static char Autotest_current[MAX_FILENAME_LEN] = "";
static int Autotest_mission_vulnerability = 0;	// the mission has AUTOTEST_VULNERABILITY_NOTE

// the event and goal states last logged, so each change is logged once
#define AUTOTEST_EVENT_PENDING	0
#define AUTOTEST_EVENT_TRUE		1
#define AUTOTEST_EVENT_FALSE		2		// false and will not be evaluated again
static int Autotest_event_state[MAX_MISSION_EVENTS];
static int Autotest_goal_state[MAX_GOALS];

// results
static int Autotest_missions_run = 0;
static int Autotest_missions_failed = 0;
static int Autotest_asserts = 0;
static int Autotest_int3s = 0;
static int Autotest_warnings = 0;

#define AUTOTEST_MAX_SITES		64
#define AUTOTEST_LOG_PER_SITE	3
typedef struct autotest_site {
	char *kind, *file;
	int line, count;
} autotest_site;
static autotest_site Autotest_sites[AUTOTEST_MAX_SITES];
static int Autotest_num_sites = 0;

// watchdog, written by the main thread and read by the watchdog thread
static volatile int Autotest_state = -1;
static volatile DWORD Autotest_state_ms = 0;
static volatile DWORD Autotest_frame_ms = 0;

static void autotest_logf(char *format, ...)
{
	va_list args;
	DWORD ms;

	if (!Autotest_log) {
		return;
	}
	ms = GetTickCount() - Autotest_start_ms;
	EnterCriticalSection(&Autotest_log_lock);
	fprintf(Autotest_log, "[%4d.%03d] ", ms / 1000, ms % 1000);
	va_start(args, format);
	vfprintf(Autotest_log, format, args);
	va_end(args);
	fflush(Autotest_log);
	LeaveCriticalSection(&Autotest_log_lock);
}

static void autotest_fatal(int code, char *format, ...)
{
	char text[512];
	va_list args;

	va_start(args, format);
	_vsnprintf(text, sizeof(text) - 1, format, args);
	text[sizeof(text) - 1] = 0;
	va_end(args);

	autotest_logf("FATAL: %s\n", text);
	autotest_logf("FAIL (exit code %d)\n", code);
	TerminateProcess(GetCurrentProcess(), code);
}

// the game is stuck if no frame has run for a while, or a menu has not moved on.  Gameplay is
// left to the mission time limit, plus a wall clock limit in case mission time stops.
static DWORD WINAPI autotest_watchdog(LPVOID)
{
	DWORD now, limit;

	for (;;) {
		Sleep(1000);
		now = GetTickCount();
		// Claude: signed, since the main thread can write a newer time after now was read; unsigned, that wrapped
		// to about 50 days and stopped healthy runs with exit 5 (D-054)
		if ((int)(now - Autotest_frame_ms) > AUTOTEST_STUCK_MS) {
			autotest_fatal(AUTOTEST_EXIT_STUCK, "no frame for %d seconds in state %d", (now - Autotest_frame_ms) / 1000, Autotest_state);
		}

		// gameplay without the time limit (replaying a player's recording) can take as long as it
		// likes.  Autotest_time is read here because a replay sets it from the recording.  The death
		// states still run game frames and the time limit.
		if ((Autotest_state == GS_STATE_GAME_PLAY) || (Autotest_state == GS_STATE_DEATH_DIED) || (Autotest_state == GS_STATE_DEATH_BLEW_UP)) {
			if (!Autotest_rules) {
				continue;
			}
			limit = (DWORD)Autotest_time * 1000 * 4 + AUTOTEST_STUCK_MS;
		} else {
			limit = AUTOTEST_STUCK_MS;
		}
		if ((int)(now - Autotest_state_ms) > (int)limit) {	// Claude: signed, as above (D-054)
			autotest_fatal(AUTOTEST_EXIT_STUCK, "stuck in state %d for %d seconds", Autotest_state, (now - Autotest_state_ms) / 1000);
		}
	}
	return 0;
}

static LONG WINAPI autotest_crash(EXCEPTION_POINTERS *info)
{
	autotest_logf("FATAL: crash, exception 0x%08x at 0x%08x in mission '%s'\n", info->ExceptionRecord->ExceptionCode,
		info->ExceptionRecord->ExceptionAddress, Autotest_current);
	autotest_logf("FAIL (exit code 0x%08x)\n", info->ExceptionRecord->ExceptionCode);
	return EXCEPTION_CONTINUE_SEARCH;
}

// a value from the command line: "" if the flag had none, NULL if the flag was not given
static int autotest_int_arg(char *flag, char *value, int def, int lo, int hi)
{
	char *end;
	long v;

	if (value == NULL) {
		return def;
	}
	v = strtol(value, &end, 10);
	if (!value[0] || *end || (v < lo) || (v > hi)) {
		autotest_fatal(AUTOTEST_EXIT_FAILED, "%s needs a whole number from %d to %d, got '%s'", flag, lo, hi, value);
	}
	return (int)v;
}

void autotest_set_cmdline(char *mission, char *time, char *speed, char *pilot, char *skill, int player_npc)
{
	Autotest_harness = 1;
	Autotest_rules = 1;
	Autotest_player_npc = Autotest_player_npc_asked = player_npc;
	Autotest_start_ms = GetTickCount();
	InitializeCriticalSection(&Autotest_log_lock);
	Autotest_log = fopen(AUTOTEST_LOG, "wt");

#ifdef NDEBUG
	autotest_logf("Autotest, Release build %s %s\n", __DATE__, __TIME__);
#else
	autotest_logf("Autotest, Debug build %s %s\n", __DATE__, __TIME__);
#endif

	Autotest_time = autotest_int_arg("-at_time", time, AUTOTEST_DEFAULT_TIME, 1, AUTOTEST_MAX_TIME);
	Autotest_speed = autotest_int_arg("-at_speed", speed, AUTOTEST_DEFAULT_SPEED, 1, 8);
	Autotest_skill = autotest_int_arg("-at_skill", skill, -1, 0, NUM_SKILL_LEVELS - 1);
	if (mission) {
		if (!mission[0] || (strlen(mission) > MAX_FILENAME_LEN - 5)) {
			autotest_fatal(AUTOTEST_EXIT_FAILED, "-at_mission needs a mission file name");
		}
		strcpy(Autotest_mission, mission);
		if (!strchr(Autotest_mission, '.')) {
			strcat(Autotest_mission, ".fs2");
		}
	}
	if (pilot) {
		if (!pilot[0] || (strlen(pilot) >= MAX_FILENAME_LEN)) {
			autotest_fatal(AUTOTEST_EXIT_FAILED, "-at_pilot needs a pilot callsign");
		}
		strcpy(Autotest_pilot, pilot);
	}

	// a crash ends the process with its exception code instead of waiting on the Windows crash
	// dialog, and the watchdog also covers start-up, before the first frame
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	SetUnhandledExceptionFilter(autotest_crash);
	Autotest_frame_ms = Autotest_state_ms = GetTickCount();
	CreateThread(NULL, 0, autotest_watchdog, NULL, 0, NULL);
}

void autotest_get_settings(int *on, int *time, int *speed)
{
	*on = Autotest_rules ? (AUTOTEST_HDR_RULES | (Autotest_player_npc_asked ? AUTOTEST_HDR_PLAYER_NPC : 0)) : 0;
	*time = Autotest_rules ? Autotest_time : 0;
	*speed = Autotest_rules ? Autotest_speed : 0;
}

// a replay plays the mission under the rules it was recorded with, whatever the command line says
void autotest_set_from_recording(int on, int time, int speed)
{
	Autotest_rules = (on & AUTOTEST_HDR_RULES) != 0;
	Autotest_player_npc = Autotest_player_npc_asked = Autotest_rules && (on & AUTOTEST_HDR_PLAYER_NPC);
	if (Autotest_rules) {
		Autotest_time = time;
		Autotest_speed = speed;
	}
	autotest_logf("Replaying a recording made %s autotest rules%s\n", Autotest_rules ? "with" : "without", Autotest_player_npc ? ", the AI flying the player" : "");
}

int autotest_load_pilot()
{
	char pilots_arr[MAX_PILOTS][MAX_FILENAME_LEN];
	char *pilots[MAX_PILOTS];
	int count;

	if (!Autotest_harness || (Replay_mode == REPLAY_PLAY)) {
		return 0;
	}

	if (!Autotest_pilot[0]) {
		// the most recently saved single-player pilot
		count = cf_get_file_list_preallocated(MAX_PILOTS, pilots_arr, pilots, CF_TYPE_SINGLE_PLAYERS, NOX("*.plr"), CF_SORT_TIME);
		if (count <= 0) {
			autotest_fatal(AUTOTEST_EXIT_FAILED, "no single-player pilot found; make one in the game or pass -at_pilot");
		}
		strcpy(Autotest_pilot, pilots[0]);
	}

	Player = &Players[0];
	if (read_pilot_file(Autotest_pilot, 1, Player)) {
		autotest_fatal(AUTOTEST_EXIT_FAILED, "cannot load pilot '%s'", Autotest_pilot);
	}
	Player->flags |= PLAYER_FLAGS_STRUCTURE_IN_USE;
	if (Autotest_skill >= 0) {
		Game_skill_level = Autotest_skill;
	}
	autotest_logf("Pilot '%s', skill level %d%s\n", Autotest_pilot, Game_skill_level, (Autotest_skill >= 0) ? " (-at_skill)" : "");
	return 1;
}

// the next mission to run, or 0 when there are no more
static int autotest_next_mission()
{
	int flags;

	if (Autotest_mission[0]) {
		if (Autotest_started) {
			return 0;
		}
		strcpy(Autotest_current, Autotest_mission);
		return 1;
	}

	// every single-player campaign mission, in campaign list order
	for (Autotest_list_index++; Autotest_list_index < *Game_profile->builtin_mission_count; Autotest_list_index++) {
		flags = Game_profile->builtin_missions[Autotest_list_index].flags;
		if ((flags & FSB_CAMPAIGN) && !(flags & (FSB_MULTI | FSB_CAMPAIGN_FILE))) {
			strcpy(Autotest_current, Game_profile->builtin_missions[Autotest_list_index].filename);
			return 1;
		}
	}
	return 0;
}

static void autotest_main_menu()
{
	if (Autotest_started) {
		Autotest_missions_run++;
		if (Autotest_gave_up) {
			Autotest_missions_failed++;
		} else if (Autotest_in_mission) {
			autotest_logf("Mission '%s' done\n", Autotest_current);
		} else {
			Autotest_missions_failed++;
			autotest_logf("FAILED: mission '%s' did not start\n", Autotest_current);
		}

		// a recording holds one mission
		if (Replay_mode == REPLAY_RECORD) {
			gameseq_post_event(GS_EVENT_QUIT_GAME);
			return;
		}
	}

	if (!autotest_next_mission()) {
		gameseq_post_event(GS_EVENT_QUIT_GAME);
		return;
	}

	Autotest_started = 1;
	Autotest_in_mission = 0;
	Autotest_ending = 0;
	Autotest_gave_up = 0;
	autotest_logf("Mission '%s': starting, %d s of mission time at %dx%s\n", Autotest_current, Autotest_time, Autotest_speed, Autotest_player_npc ? ", the AI flying the player" : "");

	// the same path as the simulator room
	strncpy(Game_current_mission_filename, Autotest_current, MAX_FILENAME_LEN - 1);
	Game_mode &= ~(GM_CAMPAIGN_MODE);
	gameseq_post_event(GS_EVENT_START_GAME);
}

// -at_menus: a tour of every single-player screen instead of playing missions.  Each stop posts the
// event a button would, waits for its state, stays a few frames, then leaves the way its Back button
// does.  The mission screens use the -at_mission mission, or the first campaign mission.
// Not visited: the pilot screen (it deletes old pilot files, #23) and the goals screen (GS_STATE_SHOW_GOALS), which
// FS2 cannot reach: its control has no key and no tab, and its art (Objectives) is not in the FS2 data.
#define AUTOTEST_TOUR_COMMIT		-1		// not an event: commit_pressed(), as the Commit button
#define AUTOTEST_TOUR_SCREEN		-2		// not an event: the briefing screens' own buttons, which pick the next screen
#define AUTOTEST_TOUR_STAY			-1		// no leave event: the next stop moves on from here
#define AUTOTEST_TOUR_FRAMES		30		// frames drawn on each screen
#define AUTOTEST_TOUR_PLAY_FRAMES	120		// frames of gameplay before the in-game screens
extern int Next_screen;	// MissionScreenCommon.cpp: the briefing screen to go to
typedef struct autotest_stop {
	char *name;
	int event, state, leave;
} autotest_stop;
static autotest_stop Autotest_tour[] = {
	{ "Options",			GS_EVENT_OPTIONS_MENU,				GS_STATE_OPTIONS_MENU,				GS_EVENT_PREVIOUS_STATE },
	{ "Control config",		GS_EVENT_CONTROL_CONFIG,			GS_STATE_CONTROL_CONFIG,			GS_EVENT_PREVIOUS_STATE },
	{ "HUD config",			GS_EVENT_HUD_CONFIG,				GS_STATE_HUD_CONFIG,				GS_EVENT_PREVIOUS_STATE },
	{ "Medals",				GS_EVENT_VIEW_MEDALS,				GS_STATE_VIEW_MEDALS,				GS_EVENT_PREVIOUS_STATE },
	{ "Barracks",			GS_EVENT_BARRACKS_MENU,				GS_STATE_BARRACKS_MENU,				GS_EVENT_MAIN_MENU },
	{ "Tech room",			GS_EVENT_TECH_MENU,					GS_STATE_TECH_MENU,					GS_EVENT_MAIN_MENU },
	{ "Campaign room",		GS_EVENT_CAMPAIGN_ROOM,				GS_STATE_CAMPAIGN_ROOM,				GS_EVENT_MAIN_MENU },
	{ "Simulator room",		GS_EVENT_SIMULATOR_ROOM,			GS_STATE_SIMULATOR_ROOM,			GS_EVENT_MAIN_MENU },
	{ "Cutscenes",			GS_EVENT_GOTO_VIEW_CUTSCENES_SCREEN,	GS_STATE_VIEW_CUTSCENES,			GS_EVENT_MAIN_MENU },
	{ "Credits",			GS_EVENT_CREDITS,					GS_STATE_CREDITS,					GS_EVENT_MAIN_MENU },
	{ "Command briefing",	GS_EVENT_START_GAME,				GS_STATE_CMD_BRIEF,					AUTOTEST_TOUR_STAY },
	{ "Briefing",			GS_EVENT_START_BRIEFING,			GS_STATE_BRIEFING,					AUTOTEST_TOUR_STAY },
	{ "Ship select",		AUTOTEST_TOUR_SCREEN,				GS_STATE_SHIP_SELECT,				AUTOTEST_TOUR_STAY },
	{ "Weapon select",		AUTOTEST_TOUR_SCREEN,				GS_STATE_WEAPON_SELECT,				AUTOTEST_TOUR_STAY },
	{ "Gameplay",			AUTOTEST_TOUR_COMMIT,				GS_STATE_GAME_PLAY,					AUTOTEST_TOUR_STAY },
	{ "Pause",				GS_EVENT_PAUSE_GAME,				GS_STATE_GAME_PAUSED,				GS_EVENT_PREVIOUS_STATE },
	{ "Mission log",		GS_EVENT_MISSION_LOG_SCROLLBACK,	GS_STATE_MISSION_LOG_SCROLLBACK,	GS_EVENT_PREVIOUS_STATE },
	{ "Hotkeys",			GS_EVENT_HOTKEY_SCREEN,				GS_STATE_HOTKEY_SCREEN,				GS_EVENT_PREVIOUS_STATE },
	{ "Gameplay help",		GS_EVENT_GAMEPLAY_HELP,				GS_STATE_GAMEPLAY_HELP,				GS_EVENT_PREVIOUS_STATE },
	{ "In-game options",	GS_EVENT_OPTIONS_MENU,				GS_STATE_OPTIONS_MENU,				GS_EVENT_PREVIOUS_STATE },
	{ "In-game controls",	GS_EVENT_CONTROL_CONFIG,			GS_STATE_CONTROL_CONFIG,			GS_EVENT_PREVIOUS_STATE },
	{ "In-game HUD config",	GS_EVENT_HUD_CONFIG,				GS_STATE_HUD_CONFIG,				GS_EVENT_PREVIOUS_STATE },
	{ "Debriefing",			GS_EVENT_DEBRIEF,					GS_STATE_DEBRIEF,					GS_EVENT_END_GAME },
};
#define AUTOTEST_TOUR_STOPS	((int)(sizeof(Autotest_tour) / sizeof(Autotest_tour[0])))

#define AUTOTEST_TOUR_GO		0		// post the stop's event
#define AUTOTEST_TOUR_ARRIVE	1		// wait for its state
#define AUTOTEST_TOUR_LOOK		2		// stay a few frames
#define AUTOTEST_TOUR_BACK		3		// wait to be back where the stop started
static int Autotest_menus = 0;				// -at_menus
static int Autotest_tour_stop = 0;
static int Autotest_tour_phase = AUTOTEST_TOUR_GO;
static int Autotest_tour_from;				// the state the stop started in
static int Autotest_tour_frames;
static DWORD Autotest_tour_ms;
static int Autotest_tour_seen = 0;
static int Autotest_tour_failed = 0;

void autotest_set_menus()
{
	if (!Autotest_harness) {
		return;
	}
	Autotest_menus = 1;
	autotest_logf("Menu tour: %d screens\n", AUTOTEST_TOUR_STOPS);
}

static void autotest_tour_end(char *why)
{
	if (why) {
		Autotest_tour_failed++;
		autotest_logf("FAILED: menu tour stopped at '%s': %s\n", Autotest_tour[Autotest_tour_stop].name, why);
	}
	autotest_logf("Menu tour: %d of %d screens seen\n", Autotest_tour_seen, AUTOTEST_TOUR_STOPS);
	if (!why && (Autotest_tour_seen < AUTOTEST_TOUR_STOPS)) {
		Autotest_tour_failed++;
		autotest_logf("FAILED: menu tour missed %d screens\n", AUTOTEST_TOUR_STOPS - Autotest_tour_seen);
	}
	Autotest_tour_stop = AUTOTEST_TOUR_STOPS;
	gameseq_post_event(GS_EVENT_QUIT_GAME);
}

// each frame, in place of the mission driving
static void autotest_tour_frame(int state)
{
	autotest_stop *s;

	if (Autotest_tour_stop >= AUTOTEST_TOUR_STOPS) {
		return;
	}
	s = &Autotest_tour[Autotest_tour_stop];

	switch (Autotest_tour_phase) {
		case AUTOTEST_TOUR_GO:
			// the tour starts from the main hall
			if ((Autotest_tour_stop == 0) && (state != GS_STATE_MAIN_MENU)) {
				return;
			}
			Autotest_tour_from = state;
			Autotest_tour_ms = Autotest_frame_ms;
			Autotest_tour_phase = AUTOTEST_TOUR_ARRIVE;
			// already there (the screen before opened it): look at it from this frame
			if (state == s->state) {
				autotest_tour_frame(state);
				return;
			}
			if (s->event == GS_EVENT_START_GAME) {
				if (!autotest_next_mission()) {
					autotest_tour_end("no mission for the mission screens");
					return;
				}
				strncpy(Game_current_mission_filename, Autotest_current, MAX_FILENAME_LEN - 1);
				Game_mode &= ~(GM_CAMPAIGN_MODE);
				autotest_logf("Menu tour: mission screens with '%s'\n", Autotest_current);
			}
			if (s->event == AUTOTEST_TOUR_COMMIT) {
				commit_pressed();
			} else if (s->event == AUTOTEST_TOUR_SCREEN) {
				// as the Ship Selection and Weapon Loadout buttons: posting the event alone leaves Next_screen on the
				// briefing, and the screen goes straight back to it
				if (brief_only_allow_briefing()) {
					autotest_tour_end("the mission has no ship or weapon selection (training or scramble)");
					return;
				}
				Next_screen = (s->state == GS_STATE_SHIP_SELECT) ? ON_SHIP_SELECT : ON_WEAPON_SELECT;
			} else {
				gameseq_post_event(s->event);
			}
			return;

		case AUTOTEST_TOUR_ARRIVE:
			// a mission with no command briefing goes straight on to the briefing
			if ((s->state == GS_STATE_CMD_BRIEF) && (state == GS_STATE_BRIEFING)) {
				autotest_logf("Menu tour: '%s' has no command briefing\n", Autotest_current);
				Autotest_tour_seen++;
				break;
			}
			if (state != s->state) {
				if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("its screen did not open");
				}
				return;
			}
			Autotest_tour_seen++;
			autotest_logf("Menu tour: '%s'\n", s->name);
			Autotest_tour_frames = 0;
			Autotest_tour_phase = AUTOTEST_TOUR_LOOK;
			return;

		case AUTOTEST_TOUR_LOOK:
			if (state != s->state) {
				autotest_tour_end("its screen closed by itself");
				return;
			}
			if (++Autotest_tour_frames < ((s->state == GS_STATE_GAME_PLAY) ? AUTOTEST_TOUR_PLAY_FRAMES : AUTOTEST_TOUR_FRAMES)) {
				return;
			}
			if (s->leave == AUTOTEST_TOUR_STAY) {
				break;
			}
			gameseq_post_event(s->leave);
			Autotest_tour_ms = Autotest_frame_ms;
			Autotest_tour_phase = AUTOTEST_TOUR_BACK;
			return;

		case AUTOTEST_TOUR_BACK:
			// a pushed screen returns to where it started; the others go back to the main hall
			if (state != ((s->leave == GS_EVENT_PREVIOUS_STATE) ? Autotest_tour_from : GS_STATE_MAIN_MENU)) {
				if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("did not go back");
				}
				return;
			}
			break;
	}

	// on to the next stop, from the next frame
	Autotest_tour_phase = AUTOTEST_TOUR_GO;
	if (++Autotest_tour_stop == AUTOTEST_TOUR_STOPS) {
		Autotest_tour_stop--;
		autotest_tour_end(NULL);
	}
}

void autotest_drive_state(int state)
{
	if (!Autotest_harness) {
		return;
	}

	Autotest_frame_ms = GetTickCount();
	if (Autotest_menus && (Replay_mode != REPLAY_PLAY)) {
		if (state != Autotest_state) {
			Autotest_state_ms = Autotest_frame_ms;
			Autotest_state = state;
		}
		autotest_tour_frame(state);
		return;
	}
	if (state == Autotest_state) {
		// a menu the autotest drives has not moved on (an event was refused or flushed), so give up on
		// this mission and go on to the next one
		if ((Replay_mode != REPLAY_PLAY) && !Autotest_gave_up && (Autotest_frame_ms - Autotest_state_ms > AUTOTEST_GIVE_UP_MS) &&
			((state == GS_STATE_CMD_BRIEF) || (state == GS_STATE_BRIEFING) || (state == GS_STATE_RED_ALERT))) {
			Autotest_gave_up = 1;
			autotest_logf("FAILED: mission '%s' did not leave state %d, back to the main hall\n", Autotest_current, state);
			gameseq_post_event(GS_EVENT_MAIN_MENU);
		}
		return;
	}

	// act once, on the first frame in each state.  The time is written first, so the watchdog never
	// sees the new state with the old state's time.
	Autotest_state_ms = Autotest_frame_ms;
	Autotest_state = state;

	// a replay drives the menus itself
	if (Replay_mode == REPLAY_PLAY) {
		return;
	}

	switch (state) {
		case GS_STATE_MAIN_MENU:
			autotest_main_menu();
			break;

		case GS_STATE_CMD_BRIEF:
			gameseq_post_event(GS_EVENT_START_BRIEFING);
			break;

		case GS_STATE_BRIEFING:
			commit_pressed();
			break;

		case GS_STATE_RED_ALERT:
			if (Replay_mode == REPLAY_RECORD) {
				autotest_logf("Mission '%s' is a red alert mission, which replay does not support yet\n", Autotest_current);
			}
			gameseq_post_event(GS_EVENT_ENTER_GAME);
			break;

		case GS_STATE_DEBRIEF:
			// the mission ended before the time limit
			autotest_logf("Mission '%s' ended itself\n", Autotest_current);
			gameseq_post_event(GS_EVENT_END_GAME);
			break;
	}
}

// log each mission event and goal as its state changes, so the log shows how far a mission got
static void autotest_log_events()
{
	int i, state;

	for (i = 0; i < Num_mission_events; i++) {
		if (Mission_events[i].result) {
			state = AUTOTEST_EVENT_TRUE;
		} else if (Mission_events[i].formula == -1) {
			state = AUTOTEST_EVENT_FALSE;
		} else {
			state = AUTOTEST_EVENT_PENDING;
		}
		if (state != Autotest_event_state[i]) {
			Autotest_event_state[i] = state;
			autotest_logf("Mission '%s': event '%s' %s at mission time %.2f\n", Game_current_mission_filename, Mission_events[i].name,
				(state == AUTOTEST_EVENT_TRUE) ? "true" : ((state == AUTOTEST_EVENT_FALSE) ? "false" : "pending"), f2fl(Missiontime));
		}
	}

	for (i = 0; i < Num_goals; i++) {
		if (Mission_goals[i].satisfied != Autotest_goal_state[i]) {
			Autotest_goal_state[i] = Mission_goals[i].satisfied;
			autotest_logf("Mission '%s': goal '%s' %s at mission time %.2f\n", Game_current_mission_filename, Mission_goals[i].name,
				(Mission_goals[i].satisfied == GOAL_COMPLETE) ? "complete" : ((Mission_goals[i].satisfied == GOAL_FAILED) ? "failed" : "incomplete"), f2fl(Missiontime));
		}
	}
}

void autotest_frame()
{
	ship *shipp;
	int i;

	if (!Autotest_rules) {
		return;
	}

	// before game_set_frametime, so this frame already runs at the test speed
	Game_time_compression = F1_0 * Autotest_speed;

	if (!Autotest_in_mission) {
		Autotest_mission_vulnerability = (strstr(The_mission.notes, AUTOTEST_VULNERABILITY_NOTE) != NULL);
		Autotest_player_npc = Autotest_player_npc_asked && (strstr(The_mission.notes, AUTOTEST_STILL_NOTE) == NULL);
		for (i = 0; i < MAX_MISSION_EVENTS; i++) {
			Autotest_event_state[i] = AUTOTEST_EVENT_PENDING;
		}
		for (i = 0; i < MAX_GOALS; i++) {
			Autotest_goal_state[i] = GOAL_INCOMPLETE;
		}
	}

	// every frame: the player may not exist on the first frame, and mission events can clear it
	if (!Autotest_mission_vulnerability && (Player_obj != NULL) && (Player_obj->type == OBJ_SHIP)) {
		shipp = &Ships[Player_obj->instance];
		shipp->flags |= SF_INVULNERABLE;
		Player_obj->flags |= OF_INVULNERABLE;
	}

	if (!Autotest_in_mission) {
		Autotest_in_mission = 1;
		Autotest_mission_start = Missiontime;
		if (gr_screen.mode == GR_DUMMY) {
			autotest_logf("No graphics device: the null renderer (-nographics)\n");	// Claude: so the log shows the run was headless
		}
		autotest_logf("Mission '%s': in gameplay%s%s\n", Game_current_mission_filename, Autotest_mission_vulnerability ? ", the mission controls the player's vulnerability" : "",
			Autotest_player_npc ? ", the AI flies the player" : (Autotest_player_npc_asked ? ", the player stays still (mission notes)" : ""));
	} else if (!Autotest_ending && (Missiontime - Autotest_mission_start >= i2f(Autotest_time))) {
		Autotest_ending = 1;
		autotest_logf("Mission '%s': time limit reached at mission time %.2f\n", Game_current_mission_filename, f2fl(Missiontime));
		gameseq_post_event(GS_EVENT_END_GAME);
	}

	autotest_log_events();
}

int autotest_popup(char *text)
{
	if (!Autotest_rules) {
		return 0;
	}
	autotest_logf("Popup answered with its first choice: '%.100s'\n", text);
	return 1;
}

// one Assert / Warning / Int3 can fire every frame, so each place is logged a few times and counted
static int autotest_count_site(char *kind, char *file, int line)
{
	int i;

	for (i = 0; i < Autotest_num_sites; i++) {
		if ((Autotest_sites[i].kind == kind) && (Autotest_sites[i].file == file) && (Autotest_sites[i].line == line)) {
			return ++Autotest_sites[i].count;
		}
	}
	if (Autotest_num_sites == AUTOTEST_MAX_SITES) {
		return 1;
	}
	Autotest_sites[i].kind = kind;
	Autotest_sites[i].file = file;
	Autotest_sites[i].line = line;
	Autotest_sites[i].count = 1;
	Autotest_num_sites++;
	return 1;
}

int autotest_report(char *kind, char *text, char *file, int line)
{
	int count;

	if (!Autotest_harness) {
		return 0;
	}

	if (!stricmp(kind, "Assert")) {
		Autotest_asserts++;
	} else if (!stricmp(kind, "Int3")) {
		Autotest_int3s++;
	} else if (!stricmp(kind, "Warning")) {
		Autotest_warnings++;
	}

	count = autotest_count_site(kind, file, line);
	if (count > AUTOTEST_LOG_PER_SITE) {
		return 1;
	}
	if (file) {
		autotest_logf("%s: %s (%s line %d)%s\n", kind, text, file, line, (count == AUTOTEST_LOG_PER_SITE) ? ", not logged again" : "");
	} else {
		autotest_logf("%s in mission '%s'%s\n", kind, Autotest_current, (count == AUTOTEST_LOG_PER_SITE) ? ", not logged again" : "");
	}
	if (!stricmp(kind, "Error")) {
		autotest_logf("FAIL (exit code %d)\n", AUTOTEST_EXIT_ERROR);
	}
	return 1;
}

void autotest_note(char *text)
{
	autotest_logf("%s", text);
}

int autotest_exit_code(int code)
{
	int failed, i;

	if (!Autotest_harness) {
		return code;
	}

	// Claude: a Warning fails the run too, as D-029 says recordings have none (#6, D-20261017-223435)
	failed = Autotest_missions_failed || Autotest_tour_failed || Autotest_asserts || Autotest_int3s || Autotest_warnings;
	for (i = 0; i < Autotest_num_sites; i++) {
		autotest_logf("%6d x %s %s line %d\n", Autotest_sites[i].count, Autotest_sites[i].kind, Autotest_sites[i].file ? Autotest_sites[i].file : "", Autotest_sites[i].line);
	}
	autotest_logf("Missions run %d, failed %d.  Asserts %d, Int3 %d, warnings %d\n", Autotest_missions_run, Autotest_missions_failed,
		Autotest_asserts, Autotest_int3s, Autotest_warnings);
	autotest_logf(failed ? "FAIL (exit code %d)\n" : "PASS\n", AUTOTEST_EXIT_FAILED);
	return failed ? AUTOTEST_EXIT_FAILED : 0;
}
