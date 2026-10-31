/*
 * Autotest: plays missions with nobody at the controls, for a build machine and for gameplay
 * recording tests.
 *
 * Usage:
 *   -autotest [-at_mission <file.fs2>] [-at_time <seconds>] [-at_speed <1-8>] [-at_pilot <callsign>] [-at_skill <0-4>] [-playernpc]
 *   -autotest -at_menus [-at_mission <file.fs2>] [-at_shots <folder>]
 *
 * With -at_menus, autotest visits every single-player screen in turn (main hall screens, then the
 * briefings, loadout, gameplay, the in-game screens and the debriefing with the -at_mission mission or
 * the first campaign mission), with each tab, page and help overlay, and quits.  Each screen logs one
 * line, "Screen '<category>/<screen>': PASS", "FAIL <why>" or "SKIP <why>".  A screen fails if it does not
 * open or close, if its help overlay does not show, or on an Assert, Int3, Warning or crash.  A screen
 * that shows the "not in demo" popup is skipped.  -at_shots saves each screen as
 * <folder>\<category>\<screen>.bmp, logs "Shot: <category>/<screen> <file>", waits 2.5 s on each view before its shot, and keeps the cursor at 0,0.
 *
 * With no -at_mission, every single-player campaign mission runs in turn.  Each mission runs for
 * -at_time seconds of mission time (default 120) at -at_speed time compression (default 4), with
 * the player invulnerable, then ends as if the player had quit.  Popups are answered with their
 * first choice, and Assert, Error, Warning and Int3 are logged instead of stopping for a dialog.
 * The pilot is -at_pilot, or the most recently used single-player pilot.  -at_skill plays at that skill
 * level (0 very easy to 4 insane) instead of the pilot's.  Autotest never saves the pilot file, so the
 * pilot is left as it was.
 * With -playernpc the AI flies the player ship like any other ship, following the orders the mission
 * gives it ($AI Goals, add-goal), instead of nobody flying it.  A mission whose notes contain
 * "autotest: player stays still" is played without it.
 *
 * Results go to autotest.log in the game directory.  Exit code: 0 pass, 1 Error(), 2 a mission
 * failed or an Assert / Int3 was hit, 5 the watchdog found the game stuck, or a crash's exception code.
 *
 * With -record only one mission runs.  The settings are stored in the recording, so a replay of it
 * plays the mission under the same rules and ends on the same frame.  A replay drives the menus
 * itself; -autotest on a replay only turns on the logging and the no-dialog handling.
 * With no -autotest flag, and no autotest recording being replayed, none of this does anything.
 */

#ifndef _FS2_AUTOTEST_H
#define _FS2_AUTOTEST_H

extern int Autotest_harness;	// -autotest: drive the menus, log, no dialogs
extern int Autotest_rules;		// this mission plays under autotest rules (from the command line, or the recording)
extern int Autotest_player_npc;	// -playernpc: the AI flies the player ship (a rule, so it comes from the recording too)

// command line (called from parse_cmdline)
void autotest_set_cmdline(char *mission, char *time, char *speed, char *pilot, char *skill, int player_npc);
void autotest_set_menus();	// -at_menus: a tour of every single-player screen instead of the missions
void autotest_set_shots(char *dir);	// -at_shots: a screenshot of each screen of the tour

// recording header
void autotest_get_settings(int *on, int *time, int *speed);
void autotest_set_from_recording(int on, int time, int speed);

int  autotest_load_pilot();				// GS_EVENT_GAME_INIT: 1 if autotest loaded the pilot
void autotest_drive_state(int state);	// each frame from game_do_state, after replay_drive_state
void autotest_frame();						// start of game_do_frame
int  autotest_popup(char *text);			// 1 if the popup must be answered with its first choice and not shown
int  autotest_report(char *kind, char *text, char *file, int line);	// Assert / Error / Warning / Int3: 1 if logged, no dialog
void autotest_note(char *text);			// a line for autotest.log, when autotest is on
int  autotest_exit_code(int code);		// the process exit code
void autotest_main_hall_click(int *code, int *action);	// main_hall_do: the menu tour clicks a region
void autotest_not_in_demo();				// game_feature_not_in_demo_popup: the menu tour skips the screen
void autotest_cursor_pos(long *x, long *y);	// mouse_get_cursor_pos: -at_shots parks the cursor at 0,0
void autotest_poll_done();				// end of os_poll, for the stuck dump
// a device's own test script (#119)
int  autotest_mission_note(char *note);	// 1 under autotest rules, in gameplay, if the mission notes have note
float autotest_mission_seconds();		// mission seconds since gameplay started, -1 before
void autotest_check(int ok, char *what);	// logs a known answer; a wrong one fails the run

#endif
