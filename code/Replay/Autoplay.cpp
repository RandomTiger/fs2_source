/*
 * Autoplay: an autotest that tries to win the mission.  See Autoplay.h.
 */

#include <stdio.h>
#include <string.h>

#include "pstypes.h"
#include "Autoplay.h"
#include "Autotest.h"
#include "Freespace.h"
#include "GameSequence.h"
#include "SystemVars.h"
#include "Object.h"
#include "ship.h"
#include "ai.h"
#include "AIGoals.h"
#include "Player.h"
#include "MissionParse.h"
#include "MissionGoals.h"
#include "MissionCampaign.h"
#include "Sexp.h"
#include "MissionMessage.h"
#include "AWACS.h"
#include "LinkList.h"
#include "vecmat.h"
#include "HUDtarget.h"
#include "Physics.h"
#include "HUDsquadmsg.h"
#include "MissionScreenCommon.h"
#include "Supernova.h"
#include "Afterburner.h"
#include "cfile.h"

int Autoplay_on = 0;
int Autoplay_scan = 0;
int Autoplay_steering = 0;
static int Ap_steer_obj = -1;				// the ship autoplay steers the player to scan, -2 - list for a directive's waypoint, or -1
static int Ap_steer_sig;
static int Ap_steer_logged = -1;

#define AP_NONE				0			// no order (AI_GOAL_* modes are bits)
#define AP_MAX_DEPTH		24			// sexp nesting, event-true and goal-true links

// what a mission goal or directive wants done to a ship
#define AP_DESTROY			(1 << 0)
#define AP_PROTECT			(1 << 1)
#define AP_DISABLE			(1 << 2)
#define AP_DISARM			(1 << 3)
#define AP_SUBSYS			(1 << 4)	// destroy the subsystem in Ap_subsys
#define AP_SCAN				(1 << 5)	// scan its cargo (is-cargo-known), with Autoplay_scan

// attack tiers, lowest first
#define AP_TIER_SCAN						-1	// scan a ship's cargo (Autoplay_scan): before defending, or ships leave unscanned while the player chases fighters (sm2-08a)
#define AP_TIER_BOMBER_ON_PROTECT		0
#define AP_TIER_FIGHTER_ON_PROTECT		1
#define AP_TIER_GOAL_SMALL				3	// disable, disarm, subsystem, or destroy a small ship
#define AP_TIER_GOAL_BIG					4	// destroy a big ship
#define AP_TIER_FIGHTER					5
#define AP_TIER_OTHER						6
#define AP_TIER_NONE						99

static int Ap_active = 0;					// this mission: autoplay on, and the AI flies the player
static int Ap_role[MAX_SHIPS];
static int Ap_marked;							// ships other than the player that ap_mark_ship marked, for ap_scan's open directives
static char Ap_subsys[MAX_SHIPS][32];
static int Ap_event_seen[MAX_MISSION_EVENTS];
static int Ap_goal_seen[MAX_GOALS];
static int Ap_goal_departs;				// the goal being read wants the player to depart
static int Ap_open_dirs;					// with Autoplay_scan: current directives still open that want more than the player's departure
static int Ap_open_dir;						// the first open directive (an event), or -1
static int Ap_open_dir_logged;
static int Ap_steer_near = -1;			// with Autoplay_scan: the object autoplay steers the player near for an arrival the goals need, or -1
static int Ap_near_logged = -1;			// the ship last logged for Ap_steer_near
static int Ap_steer_pt;					// the point autoplay steers to in a waypoint list (from 0)
static int Ap_fly_pt;						// its point (from 0)
static int Ap_fly_wp;						// with Autoplay_scan: a waypoint list an open directive wants the player near, ( < ( distance <player> "list:n" ) N ), or -1
static int Ap_prev_objectives;			// with Autoplay_scan: ap_open_objectives at the last think with no failure pending
static int Ap_hostiles, Ap_attrition, Ap_goal_hull;	// ap_attrition
static char Ap_death_logged[MAX_SHIPS];	// ap_attrition logged this protected ship's death

static fix Ap_next_think;
static int Ap_progress;					// goals complete plus events true, when it last changed
static fix Ap_progress_time;
static fix Ap_done_time;					// when the goals left nothing but departing, or -1
static int Ap_warping;
static fix Ap_warp_time;
static int Ap_died;
static fix Ap_died_time;
static int Ap_reported;
static int Ap_failed_seen;					// failed primary goals already waited on
static int Ap_failed_ok;					// failed primary goals that new objectives replaced
static fix Ap_fail_time;					// when a new primary goal failed, or -1
static int Ap_fail_objectives;			// open objectives then
static char Ap_rtb_event[MAX_MISSION_EVENTS];	// 1: the event sends Command's "return to base"; 2: a chained "Return To Base" directive
static char Ap_rtb_skip[MAX_MISSION_EVENTS];	// with Autoplay_scan: a "return to base" event passed over while goals were left, logged
static int Ap_rtb_left;						// with Autoplay_scan: goals left at the last scan
static int Ap_rtb;							// that event has fired: a success (Tom, 2026-11-07)
static int Ap_zone_wp, Ap_zone_pt;		// the warp zone: a waypoint list and point, or -1
static int Ap_zone_ship;					// or a ship, or -1
static float Ap_zone_dist;					// the player may warp out only this close to it
static int Ap_zone_logged;
static int Ap_zone_fix;					// the zone event fixes a broken warp drive (loop2-2: break-warp, then fix-warp near the jump node)
#define AP_NOVA_LEAD	150
#define AP_DEPART_WAIT	90			// s an RTB warp-out waits for a ship a primary goal wants gone
static int Ap_nova_at = -1;				// with Autoplay_scan: the ( has-time-elapsed N ) of an event still to start a supernova, or -1
static int Ap_zone_after;					// the zone event's ( has-time-elapsed N ), or 0

static int Ap_order_sig;					// the signature of the manager's goal on the player, or -1
static int Ap_order_mode, Ap_order_submode, Ap_order_ship;
static int Ap_logged_mode, Ap_logged_ship;

static float ap_time()
{
	return f2fl(Missiontime);
}

static int ap_player_shipnum()
{
	return Player_obj->instance;
}

// --- reading the goals ---

static void ap_mark_ship(int shipnum, int role, char *subsys)
{
	Ap_role[shipnum] |= role;
	if (shipnum != Player_obj->instance) {
		Ap_marked++;
	}
	if (subsys && !Ap_subsys[shipnum][0]) {
		strncpy(Ap_subsys[shipnum], subsys, sizeof(Ap_subsys[shipnum]) - 1);
	}
}

// a ship or wing name; departing means a has-departed that wants the ship to leave
static int Ap_arrival_depth;
static int Ap_arrive_wait;	// with Autoplay_scan: ships or wings the goals need still to arrive, walked this scan
static int Ap_goto = -1;		// with Autoplay_scan: a ship an arrival the goals need waits for the player to near, or -1
static int Ap_reinf = -1;		// with Autoplay_scan: a reinforcement the goals need, or -1
static int Ap_reinf_dirs;		// with Autoplay_scan: open directives that want that reinforcement arrived (loop2-1's "Call In Lambda 1")
static int Ap_reinf_near = -1;	// with Autoplay_scan: the ship such a directive wants the player near, or -1
static float Ap_reinf_dist;		// and how near
#define AP_MAX_FLEE		4
static int Ap_flee[AP_MAX_FLEE], Ap_flee_n;	// with Autoplay_scan: objects a ready chained event wants the player's wing far from
static int Ap_fleeing;						// autoplay steers the player away from them
static int Ap_flee_sig[MAX_SHIPS];			// wingmen ordered to form on the player, by signature
static void ap_walk(int n, int neg, int depth);

// the reinforcement of that name the player can still call, or -1
static int ap_reinf_index(char *name)
{
	int i;

	for (i = 0; i < Num_reinforcements; i++) {
		if (!stricmp(Reinforcements[i].name, name) && (Reinforcements[i].num_uses < Reinforcements[i].uses)) {
			return i;
		}
	}
	return -1;
}

// with Autoplay_scan: a ship or wing the goals need that has not arrived; what brings it in is part of
// the goal (loop2-1's Grall arrives once the player nears a buoy, which arrives once a wing has, which
// arrives once the player nears another buoy)
static void ap_arrive(char *name)
{
	p_object *pobjp;
	int w;

	if (!Autoplay_scan || (Ap_arrival_depth >= 4) || (ship_name_lookup(name, 1) >= 0)) {
		return;
	}
	for (pobjp = GET_FIRST(&ship_arrival_list); pobjp != END_OF_LIST(&ship_arrival_list); pobjp = GET_NEXT(pobjp)) {
		if (!stricmp(pobjp->name, name)) {
			if (!(pobjp->flags & P_SF_REINFORCEMENT)) {
				Ap_arrive_wait++;
				Ap_arrival_depth++;
				ap_walk(pobjp->arrival_cue, 0, 0);
				Ap_arrival_depth--;
			}
			return;
		}
	}
	w = wing_name_lookup(name, 1);
	if ((w >= 0) && (Wings[w].current_count == 0) && (Wings[w].total_arrived_count == 0) && !(Wings[w].flags & WF_REINFORCEMENT)) {
		Ap_arrive_wait++;
		Ap_arrival_depth++;
		ap_walk(Wings[w].arrival_cue, 0, 0);
		Ap_arrival_depth--;
	}
}

// 1 if the sexp names that reinforcement under has-arrived-delay
static int ap_wants_arrived(int n, char *name, int depth)
{
	int a;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return 0;
	}
	if (CAR(n) != -1) {
		return ap_wants_arrived(CAR(n), name, depth + 1) || ap_wants_arrived(CDR(n), name, depth + 1);
	}
	if ((Sexp_nodes[n].subtype == SEXP_ATOM_OPERATOR) && (find_operator(CTEXT(n)) == OP_HAS_ARRIVED_DELAY)) {
		for (a = (CDR(n) != -1) ? CDR(CDR(n)) : -1; a != -1; a = CDR(a)) {
			if (!stricmp(CTEXT(a), name)) {
				return 1;
			}
		}
	}
	return ap_wants_arrived(CDR(n), name, depth + 1);
}

static void ap_mark(char *name, int role, char *subsys, int departing)
{
	int s, w, i;

	// a reinforcement the goals want kept or gone: autoplay calls it in (loop2-1's Lambda 1)
	if (Autoplay_scan && (role == AP_PROTECT) && (Ap_reinf < 0) && (ship_name_lookup(name, 1) < 0)) {
		Ap_reinf = ap_reinf_index(name);
	}
	s = ship_name_lookup(name, 1);
	if (s >= 0) {
		ap_mark_ship(s, role, subsys);
		if (departing && (s == ap_player_shipnum())) {
			Ap_goal_departs = 1;
		}
		return;
	}
	w = wing_name_lookup(name, 1);
	if (w >= 0) {
		for (i = 0; i < Wings[w].current_count; i++) {
			ap_mark_ship(Wings[w].ship_index[i], role, subsys);
		}
		if (departing && (Player_ship->wingnum == w)) {
			Ap_goal_departs = 1;
		}
		return;
	}
	// a ship that must get away but has not arrived: what brings it in is part of the goal (sm2-05a's
	// Galatea arrives on "Area patrolled", the sentries and neutrals destroyed or departed)
	if (Autoplay_scan && departing && (role == AP_PROTECT) && (Ap_arrival_depth < 2)) {
		p_object *pobjp;

		for (pobjp = GET_FIRST(&ship_arrival_list); pobjp != END_OF_LIST(&ship_arrival_list); pobjp = GET_NEXT(pobjp)) {
			if (!stricmp(pobjp->name, name)) {
				Ap_arrival_depth++;
				ap_walk(pobjp->arrival_cue, 0, 0);
				Ap_arrival_depth--;
				break;
			}
		}
	}
}

static void ap_mark_list(int n, int role, int departing)
{
	for (; n != -1; n = CDR(n)) {
		if (CAR(n) == -1) {
			ap_mark(CTEXT(n), role, NULL, departing);
		}
	}
}

static int ap_event_index(char *name)
{
	int i;

	for (i = 0; i < Num_mission_events; i++) {
		if (!stricmp(Mission_events[i].name, name)) {
			return i;
		}
	}
	return -1;
}

static int ap_goal_index(char *name)
{
	int i;

	for (i = 0; i < Num_goals; i++) {
		if (!stricmp(Mission_goals[i].name, name)) {
			return i;
		}
	}
	return -1;
}

// n is a list node (its expression is CAR) or an operator atom (its arguments are CDR)
static void ap_walk(int n, int neg, int depth)
{
	int op, args, i;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return;
	}
	if (CAR(n) != -1) {
		ap_walk(CAR(n), neg, depth + 1);
		return;
	}
	if (Sexp_nodes[n].subtype != SEXP_ATOM_OPERATOR) {
		return;
	}

	op = find_operator(CTEXT(n));
	args = CDR(n);
	switch (op) {
		case OP_NOT:
			neg = !neg;
			break;

		case OP_IS_DESTROYED_DELAY:
		case OP_PERCENT_SHIPS_DESTROYED:
		case OP_DESTROYED_DEPARTED_DELAY:
			if (args != -1) {
				ap_mark_list(CDR(args), neg ? AP_PROTECT : AP_DESTROY, 0);
			}
			return;

		case OP_IS_DISABLED_DELAY:
			if (args != -1) {
				ap_mark_list(CDR(args), neg ? AP_PROTECT : AP_DISABLE, 0);
			}
			return;

		case OP_IS_DISARMED_DELAY:
			if (args != -1) {
				ap_mark_list(CDR(args), neg ? AP_PROTECT : AP_DISARM, 0);
			}
			return;

		case OP_IS_SUBSYSTEM_DESTROYED_DELAY:
			if ((args != -1) && (CDR(args) != -1)) {
				ap_mark(CTEXT(args), neg ? AP_PROTECT : AP_SUBSYS, neg ? NULL : CTEXT(CDR(args)), 0);
			}
			return;

		case OP_HAS_DEPARTED_DELAY:
		case OP_PERCENT_SHIPS_DEPARTED:
			// a friendly that must get away, or (negated) a hostile that must not
			if (args != -1) {
				ap_mark_list(CDR(args), neg ? AP_DESTROY : AP_PROTECT, !neg);
			}
			return;

		case OP_HAS_DOCKED_DELAY:
			// with Autoplay_scan, both ships of a docking the goals want are kept (sm2-06a's transports and
			// the hostile Shivan cargo they take)
			if (Autoplay_scan && !neg && (args != -1) && (CDR(args) != -1)) {
				ap_mark(CTEXT(args), AP_PROTECT, NULL, 0);
				ap_mark(CTEXT(CDR(args)), AP_PROTECT, NULL, 0);
			}
			return;

		case OP_IS_CARGO_KNOWN:
		case OP_CARGO_KNOWN_DELAY:
			// scan: close in, face it and keep it targeted (Tom, 2026-11-07); only with the header bit
			if (Autoplay_scan && !neg && (args != -1)) {
				ap_mark_list((op == OP_CARGO_KNOWN_DELAY) ? CDR(args) : args, AP_SCAN, 0);
			}
			return;

		case OP_HAS_ARRIVED_DELAY:
			if (Autoplay_scan && !neg && (args != -1)) {
				for (i = CDR(args); i != -1; i = CDR(i)) {
					if (CAR(i) == -1) {
						ap_arrive(CTEXT(i));
					}
				}
			}
			return;

		case OP_LESS_THAN:
			// in an arrival cue, ( < ( distance <player> X ) N ): the player must near X
			if (Autoplay_scan && !neg && (Ap_arrival_depth > 0) && (args != -1) && (CAR(args) != -1)) {
				int d = CAR(args);

				if ((Sexp_nodes[d].subtype == SEXP_ATOM_OPERATOR) && (find_operator(CTEXT(d)) == OP_DISTANCE) && (CDR(d) != -1) && (CDR(CDR(d)) != -1)) {
					char *a = CTEXT(CDR(d)), *b = CTEXT(CDR(CDR(d))), *x = NULL;

					if (!stricmp(a, Player_ship->ship_name)) {
						x = b;
					} else if (!stricmp(b, Player_ship->ship_name)) {
						x = a;
					}
					if (x != NULL) {
						i = ship_name_lookup(x, 1);
						if (i < 0) {
							ap_arrive(x);
						} else if ((Ap_goto < 0) && (i != ap_player_shipnum())) {
							Ap_goto = i;
						}
						return;
					}
				}
			}
			break;

		case OP_EVENT_TRUE_DELAY:
			i = (args != -1) ? ap_event_index(CTEXT(args)) : -1;
			if ((i >= 0) && !Ap_event_seen[i]) {
				Ap_event_seen[i] = 1;
				ap_walk(Mission_events[i].formula, neg, depth + 1);
			}
			return;

		case OP_GOAL_TRUE_DELAY:
			i = (args != -1) ? ap_goal_index(CTEXT(args)) : -1;
			if ((i >= 0) && !Ap_goal_seen[i]) {
				Ap_goal_seen[i] = 1;
				ap_walk(Mission_goals[i].formula, neg, depth + 1);
			}
			return;
	}

	for (; args != -1; args = CDR(args)) {
		if (CAR(args) != -1) {
			ap_walk(args, neg, depth + 1);
		}
	}
}

// with Autoplay_scan: 1 if condition n is only ( > ( distance <player or player's wing> X ) N ) checks,
// joined by and; adds each X present to Ap_flee (sm2-10's "BANG BANG": the meson bombs go off only once
// all of Alpha is 3500 m clear)
static int ap_flee_walk(int n, int depth)
{
	int op, args, d, s;
	char *a, *b, *x = NULL, *wing;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return 0;
	}
	if (CAR(n) != -1) {
		return ap_flee_walk(CAR(n), depth + 1);
	}
	if (Sexp_nodes[n].subtype != SEXP_ATOM_OPERATOR) {
		return 0;
	}
	op = find_operator(CTEXT(n));
	args = CDR(n);
	if (op == OP_AND) {
		for (; args != -1; args = CDR(args)) {
			if (!ap_flee_walk(args, depth + 1)) {
				return 0;
			}
		}
		return 1;
	}
	if ((op != OP_GREATER_THAN) || (args == -1) || (CAR(args) == -1)) {
		return 0;
	}
	d = CAR(args);
	if ((Sexp_nodes[d].subtype != SEXP_ATOM_OPERATOR) || (find_operator(CTEXT(d)) != OP_DISTANCE) || (CDR(d) == -1) || (CDR(CDR(d)) == -1)) {
		return 0;
	}
	a = CTEXT(CDR(d));
	b = CTEXT(CDR(CDR(d)));
	wing = (Player_ship->wingnum >= 0) ? Wings[Player_ship->wingnum].name : NULL;
	if (!stricmp(a, Player_ship->ship_name) || ((wing != NULL) && !stricmp(a, wing))) {
		x = b;
	} else if (!stricmp(b, Player_ship->ship_name) || ((wing != NULL) && !stricmp(b, wing))) {
		x = a;
	}
	if (x == NULL) {
		return 0;
	}
	s = ship_name_lookup(x, 1);
	if ((s >= 0) && (Ships[s].objnum >= 0) && (Ap_flee_n < AP_MAX_FLEE)) {
		Ap_flee[Ap_flee_n++] = Ships[s].objnum;
	}
	return 1;
}

// the sexp: *nova if it starts a supernova, *t its ( has-time-elapsed N ) if any
static void ap_nova_walk(int n, int depth, int *nova, int *t)
{
	int op;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return;
	}
	if (CAR(n) != -1) {
		ap_nova_walk(CAR(n), depth + 1, nova, t);
		ap_nova_walk(CDR(n), depth + 1, nova, t);
		return;
	}
	if (Sexp_nodes[n].subtype == SEXP_ATOM_OPERATOR) {
		op = find_operator(CTEXT(n));
		if (op == OP_SUPERNOVA_START) {
			*nova = 1;
		} else if ((op == OP_HAS_TIME_ELAPSED) && (CDR(n) != -1) && (CAR(CDR(n)) == -1)) {
			*t = atoi(CTEXT(CDR(n)));
		}
	}
	ap_nova_walk(CDR(n), depth + 1, nova, t);
}

// with Autoplay_scan: Ap_nova_at from the events still to fire (sm3-10's "May continue": supernova at 710 s)
static void ap_nova_find()
{
	int i, nova, t;

	Ap_nova_at = -1;
	for (i = 0; i < Num_mission_events; i++) {
		if (!Mission_events[i].result && (Mission_events[i].formula != -1)) {
			nova = 0;
			t = -1;
			ap_nova_walk(Mission_events[i].formula, 0, &nova, &t);
			if (nova && (t >= 0) && ((Ap_nova_at < 0) || (t < Ap_nova_at))) {
				Ap_nova_at = t;
			}
		}
	}
}

// with Autoplay_scan: fills Ap_flee from the chained events ready to be checked
static void ap_flee_find()
{
	int i, f, n0;

	Ap_flee_n = 0;
	for (i = 1; Autoplay_scan && (i < Num_mission_events); i++) {
		if (Mission_events[i].result || (Mission_events[i].formula == -1) || (Mission_events[i].chain_delay < 0)) {
			continue;
		}
		if (!Mission_events[i - 1].result || ((fix)Mission_events[i - 1].timestamp + i2f(Mission_events[i].chain_delay) > Missiontime)) {
			continue;
		}
		f = Mission_events[i].formula;
		if (CAR(f) != -1) {
			f = CAR(f);
		}
		if ((Sexp_nodes[f].subtype != SEXP_ATOM_OPERATOR) || (find_operator(CTEXT(f)) != OP_WHEN) || (CDR(f) == -1)) {
			continue;
		}
		n0 = Ap_flee_n;
		if (!ap_flee_walk(CDR(f), 0)) {
			Ap_flee_n = n0;
		}
	}
}

static int ap_primary(int i)
{
	return ((Mission_goals[i].type & GOAL_TYPE_MASK) == PRIMARY_GOAL) && !(Mission_goals[i].type & INVALID_GOAL);
}

static void ap_zone_walk(int n, int depth, char **other, float *dist, int *allow);

// the waypoint list a "list:point" name is in (point counted from 1), or -1
static int ap_waypoint_list(char *name)
{
	char list[NAME_LENGTH], *colon;
	int j;

	strncpy(list, name, sizeof(list) - 1);
	list[sizeof(list) - 1] = 0;
	colon = strrchr(list, ':');
	if (colon == NULL) {
		return -1;
	}
	*colon = 0;
	for (j = 0; j < Num_waypoint_lists; j++) {
		if (!stricmp(Waypoint_lists[j].name, list) && (atoi(colon + 1) >= 1) && (atoi(colon + 1) <= Waypoint_lists[j].count)) {
			return j;
		}
	}
	return -1;
}

// 1 if the sexp has a change-iff of a ship on the player's side that the goals want destroyed
static int ap_turns_friend(int n, int depth)
{
	int s, a;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return 0;
	}
	if (CAR(n) != -1) {
		return ap_turns_friend(CAR(n), depth + 1) || ap_turns_friend(CDR(n), depth + 1);
	}
	if ((Sexp_nodes[n].subtype == SEXP_ATOM_OPERATOR) && (find_operator(CTEXT(n)) == OP_CHANGE_IFF)) {
		for (a = (CDR(n) != -1) ? CDR(CDR(n)) : -1; a != -1; a = CDR(a)) {
			s = ship_name_lookup(CTEXT(a), 1);
			if ((s >= 0) && (Ap_role[s] & AP_DESTROY) && (Ships[s].team == Player_ship->team)) {
				return 1;
			}
		}
		return 0;
	}
	return ap_turns_friend(CDR(n), depth + 1);
}

// fills Ap_role from the incomplete primary goals and the pending directives; returns how many of
// those goals want more than the player's departure.  With Autoplay_scan also fills Ap_open_dirs and
// Ap_fly_wp: the mission's gameplay is not over while a directive is open (Tom, 2026-11-07, #239)
static int ap_scan()
{
	int i, left = 0;

	memset(Ap_role, 0, sizeof(Ap_role));
	memset(Ap_subsys, 0, sizeof(Ap_subsys));
	memset(Ap_event_seen, 0, sizeof(Ap_event_seen));
	memset(Ap_goal_seen, 0, sizeof(Ap_goal_seen));
	Ap_goto = -1;
	Ap_arrive_wait = 0;
	Ap_reinf = -1;

	for (i = 0; i < Num_goals; i++) {
		if (ap_primary(i) && (Mission_goals[i].satisfied == GOAL_INCOMPLETE) && (Mission_goals[i].formula != -1)) {
			Ap_goal_seen[i] = 1;
			Ap_goal_departs = 0;
			ap_walk(Mission_goals[i].formula, 0, 0);
			if (!Ap_goal_departs) {
				left++;
			}
		}
	}
	for (i = 0; i < Num_mission_events; i++) {
		if (Mission_events[i].objective_text && !Mission_events[i].result && (Mission_events[i].formula != -1) && !Ap_event_seen[i]) {
			Ap_event_seen[i] = 1;
			ap_walk(Mission_events[i].formula, 0, 0);
		}
	}
	// a ship the goals want destroyed that is on the player's side: the event that changes its side is
	// part of the goal (sm2-05a's sentry guns turn Neutral once one is scanned)
	for (i = 0; Autoplay_scan && (i < Num_mission_events); i++) {
		if (!Mission_events[i].result && (Mission_events[i].formula != -1) && !Ap_event_seen[i] && ap_turns_friend(Mission_events[i].formula, 0)) {
			Ap_event_seen[i] = 1;
			ap_walk(Mission_events[i].formula, 0, 0);
		}
	}
	Ap_open_dirs = 0;
	Ap_open_dir = -1;
	Ap_reinf_dirs = 0;
	Ap_reinf_near = -1;
	Ap_fly_wp = -1;
	for (i = 0; Autoplay_scan && (i < Num_mission_events); i++) {
		if (Mission_events[i].objective_text && !Mission_events[i].result && (Mission_events[i].formula != -1) && (Mission_events[i].flags & MEF_CURRENT)) {
			char *other = NULL;
			float dist = 0.0f;
			int allow = 0;

			int wp = -1;

			Ap_goal_departs = 0;
			Ap_marked = 0;
			ap_walk(Mission_events[i].formula, 0, 0);	// again, for the departure and the ships present: the roles it marks are marked already
			ap_zone_walk(Mission_events[i].formula, 0, &other, &dist, &allow);
			if (!allow && (other != NULL) && (dist > 0.0f)) {
				wp = ap_waypoint_list(other);
				// ( < ( distance <player> <ship> ) N ): the scan steering takes the player in close (sm2-05a's sentries)
				if ((wp < 0) && (ship_name_lookup(other, 1) >= 0)) {
					ap_mark(other, AP_SCAN, NULL, 0);
				}
			}
			if ((Ap_fly_wp < 0) && (wp >= 0)) {
				Ap_fly_wp = wp;
				Ap_fly_pt = atoi(strrchr(other, ':') + 1) - 1;
			}
			// open while it names a ship present or wants the player at a waypoint: a wing whose next
			// waves will never arrive (sm1-03a's Scorpio, stopped by Orff's arrival) does not hold the warp-out
			if (!Ap_goal_departs && (Ap_marked || (wp >= 0))) {
				if (Ap_open_dirs++ == 0) {
					Ap_open_dir = i;
				}
				// it waits on the reinforcement autoplay calls: it does not hold the call, and the
				// player is taken near what it names first (loop2-1: Alpha 1 within 1100 m of the Grall)
				if ((Ap_reinf >= 0) && ap_wants_arrived(Mission_events[i].formula, Reinforcements[Ap_reinf].name, 0)) {
					Ap_reinf_dirs++;
					if (!allow && (other != NULL) && (dist > 0.0f) && (ship_name_lookup(other, 1) >= 0)) {
						Ap_reinf_near = ship_name_lookup(other, 1);
						Ap_reinf_dist = dist;
					}
				}
			}
		}
	}
	// no directive wants a waypoint: an incomplete primary goal may (sm3-08a, ( < ( distance "Alpha 1" "Waypoint path 1:1" ) 750 ))
	for (i = 0; Autoplay_scan && (Ap_fly_wp < 0) && (i < Num_goals); i++) {
		if (ap_primary(i) && (Mission_goals[i].satisfied == GOAL_INCOMPLETE) && (Mission_goals[i].formula != -1)) {
			char *other = NULL;
			float dist = 0.0f;
			int allow = 0;
			int wp;

			ap_zone_walk(Mission_goals[i].formula, 0, &other, &dist, &allow);
			if (!allow && (other != NULL) && (dist > 0.0f) && ((wp = ap_waypoint_list(other)) >= 0) && strrchr(other, ':')) {
				Ap_fly_wp = wp;
				Ap_fly_pt = atoi(strrchr(other, ':') + 1) - 1;
			}
		}
	}
	ap_flee_find();
	ap_nova_find();
	Ap_goal_departs = 0;
	return left;
}

static void ap_count_goals(int *primaries, int *complete, int *failed)
{
	int i;

	*primaries = *complete = *failed = 0;
	for (i = 0; i < Num_goals; i++) {
		if (ap_primary(i)) {
			(*primaries)++;
			if (Mission_goals[i].satisfied == GOAL_COMPLETE) {
				(*complete)++;
			} else if (Mission_goals[i].satisfied == GOAL_FAILED) {
				(*failed)++;
			}
		}
	}
}

// the objectives open now: valid incomplete goals of any type (validate-goal makes new ones) and
// pending directives
static int ap_open_objectives()
{
	int i, n = 0;

	for (i = 0; i < Num_goals; i++) {
		// with Autoplay_scan, valid goals in any state: a failed goal and its replacement must not net to
		// no change (md-09)
		n += !(Mission_goals[i].type & INVALID_GOAL) && (Autoplay_scan || (Mission_goals[i].satisfied == GOAL_INCOMPLETE));
	}
	for (i = 0; i < Num_mission_events; i++) {
		n += (Mission_events[i].objective_text != NULL) && !Mission_events[i].result && (Mission_events[i].formula != -1);
	}
	return n;
}

// a keep-up check in formula n: ( > ( distance <player> <ship> ) N ), as sm1-05's "Keep up" nag from
// Zeta 1.  Returns that ship, or -1.
static int ap_keep_up_walk(int n, int depth)
{
	int args, d, a, b, s;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return -1;
	}
	if (CAR(n) != -1) {
		return ap_keep_up_walk(CAR(n), depth + 1);
	}
	if (Sexp_nodes[n].subtype != SEXP_ATOM_OPERATOR) {
		return -1;
	}
	args = CDR(n);
	if ((find_operator(CTEXT(n)) == OP_GREATER_THAN) && (args != -1) && (CAR(args) != -1)) {
		d = CAR(args);
		if ((Sexp_nodes[d].subtype == SEXP_ATOM_OPERATOR) && (find_operator(CTEXT(d)) == OP_DISTANCE) && (CDR(d) != -1) && (CDR(CDR(d)) != -1)) {
			a = CDR(d);
			b = CDR(a);
			s = -1;
			if (!stricmp(CTEXT(a), Player_ship->ship_name)) {
				s = ship_name_lookup(CTEXT(b), 1);
			} else if (!stricmp(CTEXT(b), Player_ship->ship_name)) {
				s = ship_name_lookup(CTEXT(a), 1);
			}
			if ((s >= 0) && (s != ap_player_shipnum())) {
				return s;
			}
		}
	}
	for (; args != -1; args = CDR(args)) {
		if (CAR(args) != -1) {
			s = ap_keep_up_walk(args, depth + 1);
			if (s >= 0) {
				return s;
			}
		}
	}
	return -1;
}

// the ship a live event wants the player to keep up with (Tom, 2026-11-07), or -1
static int ap_keep_up()
{
	int i, s;

	for (i = 0; i < Num_mission_events; i++) {
		if (Mission_events[i].formula != -1) {
			s = ap_keep_up_walk(Mission_events[i].formula, 0);
			if ((s >= 0) && (Ships[s].objnum >= 0) && !(Ships[s].flags & (SF_DYING | SF_DEPARTING | SF_ARRIVING))) {
				return s;
			}
		}
	}
	return -1;
}

// a warp zone in formula n: ( < ( distance <player> X ) N ) beside ( allow-warp <player> ), as sm3-10's
// jump node (Tom, 2026-11-07).  Sets *other and *dist from the distance check, *allow if it allows the warp.
static void ap_zone_walk(int n, int depth, char **other, float *dist, int *allow)
{
	int args, d, a, b;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return;
	}
	if (CAR(n) != -1) {
		ap_zone_walk(CAR(n), depth + 1, other, dist, allow);
		return;
	}
	if (Sexp_nodes[n].subtype != SEXP_ATOM_OPERATOR) {
		return;
	}
	args = CDR(n);
	if (!stricmp(CTEXT(n), "allow-warp") || !stricmp(CTEXT(n), "fix-warp")) {
		for (a = args; a != -1; a = CDR(a)) {
			if ((CAR(a) == -1) && !stricmp(CTEXT(a), Player_ship->ship_name)) {
				*allow = 1;
				if (!stricmp(CTEXT(n), "fix-warp")) {
					Ap_zone_fix = 1;
				}
			}
		}
		return;
	}
	if ((find_operator(CTEXT(n)) == OP_HAS_TIME_ELAPSED) && (args != -1) && (CAR(args) == -1)) {
		Ap_zone_after = atoi(CTEXT(args));
		return;
	}
	if ((find_operator(CTEXT(n)) == OP_LESS_THAN) && (args != -1) && (CAR(args) != -1) && (CDR(args) != -1)) {
		d = CAR(args);
		if ((Sexp_nodes[d].subtype == SEXP_ATOM_OPERATOR) && (find_operator(CTEXT(d)) == OP_DISTANCE) && (CDR(d) != -1) && (CDR(CDR(d)) != -1)) {
			a = CDR(d);
			b = CDR(a);
			if (!stricmp(CTEXT(a), Player_ship->ship_name)) {
				*other = CTEXT(b);
			} else if (!stricmp(CTEXT(b), Player_ship->ship_name)) {
				*other = CTEXT(a);
			}
			if (*other != NULL) {
				*dist = (float)atoi(CTEXT(CDR(args)));
			}
			return;
		}
	}
	for (; args != -1; args = CDR(args)) {
		if (CAR(args) != -1) {
			ap_zone_walk(args, depth + 1, other, dist, allow);
		}
	}
}

// finds the mission's warp zone, if any, at mission start
static void ap_zone_find()
{
	char *other, list[NAME_LENGTH], *colon;
	float dist;
	int i, j, allow;

	Ap_zone_wp = Ap_zone_pt = Ap_zone_ship = -1;
	for (i = 0; (i < Num_mission_events) && (Ap_zone_wp < 0) && (Ap_zone_ship < 0); i++) {
		other = NULL;
		dist = 0.0f;
		allow = 0;
		Ap_zone_fix = 0;
		Ap_zone_after = 0;
		if (Mission_events[i].formula != -1) {
			ap_zone_walk(Mission_events[i].formula, 0, &other, &dist, &allow);
		}
		if (!allow || (other == NULL) || (dist <= 0.0f)) {
			continue;
		}
		Ap_zone_dist = dist;
		Ap_zone_ship = ship_name_lookup(other, 1);
		if (Ap_zone_ship >= 0) {
			break;
		}
		// a waypoint, "list:point" with the point counted from 1
		strncpy(list, other, sizeof(list) - 1);
		list[sizeof(list) - 1] = 0;
		colon = strrchr(list, ':');
		if (colon == NULL) {
			continue;
		}
		*colon = 0;
		for (j = 0; j < Num_waypoint_lists; j++) {
			if (!stricmp(Waypoint_lists[j].name, list) && (atoi(colon + 1) >= 1) && (atoi(colon + 1) <= Waypoint_lists[j].count)) {
				Ap_zone_wp = j;
				Ap_zone_pt = atoi(colon + 1) - 1;
			}
		}
	}
}

// the player's distance from the warp zone, or -1 when the mission has none or it is gone
static float ap_zone_distance(vector **pos)
{
	if (Ap_zone_wp >= 0) {
		*pos = &Waypoint_lists[Ap_zone_wp].waypoints[Ap_zone_pt];
	} else if ((Ap_zone_ship >= 0) && (Ships[Ap_zone_ship].objnum >= 0)) {
		*pos = &Objects[Ships[Ap_zone_ship].objnum].pos;
	} else {
		return -1.0f;
	}
	return vm_vec_dist(*pos, &Player_obj->pos);
}

// a mission message whose name or text says "return to base"
static int ap_rtb_message(char *name)
{
	char buf[MESSAGE_LENGTH];
	int i;

	for (i = 0; i < Num_messages; i++) {
		if (!stricmp(Messages[i].name, name)) {
			strncpy(buf, Messages[i].name, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
			strlwr(buf);
			if (strstr(buf, "return to base")) {
				return 1;
			}
			strncpy(buf, Messages[i].message, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
			strlwr(buf);
			return strstr(buf, "return to base") != NULL;
		}
	}
	return 0;
}

// formula n names a "return to base" message anywhere (send-message, send-random-message, ...)
static int ap_rtb_walk(int n, int depth)
{
	if (depth > AP_MAX_DEPTH) {
		return 0;
	}
	for (; n != -1; n = CDR(n)) {
		if (CAR(n) != -1) {
			if (ap_rtb_walk(CAR(n), depth + 1)) {
				return 1;
			}
		} else if ((Sexp_nodes[n].subtype == SEXP_ATOM_STRING) && ap_rtb_message(CTEXT(n))) {
			return 1;
		}
	}
	return 0;
}

// 1 if the sexp wants a ship present, not the player, departed (not negated)
static int ap_wants_departed(int n, int depth)
{
	int a, s;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return 0;
	}
	if (CAR(n) != -1) {
		return ap_wants_departed(CAR(n), depth + 1) || ap_wants_departed(CDR(n), depth + 1);
	}
	if (Sexp_nodes[n].subtype == SEXP_ATOM_OPERATOR) {
		if (find_operator(CTEXT(n)) == OP_NOT) {
			return 0;
		}
		if (find_operator(CTEXT(n)) == OP_HAS_DEPARTED_DELAY) {
			for (a = (CDR(n) != -1) ? CDR(CDR(n)) : -1; a != -1; a = CDR(a)) {
				s = ship_name_lookup(CTEXT(a), 1);
				if ((s >= 0) && (s != ap_player_shipnum())) {
					return 1;
				}
			}
			return 0;
		}
	}
	return ap_wants_departed(CDR(n), depth + 1);
}

// ------------------------------------------------------------------------------------------------
// The campaign's own branch (#268, #283-#288): a mission advances when an event its branch names is
// true, and the game fails every event still undecided when the player leaves.  Several of those
// events fire some seconds after the last primary goal (sm1-04's "Thanks" 7 s after the kills,
// sm1-10's "May Continue" chained 7 s, sm2-10's after the Sathanas arrives), so with Autoplay_scan
// the warp-out waits for them, up to AP_BRANCH_WAIT s.  Read from the campaign file's text, so no
// sexp node is allocated during the mission.

#define AP_BRANCH_WAIT		120			// s the warp-out waits for an event the campaign branch reads
#define AP_BRANCH_MAX		8

static int Ap_branch_ev[AP_BRANCH_MAX];	// Mission_events index of each event the branch reads
static int Ap_branch_n;
static int Ap_branch_logged;

static int ap_same_mission(char *a, char *b);

// the next quoted string at or after *pp, copied to out; 0 when none before end
static int ap_branch_quoted(char **pp, char *end, char *out, int len)
{
	char *p = *pp, *q;

	while ((p < end) && (*p != '"')) {
		p++;
	}
	if (p >= end) {
		return 0;
	}
	for (q = ++p; (q < end) && (*q != '"'); q++) {
	}
	if (q >= end) {
		return 0;
	}
	if (q - p >= len) {
		p = q;	// too long for a name: skip it
		*pp = q + 1;
		out[0] = 0;
		return 1;
	}
	memcpy(out, p, q - p);
	out[q - p] = 0;
	*pp = q + 1;
	return 1;
}

// adds this mission's events named by is-previous-event-true in its branch in one campaign file; 1 if the file holds the mission
static int ap_branch_read(char *filename)
{
	char name[NAME_LENGTH], mission[MAX_FILENAME_LEN], *text, *p, *end, *block_end;
	CFILE *fp;
	int len, i, held = 0;

	fp = cfopen(filename, "rb", CFILE_NORMAL, CF_TYPE_MISSIONS);
	if (fp == NULL) {
		return 0;
	}
	len = cfilelength(fp);
	text = (len > 0) ? (char *)malloc(len + 1) : NULL;
	if (text == NULL) {
		cfclose(fp);
		return 0;
	}
	len = cfread(text, 1, len, fp);
	cfclose(fp);
	text[(len > 0) ? len : 0] = 0;
	end = text + ((len > 0) ? len : 0);

	for (p = strstr(text, "$Mission:"); p != NULL; p = strstr(block_end, "$Mission:")) {
		block_end = strstr(p + 9, "$Mission:");
		if (block_end == NULL) {
			block_end = end;
		}
		p += 9;
		while ((p < block_end) && ((*p == ' ') || (*p == '\t'))) {
			p++;
		}
		for (i = 0; (i < MAX_FILENAME_LEN - 1) && (p + i < block_end) && !strchr(" \t\r\n", p[i]); i++) {
			mission[i] = p[i];
		}
		mission[i] = 0;
		if (!ap_same_mission(mission, Game_current_mission_filename)) {
			continue;
		}
		held = 1;
		for (p = strstr(p, "is-previous-event-true"); (p != NULL) && (p < block_end); p = strstr(p, "is-previous-event-true")) {
			p += 22;
			if (!ap_branch_quoted(&p, block_end, mission, MAX_FILENAME_LEN) || !ap_branch_quoted(&p, block_end, name, NAME_LENGTH)) {
				break;
			}
			if (!ap_same_mission(mission, Game_current_mission_filename)) {
				continue;
			}
			for (i = 0; i < Num_mission_events; i++) {
				if (!stricmp(Mission_events[i].name, name)) {
					break;
				}
			}
			if ((i < Num_mission_events) && (Ap_branch_n < AP_BRANCH_MAX)) {
				Ap_branch_ev[Ap_branch_n++] = i;
			}
		}
		break;
	}
	free(text);
	return held;
}

// at mission start: the events the campaign branch reads, from the pilot's campaign or else any campaign file that holds the mission
static void ap_branch_find()
{
	char buf[MAX_FILENAME_LEN + 8], *files[MAX_CAMPAIGNS];
	int i, n, held = 0;

	Ap_branch_n = 0;
	Ap_branch_logged = 0;
	if (Campaign.filename[0]) {
		strcpy(buf, Campaign.filename);
		held = ap_branch_read(cf_add_ext(buf, FS_CAMPAIGN_FILE_EXT));
	}
	if (!held) {
		n = cf_get_file_list(MAX_CAMPAIGNS, files, CF_TYPE_MISSIONS, NOX("*.fc2"), CF_SORT_NAME);
		for (i = 0; i < n; i++) {
			if (!held) {
				held = ap_branch_read(cf_add_ext(files[i], FS_CAMPAIGN_FILE_EXT));
			}
			free(files[i]);
		}
	}
}

// 1 if the sexp wants the player's ship or wing departed (not negated)
static int ap_wants_player_departed(int n, int depth)
{
	int a, s, w;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return 0;
	}
	if (CAR(n) != -1) {
		return ap_wants_player_departed(CAR(n), depth + 1) || ap_wants_player_departed(CDR(n), depth + 1);
	}
	if (Sexp_nodes[n].subtype == SEXP_ATOM_OPERATOR) {
		if (find_operator(CTEXT(n)) == OP_NOT) {
			return 0;
		}
		if (find_operator(CTEXT(n)) == OP_HAS_DEPARTED_DELAY) {
			for (a = (CDR(n) != -1) ? CDR(CDR(n)) : -1; a != -1; a = CDR(a)) {
				s = ship_name_lookup(CTEXT(a), 1);
				w = (s < 0) ? wing_name_lookup(CTEXT(a), 1) : -1;
				if (((s >= 0) && (s == ap_player_shipnum())) || ((w >= 0) && (Player_ship->wingnum == w))) {
					return 1;
				}
			}
			return 0;
		}
	}
	return ap_wants_player_departed(CDR(n), depth + 1);
}

// 1 while an event the campaign branch reads is still undecided and can still come true before the player leaves:
// an event that wants the player gone (sm1-02's "Jump Out") counts only while its chain has not reached it
static int ap_branch_pending(char *name, int len)
{
	int i, e;

	for (i = 0; i < Ap_branch_n; i++) {
		e = Ap_branch_ev[i];
		if (Mission_events[e].formula == -1) {
			if (Mission_events[e].result) {
				return 0;	// the branch's event is true: the mission advances
			}
			continue;		// decided false: no wait helps
		}
		if (Mission_events[e].chain_delay >= 0) {
			if ((e > 0) && !Mission_events[e - 1].result) {
				strncpy(name, Mission_events[e].name, len - 1);
				name[len - 1] = 0;
				return 1;	// the chain has not reached it yet
			}
		}
		if (!ap_wants_player_departed(Mission_events[e].formula, 0)) {
			strncpy(name, Mission_events[e].name, len - 1);
			name[len - 1] = 0;
			return 1;
		}
	}
	return 0;
}

// with logging once: the warp-out waits for the campaign branch's event
static int ap_branch_wait()
{
	char name[NAME_LENGTH];

	if (!ap_branch_pending(name, NAME_LENGTH)) {
		return 0;
	}
	if (!Ap_branch_logged) {
		Ap_branch_logged = 1;
		autotest_logf("Autoplay: the warp-out waits up to %d s for '%s', the event the campaign advances on\n", AP_BRANCH_WAIT, name);
	}
	return 1;
}

// with Autoplay_scan: an incomplete primary goal waits on a ship's departure (loop2-1: "return to base"
// as Lambda 1 undocks, the goal is Lambda 1 gone, and the player's warp-out first fails it)
static int ap_departure_pending()
{
	int i;

	for (i = 0; i < Num_goals; i++) {
		if (ap_primary(i) && (Mission_goals[i].satisfied == GOAL_INCOMPLETE) && (Mission_goals[i].formula != -1) && ap_wants_departed(Mission_goals[i].formula, 0)) {
			return 1;
		}
	}
	return 0;
}

// --- orders ---

static int ap_find_order(ai_info *aip)
{
	int i;

	if (Ap_order_sig < 0) {
		return -1;
	}
	for (i = 0; i < MAX_AI_GOALS; i++) {
		if ((aip->goals[i].ai_mode != AI_GOAL_NONE) && (aip->goals[i].signature == Ap_order_sig)) {
			return i;
		}
	}
	return -1;
}

static char *ap_mode_name(int mode)
{
	switch (mode) {
		case AI_GOAL_CHASE: return "attack";
		case AI_GOAL_DISABLE_SHIP: return "disable";
		case AI_GOAL_DISARM_SHIP: return "disarm";
		case AI_GOAL_DESTROY_SUBSYSTEM: return "destroy subsystem of";
		case AI_GOAL_GUARD: return "guard";
		case AI_GOAL_FORM_ON_WING: return "form on";
		case AI_GOAL_STAY_NEAR_SHIP: return "close in on";
		case AI_GOAL_WAYPOINTS_ONCE: return "fly to";
	}
	return "order";
}

// gives the player the order, as the squad message menu gives one to a wingman; AP_NONE takes the
// manager's order back so the mission's own orders fly the player
// key tells orders apart: the ship index, or -2 - the waypoint list for AI_GOAL_WAYPOINTS_ONCE
static void ap_order_key(int mode, int submode, int key, char *name, char *why)
{
	ai_info *aip = &Ai_info[Player_ship->ai_index];
	int i;

	i = ap_find_order(aip);
	if ((i >= 0) && (mode == Ap_order_mode) && (submode == Ap_order_submode) && (key == Ap_order_ship)) {
		return;
	}
	if (i >= 0) {
		ai_remove_ship_goal(aip, i);
	}
	Ap_order_sig = -1;
	Ap_order_mode = mode;
	Ap_order_submode = submode;
	Ap_order_ship = key;

	if (mode != AP_NONE) {
		ai_add_ship_goal_player(AIG_TYPE_PLAYER_SHIP, mode, submode, name, aip);
		Ap_order_sig = Ai_goal_signature - 1;
		if (mode == AI_GOAL_WAYPOINTS_ONCE) {
			// the player order path leaves wp_index as the slot had it; -1 looks the list up by name
			i = ap_find_order(aip);
			if (i >= 0) {
				aip->goals[i].wp_index = -1;
			}
		}
	}

	// a goal the AI drops at once comes back each second: log only a change
	if ((mode != Ap_logged_mode) || (key != Ap_logged_ship)) {
		Ap_logged_mode = mode;
		Ap_logged_ship = key;
		if (mode == AP_NONE) {
			autotest_logf("Autoplay: no order, the mission's orders fly the player, at mission time %.2f\n", ap_time());
		} else {
			autotest_logf("Autoplay: %s '%s'%s%s (%s) at mission time %.2f\n", ap_mode_name(mode), name,
				(mode == AI_GOAL_DESTROY_SUBSYSTEM) ? " " : "", (mode == AI_GOAL_DESTROY_SUBSYSTEM) ? Ap_subsys[key] : "", why, ap_time());
		}
	}
}

static void ap_order(int mode, int submode, int shipnum, char *why)
{
	ap_order_key(mode, submode, shipnum, (mode == AP_NONE) ? NULL : Ships[shipnum].ship_name, why);
}

static int ap_usable(ship *sp, object *objp)
{
	return (objp != Player_obj) && !(sp->flags & (SF_DYING | SF_DEPARTING | SF_ARRIVING | SF_HIDDEN_FROM_SENSORS));
}

// a ship autoplay may give orders to: a fighter or bomber that is not docked (sm1-08: the transport docked
// to Enif Station took an attack order and dragged the station after the target)
static int ap_orderable(ship *sp)
{
	return (Ship_info[sp->ship_info_index].flags & (SIF_FIGHTER | SIF_BOMBER)) && (sp->ai_index >= 0) && !(Ai_info[sp->ai_index].ai_flags & AIF_DOCKED);
}

static int ap_hostile(ship *sp)
{
	return (sp->team & (TEAM_HOSTILE | TEAM_TRAITOR)) != 0;
}

// with Autoplay_scan, a neutral ship attacking the player's team is a target too (sm2-05a: the sentry guns
// turn neutral once inspected, and the Galatea only arrives once they and the neutral wings are gone)
static int ap_enemy(ship *sp)
{
	if (ap_hostile(sp)) {
		return 1;
	}
	if (Autoplay_scan && (sp->team == TEAM_NEUTRAL) && (sp->ai_index >= 0)) {
		int t = Ai_info[sp->ai_index].target_objnum;

		return (t >= 0) && (Objects[t].type == OBJ_SHIP) && (Ships[Objects[t].instance].team == Player_ship->team);
	}
	return 0;
}

static int ap_small(ship *sp)
{
	return (Ship_info[sp->ship_info_index].flags & SIF_SMALL_SHIP) != 0;
}

// the tier a hostile ship has as a target, and the order to give for it
static int ap_tier(ship *sp, object *objp, int *mode, int *submode, char **why)
{
	int role = Ap_role[objp->instance];
	int sif = Ship_info[sp->ship_info_index].flags;
	ai_info *aip;
	int i;

	*mode = AI_GOAL_CHASE;
	*submode = SM_ATTACK;

	// scan: the chase closes in and faces it, and the AI's target is the HUD target the scan needs.
	// It fires primaries meanwhile; no AI order closes in without firing
	if ((role & AP_SCAN) && !(sp->flags & SF_CARGO_REVEALED)) {
		*why = "goal: scan";
		return AP_TIER_SCAN;
	}

	// with Autoplay_scan, disabling or disarming is how a capture is done: it wins over keeping the ship
	// (sm1-04a's Omega: disable it, then the Comet docks and takes it away)
	if (Autoplay_scan && (role & AP_PROTECT) && (role & (AP_DISABLE | AP_DISARM))) {
		if ((role & AP_DISABLE) && !(sp->flags & SF_DISABLED)) {
			*mode = AI_GOAL_DISABLE_SHIP;
			*submode = -SUBSYSTEM_ENGINE;
			*why = "goal: disable";
			return AP_TIER_GOAL_SMALL;
		}
		if (role & AP_DISARM) {
			*mode = AI_GOAL_DISARM_SHIP;
			*submode = -SUBSYSTEM_TURRET;
			*why = "goal: disarm";
			return AP_TIER_GOAL_SMALL;
		}
	}

	if (role & AP_PROTECT) {
		return AP_TIER_NONE;		// a hostile the goals want kept (to capture, or to scan)
	}

	// attacking a ship to protect
	if ((sif & (SIF_FIGHTER | SIF_BOMBER)) && (sp->ai_index >= 0)) {
		aip = &Ai_info[sp->ai_index];
		if ((aip->target_objnum >= 0) && (Objects[aip->target_objnum].type == OBJ_SHIP)) {
			i = Objects[aip->target_objnum].instance;
			if ((Ap_role[i] & AP_PROTECT) && !ap_hostile(&Ships[i]) && ap_hostile(sp)) {
				*why = "attacking a ship to protect";
				return (sif & SIF_BOMBER) ? AP_TIER_BOMBER_ON_PROTECT : AP_TIER_FIGHTER_ON_PROTECT;
			}
		}
		// with Autoplay_scan, any hostile bomber while a big ship to protect is alive: its target flips
		// between launches, and sm3-07a's Vishnu bombers sank the Bastion while the player chased fighters
		if (Autoplay_scan && (sif & SIF_BOMBER) && ap_hostile(sp)) {
			for (i = 0; i < MAX_SHIPS; i++) {
				if ((Ships[i].objnum >= 0) && (Ap_role[i] & AP_PROTECT) && !ap_hostile(&Ships[i]) && !ap_small(&Ships[i]) && !(Ships[i].flags & SF_DYING)) {
					*why = "bomber near a ship to protect";
					return AP_TIER_BOMBER_ON_PROTECT;
				}
			}
		}
	}

	if (role & AP_DISABLE) {
		if (sp->flags & SF_DISABLED) {
			return AP_TIER_NONE;
		}
		*mode = AI_GOAL_DISABLE_SHIP;
		*submode = -SUBSYSTEM_ENGINE;
		*why = "goal: disable";
		return AP_TIER_GOAL_SMALL;
	}
	if (role & AP_DISARM) {
		*mode = AI_GOAL_DISARM_SHIP;
		*submode = -SUBSYSTEM_TURRET;
		*why = "goal: disarm";
		return AP_TIER_GOAL_SMALL;
	}
	if (role & AP_SUBSYS) {
		i = ship_get_subsys_index(sp, Ap_subsys[objp->instance], 1);
		if (i >= 0) {
			*mode = AI_GOAL_DESTROY_SUBSYSTEM;
			*submode = i;
			*why = "goal: destroy subsystem";
			return AP_TIER_GOAL_SMALL;
		}
	}
	if (role & AP_DESTROY) {
		*why = "goal: destroy";
		return ap_small(sp) ? AP_TIER_GOAL_SMALL : AP_TIER_GOAL_BIG;
	}

	// no goal names it
	if (!ap_enemy(sp) || (sif & SIF_HARMLESS) || (objp->flags & OF_PROTECTED)) {
		return AP_TIER_NONE;
	}
	if (sif & (SIF_FIGHTER | SIF_BOMBER)) {
		*why = "nearest hostile fighter";
		return AP_TIER_FIGHTER;
	}
	// with Autoplay_scan, no capital ship no goal names while a ship to protect is alive: a fighter cannot
	// kill it, and guarding is what saves the ship (sm3-07a's Bastion, left alone for the Tantalus)
	if (Autoplay_scan && !ap_small(sp)) {
		for (i = 0; i < MAX_SHIPS; i++) {
			if ((Ships[i].objnum >= 0) && (Ap_role[i] & AP_PROTECT) && !ap_hostile(&Ships[i]) && !(Ships[i].flags & SF_DYING)) {
				return AP_TIER_NONE;
			}
		}
	}
	*why = "nearest hostile";
	return AP_TIER_OTHER;
}

// with Autoplay_scan, friendly wings other than the player's, as they arrive: ordered to protect the ship a
// goal wants kept alive that has the least hull left, as a pilot orders "protect my target" (Tom, 2026-11-08,
// #263, #262); ordered again once the ship they guard is gone.  The ships they guard, by guard's signature
static int Ap_guard_sig[MAX_SHIPS];		// the guard's signature when ordered
static int Ap_guard_tsig[MAX_SHIPS];	// the signature of the ship it guards

static int ap_guarding(int shipnum)
{
	return (Ap_guard_sig[shipnum] == Objects[Ships[shipnum].objnum].signature) && (ship_get_by_signature(Ap_guard_tsig[shipnum]) >= 0);
}

// with Autoplay_scan, a big ship a goal wants destroyed: the player's wingmen are ordered onto it too, as
// a pilot orders "attack my target" (md-12: a lone fighter cannot kill the Hades), once per target
static int Ap_wing_target_sig;

static void ap_wing_order(int target, int tier)
{
	ship_obj *so;
	int n = 0;

	if (!Autoplay_scan || (tier != AP_TIER_GOAL_BIG) || (Objects[Ships[target].objnum].signature == Ap_wing_target_sig)) {
		return;
	}
	Ap_wing_target_sig = Objects[Ships[target].objnum].signature;
	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		object *objp = &Objects[so->objnum];
		ship *sp = &Ships[objp->instance];

		if ((objp != Player_obj) && (sp->team == Player_ship->team) && (sp->ai_index >= 0) && ap_usable(sp, objp)
			&& (sp->orders_accepted & ATTACK_TARGET_ITEM) && !ap_guarding(objp->instance) && ap_orderable(sp)) {
			ai_add_ship_goal_player(AIG_TYPE_PLAYER_SHIP, AI_GOAL_CHASE, SM_ATTACK, Ships[target].ship_name, &Ai_info[sp->ai_index]);
			n++;
		}
	}
	if (n > 0) {
		autotest_logf("Autoplay: %d wingmen ordered to attack '%s' at mission time %.2f\n", n, Ships[target].ship_name, ap_time());
	}
}

static void ap_wing_guard()
{
	ship_obj *so;
	int t = -1, n = 0;
	float f, best_f = 0.0f;

	if (!Autoplay_scan) {
		return;
	}
	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		object *objp = &Objects[so->objnum];
		ship *sp = &Ships[objp->instance];

		if ((objp == Player_obj) || (sp->team != Player_ship->team) || !(Ap_role[objp->instance] & AP_PROTECT) || !ap_usable(sp, objp)) {
			continue;
		}
		f = objp->hull_strength / Ship_info[sp->ship_info_index].initial_hull_strength;
		if ((t < 0) || (f < best_f)) {
			t = objp->instance;
			best_f = f;
		}
	}
	if (t < 0) {
		return;
	}
	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		object *objp = &Objects[so->objnum];
		ship *sp = &Ships[objp->instance];

		if ((objp == Player_obj) || (objp->instance == t) || (sp->team != Player_ship->team) || (sp->ai_index < 0) || (sp->wingnum < 0)
			|| (sp->wingnum == Player_ship->wingnum) || (Ap_role[objp->instance] & AP_PROTECT) || !(sp->orders_accepted & PROTECT_TARGET_ITEM) || !ap_orderable(sp)
			|| !ap_usable(sp, objp) || ap_guarding(objp->instance)) {
			continue;
		}
		ai_add_ship_goal_player(AIG_TYPE_PLAYER_SHIP, AI_GOAL_GUARD, AIS_GUARD_PATROL, Ships[t].ship_name, &Ai_info[sp->ai_index]);
		Ap_guard_sig[objp->instance] = objp->signature;
		Ap_guard_tsig[objp->instance] = Objects[Ships[t].objnum].signature;
		n++;
	}
	if (n > 0) {
		autotest_logf("Autoplay: %d friendlies ordered to protect '%s' (%.0f%% hull) at mission time %.2f\n", n, Ships[t].ship_name, best_f * 100.0f, ap_time());
	}
}

// with Autoplay_scan, a friendly shooting a ship of another team that a goal wants kept alive (sm2-06a:
// Delta's bombers, on chase-any, kill the Shivan cargo): every friendly that takes orders is ordered to
// ignore it, as a pilot orders "ignore my target", once per ship
static int Ap_ignore_sig[MAX_SHIPS];

static void ap_wing_spare()
{
	ship_obj *so, *so2;
	int t, n;

	if (!Autoplay_scan) {
		return;
	}
	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		object *objp = &Objects[so->objnum];
		ship *sp = &Ships[objp->instance];
		object *tp;

		if ((objp == Player_obj) || (sp->team != Player_ship->team) || (sp->ai_index < 0)) {
			continue;
		}
		t = Ai_info[sp->ai_index].target_objnum;
		if ((t < 0) || (Objects[t].type != OBJ_SHIP)) {
			continue;
		}
		tp = &Objects[t];
		if ((Ships[tp->instance].team == Player_ship->team) || !(Ap_role[tp->instance] & AP_PROTECT) || (Ap_role[tp->instance] & AP_DESTROY)
			|| (Ap_ignore_sig[tp->instance] == tp->signature)) {
			continue;
		}
		Ap_ignore_sig[tp->instance] = tp->signature;
		n = 0;
		for (so2 = GET_FIRST(&Ship_obj_list); so2 != END_OF_LIST(&Ship_obj_list); so2 = GET_NEXT(so2)) {
			object *op = &Objects[so2->objnum];
			ship *s2 = &Ships[op->instance];

			if ((op != Player_obj) && (s2->team == Player_ship->team) && (s2->ai_index >= 0) && ap_usable(s2, op)
				&& (s2->orders_accepted & IGNORE_TARGET_ITEM)) {
				ai_add_ship_goal_player(AIG_TYPE_PLAYER_SHIP, AI_GOAL_IGNORE, 0, Ships[tp->instance].ship_name, &Ai_info[s2->ai_index]);
				n++;
			}
		}
		autotest_logf("Autoplay: '%s' shot at '%s' (a ship to protect): %d friendlies ordered to ignore it at mission time %.2f\n", sp->ship_name, Ships[tp->instance].ship_name, n, ap_time());
	}
}

// 1 if any atom of the sexp is that name
static int ap_sexp_names(int n, char *name, int depth)
{
	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return 0;
	}
	if (CAR(n) != -1) {
		return ap_sexp_names(CAR(n), name, depth + 1) || ap_sexp_names(CDR(n), name, depth + 1);
	}
	if ((Sexp_nodes[n].subtype != SEXP_ATOM_OPERATOR) && !stricmp(CTEXT(n), name)) {
		return 1;
	}
	return ap_sexp_names(CDR(n), name, depth + 1);
}

// with Autoplay_scan, a reinforcement the player may call (on the menu, available, uses left) that no goal
// or event names, once an enemy is in the mission: called, as a pilot would (Tom, 2026-11-08, #261).  One the
// mission's logic names is left to the goals' own call (loop2-1's Lambda 1 must wait for the Grall)
static int ap_reinf_spare()
{
	ship_obj *so;
	int i, j;

	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		if ((&Objects[so->objnum] != Player_obj) && ap_enemy(&Ships[Objects[so->objnum].instance])) {
			break;
		}
	}
	if (so == END_OF_LIST(&Ship_obj_list)) {
		return -1;
	}
	for (i = 0; i < Num_reinforcements; i++) {
		if (!(Reinforcements[i].flags & RF_IS_AVAILABLE) || (Reinforcements[i].num_uses >= Reinforcements[i].uses) || (ship_get_reinforcement_team(i) != Player_ship->team)) {
			continue;
		}
		for (j = 0; j < Num_goals; j++) {
			if (ap_sexp_names(Mission_goals[j].formula, Reinforcements[i].name, 0)) {
				break;
			}
		}
		if (j < Num_goals) {
			continue;
		}
		for (j = 0; j < Num_mission_events; j++) {
			if (ap_sexp_names(Mission_events[j].formula, Reinforcements[i].name, 0)) {
				break;
			}
		}
		if (j < Num_mission_events) {
			continue;
		}
		return i;
	}
	return -1;
}

static void ap_think()
{
	ship_obj *so;
	object *objp;
	ship *sp;
	int tier, mode, submode, best = -1, best_tier = AP_TIER_NONE, best_mode = AP_NONE, best_submode = 0;
	int guard = -1, guard_protect = 0, role, follow, hidden = -1, hidden_tier = AP_TIER_NONE, hidden_goal = 0;
	float d, best_d = 0.0f, guard_d = 0.0f, hidden_d = 0.0f;
	char *why = "", *best_why = "";

	ap_wing_spare();
	ap_wing_guard();
	// dual-fire missiles, as a pilot would (Tom, 2026-11-08, #260): nothing in the game clears it again
	if (Autoplay_scan && (Player_ship->weapons.num_secondary_banks > 0) && !(Player_ship->flags & SF_SECONDARY_DUAL_FIRE)) {
		Player_ship->flags |= SF_SECONDARY_DUAL_FIRE;
		autotest_logf("Autoplay: missiles set to dual fire at mission time %.2f\n", ap_time());
	}
	// a reinforcement the goals need, once no directive is open and nothing it needs is still to arrive (loop2-1: "call in Lambda 1 once you
	// locate Snipes": Lambda 1 called before the Grall it docks with is there is destroyed)
	if (Autoplay_scan && (Ap_reinf >= 0) && (Ap_open_dirs == Ap_reinf_dirs) && (Ap_goto < 0)
		&& ((Ap_reinf_near < 0) || (vm_vec_dist_quick(&Player_obj->pos, &Objects[Ships[Ap_reinf_near].objnum].pos) < Ap_reinf_dist * 0.8f)) && (Ap_arrive_wait == 0) && (Reinforcements[Ap_reinf].flags & RF_IS_AVAILABLE)
		&& (Reinforcements[Ap_reinf].num_uses < Reinforcements[Ap_reinf].uses) && (ship_get_reinforcement_team(Ap_reinf) == Player_ship->team)) {
		autotest_logf("Autoplay: calling in reinforcement '%s' at mission time %.2f\n", Reinforcements[Ap_reinf].name, ap_time());
		hud_squadmsg_call_reinforcement(Ap_reinf);
		Ap_reinf = -1;
	}
	if (Autoplay_scan && (Ap_reinf < 0)) {
		int r = ap_reinf_spare();

		if (r >= 0) {
			autotest_logf("Autoplay: calling in reinforcement '%s' (allowed, not named by the goals or events) at mission time %.2f\n", Reinforcements[r].name, ap_time());
			hud_squadmsg_call_reinforcement(r);
		}
	}
	// a chained event waits for the player's wing to get clear of something: autoplay flies the player
	// away and the wingmen form on the player, ahead of any fight
	if (Autoplay_scan && (Ap_flee_n > 0)) {
		ship_obj *fo;

		if (!Ap_fleeing) {
			autotest_logf("Autoplay: getting the wing clear of '%s' (a chained event waits for it) at mission time %.2f\n", Ships[Objects[Ap_flee[0]].instance].ship_name, ap_time());
		}
		for (fo = GET_FIRST(&Ship_obj_list); fo != END_OF_LIST(&Ship_obj_list); fo = GET_NEXT(fo)) {
			objp = &Objects[fo->objnum];
			sp = &Ships[objp->instance];
			if ((sp->wingnum == Player_ship->wingnum) && (sp->wingnum >= 0) && ap_usable(sp, objp) && (sp->ai_index >= 0)
				&& (sp->orders_accepted & FORMATION_ITEM) && (Ap_flee_sig[objp->instance] != objp->signature)) {
				ai_add_ship_goal_player(AIG_TYPE_PLAYER_SHIP, AI_GOAL_FORM_ON_WING, 0, Player_ship->ship_name, &Ai_info[sp->ai_index]);
				Ap_flee_sig[objp->instance] = objp->signature;
			}
		}
		Ap_fleeing = 1;
		Autoplay_steering = 1;
		Ap_steer_obj = -1;
		Ap_steer_logged = -1;
		return;
	}
	if (Ap_fleeing) {
		Ap_fleeing = 0;
		Autoplay_steering = 0;
	}
	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		objp = &Objects[so->objnum];
		sp = &Ships[objp->instance];
		if (!ap_usable(sp, objp)) {
			continue;
		}
		d = vm_vec_dist_quick(&objp->pos, &Player_obj->pos);
		role = Ap_role[objp->instance];
		// a target: any hostile, or a ship of another team that a goal wants destroyed, disabled or
		// disarmed (sm1-02's sentry guns are neutral)
		if (ap_enemy(sp) || ((sp->team != Player_ship->team) && (role & (AP_DESTROY | AP_DISABLE | AP_DISARM | AP_SUBSYS)) && !(role & AP_PROTECT))
			|| (Autoplay_scan && (sp->team != Player_ship->team) && (role & AP_PROTECT) && (((role & AP_DISABLE) && !(sp->flags & SF_DISABLED)) || (role & AP_DISARM)))
			|| ((role & AP_SCAN) && !(sp->flags & SF_CARGO_REVEALED))) {
			tier = ap_tier(sp, objp, &mode, &submode, &why);
			// not yet confirmed on the HUD (nebula, stealth): the player could not target it, so it is
			// only a place to fly to (Tom, 2026-11-07)
			if ((tier != AP_TIER_NONE) && (awacs_get_level(objp, Player_ship) < 1.0f)) {
				// with Autoplay_scan, a hidden ship a goal names comes first: it is only a place to fly to, and
				// staying near a stealth fighter that never shows held sm2-07 off its last sentry gun
				int goal = Autoplay_scan && ((role & (AP_DESTROY | AP_DISABLE | AP_DISARM | AP_SUBSYS | AP_SCAN)) != 0);

				if ((goal > hidden_goal) || ((goal == hidden_goal) && ((tier < hidden_tier) || ((tier == hidden_tier) && (d < hidden_d))))) {
					hidden = objp->instance;
					hidden_tier = tier;
					hidden_d = d;
					hidden_goal = goal;
				}
				continue;
			}
			if ((tier < best_tier) || ((tier == best_tier) && (d < best_d))) {
				best = objp->instance;
				best_tier = tier;
				best_d = d;
				best_mode = mode;
				best_submode = submode;
				best_why = why;
			}
		} else if ((role & AP_PROTECT) || (sp->team == Player_ship->team)) {
			// a ship to guard: one the goals want protected (any team but hostile), else an escort ship
			if (role & AP_PROTECT) {
				if (!guard_protect || (d < guard_d)) {
					guard = objp->instance;
					guard_protect = 1;
					guard_d = d;
				}
			} else if (!guard_protect && (sp->flags & SF_ESCORT) && ((guard < 0) || (d < guard_d))) {
				guard = objp->instance;
				guard_d = d;
			}
		}
	}

	// a scan: autoplay steers the player itself (autoplay_steer); the order below takes over again
	// once the scan is done
	if ((best >= 0) && (best_tier == AP_TIER_SCAN)) {
		if (Ap_steer_logged != Ships[best].objnum) {
			Ap_steer_logged = Ships[best].objnum;
			autotest_logf("Autoplay: steering to scan '%s' at mission time %.2f\n", Ships[best].ship_name, ap_time());
		}
		Autoplay_steering = 1;
		Ap_steer_obj = Ships[best].objnum;
		Ap_steer_sig = Objects[Ap_steer_obj].signature;
	} else {
		// an open directive wants the player at a waypoint (sm2-08a's "Enter docking bay"): autoplay steers
		// there and holds, ahead of any fight.  A waypoint order ends on arrival and the AI then leaves the
		// point for its own targets (Tom, 2026-11-07, #239)
		if (Autoplay_scan && (Ap_fly_wp >= 0)) {
			if (Ap_steer_logged != -2 - Ap_fly_wp) {
				Ap_steer_logged = -2 - Ap_fly_wp;
				autotest_logf("Autoplay: steering to '%s:%d' (directive) at mission time %.2f\n", Waypoint_lists[Ap_fly_wp].name, Ap_fly_pt + 1, ap_time());
			}
			Autoplay_steering = 1;
			Ap_steer_obj = -2 - Ap_fly_wp;
			Ap_steer_pt = Ap_fly_pt;
			return;
		}
		Ap_steer_logged = -1;
	}

	if ((best >= 0) && (best_tier == AP_TIER_SCAN) && !ap_hostile(&Ships[best])) {
		ap_order(AI_GOAL_STAY_NEAR_SHIP, 0, best, best_why);	// never chase a ship not hostile
	} else if (best >= 0) {
		ap_order(best_mode, best_submode, best, best_why);
		ap_wing_order(best, best_tier);
	} else if (Autoplay_scan && (Ap_goto >= 0) && (Ships[Ap_goto].objnum >= 0) && ap_usable(&Ships[Ap_goto], &Objects[Ships[Ap_goto].objnum])) {
		// steered: the AI's stay-near aims at a point that moves away as the player does, and flew the
		// player 20 km from loop2-1's NB 8 in a straight line
		ap_order(AP_NONE, 0, -1, "");
		if (Ap_near_logged != Ap_goto) {
			Ap_near_logged = Ap_goto;
			autotest_logf("Autoplay: steering near '%s' (an arrival the goals need waits for the player near it) at mission time %.2f\n", Ships[Ap_goto].ship_name, ap_time());
		}
		Autoplay_steering = 1;
		Ap_steer_near = Ships[Ap_goto].objnum;
	} else if ((follow = ap_keep_up()) >= 0) {
		// nothing to attack and the mission wants the player near a ship: form on it, ahead of closing in
		// on unconfirmed ships (sm1-05's Zeta 1 leads the player to them) and of guarding
		ap_order(AI_GOAL_FORM_ON_WING, 0, follow, "keep up: the mission checks the distance");
	} else if (hidden >= 0) {
		ap_order(AI_GOAL_STAY_NEAR_SHIP, 0, hidden, "not confirmed on the HUD yet");
	} else if (guard >= 0) {
		ap_order(AI_GOAL_GUARD, AIS_GUARD_PATROL, guard, guard_protect ? "goal: protect" : "escort ship");
	} else {
		ap_order(AP_NONE, 0, -1, "");
	}
}

// --- steering for a scan ---

// The scan needs the ship targeted, within max(CARGO_REVEAL_MIN_DIST, radius + CARGO_RADIUS_DELTA) of its
// hull and the nose within CARGO_MIN_DOT_TO_REVEAL of its centre, held for its scan time.  No AI order
// does that without firing or strafing, so autoplay flies the player for it, the way the AI turns a
// ship (ai_turn_towards_vector) and a pilot sets the throttle; it never fires.  Called from the frame
// in place of read_player_controls, and the AI does not run on the player meanwhile.
void autoplay_steer(object *objp, float frametime)
{
	object *tp;
	ship *sp;
	control_info ci;
	float d, range, speed, dot;
	vector to;

	if (Autoplay_steering && Ap_fleeing) {
		// away from the objects a chained event wants the wing clear of, at full speed
		vector away;
		int i;

		if ((objp->type != OBJ_SHIP) || (Ships[objp->instance].flags & SF_DYING) || (Player->control_mode != PCM_NORMAL)) {
			return;
		}
		sp = &Ships[objp->instance];
		vm_vec_zero(&away);
		for (i = 0; i < Ap_flee_n; i++) {
			vm_vec_normalized_dir(&to, &objp->pos, &Objects[Ap_flee[i]].pos);
			vm_vec_add2(&away, &to);
		}
		if (vm_vec_mag_quick(&away) < 0.01f) {
			away = objp->orient.fvec;
		}
		vm_vec_normalize(&away);
		vm_vec_scale_add(&to, &objp->pos, &away, 1000.0f);
		ai_turn_towards_vector(&to, objp, frametime, Ship_info[sp->ship_info_index].srotation_time, NULL, NULL, 0.0f, 0);
		memset(&ci, 0, sizeof(ci));
		objp->phys_info.max_vel.z = sp->current_max_speed;
		ci.forward = 1.0f;
		physics_read_flying_controls(&objp->orient, &objp->phys_info, &ci, frametime);
		return;
	}
	if (Autoplay_steering && (Ap_steer_near >= 0)) {
		// near an object: fly at it and hold within 200 m
		tp = &Objects[Ap_steer_near];
		if ((objp->type != OBJ_SHIP) || (Ships[objp->instance].flags & SF_DYING) || (Player->control_mode != PCM_NORMAL) || (tp->type == OBJ_NONE)) {
			Autoplay_steering = 0;
			Ap_steer_near = -1;
			return;
		}
		sp = &Ships[objp->instance];
		ai_turn_towards_vector(&tp->pos, objp, frametime, Ship_info[sp->ship_info_index].srotation_time, NULL, NULL, 0.0f, 0);
		d = vm_vec_dist(&tp->pos, &objp->pos) - tp->radius;
		vm_vec_normalized_dir(&to, &tp->pos, &objp->pos);
		dot = vm_vec_dot(&to, &objp->orient.fvec);
		speed = (d < 200.0f) ? 0.0f : (d - 200.0f) * 0.5f;
		if ((dot < 0.7f) && (d < 500.0f)) {
			speed = 0.0f;
		}
		memset(&ci, 0, sizeof(ci));
		objp->phys_info.max_vel.z = sp->current_max_speed;
		if (sp->current_max_speed > 0.0f) {
			ci.forward = speed / sp->current_max_speed;
		}
		if (ci.forward > 1.0f) {
			ci.forward = 1.0f;
		}
		physics_read_flying_controls(&objp->orient, &objp->phys_info, &ci, frametime);
		return;
	}
	if (!Autoplay_steering || (Ap_steer_obj == -1)) {
		return;
	}
	if (Ap_steer_obj < -1) {
		// to a directive's waypoint: fly at it and hold within 50 m
		vector *pt = &Waypoint_lists[-2 - Ap_steer_obj].waypoints[Ap_steer_pt];

		if ((objp->type != OBJ_SHIP) || (Ships[objp->instance].flags & SF_DYING) || (Player->control_mode != PCM_NORMAL)) {
			Autoplay_steering = 0;
			Ap_steer_obj = -1;
			return;
		}
		sp = &Ships[objp->instance];
		ai_turn_towards_vector(pt, objp, frametime, Ship_info[sp->ship_info_index].srotation_time, NULL, NULL, 0.0f, 0);
		d = vm_vec_dist(pt, &objp->pos);
		vm_vec_normalized_dir(&to, pt, &objp->pos);
		dot = vm_vec_dot(&to, &objp->orient.fvec);
		speed = (d < 50.0f) ? 0.0f : (d - 50.0f) * 0.5f;
		if ((dot < 0.7f) && (d < 300.0f)) {
			speed = 0.0f;
		}
		memset(&ci, 0, sizeof(ci));
		objp->phys_info.max_vel.z = sp->current_max_speed;
		if (sp->current_max_speed > 0.0f) {
			ci.forward = speed / sp->current_max_speed;
		}
		if (ci.forward > 1.0f) {
			ci.forward = 1.0f;
		}
		// a supernova on its way: afterburners, as a player would (sm3-10, 5 km to its jump node in 58 s)
		if ((supernova_active() == 1) && (d > 300.0f) && (dot > 0.9f)) {
			if (!(objp->phys_info.flags & PF_AFTERBURNER_ON) && ship_get_subsystem_strength(sp, SUBSYSTEM_ENGINE)) {
				afterburners_start(objp);
			}
			ci.forward = 1.0f;
		} else if (objp->phys_info.flags & PF_AFTERBURNER_ON) {
			afterburners_stop(objp, 1);
		}
		physics_read_flying_controls(&objp->orient, &objp->phys_info, &ci, frametime);
		return;
	}
	tp = &Objects[Ap_steer_obj];
	if ((tp->signature != Ap_steer_sig) || (tp->type != OBJ_SHIP) || (objp->type != OBJ_SHIP)
		|| (Ships[tp->instance].flags & (SF_CARGO_REVEALED | SF_DYING | SF_DEPARTING))
		|| (Ships[objp->instance].flags & SF_DYING) || (Player->control_mode != PCM_NORMAL)) {
		Autoplay_steering = 0;		// the AI takes over from the next frame
		Ap_steer_obj = -1;
		return;
	}
	sp = &Ships[objp->instance];

	if (Player_ai->target_objnum != Ap_steer_obj) {
		set_target_objnum(Player_ai, Ap_steer_obj);
	}

	ai_turn_towards_vector(&tp->pos, objp, frametime, Ship_info[sp->ship_info_index].srotation_time, NULL, NULL, 0.0f, 0);

	// close to half the scan range from the hull, matching the target's speed; slower while turning
	d = hud_find_target_distance(tp, objp);
	range = (float)max(CARGO_REVEAL_MIN_DIST, tp->radius + CARGO_RADIUS_DELTA);
	speed = tp->phys_info.fspeed + (d - range * 0.5f) * 0.5f;
	vm_vec_normalized_dir(&to, &tp->pos, &objp->pos);
	dot = vm_vec_dot(&to, &objp->orient.fvec);
	if ((dot < 0.7f) && (d < range)) {
		speed = 0.0f;
	}

	memset(&ci, 0, sizeof(ci));
	objp->phys_info.max_vel.z = sp->current_max_speed;
	if (sp->current_max_speed > 0.0f) {
		ci.forward = speed / sp->current_max_speed;
	}
	if (ci.forward < 0.0f) {
		ci.forward = 0.0f;
	}
	physics_read_flying_controls(&objp->orient, &objp->phys_info, &ci, frametime);
}

// --- the run ---

void autoplay_mission_start()
{
	int i;

	Ap_active = Autoplay_on && Autotest_player_npc;
	Autoplay_steering = 0;
	Ap_steer_obj = Ap_steer_logged = -1;
	Ap_steer_near = Ap_near_logged = -1;
	Ap_open_dir = Ap_open_dir_logged = -1;
	Ap_hostiles = Ap_attrition = Ap_goal_hull = 0;
	Ap_wing_target_sig = -1;
	memset(Ap_flee_sig, -1, sizeof(Ap_flee_sig));
	Ap_flee_n = 0;
	Ap_fleeing = 0;
	memset(Ap_ignore_sig, -1, sizeof(Ap_ignore_sig));
	memset(Ap_guard_sig, -1, sizeof(Ap_guard_sig));
	memset(Ap_rtb_skip, 0, sizeof(Ap_rtb_skip));
	Ap_rtb_left = 0;
	memset(Ap_death_logged, 0, sizeof(Ap_death_logged));
	Ap_prev_objectives = ap_open_objectives();
	Ap_next_think = Missiontime;
	Ap_progress = 0;
	Ap_progress_time = Missiontime;
	Ap_done_time = -1;
	Ap_warping = 0;
	Ap_died = 0;
	Ap_reported = 0;
	Ap_failed_seen = Ap_failed_ok = 0;
	Ap_fail_time = -1;
	Ap_order_sig = -1;
	Ap_order_mode = Ap_logged_mode = AP_NONE;
	Ap_order_ship = Ap_logged_ship = -1;
	Ap_order_submode = 0;
	Ap_rtb = 0;
	Ap_zone_logged = 0;
	ap_zone_find();
	ap_branch_find();
	for (i = 0; i < Num_mission_events; i++) {
		Ap_rtb_event[i] = (char)((Mission_events[i].formula != -1) && ap_rtb_walk(Mission_events[i].formula, 0));
		// a chained "Return To Base" directive (sm3-02): Command's order once the chain makes it current
		if (!Ap_rtb_event[i] && (Mission_events[i].formula != -1) && (Mission_events[i].chain_delay >= 0) && Mission_events[i].objective_text) {
			char buf[MESSAGE_LENGTH];

			strncpy(buf, Mission_events[i].objective_text, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
			strlwr(buf);
			if (strstr(buf, "return to base")) {
				Ap_rtb_event[i] = 2;
			}
		}
	}
	if (Autoplay_on && !Ap_active) {
		autotest_logf("Autoplay: off for this mission, the player stays still (mission notes)\n");
	}
}

// with Autoplay_scan, the fight counts as progress for the stall timer: hostiles going (md-04's waves,
// chained only by arrival cues) and hull lost by the ships a goal wants destroyed (md-12's Hades)

static int ap_attrition()
{
	ship_obj *so;
	int hostiles = 0, hull = 0;

	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		object *objp = &Objects[so->objnum];
		ship *sp = &Ships[objp->instance];

		if (ap_hostile(sp) && !(sp->flags & SF_DYING)) {
			hostiles++;
		}
		if ((Ap_role[objp->instance] & AP_DESTROY) && !(sp->flags & SF_DYING)) {
			hull += (int)(objp->hull_strength / 100.0f);	// steps of 100 hit points
		}
		// a ship the goals want kept alive dies: log who did most of its damage (md-08's Iotas, sm2-06a's cargo)
		if ((Ap_role[objp->instance] & AP_PROTECT) && (sp->flags & SF_DYING) && !Ap_death_logged[objp->instance]) {
			int j, best = -1, k;
			char *who = "unknown";

			Ap_death_logged[objp->instance] = 1;
			for (j = 0; j < MAX_DAMAGE_SLOTS; j++) {
				if ((sp->damage_ship_id[j] != 0) && ((best < 0) || (sp->damage_ship[j] > sp->damage_ship[best]))) {
					best = j;
				}
			}
			for (k = 0; (best >= 0) && (k <= Highest_object_index); k++) {
				if ((Objects[k].type == OBJ_SHIP) && (Objects[k].signature == sp->damage_ship_id[best])) {
					who = Ships[Objects[k].instance].ship_name;
				}
			}
			autotest_logf("Autoplay: '%s' (a ship to protect) is dying at mission time %.2f, most damage from '%s'\n", sp->ship_name, ap_time(), who);
		}
	}
	if (hostiles < Ap_hostiles) {
		Ap_attrition++;
	}
	Ap_hostiles = hostiles;
	if ((hull > 0) && (Ap_goal_hull >= 0) && (hull < Ap_goal_hull)) {
		Ap_attrition++;
	}
	Ap_goal_hull = hull;
	return Ap_attrition;
}

int autoplay_frame()
{
	int i, progress, primaries, complete, failed, left;

	if (!Ap_active || (Player_obj == NULL)) {
		return AUTOPLAY_END_NONE;
	}

	if ((Game_mode & GM_DEAD) || (Player_obj->type != OBJ_SHIP)) {
		if (!Ap_died) {
			Ap_died = 1;
			Ap_died_time = Missiontime;
			autotest_logf("Autoplay: the player died at mission time %.2f\n", ap_time());
		} else if (Missiontime - Ap_died_time >= i2f(AUTOPLAY_DEATH_TIME)) {
			return AUTOPLAY_END_DIED;
		}
		return AUTOPLAY_END_NONE;
	}

	if (Missiontime < Ap_next_think) {
		return AUTOPLAY_END_NONE;
	}
	Ap_next_think = Missiontime + F1_0;
	Autoplay_steering = 0;		// ap_think steers again if a scan is still wanted
	Ap_steer_obj = -1;
	Ap_steer_near = -1;

	progress = 0;
	for (i = 0; i < Num_goals; i++) {
		progress += (Mission_goals[i].satisfied != GOAL_INCOMPLETE);
	}
	for (i = 0; i < Num_mission_events; i++) {
		progress += (Mission_events[i].result != 0);
	}
	if (Autoplay_scan) {
		progress += ap_attrition();
	}
	if (progress != Ap_progress) {
		Ap_progress = progress;
		Ap_progress_time = Missiontime;
	}

	// Command's "return to base" is a success in any mission (Tom, 2026-11-07): warp out
	// with Autoplay_scan, a "return to base" message only while no goal is left (sm2-02: Command sends a
	// badly damaged Kappa 3 home, the player's goals still open; #248); a "Return To Base" directive always
	for (i = 0; (i < Num_mission_events) && !Ap_rtb; i++) {
		if (Autoplay_scan && (Ap_rtb_event[i] == 1) && Mission_events[i].result && (Ap_rtb_left > 0)) {
			if (!Ap_rtb_skip[i]) {
				Ap_rtb_skip[i] = 1;
				autotest_logf("Autoplay: 'return to base' in event '%s' with %d goals left, taken as meant for another ship, at mission time %.2f\n", Mission_events[i].name, Ap_rtb_left, ap_time());
			}
			continue;
		}
		if (((Ap_rtb_event[i] == 1) && Mission_events[i].result) || ((Ap_rtb_event[i] == 2) && (Mission_events[i].flags & MEF_CURRENT))) {
			Ap_rtb = 1;
			autotest_logf("Autoplay: return to base ordered (event '%s') at mission time %.2f, counted as success\n", Mission_events[i].name, ap_time());
		}
	}

	// a failed objective is not always a failed mission: wait AUTOPLAY_FAIL_WAIT for new objectives
	// (Tom, 2026-11-07)
	ap_count_goals(&primaries, &complete, &failed);
	if ((failed > Ap_failed_seen) && !Ap_rtb) {
		if (Ap_fail_time < 0) {
			Ap_fail_time = Missiontime;
			// with Autoplay_scan, the count from the think before the failure: a goal validated in the same
			// second (md-09's escape pods, 0.03 s before) is new
			Ap_fail_objectives = Autoplay_scan ? Ap_prev_objectives : ap_open_objectives();
			autotest_logf("Autoplay: a primary goal failed at mission time %.2f, waiting %d s for new objectives\n", ap_time(), AUTOPLAY_FAIL_WAIT);
		} else if (ap_open_objectives() > Ap_fail_objectives) {
			autotest_logf("Autoplay: new objectives after the failed goal, playing on, at mission time %.2f\n", ap_time());
			Ap_failed_ok += failed - Ap_failed_seen;
			Ap_failed_seen = failed;
			Ap_fail_time = -1;
		} else if (Missiontime - Ap_fail_time >= i2f(AUTOPLAY_FAIL_WAIT)) {
			return AUTOPLAY_END_FAILED;
		}
	}
	if (Autoplay_scan && (Ap_fail_time < 0)) {
		Ap_prev_objectives = ap_open_objectives();
	}

	// with Autoplay_scan: end-campaign with the player unable to warp (FS1's sm3-09a: never-warp, the
	// Lucifer destroyed) is the mission's end.  Retail FS1 ended the mission there; this engine only
	// sets the flag, so nothing else would end it
	// (not with a warp zone: sm3-10 ends the campaign as its supernova starts, and the player leaves through the jump node)
	if (Autoplay_scan && Campaign_ended_in_mission && (Player_ship->flags & (SF_WARP_BROKEN | SF_WARP_NEVER)) && (Ap_zone_wp < 0) && (Ap_zone_ship < 0)) {
		autotest_logf("Autoplay: end-campaign with the warp drive off at mission time %.2f, the mission is over\n", ap_time());
		return AUTOPLAY_END_ITSELF;
	}

	if (Ap_warping) {
		// with Autoplay_scan: an aborted warp-out (hit while speeding up, a broken drive) is not the end:
		// the AI flies the player again and autoplay warps once more after the settle time (Tom, 2026-11-07, #239)
		if (Autoplay_scan && (Player->control_mode == PCM_NORMAL) && (Missiontime - Ap_warp_time >= F1_0)) {
			autotest_logf("Autoplay: the warp-out was aborted at mission time %.2f, playing on\n", ap_time());
			Ap_warping = 0;
			Ap_done_time = -1;
			Autotest_player_npc = 1;
			return AUTOPLAY_END_NONE;
		}
		return (Missiontime - Ap_warp_time >= i2f(AUTOPLAY_WARP_TIME)) ? AUTOPLAY_END_NO_WARP : AUTOPLAY_END_NONE;
	}

	// no warp-out while waiting on a failed goal: the mission may still give new objectives.  Nor while
	// no goal is complete: a lone "has-departed Alpha 1" goal is a placeholder (sm3-08, sm3-10) and the
	// mission's events, its "return to base" or its own end decide instead
	// with Autoplay_scan, nor while a directive is open or the player's warp drive is broken (#239)
	left = Ap_rtb ? 0 : ap_scan();
	Ap_rtb_left = left;
	// the goals are done but a directive is open: name it once, for the log (after this frame's scan)
	if (Autoplay_scan && !Ap_rtb && (complete > 0) && (Ap_open_dir >= 0) && (Ap_open_dir_logged != Ap_open_dir)) {
		Ap_open_dir_logged = Ap_open_dir;
		autotest_logf("Autoplay: directive '%s' (%s) is open, no warp-out yet, at mission time %.2f\n", Mission_events[Ap_open_dir].objective_text, Mission_events[Ap_open_dir].name, ap_time());
	}
	// with Autoplay_scan, a supernova on its way: the mission wants the player gone (sm3-10, through its jump node)
	// with Autoplay_scan, a mission that breaks the warp drive and fixes it near its jump node after a time
	// (loop2-2: "Fix Jump" at 900 s within 1500 m): it wants the player gone through there once that time is up
	// with Autoplay_scan, a supernova the mission starts at a set time: the player waits at the jump node from
	// AP_NOVA_LEAD s before (sm3-10: 8.5 km from the node when it starts, 58 s before it hits)
	if ((Ap_rtb || (Autoplay_scan && (supernova_active() == 1))
		|| (Autoplay_scan && (Ap_nova_at >= 0) && ((Ap_zone_wp >= 0) || (Ap_zone_ship >= 0)) && (Missiontime >= i2f(Ap_nova_at - AP_NOVA_LEAD)) && (Ap_fail_time < 0))
		|| (Autoplay_scan && Ap_zone_fix && ((Ap_zone_wp >= 0) || (Ap_zone_ship >= 0)) && (Missiontime >= i2f(Ap_zone_after)) && (Ap_fail_time < 0) && !Ap_open_dirs) || ((left == 0) && (primaries > 0) && (complete > 0) && (Ap_fail_time < 0) && !(Autoplay_scan && Ap_open_dirs)))
		&& !(Autoplay_scan && (Player_ship->flags & (SF_WARP_BROKEN | SF_WARP_NEVER)) && (Ap_zone_wp < 0) && (Ap_zone_ship < 0))) {	// sm2-08a's break-warp; a warp zone mission handles it below (sm3-10's never-warp leaves the drive broken)
		vector *zone_pos;
		float zone_d = ap_zone_distance(&zone_pos);

		// the mission allows the warp-out only near its jump node: fly there first, and warp once the
		// mission has allowed it
		if ((zone_d >= 0.0f) && ((zone_d > Ap_zone_dist * 0.8f) || (Player_ship->flags & (SF_WARP_BROKEN | SF_WARP_NEVER)) || (Autoplay_scan && (Ap_nova_at >= 0) && (supernova_active() != 1)))) {
			if (!Ap_zone_logged) {
				Ap_zone_logged = 1;
				autotest_logf("Autoplay: the mission allows a warp-out only within %.0f m of its jump node, %.0f m away\n", Ap_zone_dist, zone_d);
			}
			if (Autoplay_scan && (Ap_zone_wp >= 0)) {
				// steered, as a directive's waypoint: the AI's waypoint order lost its way for 860 s (loop2-2)
				ap_order(AP_NONE, 0, -1, "");
				Autoplay_steering = 1;
				Ap_steer_obj = -2 - Ap_zone_wp;
				Ap_steer_pt = Ap_zone_pt;
			} else if (Ap_zone_wp >= 0) {
				ap_order_key(AI_GOAL_WAYPOINTS_ONCE, 0, -2 - Ap_zone_wp, Waypoint_lists[Ap_zone_wp].name, "the warp zone");
			} else {
				ap_order(AI_GOAL_STAY_NEAR_SHIP, 0, Ap_zone_ship, "the warp zone");
			}
			Ap_done_time = -1;
			return AUTOPLAY_END_NONE;
		}
		if (Ap_done_time < 0) {
			Ap_done_time = Missiontime;
			if (!Ap_rtb) {
				autotest_logf("Autoplay: primary goals done (%d/%d complete) at mission time %.2f\n", complete, primaries, ap_time());
			}
		} else if ((Missiontime - Ap_done_time >= i2f(AUTOPLAY_SETTLE_TIME))
			&& !(Autoplay_scan && Ap_rtb && (Missiontime - Ap_done_time < i2f(AP_DEPART_WAIT)) && ap_departure_pending())
			&& !(Autoplay_scan && (Missiontime - Ap_done_time < i2f(AP_BRANCH_WAIT)) && ap_branch_wait())) {
			// the player's own warp-out, as Alt-J starts it; the player's controls (not the AI)
			// fly the warp-out, so the AI stops flying the player
			ap_order(AP_NONE, 0, -1, "");
			Autotest_player_npc = 0;
			Ap_warping = 1;
			Ap_warp_time = Missiontime;
			autotest_logf("Autoplay: warping out at mission time %.2f%s\n", ap_time(), (Player_ship->flags & (SF_WARP_BROKEN | SF_WARP_NEVER)) ? " (warp drive broken)" : "");
			gameseq_post_event(GS_EVENT_PLAYER_WARPOUT_START);
			return AUTOPLAY_END_NONE;
		}
	} else {
		Ap_done_time = -1;
	}

	if (Missiontime - Ap_progress_time >= i2f(AUTOPLAY_STALL_TIME)) {
		// every primary goal invalidated (md-08's "All Iotas down" invalidates the escort): the mission was lost
		for (i = 0; Autoplay_scan && (primaries == 0) && (i < Num_goals); i++) {
			if (((Mission_goals[i].type & GOAL_TYPE_MASK) == PRIMARY_GOAL) && (Mission_goals[i].type & INVALID_GOAL)) {
				autotest_logf("Autoplay: every primary goal was invalidated, the mission is lost\n");
				return AUTOPLAY_END_FAILED;
			}
		}
		// what each ship to protect is doing, to show why nothing moves on
		for (i = 0; Autoplay_scan && (i < MAX_SHIPS); i++) {
			if ((Ships[i].objnum >= 0) && (Ap_role[i] & AP_PROTECT)) {
				object *pp = &Objects[Ships[i].objnum];
				autotest_logf("Autoplay: '%s' (a ship to protect) hull %.0f, engines %.0f%%, speed %.1f, ai mode %d%s at the stall\n", Ships[i].ship_name, pp->hull_strength, 100.0f * ship_get_subsystem_strength(&Ships[i], SUBSYSTEM_ENGINE), pp->phys_info.fspeed, Ai_info[Ships[i].ai_index].mode, (Ships[i].flags & SF_DISABLED) ? ", disabled" : "");
			}
		}
		return AUTOPLAY_END_STALLED;
	}

	ap_think();
	return AUTOPLAY_END_NONE;
}

// ------------------------------------------------------------------------------------------------
// Campaign verdict (#268): many missions send the player home with a goal failed and still go on, so
// whether a mission was passed is what its campaign does next.  The game's own branch formula is
// evaluated with this mission's goals and events as the debriefing would store them (incomplete
// ones failed), without changing them.  Returns "advances to X", "repeats", "ends the campaign" or
// "not in a campaign".

// 1 if mission file names a and b match, ignoring the extension
static int ap_same_mission(char *a, char *b)
{
	char x[MAX_FILENAME_LEN], y[MAX_FILENAME_LEN], *p;

	strncpy(x, a, MAX_FILENAME_LEN - 1);
	x[MAX_FILENAME_LEN - 1] = 0;
	strncpy(y, b, MAX_FILENAME_LEN - 1);
	y[MAX_FILENAME_LEN - 1] = 0;
	if ((p = strchr(x, '.')) != NULL) {
		*p = 0;
	}
	if ((p = strchr(y, '.')) != NULL) {
		*p = 0;
	}
	return !stricmp(x, y);
}

static int ap_campaign_find(char *name)
{
	int i;

	for (i = 0; i < Campaign.num_missions; i++) {
		if (ap_same_mission(Campaign.missions[i].name, name)) {
			return i;
		}
	}
	return -1;
}

static void ap_campaign_verdict(char *out, int len)
{
	static char orig[MAX_FILENAME_LEN];
	char *list[MAX_CAMPAIGN_MISSIONS];
	cmission *cm;
	int cur, i, j, n, next, found, save_cur, save_next, save_loop, save_mode, loaded = 0;

	strcpy(out, "not in a campaign");
	strcpy(orig, Campaign.filename);
	cur = (Campaign.num_missions > 0) ? ap_campaign_find(Game_current_mission_filename) : -1;
	if (cur < 0) {
		// not in the pilot's campaign (Silent Threat): find the single-player campaign file that holds it
		// (the campaign room lists loose files only; Silent Threat's is in a pack)
		char *files[MAX_CAMPAIGNS];
		int nfiles = cf_get_file_list(MAX_CAMPAIGNS, files, CF_TYPE_MISSIONS, NOX("*.fc2"), CF_SORT_NAME);
		for (i = 0; (i < nfiles) && (cur < 0); i++) {
			n = mission_campaign_get_mission_list(files[i], list, MAX_CAMPAIGN_MISSIONS);
			for (found = 0, j = 0; j < n; j++) {
				found |= ap_same_mission(list[j], Game_current_mission_filename);
				free(list[j]);
			}
			if (found && !mission_campaign_load(files[i], 0)) {
				loaded = 1;
				cur = ap_campaign_find(Game_current_mission_filename);
			}
		}
		for (i = 0; i < nfiles; i++) {
			free(files[i]);
		}
	}
	if (cur < 0) {
		if (loaded && orig[0]) {
			mission_campaign_load(orig);
		}
		return;
	}

	cm = &Campaign.missions[cur];
	if (cm->num_goals > 0) {
		free(cm->goals);
	}
	if (cm->num_events > 0) {
		free(cm->events);
	}
	cm->num_goals = Num_goals;
	cm->goals = (Num_goals > 0) ? (mgoal *)malloc(sizeof(mgoal) * Num_goals) : NULL;
	for (i = 0; i < Num_goals; i++) {
		if (strlen(Mission_goals[i].name) == 0) {
			sprintf(cm->goals[i].name, "Goal #%d", i);
		} else {
			strcpy(cm->goals[i].name, Mission_goals[i].name);
		}
		cm->goals[i].status = (char)((Mission_goals[i].satisfied == GOAL_INCOMPLETE) ? GOAL_FAILED : Mission_goals[i].satisfied);
	}
	cm->num_events = Num_mission_events;
	cm->events = (Num_mission_events > 0) ? (mevent *)malloc(sizeof(mevent) * Num_mission_events) : NULL;
	for (i = 0; i < Num_mission_events; i++) {
		if (strlen(Mission_events[i].name) == 0) {
			sprintf(cm->events[i].name, "Event #%d", i);
		} else {
			strcpy(cm->events[i].name, Mission_events[i].name);
		}
		cm->events[i].status = ((Mission_events[i].formula == -1) && Mission_events[i].result) ? EVENT_SATISFIED : EVENT_FAILED;
	}

	// the previous-goal and previous-event operators read the stored states only in campaign mode
	save_cur = Campaign.current_mission;
	save_next = Campaign.next_mission;
	save_loop = Campaign.loop_mission;
	save_mode = Game_mode;
	Campaign.current_mission = cur;
	Game_mode |= GM_CAMPAIGN_MODE;
	mission_campaign_eval_next_mission();
	next = Campaign.next_mission;
	Game_mode = save_mode;
	Campaign.current_mission = save_cur;
	Campaign.next_mission = save_next;
	Campaign.loop_mission = save_loop;

	if (next == cur) {
		strcpy(out, "repeats");
	} else if ((next < 0) || (next >= Campaign.num_missions)) {
		strcpy(out, "ends the campaign");
	} else {
		_snprintf(out, len - 1, "advances to %s", Campaign.missions[next].name);
		out[len - 1] = 0;
	}
	if (loaded && orig[0]) {
		mission_campaign_load(orig);
	}
}

void autoplay_result(int why)
{
	int primaries, complete, failed;
	char *result, partial[32], campaign[80], judged[64];
	static char *reasons[] = { "", "the mission ended", "time limit", "no progress", "a primary goal failed", "the player died", "the warp-out did not end the mission" };

	if (!Ap_active || Ap_reported) {
		return;
	}
	Ap_reported = 1;

	ap_count_goals(&primaries, &complete, &failed);
	if (Ap_died) {
		result = "FAILED";
	} else if (Ap_rtb && !(Autoplay_scan && (failed > Ap_failed_ok))) {
		// with Autoplay_scan, being sent home after a primary goal failed is no win (md-09's escape pods)
		result = "WON (return to base)";
	} else if (failed > Ap_failed_ok) {
		result = "FAILED";
	} else if ((primaries > 0) && (complete + failed == primaries) && (complete > 0)) {	// failed ones were replaced
		result = "WON";
	} else if (Autoplay_scan && (why == AUTOPLAY_END_FAILED)) {	// every primary invalidated (md-08)
		result = "FAILED";
	} else if (why == AUTOPLAY_END_STALLED) {
		result = "STALLED";
	} else if (why == AUTOPLAY_END_TIME) {
		result = "TIMEOUT";
	} else {
		sprintf(partial, "PARTIAL %d/%d", complete, primaries);
		result = partial;
	}
	ap_campaign_verdict(campaign, sizeof(campaign));
	// the campaign's own branch is the verdict (Tom, #268): many missions send the player home with a goal failed,
	// and a won mission whose branch event never fired is no win
	if (!Ap_died && (!strncmp(campaign, "advances", 8) || !strcmp(campaign, "ends the campaign")) && strncmp(result, "WON", 3)) {
		_snprintf(judged, sizeof(judged) - 1, "WON (campaign advances; goals read %s)", result);
		judged[sizeof(judged) - 1] = 0;
		result = judged;
	} else if (!strcmp(campaign, "repeats") && !strncmp(result, "WON", 3)) {
		_snprintf(judged, sizeof(judged) - 1, "FAILED (campaign repeats; goals read %s)", result);
		judged[sizeof(judged) - 1] = 0;
		result = judged;
	}
	autotest_logf("Autoplay result: %s, %d/%d primary goals complete, %d failed, mission time %.2f (%s), campaign: %s\n", result, complete, primaries, failed, ap_time(),
		((why >= 0) && (why < (int)(sizeof(reasons) / sizeof(reasons[0])))) ? reasons[why] : "", campaign);
}

// ------------------------------------------------------------------------------------------------
// Loadout (steward D-20261108-030855, option A): when a primary goal wants a capital ship destroyed
// and the player carries no bombs, autoplay puts the player in the toughest bomber in the ship pool
// with the heaviest bombs in the weapon pool, before launch, the way the loadout screens would.
// Goals still marked invalid count (md-12 validates its goals by event).  Only the player's own
// unlocked slot changes, within the pools; deterministic.

// MissionShipChoice.cpp's wing slot data (same definitions as there)
typedef struct ss_slot_info {
	int status;
	int sa_index;
	int original_ship_class;
} ss_slot_info;
typedef struct ss_wing_info {
	int num_slots;
	int wingnum;
	int is_late;
	ss_slot_info ss_slots[MAX_WING_SLOTS];
} ss_wing_info;
extern ss_wing_info *Ss_wings;
extern int wl_calc_missile_fit(int wi_index, int capacity);

#define AP_BIG_CLASS	(SIF_CAPITAL | SIF_SUPERCAP | SIF_CORVETTE | SIF_CRUISER)

// ship class of a ship in the mission or still to arrive, -1 if none
static int ap_class_of(char *name)
{
	p_object *pobjp;
	int s = ship_name_lookup(name, 1);

	if (s >= 0) {
		return Ships[s].ship_info_index;
	}
	for (pobjp = GET_FIRST(&ship_arrival_list); pobjp != END_OF_LIST(&ship_arrival_list); pobjp = GET_NEXT(pobjp)) {
		if (!stricmp(pobjp->name, name)) {
			return pobjp->ship_class;
		}
	}
	return -1;
}

// 1 if the sexp wants a capital ship destroyed (not negated)
static int ap_wants_big_dead(int n, int neg, int depth)
{
	int op, a, c;

	if ((n < 0) || (depth > AP_MAX_DEPTH)) {
		return 0;
	}
	if (CAR(n) != -1) {
		return ap_wants_big_dead(CAR(n), neg, depth + 1) || ap_wants_big_dead(CDR(n), neg, depth + 1);
	}
	if (Sexp_nodes[n].subtype != SEXP_ATOM_OPERATOR) {
		return ap_wants_big_dead(CDR(n), neg, depth + 1);
	}
	op = find_operator(CTEXT(n));
	if (op == OP_NOT) {
		return ap_wants_big_dead(CDR(n), !neg, depth + 1);
	}
	if ((op == OP_IS_DESTROYED_DELAY) && !neg) {
		for (a = (CDR(n) != -1) ? CDR(CDR(n)) : -1; a != -1; a = CDR(a)) {
			c = ap_class_of(CTEXT(a));
			if ((c >= 0) && (Ship_info[c].flags & AP_BIG_CLASS)) {
				return 1;
			}
		}
		return 0;
	}
	return ap_wants_big_dead(CDR(n), neg, depth + 1);
}

// the best weapon of a kind that ship class c may carry and the pool still has, -1 if none
static int ap_best_weapon(int c, int *avail, int subtype, int flags)
{
	int w, best = -1;

	for (w = 0; w < Num_weapon_types; w++) {
		if ((avail[w] <= 0) || !Ship_info[c].allowed_weapons[w] || (Weapon_info[w].subtype != subtype)) {
			continue;
		}
		if (flags && !(Weapon_info[w].wi_flags & flags)) {
			continue;
		}
		if ((best < 0) || (Weapon_info[w].damage > Weapon_info[best].damage)) {
			best = w;
		}
	}
	return best;
}

// commit_pressed, before the loadout is applied (record and replay alike)
// with Autoplay_scan, after the AI's own choice for the player's ship: both primaries linked, as a pilot
// would (Tom, 2026-11-08, #260).  Not when weapon slots run low (the AI's own rule), nor on a ship to
// disable or disarm unless both guns are puncture weapons
void autoplay_link(object *objp)
{
	ship *shipp;
	ship_weapon *swp;
	ai_info *aip;
	int t;

	if (!Autotest_rules || !Autoplay_scan || (objp != Player_obj) || (Game_mode & GM_MULTIPLAYER)) {
		return;
	}
	shipp = &Ships[objp->instance];
	swp = &shipp->weapons;
	if ((swp->num_primary_banks < 2) || (shipp->ai_index < 0) || (Num_weapons > (int)(MAX_WEAPONS * 0.75f))) {
		return;
	}
	aip = &Ai_info[shipp->ai_index];
	t = aip->target_objnum;
	if ((t >= 0) && (Objects[t].type == OBJ_SHIP) && (Ap_role[Objects[t].instance] & (AP_DISABLE | AP_DISARM))
		&& !((Weapon_info[swp->primary_bank_weapons[0]].wi_flags & WIF_PUNCTURE) && (Weapon_info[swp->primary_bank_weapons[1]].wi_flags & WIF_PUNCTURE))) {
		return;
	}
	shipp->flags |= SF_PRIMARY_LINKED;
}

void autoplay_loadout()
{
	static int avail[MAX_WEAPON_TYPES];
	wss_unit *u, nu;
	ship_info *sip;
	int i, b, s, c, w, slot, st, fit, bombs;

	if (!Autotest_rules || !Autoplay_scan || (Game_mode & GM_MULTIPLAYER) || (Wss_num_wings <= 0) || !Ss_wings || !Wss_slots || !Ss_pool || !Wl_pool) {
		return;
	}
	for (i = 0; i < Num_goals; i++) {
		if (((Mission_goals[i].type & GOAL_TYPE_MASK) == PRIMARY_GOAL) && ap_wants_big_dead(Mission_goals[i].formula, 0, 0)) {
			break;
		}
	}
	if (i >= Num_goals) {
		return;
	}

	slot = -1;
	for (b = 0; (b < MAX_WING_BLOCKS) && (slot < 0); b++) {
		if (Ss_wings[b].wingnum < 0) {
			continue;
		}
		for (s = 0; s < MAX_WING_SLOTS; s++) {
			st = Ss_wings[b].ss_slots[s].status;
			if ((st & WING_SLOT_IS_PLAYER) && (st & WING_SLOT_FILLED) && !(st & WING_SLOT_IGNORE)) {
				slot = b * MAX_WING_SLOTS + s;
				break;
			}
		}
	}
	if (slot < 0) {
		return;
	}
	u = &Wss_slots[slot];
	if (u->ship_class < 0) {
		return;
	}
	for (i = MAX_WL_PRIMARY; i < MAX_WL_WEAPONS; i++) {
		if ((u->wep[i] >= 0) && (u->wep_count[i] > 0) && (Weapon_info[u->wep[i]].wi_flags & (WIF_BOMB | WIF_HUGE))) {
			return;		// already carries bombs
		}
	}

	// the ship: the player's own if it is a bomber, else the toughest bomber in the pool
	c = -1;
	if (Ship_info[u->ship_class].flags & SIF_BOMBER) {
		c = u->ship_class;
	} else {
		for (i = 0; i < Num_ship_types; i++) {
			if ((Ss_pool[i] > 0) && (Ship_info[i].flags & SIF_BOMBER) && ((c < 0) || (Ship_info[i].initial_hull_strength > Ship_info[c].initial_hull_strength))) {
				c = i;
			}
		}
	}
	if (c < 0) {
		return;
	}
	sip = &Ship_info[c];

	// the pool with the player's own weapons handed back
	for (w = 0; w < MAX_WEAPON_TYPES; w++) {
		avail[w] = (w < Num_weapon_types) ? Wl_pool[w] : 0;
	}
	for (i = 0; i < MAX_WL_WEAPONS; i++) {
		if ((u->wep[i] >= 0) && (u->wep_count[i] > 0)) {
			avail[u->wep[i]] += u->wep_count[i];
		}
	}

	nu.ship_class = c;
	for (i = 0; i < MAX_WL_WEAPONS; i++) {
		nu.wep[i] = -1;
		nu.wep_count[i] = 0;
	}
	for (i = 0; (i < sip->num_primary_banks) && (i < MAX_WL_PRIMARY); i++) {
		w = u->wep[i];
		if ((w < 0) || (avail[w] <= 0) || !sip->allowed_weapons[w]) {
			w = ap_best_weapon(c, avail, WP_LASER, 0);
		}
		if (w >= 0) {
			nu.wep[i] = w;
			nu.wep_count[i] = 1;
			avail[w]--;
		}
	}
	bombs = 0;
	for (i = 0; (i < sip->num_secondary_banks) && (i < MAX_WL_SECONDARY); i++) {
		w = ap_best_weapon(c, avail, WP_MISSILE, WIF_BOMB);
		if (w >= 0) {
			bombs++;
		} else {
			w = u->wep[MAX_WL_PRIMARY + i];
			if ((w < 0) || (avail[w] <= 0) || !sip->allowed_weapons[w]) {
				w = ap_best_weapon(c, avail, WP_MISSILE, 0);
			}
		}
		if (w >= 0) {
			fit = wl_calc_missile_fit(w, sip->secondary_bank_ammo_capacity[i]);
			if (fit > avail[w]) {
				fit = avail[w];
			}
			if (fit > 0) {
				nu.wep[MAX_WL_PRIMARY + i] = w;
				nu.wep_count[MAX_WL_PRIMARY + i] = fit;
				avail[w] -= fit;
			}
		}
	}
	if (!bombs || (nu.wep[0] < 0)) {
		return;		// no bombs or no guns to be had: leave the loadout as it is
	}

	autotest_logf("Autoplay: loadout for a capital ship goal: %s -> %s, %s x%d\n", Ship_info[u->ship_class].name, sip->name, (nu.wep[MAX_WL_PRIMARY] >= 0) ? Weapon_info[nu.wep[MAX_WL_PRIMARY]].name : "-", nu.wep_count[MAX_WL_PRIMARY]);
	if (c != u->ship_class) {
		Ss_pool[u->ship_class]++;
		Ss_pool[c]--;
	}
	for (w = 0; w < Num_weapon_types; w++) {
		Wl_pool[w] = avail[w];
	}
	*u = nu;
}
