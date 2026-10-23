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
#include "MissionBrief.h"
#include "key.h"
#include "2d.h"
#include "GrInternal.h"
#include "SnazzyUI.h"
#include "Popup.h"

#ifdef _DEBUG
#include <rtcapi.h>
#include <crtdbg.h>
#endif

#define AUTOTEST_LOG				"autotest.log"
#define AUTOTEST_DEFAULT_TIME		120		// seconds of mission time
#define AUTOTEST_DEFAULT_SPEED	4			// time compression
#define AUTOTEST_MAX_TIME			30000		// Missiontime is 16.16 fixed point
#define AUTOTEST_STUCK_MS			120000	// a menu state that lasts this long is stuck
#define AUTOTEST_GIVE_UP_MS		30000		// a menu the autotest drives that lasts this long fails the mission
#define AUTOTEST_EXIT_ERROR		1
#define AUTOTEST_EXIT_FAILED		2
#define AUTOTEST_EXIT_STUCK		5
#define AUTOTEST_EXIT_BREAKPOINT	0x80000003	// a run-time check or CRT assert, as the breakpoint exception it raises

// A test mission whose notes contain this decides the player's vulnerability itself (ship-vulnerable,
// ship-invulnerable), for example so that a supernova can end it.  Campaign missions do not, so
// they keep the player invulnerable however their events change it.
#define AUTOTEST_VULNERABILITY_NOTE	"autotest: mission controls vulnerability"

// A test mission whose notes contain this needs the player ship left alone (the unflown player scans
// cargo in an EMP storm), so the AI does not fly it even with -playernpc.
#define AUTOTEST_STILL_NOTE			"autotest: player stays still"

extern fs_builtin_mission Game_builtin_mission_list[];
extern int Game_builtin_mission_count;

int Autotest_harness = 0;
int Autotest_rules = 0;
int Autotest_player_npc = 0;
static int Autotest_player_npc_asked = 0;	// -playernpc, or the recording; a mission can still refuse it

// the recording header's autotest field: bit 0 autotest rules, bit 1 -playernpc
#define AUTOTEST_HDR_RULES			1
#define AUTOTEST_HDR_PLAYER_NPC	2

static char Autotest_mission[MAX_FILENAME_LEN] = "";	// -at_mission, or "" for every campaign mission
static char Autotest_pilot[MAX_FILENAME_LEN] = "";

// the pilot autotest makes when there is none, or the one asked for will not load (#95)
#define AUTOTEST_SCRATCH_PILOT		"AutotestPilot"
static int Autotest_time = AUTOTEST_DEFAULT_TIME;
static int Autotest_speed = AUTOTEST_DEFAULT_SPEED;
static int Autotest_skill = -1;			// -at_skill, or -1 for the pilot's own

static FILE *Autotest_log = NULL;
static CRITICAL_SECTION Autotest_log_lock;
static DWORD Autotest_start_ms;

// the mission being run
static int Autotest_list_index = -1;		// index into Game_builtin_mission_list
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
static volatile int Autotest_frames = 0;			// Claude: frames seen by autotest_drive_state, for the stuck dump (D-053)
static volatile DWORD Autotest_poll_ms = 0;		// Claude: when os_poll last finished, for the stuck dump (D-053)

// Claude: os_poll finished (OsApi.cpp). Only the stuck dump reads it.
void autotest_poll_done()
{
	Autotest_poll_ms = GetTickCount();
}

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

// Claude: what the game was doing when the watchdog gave up (D-053): read from the watchdog thread, so a
// diagnostic only
static void autotest_stuck_dump(DWORD now, char *why)
{
	autotest_logf("STUCK (%s): state %d, gameseq state %d, %d frames, %d ms since the last frame, %d ms since the last os_poll, popup %s\n", why,
		Autotest_state, gameseq_get_state(), Autotest_frames, now - Autotest_frame_ms, Autotest_poll_ms ? (int)(now - Autotest_poll_ms) : -1,
		popup_active() ? "open" : "none");
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
			autotest_stuck_dump(now, "no frame for 120 s");
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
			autotest_stuck_dump(now, "the state lasted past its limit");
			autotest_fatal(AUTOTEST_EXIT_STUCK, "stuck in state %d for %d seconds", Autotest_state, (now - Autotest_state_ms) / 1000);
		}
	}
	return 0;
}

static char *autotest_tour_screen();

static LONG WINAPI autotest_crash(EXCEPTION_POINTERS *info)
{
	char *screen = autotest_tour_screen();

	autotest_logf("FATAL: crash, exception 0x%08x at 0x%08x in mission '%s'\n", info->ExceptionRecord->ExceptionCode,
		info->ExceptionRecord->ExceptionAddress, Autotest_current);
	if (screen) {
		autotest_logf("Screen '%s': FAIL crash, exception 0x%08x\n", screen, info->ExceptionRecord->ExceptionCode);
	}
	autotest_logf("FAIL (exit code 0x%08x)\n", info->ExceptionRecord->ExceptionCode);
	return EXCEPTION_CONTINUE_SEARCH;
}

#ifdef _DEBUG
// a run-time check failure (/RTC) or a CRT assert stops on a dialog, or on a breakpoint with nobody to catch it.  Under
// autotest it is logged, with the screen the menu tour is on, and the game exits with the breakpoint exception's code.
static void autotest_debug_failure(char *kind, char *text)
{
	char *screen = autotest_tour_screen();

	autotest_logf("FATAL: %s: %s in mission '%s'\n", kind, text, Autotest_current);
	if (screen) {
		autotest_logf("Screen '%s': FAIL %s\n", screen, kind);
	}
	autotest_logf("FAIL (exit code 0x%08x)\n", AUTOTEST_EXIT_BREAKPOINT);
	TerminateProcess(GetCurrentProcess(), AUTOTEST_EXIT_BREAKPOINT);
}

static int __cdecl autotest_rtc_error(int type, const wchar_t *file, int line, const wchar_t *module, const wchar_t *format, ...)
{
	wchar_t wtext[400];
	char text[512];
	va_list args;

	va_start(args, format);
	_vsnwprintf(wtext, 399, format, args);
	wtext[399] = 0;
	va_end(args);
	_snprintf(text, sizeof(text) - 1, "%S (%S line %d)", wtext, file ? file : L"no file", line);
	text[sizeof(text) - 1] = 0;
	autotest_debug_failure("run-time check failure", text);
	return 1;
}

static int __cdecl autotest_crt_report(int type, char *message, int *retval)
{
	char text[512];
	int len;

	if (type == _CRT_WARN) {
		return FALSE;
	}
	_snprintf(text, sizeof(text) - 1, "%s", message ? message : "");
	text[sizeof(text) - 1] = 0;
	len = strlen(text);
	while ((len > 0) && ((text[len - 1] == '\n') || (text[len - 1] == '\r'))) {
		text[--len] = 0;
	}
	autotest_debug_failure((type == _CRT_ASSERT) ? "CRT assert" : "CRT error", text);
	*retval = 0;
	return TRUE;
}
#endif

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
#ifdef _DEBUG
	_RTC_SetErrorFuncW(autotest_rtc_error);
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, autotest_crt_report);
#endif
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

// ManagePilot.cpp: writes the file even under autotest, which write_pilot_file does not
extern int write_pilot_file_core(player *p);

// Load the scratch pilot, first creating it as the pilot screen's "create" button does if it is
// missing or will not load.  Only ever writes AUTOTEST_SCRATCH_PILOT.plr, never a player's pilot.
static int autotest_load_scratch_pilot(char *why)
{
	strcpy(Autotest_pilot, AUTOTEST_SCRATCH_PILOT);
	Player = &Players[0];
	if (!read_pilot_file(Autotest_pilot, 1, Player)) {
		autotest_logf("%s; using the scratch pilot '%s'\n", why, Autotest_pilot);
		return 1;
	}

	memset(Player, 0, sizeof(player));
	Player->flags |= PLAYER_FLAGS_STRUCTURE_IN_USE;
	strcpy(Player->callsign, Autotest_pilot);
	init_new_pilot(Player, 1);
	if (write_pilot_file_core(Player)) {
		return 0;
	}
	memset(Player, 0, sizeof(player));
	if (read_pilot_file(Autotest_pilot, 1, Player)) {
		return 0;
	}
	autotest_logf("%s; made a new scratch pilot '%s'\n", why, Autotest_pilot);
	return 1;
}

int autotest_load_pilot()
{
	char pilots_arr[MAX_PILOTS][MAX_FILENAME_LEN];
	char *pilots[MAX_PILOTS];
	char why[MAX_FILENAME_LEN + 64];
	int count, i;

	if (!Autotest_harness || (Replay_mode == REPLAY_PLAY)) {
		return 0;
	}

	why[0] = 0;
	if (!Autotest_pilot[0]) {
		// the most recently saved single-player pilot that is not the scratch one
		count = cf_get_file_list_preallocated(MAX_PILOTS, pilots_arr, pilots, CF_TYPE_SINGLE_PLAYERS, NOX("*.plr"), CF_SORT_TIME);
		for (i = 0; i < count; i++) {
			if (stricmp(pilots[i], AUTOTEST_SCRATCH_PILOT)) {
				strcpy(Autotest_pilot, pilots[i]);
				break;
			}
		}
		if (!Autotest_pilot[0]) {
			strcpy(why, "No single-player pilot found");
		}
	}

	Player = &Players[0];
	if (!why[0] && read_pilot_file(Autotest_pilot, 1, Player)) {
		sprintf(why, "Cannot load pilot '%s'", Autotest_pilot);
	}
	if (why[0] && !autotest_load_scratch_pilot(why)) {
		autotest_fatal(AUTOTEST_EXIT_FAILED, "%s, and cannot make the scratch pilot '%s'", why, AUTOTEST_SCRATCH_PILOT);
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
	for (Autotest_list_index++; Autotest_list_index < Game_builtin_mission_count; Autotest_list_index++) {
		flags = Game_builtin_mission_list[Autotest_list_index].flags;
		if ((flags & FSB_CAMPAIGN) && !(flags & (FSB_MULTI | FSB_CAMPAIGN_FILE))) {
			strcpy(Autotest_current, Game_builtin_mission_list[Autotest_list_index].filename);
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

// -at_menus: a tour of every single-player screen instead of playing missions.  Each stop opens its screen the way the
// player does (a click in the main hall, a key, or the event its button posts) and waits for its state.  A screen with
// a demo wall opens through the player's entry, never by posting its state event, so the wall is what the tour meets
// (D-20261021-201402): HUD config is the H key on the Options screen, which the tour opens first and closes after.  Then it shows
// each of the screen's views in turn: a key press (a tab, the next page, F1 for the help overlay), then a few frames
// drawn.  Each view logs one line, "Screen '<name>': PASS", "FAIL <why>" or "SKIP <why>"; a view fails on an Assert,
// Int3 or Warning, or if its help overlay does not show.  The names are those of the reference captures,
// <category>/<screen>.  A screen that shows the "not in demo" popup instead of opening is skipped, and so are the stops
// it hosts, which name it ("not in demo (host Tech room)", D-20261022-090902).  Then the stop
// leaves the way its Back button does.  The mission screens use the -at_mission mission, or the first campaign mission.
// Not visited: the pilot screen (it deletes old pilot files, #23) and the goals screen (GS_STATE_SHOW_GOALS), which
// FS2 cannot reach: its control has no key and no tab, and its art (Objectives) is not in the FS2 data.
#define AUTOTEST_OPEN_NONE			0		// the tour is already on the screen
#define AUTOTEST_OPEN_EVENT			1		// post the event its button posts
#define AUTOTEST_OPEN_REGION		2		// click a main hall region
#define AUTOTEST_OPEN_KEY			3		// press a key
#define AUTOTEST_OPEN_COMMIT		4		// commit_pressed(), as the Commit button
#define AUTOTEST_OPEN_SCREEN		5		// the briefing screens' own buttons, which pick the next screen
#define AUTOTEST_OPEN_OPTIONS_KEY	6		// press a key on the Options screen, opened first (D-20261021-201402)
#define AUTOTEST_OPEN_BARRACKS_BUTTON	7	// press a Barracks button, Barracks opened first and closed after (D-20261022-060501)
#define AUTOTEST_OPEN_SIM_ROOM_BUTTON	8	// press a Simulator room button, the Simulator room opened first and closed after (D-20261022-065951)
#define AUTOTEST_OPEN_TECH_ROOM_BUTTON	9	// press a Tech room button, the Tech room opened first by its main hall region and closed after (D-20261022-090902)
#define AUTOTEST_TOUR_STAY			-1		// no leave event: the next stop moves on from here
#define AUTOTEST_TOUR_HELP			-1		// a view's key: F1 shows the help overlay, which must appear, and F1 again hides it
#define AUTOTEST_TOUR_FRAMES		30		// frames drawn for each view
#define AUTOTEST_TOUR_PLAY_FRAMES	120		// frames of gameplay before the in-game screens
#define AUTOTEST_TOUR_CLOSE_FRAMES	2		// frames for a help overlay to go
#define AUTOTEST_TOUR_SHOT_MS		2500	// with -at_shots, a view also shows this long first, for a screen's intro wipe or fade
#define AUTOTEST_MAX_VIEWS			9
// MainHallMenu.cpp's regions
#define AUTOTEST_REGION_BARRACKS		1
#define AUTOTEST_REGION_TECH_ROOM		3
#define AUTOTEST_REGION_OPTIONS			4
#define AUTOTEST_REGION_CAMPAIGN_ROOM	5
// Barracks.cpp's buttons
#define AUTOTEST_BARRACKS_MEDALS_BUTTON	8	// B_STATS_MEDAL_BUTTON
// ReadyRoom.cpp's Simulator room buttons
#define AUTOTEST_SIM_ROOM_CUTSCENES_BUTTON	9	// CUTSCENES_BUTTON
// TechMenu.cpp's buttons
#define AUTOTEST_TECH_ROOM_SIMULATOR_TAB	4	// SIMULATOR_TAB
#define AUTOTEST_TECH_ROOM_CREDITS_TAB		6	// CREDITS_TAB
extern int Next_screen;	// MissionScreenCommon.cpp: the briefing screen to go to
extern int Help_overlay_flags;	// ContextHelp.cpp: the help overlays showing
void barracks_button_pressed(int n);	// Barracks.cpp: as the player clicks a Barracks button
int sim_room_button_pressed(int n);	// ReadyRoom.cpp: as the player clicks a Simulator room button
int techroom_button_pressed(int num);	// TechMenu.cpp: as the player clicks a Tech room button
typedef struct autotest_view {
	char *name;		// <category>/<screen>, as the reference captures are named
	int key;		// pressed first: 0 for none, a key, or AUTOTEST_TOUR_HELP
	char *training;	// the name instead in a mission with only a briefing (training or scramble), or NULL
} autotest_view;
typedef struct autotest_stop {
	char *name;
	int open, arg;	// how its screen opens (AUTOTEST_OPEN_*), and the event, region or key
	int state, leave;
	autotest_view views[AUTOTEST_MAX_VIEWS];
} autotest_stop;
static autotest_stop Autotest_tour[] = {
	{ "Main hall",			AUTOTEST_OPEN_NONE,		0,									GS_STATE_MAIN_MENU,					AUTOTEST_TOUR_STAY,
		{ { "main_hall_pilot/main_hall", 0 }, { "main_hall_pilot/main_hall_help", AUTOTEST_TOUR_HELP } } },
	{ "Options",			AUTOTEST_OPEN_REGION,	AUTOTEST_REGION_OPTIONS,			GS_STATE_OPTIONS_MENU,				GS_EVENT_PREVIOUS_STATE,
		{ { "options/options_general", 0 }, { "options/options_multi", KEY_RIGHT }, { "options/options_detail", KEY_RIGHT } } },
	{ "Control config",		AUTOTEST_OPEN_EVENT,	GS_EVENT_CONTROL_CONFIG,			GS_STATE_CONTROL_CONFIG,			GS_EVENT_PREVIOUS_STATE,
		{ { "controls/controls_targeting", 0 }, { "controls/controls_targeting_help", AUTOTEST_TOUR_HELP }, { "controls/controls_ship", KEY_TAB },
		  { "controls/controls_weapons", KEY_TAB }, { "controls/controls_misc", KEY_TAB } } },
	{ "HUD config",			AUTOTEST_OPEN_OPTIONS_KEY,	KEY_H,						GS_STATE_HUD_CONFIG,				GS_EVENT_PREVIOUS_STATE,
		{ { "main_hall_pilot/hud_config", 0 } } },
	{ "Medals",				AUTOTEST_OPEN_BARRACKS_BUTTON,	AUTOTEST_BARRACKS_MEDALS_BUTTON,	GS_STATE_VIEW_MEDALS,				GS_EVENT_PREVIOUS_STATE,
		{ { "main_hall_pilot/medals", 0 } } },
	{ "Barracks",			AUTOTEST_OPEN_REGION,	AUTOTEST_REGION_BARRACKS,			GS_STATE_BARRACKS_MENU,				GS_EVENT_MAIN_MENU,
		{ { "main_hall_pilot/barracks", 0 }, { "main_hall_pilot/barracks_help", AUTOTEST_TOUR_HELP } } },
	{ "Tech room",			AUTOTEST_OPEN_REGION,	AUTOTEST_REGION_TECH_ROOM,			GS_STATE_TECH_MENU,					GS_EVENT_MAIN_MENU,
		{ { "campaign_techroom/techroom_ships", 0 }, { "campaign_techroom/techroom_ships_help", AUTOTEST_TOUR_HELP },
		  { "campaign_techroom/techroom_weapons", KEY_TAB }, { "campaign_techroom/techroom_weapons_help", AUTOTEST_TOUR_HELP },
		  { "campaign_techroom/techroom_species", KEY_TAB } } },
	{ "Campaign room",		AUTOTEST_OPEN_REGION,	AUTOTEST_REGION_CAMPAIGN_ROOM,		GS_STATE_CAMPAIGN_ROOM,				GS_EVENT_MAIN_MENU,
		{ { "campaign_techroom/campaign_room", 0 }, { "campaign_techroom/campaign_room_help", AUTOTEST_TOUR_HELP } } },
	{ "Simulator room",		AUTOTEST_OPEN_TECH_ROOM_BUTTON,	AUTOTEST_TECH_ROOM_SIMULATOR_TAB,	GS_STATE_SIMULATOR_ROOM,	GS_EVENT_MAIN_MENU,
		{ { "campaign_techroom/simulator_single", 0 }, { "campaign_techroom/simulator_single_help", AUTOTEST_TOUR_HELP } } },
	{ "Cutscenes",			AUTOTEST_OPEN_SIM_ROOM_BUTTON,	AUTOTEST_SIM_ROOM_CUTSCENES_BUTTON,	GS_STATE_VIEW_CUTSCENES,		GS_EVENT_MAIN_MENU,
		{ { "campaign_techroom/cutscenes", 0 } } },
	{ "Credits",			AUTOTEST_OPEN_TECH_ROOM_BUTTON,	AUTOTEST_TECH_ROOM_CREDITS_TAB,	GS_STATE_CREDITS,			GS_EVENT_MAIN_MENU,
		{ { "campaign_techroom/credits", 0 } } },
	{ "Command briefing",	AUTOTEST_OPEN_EVENT,	GS_EVENT_START_GAME,				GS_STATE_CMD_BRIEF,					AUTOTEST_TOUR_STAY,
		{ { "briefing/command_briefing", 0 }, { "briefing/command_briefing_help", AUTOTEST_TOUR_HELP } } },
	{ "Briefing",			AUTOTEST_OPEN_EVENT,	GS_EVENT_START_BRIEFING,			GS_STATE_BRIEFING,					AUTOTEST_TOUR_STAY,
		{ { "briefing/briefing", 0, "briefing/briefing_training" }, { "briefing/briefing_help", AUTOTEST_TOUR_HELP, "briefing/briefing_training_help" } } },
	{ "Ship select",		AUTOTEST_OPEN_SCREEN,	0,									GS_STATE_SHIP_SELECT,				AUTOTEST_TOUR_STAY,
		{ { "briefing/ship_select", 0 }, { "briefing/ship_select_help", AUTOTEST_TOUR_HELP } } },
	{ "Weapon select",		AUTOTEST_OPEN_SCREEN,	0,									GS_STATE_WEAPON_SELECT,				AUTOTEST_TOUR_STAY,
		{ { "briefing/weapon_loadout", 0 }, { "briefing/weapon_loadout_help", AUTOTEST_TOUR_HELP } } },
	{ "Gameplay",			AUTOTEST_OPEN_COMMIT,	0,									GS_STATE_GAME_PLAY,					AUTOTEST_TOUR_STAY,
		{ { "mission/hud_in_flight", 0 } } },
	{ "Pause",				AUTOTEST_OPEN_EVENT,	GS_EVENT_PAUSE_GAME,				GS_STATE_GAME_PAUSED,				GS_EVENT_PREVIOUS_STATE,
		{ { "mission/pause", 0 } } },
	{ "Mission log",		AUTOTEST_OPEN_KEY,		KEY_F4,								GS_STATE_MISSION_LOG_SCROLLBACK,	GS_EVENT_PREVIOUS_STATE,
		{ { "mission/mission_log_objectives", 0 }, { "mission/mission_log_messages", KEY_RIGHT }, { "mission/mission_log_events", KEY_RIGHT } } },
	{ "Hotkeys",			AUTOTEST_OPEN_KEY,		KEY_F3,								GS_STATE_HOTKEY_SCREEN,				GS_EVENT_PREVIOUS_STATE,
		{ { "mission/hotkeys", 0 }, { "mission/hotkeys_help", AUTOTEST_TOUR_HELP } } },
	{ "Gameplay help",		AUTOTEST_OPEN_KEY,		KEY_F1,								GS_STATE_GAMEPLAY_HELP,				GS_EVENT_PREVIOUS_STATE,
		{ { "mission/help_keys", 0 }, { "mission/help_keys_2", KEY_TAB }, { "mission/help_keys_3", KEY_TAB }, { "mission/help_keys_4", KEY_TAB },
		  { "mission/help_keys_5", KEY_TAB }, { "mission/help_keys_6", KEY_TAB }, { "mission/help_keys_7", KEY_TAB }, { "mission/help_keys_8", KEY_TAB },
		  { "mission/help_keys_9", KEY_TAB } } },
	{ "In-game options",	AUTOTEST_OPEN_EVENT,	GS_EVENT_OPTIONS_MENU,				GS_STATE_OPTIONS_MENU,				GS_EVENT_PREVIOUS_STATE,
		{ { "mission/options", 0 } } },
	{ "In-game controls",	AUTOTEST_OPEN_EVENT,	GS_EVENT_CONTROL_CONFIG,			GS_STATE_CONTROL_CONFIG,			GS_EVENT_PREVIOUS_STATE,
		{ { "mission/controls", 0 } } },
	{ "In-game HUD config",	AUTOTEST_OPEN_OPTIONS_KEY,	KEY_H,						GS_STATE_HUD_CONFIG,				GS_EVENT_PREVIOUS_STATE,
		{ { "mission/hud_config", 0 } } },
	{ "Debriefing",			AUTOTEST_OPEN_EVENT,	GS_EVENT_DEBRIEF,					GS_STATE_DEBRIEF,					GS_EVENT_END_GAME,
		{ { "debrief/debrief", 0 }, { "debrief/debrief_help", AUTOTEST_TOUR_HELP }, { "debrief/debrief_stats", KEY_TAB },
		  { "debrief/debrief_stats_help", AUTOTEST_TOUR_HELP }, { "debrief/debrief_stats_kills", KEY_RIGHT },
		  { "debrief/debrief_stats_alltime", KEY_RIGHT }, { "debrief/debrief_stats_alltime_kills", KEY_RIGHT } } },
};
#define AUTOTEST_TOUR_STOPS	((int)(sizeof(Autotest_tour) / sizeof(Autotest_tour[0])))

#define AUTOTEST_TOUR_GO		0		// open the stop's screen
#define AUTOTEST_TOUR_ARRIVE	1		// wait for its state
#define AUTOTEST_TOUR_LOOK		2		// show a view for a few frames
#define AUTOTEST_TOUR_CLOSE		3		// wait for a help overlay to go
#define AUTOTEST_TOUR_BACK		4		// wait to be back where the stop started
static int Autotest_menus = 0;				// -at_menus
static char Autotest_shots[MAX_PATH] = "";	// -at_shots: the folder for a screenshot of each view, or ""
static int Autotest_tour_stop = 0;
static int Autotest_tour_view = 0;
static int Autotest_tour_phase = AUTOTEST_TOUR_GO;
static int Autotest_tour_from;				// the state the stop started in
static int Autotest_tour_frames;
static DWORD Autotest_tour_ms;
static DWORD Autotest_tour_view_ms;			// when the view started showing
static int Autotest_tour_region = -1;		// a main hall region to click on the next main hall frame
static int Autotest_tour_options = 0;		// AUTOTEST_OPEN_OPTIONS_KEY, _BARRACKS_BUTTON, _SIM_ROOM_BUTTON and _TECH_ROOM_BUTTON: 1 opening the host screen (Options, Barracks, the Simulator room or the Tech room), 2 open, 3 closing it again
static int Autotest_tour_options_from;		// the state the host screen was opened from
static int Autotest_tour_options_leave;		// the event that closes the host screen
static char *Autotest_tour_walled[AUTOTEST_TOUR_STOPS];	// the screen whose "not in demo" popup kept each stop's screen from opening, or NULL
static char Autotest_tour_host_why[128];	// a hosted stop's SKIP reason
static int Autotest_tour_tab_pressed;		// AUTOTEST_OPEN_SIM_ROOM_BUTTON: the Tech room's Mission Simulator tab is pressed, on the way to the Simulator room
static int Autotest_tour_asserts, Autotest_tour_int3s, Autotest_tour_warnings;	// the counts when the view started
static int Autotest_tour_help_seen;			// the view's help overlay showed
static int Autotest_tour_not_in_demo;		// the "not in demo" popup showed during the view
static int Autotest_tour_logged;			// the view's line is in the log
static char Autotest_tour_why[128];			// why the stop's last view failed, held until the stop has gone back, or ""
static int Autotest_tour_screens = 0;		// views in the tour
static int Autotest_tour_passed = 0;
static int Autotest_tour_skipped = 0;
static int Autotest_tour_failed = 0;

void autotest_set_menus()
{
	int i, j;

	if (!Autotest_harness) {
		return;
	}
	Autotest_menus = 1;
	for (i = 0; i < AUTOTEST_TOUR_STOPS; i++) {
		for (j = 0; (j < AUTOTEST_MAX_VIEWS) && Autotest_tour[i].views[j].name; j++) {
			Autotest_tour_screens++;
		}
	}
	autotest_logf("Menu tour: %d screens\n", Autotest_tour_screens);
}

void autotest_set_shots(char *dir)
{
	if (!Autotest_harness) {
		return;
	}
	if (!dir[0] || (strlen(dir) > MAX_PATH - 80)) {
		autotest_fatal(AUTOTEST_EXIT_FAILED, "-at_shots needs a folder");
	}
	strcpy(Autotest_shots, dir);
	CreateDirectory(Autotest_shots, NULL);
	autotest_logf("Menu tour: a screenshot of each screen to '%s'\n", Autotest_shots);
}

// a view's name in this mission
static char *autotest_view_name(autotest_view *v)
{
	return (v->training && brief_only_allow_briefing()) ? v->training : v->name;
}

// the screen the tour is on, for the crash log, or NULL
static char *autotest_tour_screen()
{
	if (!Autotest_menus || (Autotest_tour_stop >= AUTOTEST_TOUR_STOPS)) {
		return NULL;
	}
	return autotest_view_name(&Autotest_tour[Autotest_tour_stop].views[Autotest_tour_view]);
}

void autotest_main_hall_click(int *code, int *action)
{
	if (Autotest_tour_region < 0) {
		return;
	}
	*code = Autotest_tour_region;
	*action = SNAZZY_CLICKED;
	Autotest_tour_region = -1;
}

void autotest_not_in_demo()
{
	if (!Autotest_harness) {
		return;
	}
	Autotest_tour_not_in_demo = 1;
}

// with -at_shots the game's cursor stays in the top left corner, so a person's real mouse cannot light a button in a shot
void autotest_cursor_pos(long *x, long *y)
{
	if (Autotest_shots[0]) {
		*x = 0;
		*y = 0;
	}
}

static void autotest_press(int key)
{
	key_mark(key, 1, 0);
	key_mark(key, 0, 0);
}

// the last frame drawn, from the back buffer, as a 24-bit BMP: <-at_shots>\<category>\<screen>.bmp.  The back buffer
// still holds it at the start of a frame: in a window the frame is copied to the screen, and in full screen the buffers swap.
static void autotest_shot(char *name)
{
	char path[MAX_PATH], *p;
	int w, h, bpp, row, x, y;
	ubyte *data, *line;
	uint pixel;
	color_gun *guns[3] = { &Gr_blue, &Gr_green, &Gr_red };
	BITMAPFILEHEADER fh;
	BITMAPINFOHEADER ih;
	FILE *fp;

	if (!Autotest_shots[0]) {
		return;
	}
	w = gr_screen.max_w;
	h = gr_screen.max_h;
	bpp = gr_screen.bytes_per_pixel;
	if ((bpp != 2) && (bpp != 4)) {
		autotest_logf("Screenshot of '%s' not saved: %d bytes per pixel\n", name, bpp);
		return;
	}

	sprintf(path, "%s\\%s.bmp", Autotest_shots, name);
	for (p = path + strlen(Autotest_shots) + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			CreateDirectory(path, NULL);
			*p = '\\';
		}
	}

	row = (w * 3 + 3) & ~3;
	data = (ubyte *)malloc(w * h * bpp);
	line = (ubyte *)malloc(row);
	fp = fopen(path, "wb");
	if (!data || !line || !fp) {
		autotest_logf("Screenshot of '%s' not saved: cannot write '%s'\n", name, path);
	} else {
		memset(data, 0, w * h * bpp);
		memset(line, 0, row);
		gr_get_region(0, w, h, data);

		memset(&fh, 0, sizeof(fh));
		memset(&ih, 0, sizeof(ih));
		fh.bfType = 0x4d42;	// "BM"
		fh.bfOffBits = sizeof(fh) + sizeof(ih);
		fh.bfSize = fh.bfOffBits + row * h;
		ih.biSize = sizeof(ih);
		ih.biWidth = w;
		ih.biHeight = h;		// bottom row first
		ih.biPlanes = 1;
		ih.biBitCount = 24;
		ih.biCompression = BI_RGB;
		fwrite(&fh, sizeof(fh), 1, fp);
		fwrite(&ih, sizeof(ih), 1, fp);
		for (y = h - 1; y >= 0; y--) {
			for (x = 0; x < w; x++) {
				pixel = (bpp == 2) ? ((ushort *)data)[y * w + x] : ((uint *)data)[y * w + x];
				for (int c = 0; c < 3; c++) {
					uint max = (uint)guns[c]->mask >> guns[c]->shift;
					line[x * 3 + c] = max ? (ubyte)((((pixel & guns[c]->mask) >> guns[c]->shift) * 255) / max) : 0;
				}
			}
			fwrite(line, row, 1, fp);
		}
		autotest_logf("Shot: %s %s\n", name, path);
	}
	if (fp) {
		fclose(fp);
	}
	free(data);
	free(line);
}

// the current view's line: PASS, or FAIL with why, or with the Asserts, Int3s and Warnings logged since it started
static void autotest_tour_log(char *why)
{
	char text[128];
	char *name = autotest_view_name(&Autotest_tour[Autotest_tour_stop].views[Autotest_tour_view]);

	if (Autotest_tour_logged) {
		return;
	}
	Autotest_tour_logged = 1;
	if (!why && ((Autotest_asserts != Autotest_tour_asserts) || (Autotest_int3s != Autotest_tour_int3s) || (Autotest_warnings != Autotest_tour_warnings))) {
		sprintf(text, "%d Assert, %d Int3, %d Warning (logged above)", Autotest_asserts - Autotest_tour_asserts, Autotest_int3s - Autotest_tour_int3s,
			Autotest_warnings - Autotest_tour_warnings);
		why = text;
	}
	if (why) {
		Autotest_tour_failed++;
		autotest_logf("Screen '%s': FAIL %s\n", name, why);
	} else {
		Autotest_tour_passed++;
		autotest_logf("Screen '%s': PASS\n", name);
	}
}

// the stop's views from the current one on, which the tour will not show
static void autotest_tour_skip(char *why)
{
	autotest_view *v = Autotest_tour[Autotest_tour_stop].views;

	for (; (Autotest_tour_view < AUTOTEST_MAX_VIEWS) && v[Autotest_tour_view].name; Autotest_tour_view++) {
		Autotest_tour_skipped++;
		autotest_logf("Screen '%s': SKIP %s\n", autotest_view_name(&v[Autotest_tour_view]), why);
	}
	Autotest_tour_view = 0;
}

static void autotest_tour_end(char *why)
{
	int i, j;

	if (why) {
		autotest_logf("FAILED: menu tour stopped at '%s': %s\n", Autotest_tour[Autotest_tour_stop].name, why);
		autotest_tour_log(why);
		for (i = Autotest_tour_stop, j = Autotest_tour_view + 1; i < AUTOTEST_TOUR_STOPS; i++, j = 0) {
			for (; (j < AUTOTEST_MAX_VIEWS) && Autotest_tour[i].views[j].name; j++) {
				Autotest_tour_failed++;
				autotest_logf("Screen '%s': FAIL not reached\n", autotest_view_name(&Autotest_tour[i].views[j]));
			}
		}
	}
	autotest_logf("Menu tour: %d screens, %d passed, %d failed, %d skipped\n", Autotest_tour_screens, Autotest_tour_passed, Autotest_tour_failed,
		Autotest_tour_skipped);
	Autotest_tour_stop = AUTOTEST_TOUR_STOPS;
	gameseq_post_event(GS_EVENT_QUIT_GAME);
}

// a view starts: the counts it is judged by, then its key
static void autotest_tour_start_view(int key)
{
	Autotest_tour_asserts = Autotest_asserts;
	Autotest_tour_int3s = Autotest_int3s;
	Autotest_tour_warnings = Autotest_warnings;
	Autotest_tour_help_seen = 0;
	Autotest_tour_not_in_demo = 0;
	Autotest_tour_logged = 0;
	Autotest_tour_frames = 0;
	Autotest_tour_view_ms = Autotest_frame_ms;
	if (key) {
		autotest_press((key == AUTOTEST_TOUR_HELP) ? KEY_F1 : key);
	}
}

static void autotest_tour_next_stop()
{
	Autotest_tour_phase = AUTOTEST_TOUR_GO;
	Autotest_tour_view = 0;
	if (++Autotest_tour_stop == AUTOTEST_TOUR_STOPS) {
		Autotest_tour_stop--;
		autotest_tour_end(NULL);
	}
}

// the current view has been shown: on to the next view, or leave the screen
static void autotest_tour_view_done(char *why)
{
	autotest_stop *s = &Autotest_tour[Autotest_tour_stop];

	if ((Autotest_tour_view + 1 < AUTOTEST_MAX_VIEWS) && s->views[Autotest_tour_view + 1].name) {
		autotest_tour_log(why);
		Autotest_tour_view++;
		autotest_tour_start_view(s->views[Autotest_tour_view].key);
		Autotest_tour_phase = AUTOTEST_TOUR_LOOK;
		return;
	}
	if (s->leave == AUTOTEST_TOUR_STAY) {
		autotest_tour_log(why);
		autotest_tour_next_stop();
		return;
	}
	// the last view is judged once the screen has closed, so leaving it counts too
	strcpy(Autotest_tour_why, why ? why : "");
	gameseq_post_event(s->leave);
	Autotest_tour_ms = Autotest_frame_ms;
	Autotest_tour_phase = AUTOTEST_TOUR_BACK;
}

// a host-screen step's host whose own stop met a "not in demo" popup instead of opening: the screen that showed it, or NULL
// (D-20261022-090902).  The host is the nearest stop before this one with the host's state.
static char *autotest_tour_host_walled(autotest_stop *s)
{
	int i, host;

	switch (s->open) {
		case AUTOTEST_OPEN_OPTIONS_KEY:			host = GS_STATE_OPTIONS_MENU;		break;
		case AUTOTEST_OPEN_BARRACKS_BUTTON:		host = GS_STATE_BARRACKS_MENU;		break;
		case AUTOTEST_OPEN_SIM_ROOM_BUTTON:		host = GS_STATE_SIMULATOR_ROOM;		break;
		case AUTOTEST_OPEN_TECH_ROOM_BUTTON:	host = GS_STATE_TECH_MENU;			break;
		default:								return NULL;
	}
	for (i = Autotest_tour_stop - 1; i >= 0; i--) {
		if (Autotest_tour[i].state == host) {
			return Autotest_tour_walled[i];
		}
	}
	return NULL;
}

// each frame, in place of the mission driving, before the state's own frame (so a key pressed here is read this frame)
static void autotest_tour_frame(int state)
{
	autotest_stop *s;
	autotest_view *v;
	int frames;

	if (Autotest_tour_stop >= AUTOTEST_TOUR_STOPS) {
		return;
	}
	s = &Autotest_tour[Autotest_tour_stop];
	v = &s->views[Autotest_tour_view];

	switch (Autotest_tour_phase) {
		case AUTOTEST_TOUR_GO:
			// the tour starts from the main hall
			if ((Autotest_tour_stop == 0) && (state != GS_STATE_MAIN_MENU)) {
				return;
			}
			// the host screen (Options, Barracks, the Simulator room or the Tech room) the stop before opened on its way closes first, back to where it was opened from
			if (Autotest_tour_options >= 2) {
				if (state != Autotest_tour_options_from) {
					if (Autotest_tour_options == 2) {
						gameseq_post_event(Autotest_tour_options_leave);
						Autotest_tour_options = 3;
						Autotest_tour_ms = Autotest_frame_ms;
					} else if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
						autotest_tour_end("the host screen did not go back");
					}
					return;
				}
				Autotest_tour_options = 0;
			}
			// a host screen whose own stop met the "not in demo" popup: the stop is skipped, its host not opened again
			if ((Autotest_tour_options == 0) && autotest_tour_host_walled(s)) {
				Autotest_tour_walled[Autotest_tour_stop] = autotest_tour_host_walled(s);
				sprintf(Autotest_tour_host_why, "not in demo (host %s)", Autotest_tour_walled[Autotest_tour_stop]);
				autotest_tour_skip(Autotest_tour_host_why);
				autotest_tour_next_stop();
				return;
			}
			// a key on the Options screen: open it first, as the player does (the main hall's region; in a mission, its event)
			if ((s->open == AUTOTEST_OPEN_OPTIONS_KEY) && (state != GS_STATE_OPTIONS_MENU)) {
				if (Autotest_tour_options == 0) {
					Autotest_tour_options = 1;
					Autotest_tour_options_from = state;
					Autotest_tour_options_leave = GS_EVENT_PREVIOUS_STATE;
					Autotest_tour_ms = Autotest_frame_ms;
					if (state == GS_STATE_MAIN_MENU) {
						Autotest_tour_region = AUTOTEST_REGION_OPTIONS;
					} else {
						gameseq_post_event(GS_EVENT_OPTIONS_MENU);
					}
				} else if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("the Options screen did not open");
				}
				return;
			}
			// a Barracks button: open Barracks first through its main hall region, as the player does; closed to the main hall after
			if ((s->open == AUTOTEST_OPEN_BARRACKS_BUTTON) && (state != GS_STATE_BARRACKS_MENU)) {
				if (Autotest_tour_options == 0) {
					if (state != GS_STATE_MAIN_MENU) {
						autotest_tour_end("Barracks opens only from the main hall");
						return;
					}
					Autotest_tour_options = 1;
					Autotest_tour_options_from = state;
					Autotest_tour_options_leave = GS_EVENT_MAIN_MENU;
					Autotest_tour_ms = Autotest_frame_ms;
					Autotest_tour_region = AUTOTEST_REGION_BARRACKS;
				} else if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("the Barracks screen did not open");
				}
				return;
			}
			// a Simulator room button: open the Simulator room first, as its own stop does (the Tech room by its main hall region,
			// then its Mission Simulator tab); closed to the main hall after
			if ((s->open == AUTOTEST_OPEN_SIM_ROOM_BUTTON) && (state != GS_STATE_SIMULATOR_ROOM)) {
				if (Autotest_tour_options == 0) {
					if (state != GS_STATE_MAIN_MENU) {
						autotest_tour_end("the Simulator room opens only from the main hall");
						return;
					}
					Autotest_tour_options = 1;
					Autotest_tour_options_from = state;
					Autotest_tour_options_leave = GS_EVENT_MAIN_MENU;
					Autotest_tour_ms = Autotest_frame_ms;
					Autotest_tour_region = AUTOTEST_REGION_TECH_ROOM;
					Autotest_tour_tab_pressed = 0;
				} else if ((state == GS_STATE_TECH_MENU) && !Autotest_tour_tab_pressed) {
					Autotest_tour_tab_pressed = 1;
					techroom_button_pressed(AUTOTEST_TECH_ROOM_SIMULATOR_TAB);
				} else if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("the Simulator room did not open");
				}
				return;
			}
			// a Tech room button: open the Tech room first through its main hall region, as the player does; closed to the main hall after
			if ((s->open == AUTOTEST_OPEN_TECH_ROOM_BUTTON) && (state != GS_STATE_TECH_MENU)) {
				if (Autotest_tour_options == 0) {
					if (state != GS_STATE_MAIN_MENU) {
						autotest_tour_end("the Tech room opens only from the main hall");
						return;
					}
					Autotest_tour_options = 1;
					Autotest_tour_options_from = state;
					Autotest_tour_options_leave = GS_EVENT_MAIN_MENU;
					Autotest_tour_ms = Autotest_frame_ms;
					Autotest_tour_region = AUTOTEST_REGION_TECH_ROOM;
				} else if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("the Tech room did not open");
				}
				return;
			}
			if (Autotest_tour_options == 1) {
				Autotest_tour_options = 2;
			}
			Autotest_tour_from = state;
			Autotest_tour_ms = Autotest_frame_ms;
			Autotest_tour_phase = AUTOTEST_TOUR_ARRIVE;
			autotest_tour_start_view(0);
			// already there (the screen before opened it): look at it from this frame
			if (state == s->state) {
				autotest_tour_frame(state);
				return;
			}
			switch (s->open) {
				case AUTOTEST_OPEN_EVENT:
					if (s->arg == GS_EVENT_START_GAME) {
						if (!autotest_next_mission()) {
							autotest_tour_end("no mission for the mission screens");
							return;
						}
						strncpy(Game_current_mission_filename, Autotest_current, MAX_FILENAME_LEN - 1);
						Game_mode &= ~(GM_CAMPAIGN_MODE);
						autotest_logf("Menu tour: mission screens with '%s'\n", Autotest_current);
					}
					gameseq_post_event(s->arg);
					break;

				case AUTOTEST_OPEN_REGION:
					Autotest_tour_region = s->arg;
					break;

				case AUTOTEST_OPEN_KEY:
				case AUTOTEST_OPEN_OPTIONS_KEY:
					autotest_press(s->arg);
					break;

				case AUTOTEST_OPEN_COMMIT:
					commit_pressed();
					break;

				case AUTOTEST_OPEN_BARRACKS_BUTTON:
					barracks_button_pressed(s->arg);
					break;

				case AUTOTEST_OPEN_SIM_ROOM_BUTTON:
					sim_room_button_pressed(s->arg);
					break;

				case AUTOTEST_OPEN_TECH_ROOM_BUTTON:
					techroom_button_pressed(s->arg);
					break;

				case AUTOTEST_OPEN_SCREEN:
					// as the Ship Selection and Weapon Loadout buttons: posting the event alone leaves Next_screen on the
					// briefing, and the screen goes straight back to it
					if (brief_only_allow_briefing()) {
						autotest_tour_skip("the mission has no ship or weapon selection (training or scramble)");
						autotest_tour_next_stop();
						return;
					}
					Next_screen = (s->state == GS_STATE_SHIP_SELECT) ? ON_SHIP_SELECT : ON_WEAPON_SELECT;
					break;

				default:
					autotest_tour_end("its screen is not open");
					break;
			}
			return;

		case AUTOTEST_TOUR_ARRIVE:
			// only the full game has it
			if (Autotest_tour_not_in_demo && (state == Autotest_tour_from)) {
				Autotest_tour_region = -1;
				Autotest_tour_walled[Autotest_tour_stop] = s->name;	// the stops it hosts are skipped too
				autotest_tour_skip("not in demo");
				autotest_tour_next_stop();
				return;
			}
			// a mission with no command briefing goes straight on to the briefing
			if ((s->state == GS_STATE_CMD_BRIEF) && (state == GS_STATE_BRIEFING)) {
				autotest_tour_skip("the mission has no command briefing");
				autotest_tour_next_stop();
				return;
			}
			if (state != s->state) {
				if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("its screen did not open");
				}
				return;
			}
			autotest_logf("Menu tour: '%s'\n", s->name);
			Autotest_tour_region = -1;
			Autotest_tour_frames = 0;
			Autotest_tour_view_ms = Autotest_frame_ms;
			Autotest_tour_phase = AUTOTEST_TOUR_LOOK;
			return;

		case AUTOTEST_TOUR_LOOK:
			if (state != s->state) {
				autotest_tour_end("its screen closed by itself");
				return;
			}
			if (Autotest_tour_not_in_demo) {
				Autotest_tour_skipped++;
				autotest_logf("Screen '%s': SKIP not in demo\n", autotest_view_name(v));
				Autotest_tour_logged = 1;
				autotest_tour_view_done(NULL);
				return;
			}
			frames = ((state == GS_STATE_GAME_PLAY) && (Autotest_tour_view == 0)) ? AUTOTEST_TOUR_PLAY_FRAMES : AUTOTEST_TOUR_FRAMES;
			if (++Autotest_tour_frames < frames) {
				return;
			}
			if (Autotest_shots[0] && (Autotest_frame_ms - Autotest_tour_view_ms < AUTOTEST_TOUR_SHOT_MS)) {
				return;
			}
			autotest_shot(autotest_view_name(v));
			if (v->key == AUTOTEST_TOUR_HELP) {
				Autotest_tour_help_seen = (Help_overlay_flags != 0);
				autotest_press(KEY_F1);
				Autotest_tour_frames = 0;
				Autotest_tour_phase = AUTOTEST_TOUR_CLOSE;
				return;
			}
			autotest_tour_view_done(NULL);
			return;

		case AUTOTEST_TOUR_CLOSE:
			if (state != s->state) {
				autotest_tour_end("its screen closed by itself");
				return;
			}
			if (++Autotest_tour_frames < AUTOTEST_TOUR_CLOSE_FRAMES) {
				return;
			}
			autotest_tour_view_done(!Autotest_tour_help_seen ? "no help overlay showed" : (Help_overlay_flags ? "its help overlay did not close" : NULL));
			return;

		case AUTOTEST_TOUR_BACK:
			// a pushed screen returns to where it started; the others go back to the main hall
			if (state != ((s->leave == GS_EVENT_PREVIOUS_STATE) ? Autotest_tour_from : GS_STATE_MAIN_MENU)) {
				if (Autotest_frame_ms - Autotest_tour_ms > AUTOTEST_GIVE_UP_MS) {
					autotest_tour_end("did not go back");
				}
				return;
			}
			autotest_tour_log(Autotest_tour_why[0] ? Autotest_tour_why : NULL);
			autotest_tour_next_stop();
			return;
	}
}

void autotest_drive_state(int state)
{
	if (!Autotest_harness) {
		return;
	}

	Autotest_frame_ms = GetTickCount();
	Autotest_frames++;
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
