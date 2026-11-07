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
#include "Sexp.h"
#include "MissionMessage.h"
#include "AWACS.h"
#include "LinkList.h"
#include "vecmat.h"

int Autoplay_on = 0;

#define AP_NONE				0			// no order (AI_GOAL_* modes are bits)
#define AP_MAX_DEPTH		24			// sexp nesting, event-true and goal-true links

// what a mission goal or directive wants done to a ship
#define AP_DESTROY			(1 << 0)
#define AP_PROTECT			(1 << 1)
#define AP_DISABLE			(1 << 2)
#define AP_DISARM			(1 << 3)
#define AP_SUBSYS			(1 << 4)	// destroy the subsystem in Ap_subsys

// attack tiers, lowest first
#define AP_TIER_BOMBER_ON_PROTECT		0
#define AP_TIER_FIGHTER_ON_PROTECT		1
#define AP_TIER_GOAL_SMALL				2	// disable, disarm, subsystem, or destroy a small ship
#define AP_TIER_GOAL_BIG					3	// destroy a big ship
#define AP_TIER_FIGHTER					4
#define AP_TIER_OTHER						5
#define AP_TIER_NONE						99

static int Ap_active = 0;					// this mission: autoplay on, and the AI flies the player
static int Ap_role[MAX_SHIPS];
static char Ap_subsys[MAX_SHIPS][32];
static int Ap_event_seen[MAX_MISSION_EVENTS];
static int Ap_goal_seen[MAX_GOALS];
static int Ap_goal_departs;				// the goal being read wants the player to depart

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
static int Ap_rtb;							// that event has fired: a success (Tom, 2026-11-07)
static int Ap_zone_wp, Ap_zone_pt;		// the warp zone: a waypoint list and point, or -1
static int Ap_zone_ship;					// or a ship, or -1
static float Ap_zone_dist;					// the player may warp out only this close to it
static int Ap_zone_logged;

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
	if (subsys && !Ap_subsys[shipnum][0]) {
		strncpy(Ap_subsys[shipnum], subsys, sizeof(Ap_subsys[shipnum]) - 1);
	}
}

// a ship or wing name; departing means a has-departed that wants the ship to leave
static void ap_mark(char *name, int role, char *subsys, int departing)
{
	int s, w, i;

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

static int ap_primary(int i)
{
	return ((Mission_goals[i].type & GOAL_TYPE_MASK) == PRIMARY_GOAL) && !(Mission_goals[i].type & INVALID_GOAL);
}

// fills Ap_role from the incomplete primary goals and the pending directives; returns how many of
// those goals want more than the player's departure
static int ap_scan()
{
	int i, left = 0;

	memset(Ap_role, 0, sizeof(Ap_role));
	memset(Ap_subsys, 0, sizeof(Ap_subsys));
	memset(Ap_event_seen, 0, sizeof(Ap_event_seen));
	memset(Ap_goal_seen, 0, sizeof(Ap_goal_seen));

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
		n += !(Mission_goals[i].type & INVALID_GOAL) && (Mission_goals[i].satisfied == GOAL_INCOMPLETE);
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
	if (!stricmp(CTEXT(n), "allow-warp")) {
		for (a = args; a != -1; a = CDR(a)) {
			if ((CAR(a) == -1) && !stricmp(CTEXT(a), Player_ship->ship_name)) {
				*allow = 1;
			}
		}
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

static int ap_hostile(ship *sp)
{
	return (sp->team & (TEAM_HOSTILE | TEAM_TRAITOR)) != 0;
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
	if (!ap_hostile(sp) || (sif & SIF_HARMLESS) || (objp->flags & OF_PROTECTED)) {
		return AP_TIER_NONE;
	}
	if (sif & (SIF_FIGHTER | SIF_BOMBER)) {
		*why = "nearest hostile fighter";
		return AP_TIER_FIGHTER;
	}
	*why = "nearest hostile";
	return AP_TIER_OTHER;
}

static void ap_think()
{
	ship_obj *so;
	object *objp;
	ship *sp;
	int tier, mode, submode, best = -1, best_tier = AP_TIER_NONE, best_mode = AP_NONE, best_submode = 0;
	int guard = -1, guard_protect = 0, role, follow, hidden = -1, hidden_tier = AP_TIER_NONE;
	float d, best_d = 0.0f, guard_d = 0.0f, hidden_d = 0.0f;
	char *why = "", *best_why = "";

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
		if (ap_hostile(sp) || ((sp->team != Player_ship->team) && (role & (AP_DESTROY | AP_DISABLE | AP_DISARM | AP_SUBSYS)) && !(role & AP_PROTECT))) {
			tier = ap_tier(sp, objp, &mode, &submode, &why);
			// not yet confirmed on the HUD (nebula, stealth): the player could not target it, so it is
			// only a place to fly to (Tom, 2026-11-07)
			if ((tier != AP_TIER_NONE) && (awacs_get_level(objp, Player_ship) < 1.0f)) {
				if ((tier < hidden_tier) || ((tier == hidden_tier) && (d < hidden_d))) {
					hidden = objp->instance;
					hidden_tier = tier;
					hidden_d = d;
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

	if (best >= 0) {
		ap_order(best_mode, best_submode, best, best_why);
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

// --- the run ---

void autoplay_mission_start()
{
	int i;

	Ap_active = Autoplay_on && Autotest_player_npc;
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

int autoplay_frame()
{
	int i, progress, primaries, complete, failed;

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

	progress = 0;
	for (i = 0; i < Num_goals; i++) {
		progress += (Mission_goals[i].satisfied != GOAL_INCOMPLETE);
	}
	for (i = 0; i < Num_mission_events; i++) {
		progress += (Mission_events[i].result != 0);
	}
	if (progress != Ap_progress) {
		Ap_progress = progress;
		Ap_progress_time = Missiontime;
	}

	// Command's "return to base" is a success in any mission (Tom, 2026-11-07): warp out
	for (i = 0; (i < Num_mission_events) && !Ap_rtb; i++) {
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
			Ap_fail_objectives = ap_open_objectives();
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

	if (Ap_warping) {
		return (Missiontime - Ap_warp_time >= i2f(AUTOPLAY_WARP_TIME)) ? AUTOPLAY_END_NO_WARP : AUTOPLAY_END_NONE;
	}

	// no warp-out while waiting on a failed goal: the mission may still give new objectives.  Nor while
	// no goal is complete: a lone "has-departed Alpha 1" goal is a placeholder (sm3-08, sm3-10) and the
	// mission's events, its "return to base" or its own end decide instead
	if (Ap_rtb || ((ap_scan() == 0) && (primaries > 0) && (complete > 0) && (Ap_fail_time < 0))) {
		vector *zone_pos;
		float zone_d = ap_zone_distance(&zone_pos);

		// the mission allows the warp-out only near its jump node: fly there first, and warp once the
		// mission has allowed it
		if ((zone_d >= 0.0f) && ((zone_d > Ap_zone_dist * 0.8f) || (Player_ship->flags & SF_WARP_NEVER))) {
			if (!Ap_zone_logged) {
				Ap_zone_logged = 1;
				autotest_logf("Autoplay: the mission allows a warp-out only within %.0f m of its jump node, %.0f m away\n", Ap_zone_dist, zone_d);
			}
			if (Ap_zone_wp >= 0) {
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
		} else if (Missiontime - Ap_done_time >= i2f(AUTOPLAY_SETTLE_TIME)) {
			// the player's own warp-out, as Alt-J starts it; the player's controls (not the AI)
			// fly the warp-out, so the AI stops flying the player
			ap_order(AP_NONE, 0, -1, "");
			Autotest_player_npc = 0;
			Ap_warping = 1;
			Ap_warp_time = Missiontime;
			autotest_logf("Autoplay: warping out at mission time %.2f\n", ap_time());
			gameseq_post_event(GS_EVENT_PLAYER_WARPOUT_START);
			return AUTOPLAY_END_NONE;
		}
	} else {
		Ap_done_time = -1;
	}

	if (Missiontime - Ap_progress_time >= i2f(AUTOPLAY_STALL_TIME)) {
		return AUTOPLAY_END_STALLED;
	}

	ap_think();
	return AUTOPLAY_END_NONE;
}

void autoplay_result(int why)
{
	int primaries, complete, failed;
	char *result, partial[32];
	static char *reasons[] = { "", "the mission ended", "time limit", "no progress", "a primary goal failed", "the player died", "the warp-out did not end the mission" };

	if (!Ap_active || Ap_reported) {
		return;
	}
	Ap_reported = 1;

	ap_count_goals(&primaries, &complete, &failed);
	if (Ap_died) {
		result = "FAILED";
	} else if (Ap_rtb) {
		result = "WON (return to base)";
	} else if (failed > Ap_failed_ok) {
		result = "FAILED";
	} else if ((primaries > 0) && (complete + failed == primaries) && (complete > 0)) {	// failed ones were replaced
		result = "WON";
	} else if (why == AUTOPLAY_END_STALLED) {
		result = "STALLED";
	} else if (why == AUTOPLAY_END_TIME) {
		result = "TIMEOUT";
	} else {
		sprintf(partial, "PARTIAL %d/%d", complete, primaries);
		result = partial;
	}
	autotest_logf("Autoplay result: %s, %d/%d primary goals complete, %d failed, mission time %.2f (%s)\n", result, complete, primaries, failed, ap_time(),
		((why >= 0) && (why < (int)(sizeof(reasons) / sizeof(reasons[0])))) ? reasons[why] : "");
}
