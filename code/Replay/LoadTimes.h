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
 * Texture loads after level paging: a bitmap read from disk in bm_lock after bm_page_in_stop, during play, is a
 * texture made inside a frame.  Each one is counted as a texture, an animation (BM_TYPE_ANI, all frames) or a nebula
 * bitmap, and logged by name (Debug log always, loadtimes.log with -loadtimes).  At game_level_close one line gives
 * the totals for the level: "after load: textures N (bytes) animations N (bytes) nebula N (bytes)".  The counts read
 * no timer and change nothing, so they are kept with the log off.
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
	LT_BM_LOCK,			// bm_lock + bm_unlock in bm_page_in_stop, bytes = decoded size (not animations)
	LT_BM_LOCK_ANI,		// the same for animation frames (BM_TYPE_ANI)
	LT_D3D_PRELOAD,		// gr_preload in bm_page_in_stop
	LT_NUM_COUNTERS
};

extern int Cmdline_loadtimes;

void loadtime_begin(char *mission);		// start of a mission load
void loadtime_mark(char *what);			// a step of the load has finished
void loadtime_end();					// end of the load; writes the block

// Texture loads after level paging (see above).
enum {
	LT_LATE_TEXTURE,
	LT_LATE_ANI,
	LT_LATE_NEBULA,
	LT_LATE_NUM
};
void loadtime_level_paged();								// end of bm_page_in_stop: loads during play from now on are late
void loadtime_late_bitmap(char *filename, int kind, int bytes);	// bm_lock read a bitmap from disk
void loadtime_level_close();								// game_level_close: writes the totals, stops counting
extern int Loadtime_late_count[LT_LATE_NUM];				// late loads in the current level, by kind

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
