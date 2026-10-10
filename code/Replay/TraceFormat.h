/*
 * Gameplay state trace format.  Shared by the game (StateTrace.cpp) and tools/TraceDiff.
 *
 * File:   "FS2TRACE" (8 bytes), int32 version
 * Frame:  int32 frame, int32 missiontime (fix), int32 frametime (fix), int32 timestamp_ticker,
 *         int32 rand_count, uint32 hash_lo, uint32 hash_hi, int32 payload_size,
 *         payload_size bytes (0 when only the hash was written this frame)
 *
 * The hash is FNV-1a 64 over the payload, which is built every frame whether or not it is
 * written out.  All values are 4 bytes, little-endian.
 *
 * Payload:
 *   int32 count, then count values of the player's control_info
 *   int32 count, then count values of the player's button_info
 *   int32 num_events, then per event TRE_COUNT values
 *   int32 num_goals, then per goal TRG_COUNT values
 *   int32 num_objects, then per object: int32 signature, int32 type, int32 count, count values
 *     values: TRO_COMMON_COUNT common values, then by type:
 *       ship:   TRO_SHIP_COUNT values, then one float per subsystem (current hits)
 *       weapon: TRO_WEAPON_COUNT values
 */

#ifndef _FS2_TRACE_FORMAT_H
#define _FS2_TRACE_FORMAT_H

#define TRACE_VERSION		1

// value kinds, used by TraceDiff to pick a tolerance
#define TRK_INT			0		// always exact
#define TRK_POS			1		// metres
#define TRK_ORIENT		2		// matrix element
#define TRK_VEL			3		// metres / second
#define TRK_ROTVEL		4		// radians / second
#define TRK_HEALTH		5		// hull, shields, subsystem hits
#define TRK_ENERGY		6		// weapon energy, afterburner fuel, lifetimes
#define TRK_CONTROL		7		// control inputs
#define TRK_NUM_KINDS	8

typedef struct trace_field {
	char	*name;
	int	kind;
} trace_field;

// mission events
#define TRE_COUNT		6
static trace_field Trace_event_fields[TRE_COUNT] = {
	{ "result", TRK_INT }, { "flags", TRK_INT }, { "count", TRK_INT },
	{ "satisfied_time", TRK_INT }, { "repeat_count", TRK_INT }, { "timestamp", TRK_INT },
};

// mission goals
#define TRG_COUNT		2
static trace_field Trace_goal_fields[TRG_COUNT] = {
	{ "satisfied", TRK_INT }, { "flags", TRK_INT },
};

// control_info
#define TRC_COUNT		13
static trace_field Trace_control_fields[TRC_COUNT] = {
	{ "pitch", TRK_CONTROL }, { "vertical", TRK_CONTROL }, { "heading", TRK_CONTROL },
	{ "sideways", TRK_CONTROL }, { "bank", TRK_CONTROL }, { "forward", TRK_CONTROL },
	{ "forward_cruise_percent", TRK_CONTROL }, { "fire_primary_count", TRK_INT },
	{ "fire_secondary_count", TRK_INT }, { "fire_countermeasure_count", TRK_INT },
	{ "fire_debug_count", TRK_INT }, { "afterburner_start", TRK_INT }, { "afterburner_stop", TRK_INT },
};

// every object
#define TRO_COMMON_COUNT	24
static trace_field Trace_common_fields[TRO_COMMON_COUNT] = {
	{ "flags", TRK_INT },
	{ "pos.x", TRK_POS }, { "pos.y", TRK_POS }, { "pos.z", TRK_POS },
	{ "orient.rvec.x", TRK_ORIENT }, { "orient.rvec.y", TRK_ORIENT }, { "orient.rvec.z", TRK_ORIENT },
	{ "orient.uvec.x", TRK_ORIENT }, { "orient.uvec.y", TRK_ORIENT }, { "orient.uvec.z", TRK_ORIENT },
	{ "orient.fvec.x", TRK_ORIENT }, { "orient.fvec.y", TRK_ORIENT }, { "orient.fvec.z", TRK_ORIENT },
	{ "vel.x", TRK_VEL }, { "vel.y", TRK_VEL }, { "vel.z", TRK_VEL },
	{ "rotvel.x", TRK_ROTVEL }, { "rotvel.y", TRK_ROTVEL }, { "rotvel.z", TRK_ROTVEL },
	{ "hull", TRK_HEALTH },
	{ "shield[0]", TRK_HEALTH }, { "shield[1]", TRK_HEALTH }, { "shield[2]", TRK_HEALTH }, { "shield[3]", TRK_HEALTH },
};

// ships, after the common values
#define TRO_SHIP_COUNT		15
static trace_field Trace_ship_fields[TRO_SHIP_COUNT] = {
	{ "ship_flags", TRK_INT }, { "ship_info_index", TRK_INT },
	{ "weapon_energy", TRK_ENERGY }, { "afterburner_fuel", TRK_ENERGY },
	{ "current_primary_bank", TRK_INT }, { "current_secondary_bank", TRK_INT },
	{ "secondary_ammo[0]", TRK_INT }, { "secondary_ammo[1]", TRK_INT },
	{ "secondary_ammo[2]", TRK_INT }, { "secondary_ammo[3]", TRK_INT },
	{ "ai.mode", TRK_INT }, { "ai.submode", TRK_INT }, { "ai.target_signature", TRK_INT },
	{ "ai.active_goal", TRK_INT }, { "ai.ai_flags", TRK_INT },
};

// weapons, after the common values
#define TRO_WEAPON_COUNT	4
static trace_field Trace_weapon_fields[TRO_WEAPON_COUNT] = {
	{ "weapon_info_index", TRK_INT }, { "lifeleft", TRK_ENERGY },
	{ "target_signature", TRK_INT }, { "weapon_flags", TRK_INT },
};

static char *Trace_kind_names[TRK_NUM_KINDS] = {
	"int", "pos", "orient", "vel", "rotvel", "health", "energy", "control"
};

#endif
