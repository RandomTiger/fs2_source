/*
 * Autoplay: an autotest that tries to win the mission (-autotest -at_autoplay).
 *
 * The AI flies the player ship (as -playernpc), and a manager beside the game code gives it orders
 * the way the squad message menu gives them to a wingman: ai_add_ship_goal_player on the player's
 * own ai_info.  It changes no game code.  Once a second of mission time it reads the mission's
 * incomplete primary goals and pending directives (their sexp formulas) to find which ships the
 * player must destroy, disable, disarm or protect, then:
 *   - while hostiles are present, attacks: first the fighters and bombers attacking a ship to
 *     protect (bombers first), then the goal targets, then the nearest hostile;
 *   - with no hostiles, guards a ship to protect or an escort ship, or leaves the mission's own
 *     orders to fly the player (waypoints, form on wing);
 *   - once every primary goal is complete (or only the player's departure is left), waits
 *     AUTOPLAY_SETTLE_TIME and warps out the way Alt-J does.
 * The player can be killed (D-20261107-124842), unless the mission notes say the mission
 * controls vulnerability.  The manager uses game state and mission time only and makes no random
 * draws, so a replay of an autoplay recording runs it again and gets the same orders (header bit
 * AUTOTEST_HDR_AUTOPLAY, D-20261107-124846).
 *
 * The log ends each mission with one line:
 *   Autoplay result: WON | FAILED | PARTIAL n/m | STALLED | TIMEOUT, n/m primary goals complete, ...
 * WON: every valid primary goal complete, warp-out not needed.  FAILED: a primary goal failed or the
 * player died; it wins over the others.  STALLED: no goal or event changed for AUTOPLAY_STALL_TIME.
 * TIMEOUT: the time limit (AUTOPLAY_DEFAULT_TIME unless -at_time).  PARTIAL: the mission ended
 * itself with goals still incomplete.  (D-20261107-124845)
 */

#ifndef _FS2_AUTOPLAY_H
#define _FS2_AUTOPLAY_H

// the tunables, all in seconds of mission time (D-20261107-124844).  Changing one after autoplay
// recordings exist means re-recording the autoplay suite.
#define AUTOPLAY_DEFAULT_TIME		900	// the time limit when -at_time is not given
#define AUTOPLAY_STALL_TIME		300	// STALLED: no goal or event changed for this long
#define AUTOPLAY_SETTLE_TIME		5		// from the last primary goal to the warp-out
#define AUTOPLAY_WARP_TIME			30		// the warp-out has this long to end the mission
#define AUTOPLAY_DEATH_TIME		3		// from the player's death to ending the run

// why the run ended, for autoplay_result
#define AUTOPLAY_END_NONE			0
#define AUTOPLAY_END_ITSELF		1		// the mission ended (warp-out, or the mission's own end)
#define AUTOPLAY_END_TIME			2		// the time limit
#define AUTOPLAY_END_STALLED		3
#define AUTOPLAY_END_FAILED		4		// a primary goal failed
#define AUTOPLAY_END_DIED			5		// the player died
#define AUTOPLAY_END_NO_WARP		6		// the goals are done but the warp-out did not end the mission

extern int Autoplay_on;			// -at_autoplay, or the recording

void autoplay_mission_start();	// the first gameplay frame
int  autoplay_frame();				// each gameplay frame: AUTOPLAY_END_* when the run must end now, else AUTOPLAY_END_NONE
void autoplay_result(int why);	// logs the result line once per mission

#endif
