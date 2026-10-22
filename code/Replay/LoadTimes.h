/*
 * Load times: a diagnostic log of where mission loading spends its time.  Off by default.
 *
 * Usage:
 *   -loadtimes
 *
 * With -loadtimes, each mission load appends one block to loadtimes.log in the game directory: the
 * mission name, then one line per mark with the ms since the previous mark and since the load began,
 * then the counters (calls, total ms and bytes where known) for file lookup, cfopen, model_load,
 * snd_load, and the bm_lock and gr_preload calls of bm_page_in_stop.  Counters count only the main
 * thread and only while a load is being timed.
 *
 * Every time is read from timer_get_fixed_seconds_unrecorded, so the log adds nothing to a recording
 * and reads nothing from one; a replay with -loadtimes plays the same as one without.
 */

#ifndef _LOADTIMES_H
#define _LOADTIMES_H

#include "pstypes.h"

enum {
	LT_CF_FIND,			// cf_find_file_location
	LT_CFOPEN,			// cfopen
	LT_MODEL_LOAD,		// model_load
	LT_SND_LOAD,		// snd_load
	LT_BM_LOCK,			// bm_lock + bm_unlock in bm_page_in_stop, bytes = decoded size
	LT_D3D_PRELOAD,		// gr_preload in bm_page_in_stop
	LT_NUM_COUNTERS
};

extern int Cmdline_loadtimes;

void loadtime_begin(char *mission);		// start of a mission load
void loadtime_mark(char *what);			// a step of the load has finished
void loadtime_end();					// end of the load; writes the block

fix loadtime_start();							// 0 unless a load is being timed on this thread
void loadtime_add(int counter, fix start, int bytes);	// adds the time since start (from loadtime_start)

// Times the rest of the enclosing block into one counter.
class loadtime_scope {
public:
	loadtime_scope(int counter) : Counter(counter), Start(loadtime_start()) {}
	~loadtime_scope() { loadtime_add(Counter, Start, 0); }
private:
	int Counter;
	fix Start;
};

#endif
