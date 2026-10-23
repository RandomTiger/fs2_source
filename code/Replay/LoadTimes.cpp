/*
 * Load times: see LoadTimes.h.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "pstypes.h"
#include "LoadTimes.h"
#include "Timer.h"

#define LOADTIMES_LOG		"loadtimes.log"
#define LOADTIMES_MAX_MARKS	64

static char *Loadtime_counter_names[LT_NUM_COUNTERS] = {
	"cf_find_file_location",
	"cfopen",
	"model_load",
	"snd_load",
	"bm_lock (page_in_stop)",
	"gr_preload",
};

static int Loadtime_active = 0;
static DWORD Loadtime_thread = 0;
static int Loadtime_count = 0;						// loads timed in this process
static char Loadtime_mission[MAX_PATH];
static fix Loadtime_begin_time;
static int Loadtime_num_marks;
static char *Loadtime_mark_names[LOADTIMES_MAX_MARKS];
static fix Loadtime_mark_times[LOADTIMES_MAX_MARKS];
static int Loadtime_calls[LT_NUM_COUNTERS];
static fix Loadtime_time[LT_NUM_COUNTERS];
static double Loadtime_bytes[LT_NUM_COUNTERS];

static int loadtime_ms(fix t)
{
	return (int)(((double)t * 1000.0) / 65536.0 + 0.5);
}

void loadtime_begin(char *mission)
{
	if (!Cmdline_loadtimes) {
		return;
	}
	Loadtime_active = 1;
	Loadtime_thread = GetCurrentThreadId();
	Loadtime_count++;
	strncpy(Loadtime_mission, (mission && mission[0]) ? mission : "(unknown)", MAX_PATH - 1);
	Loadtime_mission[MAX_PATH - 1] = 0;
	Loadtime_num_marks = 0;
	memset(Loadtime_calls, 0, sizeof(Loadtime_calls));
	memset(Loadtime_time, 0, sizeof(Loadtime_time));
	memset(Loadtime_bytes, 0, sizeof(Loadtime_bytes));
	Loadtime_begin_time = timer_get_fixed_seconds_unrecorded();
}

void loadtime_mark(char *what)
{
	if (!Loadtime_active || (Loadtime_num_marks >= LOADTIMES_MAX_MARKS)) {
		return;
	}
	Loadtime_mark_names[Loadtime_num_marks] = what;
	Loadtime_mark_times[Loadtime_num_marks] = timer_get_fixed_seconds_unrecorded();
	Loadtime_num_marks++;
}

void loadtime_end()
{
	FILE *fp;
	fix prev;
	int i;

	if (!Loadtime_active) {
		return;
	}
	loadtime_mark("end");
	Loadtime_active = 0;

	fp = fopen(LOADTIMES_LOG, "a");
	if (!fp) {
		return;
	}
	fprintf(fp, "=== load %d in this process: %s\n", Loadtime_count, Loadtime_mission);
	fprintf(fp, "%8s %8s  %s\n", "step ms", "total ms", "mark");
	prev = Loadtime_begin_time;
	for (i = 0; i < Loadtime_num_marks; i++) {
		fprintf(fp, "%8d %8d  %s\n", loadtime_ms(Loadtime_mark_times[i] - prev), loadtime_ms(Loadtime_mark_times[i] - Loadtime_begin_time), Loadtime_mark_names[i]);
		prev = Loadtime_mark_times[i];
	}
	fprintf(fp, "%8s %8s %12s  %s\n", "calls", "ms", "bytes", "counter");
	for (i = 0; i < LT_NUM_COUNTERS; i++) {
		fprintf(fp, "%8d %8d %12.0f  %s\n", Loadtime_calls[i], loadtime_ms(Loadtime_time[i]), Loadtime_bytes[i], Loadtime_counter_names[i]);
	}
	fprintf(fp, "\n");
	fclose(fp);
}

fix loadtime_start()
{
	if (!Loadtime_active || (GetCurrentThreadId() != Loadtime_thread)) {
		return 0;
	}
	return timer_get_fixed_seconds_unrecorded();
}

void loadtime_add(int counter, fix start, int bytes)
{
	if (!start || !Loadtime_active || (GetCurrentThreadId() != Loadtime_thread)) {
		return;
	}
	if ((counter < 0) || (counter >= LT_NUM_COUNTERS)) {
		return;
	}
	Loadtime_calls[counter]++;
	Loadtime_time[counter] += timer_get_fixed_seconds_unrecorded() - start;
	Loadtime_bytes[counter] += bytes;
}
