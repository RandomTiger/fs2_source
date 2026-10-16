/*
 * Gameplay state trace.  Every field is written explicitly; structs are never dumped raw,
 * because they hold pointers and padding that change from run to run.
 */

#include <stdio.h>

#include "pstypes.h"
#include "StateTrace.h"
#include "TraceFormat.h"
#include "FreeSpace.h"
#include "SystemVars.h"
#include "timer.h"
#include "object.h"
#include "ship.h"
#include "ai.h"
#include "weapon.h"
#include "Player.h"
#include "MissionGoals.h"
#include "linklist.h"
extern matrix Eye_matrix;	// Claude: the eye (Render/3dSetup.cpp), set by game_view_set
extern vector Eye_position;

static FILE *Trace_fp = NULL;
static int Trace_full = 0;

static ubyte *Tbuf = NULL;
static int Tbuf_size = 0, Tbuf_len = 0;

static void t_put(void *data, int size)
{
	if (Tbuf_len + size > Tbuf_size) {
		Tbuf_size = Tbuf_size ? Tbuf_size * 2 : 65536;
		while (Tbuf_len + size > Tbuf_size) {
			Tbuf_size *= 2;
		}
		Tbuf = (ubyte *)realloc(Tbuf, Tbuf_size);
	}
	memcpy(Tbuf + Tbuf_len, data, size);
	Tbuf_len += size;
}

static void t_int(int v)
{
	t_put(&v, 4);
}

static void t_float(float f)
{
	t_put(&f, 4);
}

static void t_vec(vector *v)
{
	t_float(v->x);
	t_float(v->y);
	t_float(v->z);
}

static int t_objnum_sig(int objnum)
{
	if ((objnum < 0) || (objnum >= MAX_OBJECTS)) {
		return -1;
	}
	return Objects[objnum].signature;
}

static void trace_object(object *objp)
{
	int count_pos, count;
	int i;

	t_int(objp->signature);
	t_int(objp->type);
	count_pos = Tbuf_len;
	t_int(0);

	t_int((int)objp->flags);
	t_vec(&objp->pos);
	t_vec(&objp->orient.rvec);
	t_vec(&objp->orient.uvec);
	t_vec(&objp->orient.fvec);
	t_vec(&objp->phys_info.vel);
	t_vec(&objp->phys_info.rotvel);
	t_float(objp->hull_strength);
	for (i = 0; i < MAX_SHIELD_SECTIONS; i++) {
		t_float(objp->shields[i]);
	}

	if (objp->type == OBJ_SHIP) {
		ship *shipp = &Ships[objp->instance];
		ai_info *aip = &Ai_info[shipp->ai_index];
		ship_subsys *ss;

		t_int((int)shipp->flags);
		t_int(shipp->ship_info_index);
		t_float(shipp->weapon_energy);
		t_float(shipp->afterburner_fuel);
		t_int(shipp->weapons.current_primary_bank);
		t_int(shipp->weapons.current_secondary_bank);
		for (i = 0; i < MAX_SECONDARY_BANKS; i++) {
			t_int(shipp->weapons.secondary_bank_ammo[i]);
		}
		t_int(aip->mode);
		t_int(aip->submode);
		t_int(t_objnum_sig(aip->target_objnum));
		t_int(aip->active_goal);
		t_int(aip->ai_flags);

		for (ss = GET_FIRST(&shipp->subsys_list); ss != END_OF_LIST(&shipp->subsys_list); ss = GET_NEXT(ss)) {
			t_float(ss->current_hits);
		}
	} else if (objp->type == OBJ_WEAPON) {
		weapon *wp = &Weapons[objp->instance];

		t_int(wp->weapon_info_index);
		t_float(wp->lifeleft);
		t_int(t_objnum_sig(wp->target_num));
		t_int(wp->weapon_flags);
	}

	count = (Tbuf_len - count_pos - 4) / 4;
	memcpy(Tbuf + count_pos, &count, 4);
}

// the eye the frame was simulated with (trace version 2, TraceFormat.h)
static void trace_eye()
{
	vector zero = { 0.0f, 0.0f, 0.0f };
	int i;

	t_int(TRACE_EYE_SIGNATURE);
	t_int(TRACE_EYE_TYPE);
	t_int(TRO_COMMON_COUNT);
	t_int(0);
	t_vec(&Eye_position);
	t_vec(&Eye_matrix.rvec);
	t_vec(&Eye_matrix.uvec);
	t_vec(&Eye_matrix.fvec);
	t_vec(&zero);
	t_vec(&zero);
	for (i = 0; i < 1 + MAX_SHIELD_SECTIONS; i++) {
		t_float(0.0f);
	}
}

void trace_open(char *filename, int full_every_frame)
{
	int version = TRACE_VERSION;

	Trace_fp = fopen(filename, "wb");
	if (!Trace_fp) {
		return;
	}
	Trace_full = full_every_frame;
	fwrite("FS2TRACE", 1, 8, Trace_fp);
	fwrite(&version, 4, 1, Trace_fp);
}

void trace_frame(int frame)
{
	object *objp;
	int i, count, count_pos, size;
	uint hash_lo, hash_hi;
	unsigned __int64 hash;
	int head[8];

	if (!Trace_fp) {
		return;
	}

	Tbuf_len = 0;

	t_int(TRC_COUNT);
	t_float(Player->ci.pitch);
	t_float(Player->ci.vertical);
	t_float(Player->ci.heading);
	t_float(Player->ci.sideways);
	t_float(Player->ci.bank);
	t_float(Player->ci.forward);
	t_float(Player->ci.forward_cruise_percent);
	t_int(Player->ci.fire_primary_count);
	t_int(Player->ci.fire_secondary_count);
	t_int(Player->ci.fire_countermeasure_count);
	t_int(Player->ci.fire_debug_count);
	t_int(Player->ci.afterburner_start);
	t_int(Player->ci.afterburner_stop);

	t_int(NUM_BUTTON_FIELDS);
	for (i = 0; i < NUM_BUTTON_FIELDS; i++) {
		t_int(Player->bi.status[i]);
	}

	t_int(Num_mission_events);
	for (i = 0; i < Num_mission_events; i++) {
		t_int(Mission_events[i].result);
		t_int(Mission_events[i].flags);
		t_int(Mission_events[i].count);
		t_int(Mission_events[i].satisfied_time);
		t_int(Mission_events[i].repeat_count);
		t_int(Mission_events[i].timestamp);
	}

	t_int(Num_goals);
	for (i = 0; i < Num_goals; i++) {
		t_int(Mission_goals[i].satisfied);
		t_int(Mission_goals[i].flags);
	}

	count_pos = Tbuf_len;
	t_int(0);
	count = 0;
	for (objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp)) {
		trace_object(objp);
		count++;
	}
	trace_eye();
	count++;
	memcpy(Tbuf + count_pos, &count, 4);

	// FNV-1a 64
	hash = 14695981039346656037ui64;
	for (i = 0; i < Tbuf_len; i++) {
		hash ^= Tbuf[i];
		hash *= 1099511628211ui64;
	}
	hash_lo = (uint)(hash & 0xffffffff);
	hash_hi = (uint)(hash >> 32);

	size = (Trace_full || ((frame % TRACE_FULL_INTERVAL) == 0)) ? Tbuf_len : 0;

	head[0] = frame;
	head[1] = Missiontime;
	head[2] = Frametime;
	head[3] = timestamp_ticker;
	head[4] = Rand_count;
	head[5] = (int)hash_lo;
	head[6] = (int)hash_hi;
	head[7] = size;
	fwrite(head, 4, 8, Trace_fp);
	if (size) {
		fwrite(Tbuf, 1, size, Trace_fp);
	}
}

void trace_close()
{
	if (Trace_fp) {
		fclose(Trace_fp);
		Trace_fp = NULL;
	}
}
