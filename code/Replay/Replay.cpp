/*
 * Gameplay recorder / replayer.  See Replay.h and docs/REPLAY.md.
 */

#include <windows.h>
#include <stdio.h>
#include <direct.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include "pstypes.h"
#include "Replay.h"
#include "DataSetInstall.h"
#include "StateTrace.h"
#include "FreeSpace.h"
#include "GameSequence.h"
#include "SystemVars.h"
#include "timer.h"
#include "key.h"
#include "mouse.h"
#include "2d.h"
#include "cfile.h"
#include "CfileSystem.h"
#include "StaticRand.h"
#include "Sound.h"
#include "EventMusic.h"
#include "Player.h"
#include "ManagePilot.h"
#include "MissionParse.h"
#include "MissionCampaign.h"
#include "MissionScreenCommon.h"
#include "RedAlert.h"
#include "Autotest.h"
#include "Popup.h"
#include "localize.h"
#include "OsApi.h"

#define REPLAY_FILE_VERSION		5	// Claude: 2 adds the autotest settings, 3 the view position, 4 (PrePort, D-035) leaves the sound-device answers and the flip's cursor read out of the stream, 5 (GodotPrep, D-056) the language and low-memory mode
#define REPLAY_PILOT_DIR			"Data\\Players\\Replay"
#define REPLAY_MAX_BLOB				(1024*1024)
#define REPLAY_PATH_LEN			MAX_PATH	// Claude: MAX_PATH_LEN (128) is too short for full paths

int Replay_mode = REPLAY_OFF;

static char Replay_file[REPLAY_PATH_LEN];
static char Replay_trace_file[REPLAY_PATH_LEN];
static int Replay_trace_full = 0;

// -replay_pace
static int Replay_pace = 0;
static int Replay_pace_frames = 0;
static LARGE_INTEGER Replay_pace_freq, Replay_pace_start;
static double Replay_pace_target = 0.0;	// seconds of real time the recorded frames so far took

// -autorecord
#define REPLAY_RESTART_ENV			"FS2_AUTORECORD_RESTART_PID"
static int Replay_auto = 0;				// record each mission to its own name in Replay_auto_dir
static char Replay_auto_dir[REPLAY_PATH_LEN];
static int Replay_auto_saved = 0;		// the recording was kept
static int Replay_auto_told = 0;			// the player was told this mission is not recorded
static int Replay_auto_menu_frames = 0;
static int Replay_auto_restarting = 0;

static int Replay_active = 0;				// stream is being recorded / consumed
static int Replay_done = 0;				// the recorded mission is over
static int Replay_depth = 0;				// nesting of hooked calls
static int Replay_injecting = 0;			// replay is feeding a recorded event into the live handler
static DWORD Replay_main_thread = 0;
static int Replay_load_count = 0;
static int Replay_in_game = 0;			// past GS_EVENT_ENTER_GAME
static int Replay_frame = 0;
static int Replay_entries = 0;
static int Replay_entries_by_tag[RT_NUM_TAGS];
static int Replay_snd_mismatch = 0;

// Diagnostic, set by the environment variable FS2_REPLAY_SOUND_LIVE=1 (D-027): in replay, the sound
// device is asked live and the recorded sound entries are skipped, so a build that makes the same
// sound calls in another order can still be replayed to the end and its trace compared.  Never used
// to make or check a reference recording.
#define REPLAY_SOUND_LIVE_ENV		"FS2_REPLAY_SOUND_LIVE"
static int Replay_sound_live = 0;
static int Replay_sound_skipped = 0;

// Diagnostic FS2_REPLAY_SOUND_MODEL=1 (D-031): in replay, every sound-state query is answered by a model (see
// Replay.h), and the recorded sound and music-stream entries are skipped.  Never used to make or check a reference
// recording.
#define REPLAY_LOG_READS_ENV		"FS2_REPLAY_LOG_READS"	// Claude: diagnostic, log the entries read up to N, the last three, with their call stacks (D-055)
static int Replay_log_reads = 0;
#define REPLAY_SOUND_MODEL_ENV		"FS2_REPLAY_SOUND_MODEL"
#define REPLAY_SND_MODEL_MAX			1024
typedef struct replay_snd_model {
	int	handle;
	int	start_ms;
	int	duration_ms;
	int	looping;
	int	stopped;
} replay_snd_model;
static int Replay_sound_model = 0;
static replay_snd_model Replay_snd_models[REPLAY_SND_MODEL_MAX];
static int Replay_snd_models_next = 0;
static int Replay_snd_model_handle = 0x40000000;	// for a sound the live device refused
static int Replay_snd_model_starts = 0;
static int Replay_last_int[RT_NUM_TAGS];
static int Replay_clock_offset[RT_NUM_TAGS];	// after the replay, added to the live clocks so they carry on from the recorded ones
static int Replay_clock_set[RT_NUM_TAGS];
static FILE *Replay_log = NULL;

// driving the menus during replay
static int Replay_started_mission = 0;
static int Replay_briefing_started = 0;
static int Replay_committed = 0;

// the header.  Only int32 and char fields, so the layout has no padding.
typedef struct replay_header {
	char	magic[8];
	int	version;
	char	build[64];
	char	mission[64];
	char	mission_modified[64];
	uint	mission_checksum;
	int	game_mode;
	char	campaign[64];
	char	callsign[64];
	int	level_seed;
	int	semirand_inited;
	int	semirand[SEMIRAND_MAX];
	int	skill_level;
	int	detail_size;
	int	detail[64];
	int	screen_w, screen_h;
	int	sound_enabled;
	int	snd_inited;
	int	event_music_enabled;
	float	master_sound_volume;
	float	master_voice_volume;
	float	master_event_music_volume;
	int	loadout_size;
	int	timestamp_ticker;
	int	last_time;
	int	last_delta_time;
	int	saved_timestamp_ticker;
	int	missiontime;
	int	pilot_size;
	int	campaign_size;
	int	autotest;					// Claude: version 2 adds these three.  The mission was played under autotest rules (Autotest.h); bit 1 is -playernpc
	int	autotest_time;
	int	autotest_speed;
	float	view_position[3];			// Claude: version 3.  Left by whatever the menus last drew in 3D; engine sounds use it until the player enters
	int	language;					// Claude: version 5 (D-056).  lcl_get_language(): picks the voice files, whose length times messages
	int	low_mem;					// Claude: version 5.  The low-memory bitmap mode, which halves animation rates (D-046)
} replay_header;

#define REPLAY_HEADER_V4_SIZE		(sizeof(replay_header) - 2 * sizeof(int))	// Claude: a version 3 or 4 header ends before the language
#define REPLAY_HEADER_V2_SIZE		(REPLAY_HEADER_V4_SIZE - 3 * sizeof(float))	// Claude: a version 2 header ends before the view position
#define REPLAY_HEADER_V1_SIZE		(REPLAY_HEADER_V2_SIZE - 3 * sizeof(int))	// Claude: a version 1 header ends before the autotest settings

static replay_header Hdr;
static loadout_data Hdr_loadout;
static ubyte *Hdr_pilot = NULL;
static ubyte *Hdr_campaign = NULL;

// write side
static FILE *Wfile = NULL;
static ubyte *Wbuf = NULL;
static int Wbuf_size = 0, Wbuf_len = 0;
static int Whead_written = 0;

// read side
static ubyte *Rdata = NULL;
static int Rsize = 0, Rpos = 0;

extern int Semirand_inited;
extern fix Last_time;
extern fix Last_delta_time;
extern int saved_timestamp_ticker;
extern int Event_music_enabled;
extern vector View_position;
extern int joy_num_sticks;
extern int joy_replay_set_sticks(int count);

static const char *Replay_tag_names[RT_NUM_TAGS] = {
	"none", "timer_fix", "timer_ms", "timer_us", "os_poll", "foreground", "ev_key", "ev_mouse",
	"ev_lostfocus", "ev_gotfocus", "mouse_pos", "joy_down", "joy_down_count", "joy_up_count",
	"joy_down_time", "joy_raw_axis", "joy_pos", "axes", "snd_play", "snd_play_3d", "snd_play_looping",
	"snd_is_playing", "snd_time_remaining", "snd_pitch", "snd_num_playing", "stream_is_playing",
	"stream_is_paused", "stream_done_reading", "enter_game", "end", "stream_bytes", "input_device", "joy_present"
};

// -----------------------------------------------------------------------------------------
// log
//

static void replay_logf(char *format, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, format);
	_vsnprintf(buf, sizeof(buf) - 1, format, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;

	if (Replay_log) {
		fputs(buf, Replay_log);
		fflush(Replay_log);
	}
	mprintf(("REPLAY: %s", buf));
}

static void replay_open_log()
{
	char name[REPLAY_PATH_LEN + 32];

	sprintf(name, "%s.%s.log%s", Replay_file, (Replay_mode == REPLAY_RECORD) ? "record" : "replay", Replay_auto ? ".tmp" : "");
	Replay_log = fopen(name, "wt");
}

static void replay_log_stats()
{
	int i;

	replay_logf("frames %d, entries %d, sound handle mismatches %d\n", Replay_frame, Replay_entries, Replay_snd_mismatch);
	for (i = 1; i < RT_NUM_TAGS; i++) {
		if (Replay_entries_by_tag[i]) {
			replay_logf("  %-20s %d\n", Replay_tag_names[i], Replay_entries_by_tag[i]);
		}
	}
}

// log the call stack, so a desync names the code that asked for the value
static void replay_log_stack()
{
	void *frames[24];
	char symbuf[sizeof(SYMBOL_INFO) + 256];
	SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
	IMAGEHLP_LINE64 line;
	DWORD disp;
	HANDLE proc = GetCurrentProcess();
	int i, count;

	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	if (!SymInitialize(proc, NULL, TRUE)) {
		return;
	}
	count = CaptureStackBackTrace(1, 24, frames, NULL);
	for (i = 0; i < count; i++) {
		memset(symbuf, 0, sizeof(symbuf));
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 255;
		memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);
		if (SymFromAddr(proc, (DWORD64)frames[i], NULL, sym) && SymGetLineFromAddr64(proc, (DWORD64)frames[i], &disp, &line)) {
			replay_logf("  %s  %s:%d\n", sym->Name, line.FileName, line.LineNumber);
		} else if (SymFromAddr(proc, (DWORD64)frames[i], NULL, sym)) {
			replay_logf("  %s\n", sym->Name);
		} else {
			replay_logf("  %p\n", frames[i]);
		}
	}
	SymCleanup(proc);
}

// a recorded value or event did not match what this run asked for.  Nothing after this point
// can be trusted, so stop the process.
static void replay_desync(char *format, ...)
{
	va_list args;
	char buf[512];

	va_start(args, format);
	_vsnprintf(buf, sizeof(buf) - 1, format, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;

	replay_logf("DESYNC at frame %d, entry %d (byte %d): %s\n", Replay_frame, Replay_entries, Rpos, buf);
	replay_log_stack();
	replay_log_stats();
	replay_logf("REPLAY FAILED\n");
	trace_close();
	if (Replay_log) {
		fclose(Replay_log);
		Replay_log = NULL;
	}
	TerminateProcess(GetCurrentProcess(), 3);
}

static void replay_fatal(char *format, ...)
{
	va_list args;
	char buf[512];

	va_start(args, format);
	_vsnprintf(buf, sizeof(buf) - 1, format, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;

	replay_logf("ERROR: %s\n", buf);
	replay_logf("REPLAY FAILED\n");
	trace_close();
	if (Replay_log) {
		fclose(Replay_log);
		Replay_log = NULL;
	}
	if (!Autotest_harness) {	// Claude: nobody is there to close a dialog
		MessageBox(NULL, buf, "FS2 replay", MB_OK);
	}
	TerminateProcess(GetCurrentProcess(), 4);
}

// -----------------------------------------------------------------------------------------
// encoding: each entry is a tag byte followed by its values.  Single ints are stored as a
// zigzag varint of the difference from the previous value with the same tag.
//

static void wbuf_put(ubyte b)
{
	if (Wbuf_len >= Wbuf_size) {
		Wbuf_size = Wbuf_size ? Wbuf_size * 2 : 65536;
		Wbuf = (ubyte *)realloc(Wbuf, Wbuf_size);
	}
	Wbuf[Wbuf_len++] = b;
}

static void wbuf_flush()
{
	if (Wfile && Whead_written && Wbuf_len) {
		fwrite(Wbuf, 1, Wbuf_len, Wfile);
		Wbuf_len = 0;
	}
}

static void w_varint(int v)
{
	uint u = ((uint)v << 1) ^ (uint)(v >> 31);
	while (u >= 0x80) {
		wbuf_put((ubyte)(u | 0x80));
		u >>= 7;
	}
	wbuf_put((ubyte)u);
}

static void w_float(float f)
{
	ubyte *p = (ubyte *)&f;
	wbuf_put(p[0]); wbuf_put(p[1]); wbuf_put(p[2]); wbuf_put(p[3]);
}

static void w_tag(int tag)
{
	wbuf_put((ubyte)tag);
	Replay_entries++;
	Replay_entries_by_tag[tag]++;
}

static void w_done()
{
	if (Wbuf_len > 65536) {
		wbuf_flush();
	}
}

static int r_varint()
{
	uint u = 0;
	int shift = 0;
	ubyte b;

	do {
		if (Rpos >= Rsize) {
			replay_desync("recording ended in the middle of an entry");
		}
		b = Rdata[Rpos++];
		u |= (uint)(b & 0x7f) << shift;
		shift += 7;
	} while (b & 0x80);

	return (int)(u >> 1) ^ -(int)(u & 1);
}

static float r_float()
{
	float f;

	if (Rpos + 4 > Rsize) {
		replay_desync("recording ended in the middle of an entry");
	}
	memcpy(&f, Rdata + Rpos, 4);
	Rpos += 4;
	return f;
}

static int r_peek_tag()
{
	if (Rpos >= Rsize) {
		return 0;
	}
	return Rdata[Rpos];
}

static void replay_drain_events();

// sound device reads, all recorded as one varint each
static int replay_tag_is_sound(int tag)
{
	return (tag >= RT_SND_PLAY) && (tag <= RT_SND_NUM_PLAYING);
}

static int replay_tag_is_stream(int tag)
{
	return (tag == RT_STREAM_IS_PLAYING) || (tag == RT_STREAM_IS_PAUSED) || (tag == RT_STREAM_DONE_READING) || (tag == RT_STREAM_BYTES);
}

// Claude: D-035.  From version 4 the sound-device and music-stream answers are not recorded: no logic reads them
// (D-034), so a replay asks the device live.  Earlier recordings have them, and only the diagnostics can replay those.
static int replay_sound_unrecorded()
{
	return (Replay_mode == REPLAY_RECORD) || ((Replay_mode == REPLAY_PLAY) && (Hdr.version >= 4));
}

// the flip's cursor position read: recorded only in recordings made before version 4
int replay_flip_cursor_recorded()
{
	return (Replay_mode == REPLAY_PLAY) && (Hdr.version < 4);
}

// clock reads that only time sound (a stream starting): recorded only in recordings made before version 4
int replay_sound_clock_recorded()
{
	return (Replay_mode == REPLAY_PLAY) && (Hdr.version < 4);
}

static int replay_tag_live(int tag)
{
	if (replay_sound_unrecorded() && (replay_tag_is_sound(tag) || replay_tag_is_stream(tag))) {
		return 1;
	}
	if (Replay_sound_model) {
		return replay_tag_is_sound(tag) || replay_tag_is_stream(tag) || (tag == RT_TIMER_MS) || (tag == RT_TIMER_US);
	}
	return Replay_sound_live && replay_tag_is_sound(tag);
}

static void replay_skip_live()
{
	while ((Rpos < Rsize) && replay_tag_live(Rdata[Rpos])) {
		Rpos++;
		r_varint();
		Replay_sound_skipped++;
	}
}

static void r_tag(int tag)
{
	int got;

	replay_drain_events();
	replay_skip_live();

	if (Rpos >= Rsize) {
		replay_desync("recording ended, this run asked for '%s'", Replay_tag_names[tag]);
	}
	got = Rdata[Rpos++];
	if (got != tag) {
		replay_desync("this run asked for '%s', the recording has '%s'", Replay_tag_names[tag], (got > 0 && got < RT_NUM_TAGS) ? Replay_tag_names[got] : "?");
	}
	Replay_entries++;
	Replay_entries_by_tag[tag]++;
	if ((Replay_entries > Replay_log_reads - 3) && (Replay_entries <= Replay_log_reads)) {	// the three entries up to N
		replay_logf("entry %d, frame %d: '%s'\n", Replay_entries, Replay_frame, Replay_tag_names[tag]);
		replay_log_stack();
	}
}

// -----------------------------------------------------------------------------------------
// hooks
//

static int replay_hook_live()
{
	return Replay_active && (Replay_depth == 0) && (GetCurrentThreadId() == Replay_main_thread);
}

int replay_stream_active()
{
	return Replay_active;
}

// Only the main thread's own nesting counts.  Other threads (the music streamer's timer reads the clock)
// never touch the stream, and counting their calls made the main thread skip a recorded read (#27)
void replay_push()
{
	if (GetCurrentThreadId() == Replay_main_thread) {
		Replay_depth++;
	}
}

void replay_pop()
{
	if (GetCurrentThreadId() == Replay_main_thread) {
		Replay_depth--;
	}
}

int replay_should_read()
{
	return (Replay_mode == REPLAY_PLAY) && replay_hook_live();
}

int replay_should_read_tag(int tag)
{
	return replay_should_read() && !replay_tag_live(tag);
}

static int replay_should_write()
{
	return (Replay_mode == REPLAY_RECORD) && replay_hook_live();
}

int replay_read_int(int tag)
{
	r_tag(tag);
	Replay_last_int[tag] += r_varint();
	return Replay_last_int[tag];
}

float replay_read_float(int tag)
{
	r_tag(tag);
	return r_float();
}

int replay_write_int(int tag, int val)
{
	if (replay_should_write() && !replay_tag_live(tag)) {	// Claude: D-035: not the sound-device answers
		w_tag(tag);
		w_varint(val - Replay_last_int[tag]);
		Replay_last_int[tag] = val;
		w_done();
	}
	// the live clock is behind the recorded one when the replay ran faster than the recording.
	// Going back in time makes the frame cap sleep for the difference, so carry on from the recorded clock.
	if ((Replay_mode == REPLAY_PLAY) && Replay_done && ((tag == RT_TIMER_FIX) || (tag == RT_TIMER_MS) || (tag == RT_TIMER_US)) && (Replay_entries_by_tag[tag] > 0)) {
		if (!Replay_clock_set[tag]) {
			Replay_clock_set[tag] = 1;
			Replay_clock_offset[tag] = Replay_last_int[tag] - val;
		}
		val += Replay_clock_offset[tag];
	}
	return val;
}

float replay_write_float(int tag, float val)
{
	if (replay_should_write()) {
		w_tag(tag);
		w_float(val);
		w_done();
	}
	return val;
}

void replay_read_ints(int tag, int *vals, int count)
{
	int i, n;

	r_tag(tag);
	n = r_varint();
	if (n != count) {
		replay_desync("'%s' has %d values, this run asked for %d", Replay_tag_names[tag], n, count);
	}
	for (i = 0; i < count; i++) {
		vals[i] = r_varint();
	}
}

void replay_write_ints(int tag, int *vals, int count)
{
	int i;

	if (replay_should_write()) {
		w_tag(tag);
		w_varint(count);
		for (i = 0; i < count; i++) {
			w_varint(vals[i]);
		}
		w_done();
	}
}

int replay_input_device(int *vals, int count, int max)
{
	int i, n;

	if (!replay_should_read()) {
		replay_write_ints(RT_INPUT_DEVICE, vals, count);
		return count;
	}

	// no entry here: the recording was made by a build without this device, so it is not connected
	replay_drain_events();
	if ((Rpos >= Rsize) || (Rdata[Rpos] != RT_INPUT_DEVICE)) {
		return 0;
	}

	r_tag(RT_INPUT_DEVICE);
	n = r_varint();
	if ((n < 0) || (n > max)) {
		replay_desync("'%s' has %d values, this run takes at most %d", Replay_tag_names[RT_INPUT_DEVICE], n, max);
		return 0;
	}
	for (i = 0; i < n; i++) {
		vals[i] = r_varint();
	}
	return n;
}

// The same, for a device that shares the entry with others: in replay, the entry here is
// this device's only when its first value is magic; otherwise it is left for the next reader
// and 0 is returned.  The device must put magic in vals[0] of every entry it records.
int replay_input_device_tagged(int magic, int *vals, int count, int max)
{
	int pos, n;

	if (!replay_should_read()) {
		return replay_input_device(vals, count, max);
	}

	replay_drain_events();
	if ((Rpos >= Rsize) || (Rdata[Rpos] != RT_INPUT_DEVICE)) {
		return 0;
	}

	// peek: the tag, the count and the first value; no entry is consumed unless it matches
	pos = Rpos++;		// past the tag byte (r_tag would count the entry)
	n = r_varint();
	if ((n < 1) || (r_varint() != magic)) {
		Rpos = pos;
		return 0;
	}
	Rpos = pos;
	return replay_input_device(vals, count, max);
}

// the recorded frame clock, in ms; reading it makes no recorded read
static int replay_snd_model_now()
{
	return (int)(((__int64)Replay_last_int[RT_TIMER_FIX] * 1000) >> 16);
}

int replay_snd_model_active()
{
	return Replay_sound_model && (Replay_mode == REPLAY_PLAY) && replay_hook_live();
}

int replay_snd_model_clock(int tag)
{
	__int64 f = Replay_last_int[RT_TIMER_FIX];

	return (int)((tag == RT_TIMER_US) ? ((f * 1000000) >> 16) : ((f * 1000) >> 16));
}

static int replay_snd_model_playing(replay_snd_model *m, int now)
{
	if (m->stopped) {
		return 0;
	}
	return m->looping || (now - m->start_ms < m->duration_ms);
}

int replay_snd_model_query(int tag, int handle)
{
	int i, now = replay_snd_model_now(), count = 0;

	for (i = 0; i < REPLAY_SND_MODEL_MAX; i++) {
		replay_snd_model *m = &Replay_snd_models[i];

		if (!m->duration_ms && !m->looping) {
			continue;
		}
		if (tag == RT_SND_NUM_PLAYING) {
			count += replay_snd_model_playing(m, now);
		} else if (m->handle == handle) {
			if (tag == RT_SND_IS_PLAYING) {
				return replay_snd_model_playing(m, now);
			}
			if (!replay_snd_model_playing(m, now)) {
				return 0;
			}
			return m->looping ? m->duration_ms : (m->duration_ms - (now - m->start_ms));	// RT_SND_TIME_REMAINING
		}
	}
	return count;
}

void replay_snd_model_stop(int handle)
{
	int i;

	if (!replay_snd_model_active() || ((handle < 0) && (handle != REPLAY_SND_MODEL_STOP_ALL))) {
		return;		// snd_stop ignores a negative handle, and the game calls it with -1 every frame
	}
	for (i = 0; i < REPLAY_SND_MODEL_MAX; i++) {
		if ((handle == REPLAY_SND_MODEL_STOP_ALL) || (Replay_snd_models[i].handle == handle)) {
			Replay_snd_models[i].stopped = 1;
		}
	}
}

int replay_snd_handle(int tag, int live_handle, int duration_ms, int looping)
{
	int recorded;

	if (replay_snd_model_active()) {
		replay_snd_model *m = &Replay_snd_models[Replay_snd_models_next];

		Replay_snd_models_next = (Replay_snd_models_next + 1) % REPLAY_SND_MODEL_MAX;
		m->handle = (live_handle != -1) ? live_handle : Replay_snd_model_handle++;
		m->start_ms = replay_snd_model_now();
		m->duration_ms = duration_ms;
		m->looping = looping;
		m->stopped = 0;
		Replay_snd_model_starts++;
		return m->handle;
	}

	if (replay_should_read_tag(tag)) {
		recorded = replay_read_int(tag);
		if ((recorded == -1) && (live_handle != -1)) {
			// the recorded run could not play this sound, so neither may this one
			snd_stop(live_handle);
			Replay_snd_mismatch++;
			return -1;
		}
		if ((recorded != -1) && (live_handle == -1)) {
			// the recorded run played it; hand back its handle, which no live sound will match
			Replay_snd_mismatch++;
			return recorded;
		}
		return live_handle;
	}

	return replay_write_int(tag, live_handle);
}

// -----------------------------------------------------------------------------------------
// events.  Recorded where the OS delivers them; replayed at the same point in the stream.
//

int replay_event_key(uint code, int state, uint latency)
{
	if (Replay_mode == REPLAY_PLAY) {
		return Replay_active ? Replay_injecting : 1;
	}
	if ((Replay_mode == REPLAY_RECORD) && Replay_active && (GetCurrentThreadId() == Replay_main_thread)) {
		w_tag(RT_EV_KEY);
		w_varint((int)code);
		w_varint(state);
		w_varint((int)latency);
		w_done();
	}
	return 1;
}

int replay_event_mouse(uint flags, int set)
{
	if (Replay_mode == REPLAY_PLAY) {
		return Replay_active ? Replay_injecting : 1;
	}
	if ((Replay_mode == REPLAY_RECORD) && Replay_active && (GetCurrentThreadId() == Replay_main_thread)) {
		w_tag(RT_EV_MOUSE);
		w_varint((int)flags);
		w_varint(set);
		w_done();
	}
	return 1;
}

int replay_event_focus(int got_focus)
{
	if (Replay_mode == REPLAY_PLAY) {
		return Replay_active ? Replay_injecting : 1;
	}
	if ((Replay_mode == REPLAY_RECORD) && Replay_active && (GetCurrentThreadId() == Replay_main_thread)) {
		w_tag(got_focus ? RT_EV_GOTFOCUS : RT_EV_LOSTFOCUS);
		w_done();
	}
	return 1;
}

int replay_live_focus()
{
	return !((Replay_mode == REPLAY_PLAY) && Replay_active);
}

static void replay_drain_events()
{
	int tag;
	uint code, latency;
	int state;

	if (Replay_injecting) {
		return;
	}

	for (;;) {
		replay_skip_live();
		tag = r_peek_tag();
		if ((tag != RT_EV_KEY) && (tag != RT_EV_MOUSE) && (tag != RT_EV_LOSTFOCUS) && (tag != RT_EV_GOTFOCUS)) {
			return;
		}

		Rpos++;
		Replay_entries++;
		Replay_entries_by_tag[tag]++;
		Replay_injecting = 1;
		switch (tag) {
			case RT_EV_KEY:
				code = (uint)r_varint();
				state = r_varint();
				latency = (uint)r_varint();
				key_mark(code, state, latency);		// reads the clock, which comes from the stream
				break;

			case RT_EV_MOUSE:
				code = (uint)r_varint();
				state = r_varint();
				mouse_mark_button(code, state);
				break;

			case RT_EV_LOSTFOCUS:
				key_lost_focus();
				// losing focus also shows the cursor (OsApi.cpp), which makes the next flip read the mouse
				Mouse_hidden = 0;
				break;

			case RT_EV_GOTFOCUS:
				key_got_focus();
				break;
		}
		Replay_injecting = 0;
	}
}

// A poll that only a fullscreen game away from the front makes (the flip waiting for its surface).
// A windowed replay never makes it, so it leaves no os_poll entry; the events it dispatches are still
// recorded with their own reads and replay at the next read (#108)
static int Replay_poll_unrecorded = 0;

void replay_unrecorded_os_poll()
{
	Replay_poll_unrecorded = 1;
	os_poll();
	Replay_poll_unrecorded = 0;
}

void replay_os_poll()
{
	if (!replay_hook_live() || Replay_poll_unrecorded) {
		return;
	}

	if (Replay_mode == REPLAY_RECORD) {
		w_tag(RT_OSPOLL);
		w_done();
	} else {
		r_tag(RT_OSPOLL);
	}
}

// -----------------------------------------------------------------------------------------
// file
//

static void replay_write_header()
{
	Hdr.loadout_size = sizeof(loadout_data);

	fwrite(&Hdr, sizeof(Hdr), 1, Wfile);
	fwrite(&Hdr_loadout, sizeof(Hdr_loadout), 1, Wfile);
	if (Hdr.pilot_size) {
		fwrite(Hdr_pilot, 1, Hdr.pilot_size, Wfile);
	}
	if (Hdr.campaign_size) {
		fwrite(Hdr_campaign, 1, Hdr.campaign_size, Wfile);
	}
	Whead_written = 1;
}

static void replay_read_file()
{
	FILE *fp;
	int pos, hdr_size;

	fp = fopen(Replay_file, "rb");
	if (!fp) {
		replay_fatal("Cannot open recording '%s'", Replay_file);
	}
	fseek(fp, 0, SEEK_END);
	Rsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	Rdata = (ubyte *)malloc(Rsize);
	if (!Rdata || ((int)fread(Rdata, 1, Rsize, fp) != Rsize)) {
		replay_fatal("Cannot read recording '%s'", Replay_file);
	}
	fclose(fp);

	// Claude: version 1 recordings have no autotest settings, so they replay with them off
	if (Rsize < (int)REPLAY_HEADER_V1_SIZE) {
		replay_fatal("'%s' is not a recording", Replay_file);
	}
	memset(&Hdr, 0, sizeof(Hdr));
	memcpy(&Hdr, Rdata, REPLAY_HEADER_V1_SIZE);
	if (memcmp(Hdr.magic, "FS2REC", 6) || (Hdr.version < 1) || (Hdr.version > REPLAY_FILE_VERSION)) {
		replay_fatal("'%s' is not a version 1 to %d recording", Replay_file, REPLAY_FILE_VERSION);
	}
	// Claude: D-035: before version 4 the stream holds the sound-device answers, which this build no longer asks for in
	// that order.  Only the sound diagnostics, which skip them, can replay such a recording.
	if ((Hdr.version < 4) && !Replay_sound_live && !Replay_sound_model) {
		replay_fatal("'%s' is a version %d recording, made before the sound-device answers left the stream (D-035): record it again", Replay_file, Hdr.version);
	}
	hdr_size = (int)((Hdr.version == 1) ? REPLAY_HEADER_V1_SIZE : (Hdr.version == 2) ? REPLAY_HEADER_V2_SIZE : (Hdr.version <= 4) ? REPLAY_HEADER_V4_SIZE : sizeof(Hdr));
	if (Rsize < hdr_size) {
		replay_fatal("'%s' is not a recording", Replay_file);
	}
	memcpy(&Hdr, Rdata, hdr_size);
	if (Hdr.version < 5) {
		// Claude: every older recording was made in English, high memory (D-056)
		Hdr.language = LCL_ENGLISH;
		Hdr.low_mem = 0;
	}
	if (Hdr.loadout_size != (int)sizeof(loadout_data)) {
		replay_fatal("Recording loadout size %d does not match this build (%d)", Hdr.loadout_size, sizeof(loadout_data));
	}

	pos = hdr_size;	// Claude: was sizeof(Hdr)
	memcpy(&Hdr_loadout, Rdata + pos, sizeof(Hdr_loadout));
	pos += sizeof(Hdr_loadout);
	Hdr_pilot = Rdata + pos;
	pos += Hdr.pilot_size;
	Hdr_campaign = Rdata + pos;
	pos += Hdr.campaign_size;
	if (pos > Rsize) {
		replay_fatal("Recording '%s' is truncated", Replay_file);
	}
	Rpos = pos;
}

// read a file from the pilot directory into a malloc'd blob
static int replay_read_player_file(char *filename, ubyte **blob)
{
	CFILE *fp;
	int size;

	*blob = NULL;
	fp = cfopen(filename, "rb", CFILE_NORMAL, CF_TYPE_SINGLE_PLAYERS);
	if (!fp) {
		return 0;
	}
	size = cfilelength(fp);
	if ((size <= 0) || (size > REPLAY_MAX_BLOB)) {
		cfclose(fp);
		return 0;
	}
	*blob = (ubyte *)malloc(size);
	cfread(*blob, 1, size, fp);
	cfclose(fp);
	return size;
}

static void replay_write_pilot_blob(char *filename, ubyte *blob, int size)
{
	char path[REPLAY_PATH_LEN];
	FILE *fp;

	sprintf(path, "%s\\%s", REPLAY_PILOT_DIR, filename);
	fp = fopen(path, "wb");
	if (!fp) {
		replay_fatal("Cannot write '%s'", path);
	}
	fwrite(blob, 1, size, fp);
	fclose(fp);
}

static void replay_campaign_save_name(char *filename)
{
	char base[_MAX_FNAME];

	_splitpath(Hdr.campaign, NULL, NULL, base, NULL);
	sprintf(filename, "%s.%s.csg", Hdr.callsign, base);
}

// -----------------------------------------------------------------------------------------
// lifecycle
//

void replay_set_cmdline(char *record_file, char *replay_file, char *trace_file, int trace_full)
{
	if (record_file) {
		Replay_mode = REPLAY_RECORD;
		strncpy(Replay_file, record_file, REPLAY_PATH_LEN - 1);
	} else if (replay_file) {
		Replay_mode = REPLAY_PLAY;
		strncpy(Replay_file, replay_file, REPLAY_PATH_LEN - 1);
	}
	if (trace_file) {
		strncpy(Replay_trace_file, trace_file, REPLAY_PATH_LEN - 1);
	}
	Replay_trace_full = trace_full;
}

void replay_set_autorecord(char *dir)
{
	int len;

	Replay_mode = REPLAY_RECORD;
	Replay_auto = 1;
	// made absolute now: startup later changes to the data set folder, and a relative folder would then point inside it
	if (!_fullpath(Replay_auto_dir, (dir && dir[0]) ? dir : "Recordings", REPLAY_PATH_LEN)) {
		strncpy(Replay_auto_dir, (dir && dir[0]) ? dir : "Recordings", REPLAY_PATH_LEN - 1);
	}
	_mkdir(Replay_auto_dir);
	// Claude: x64, cast to int, its width on Win32 (D-20261020-194406 batch 2)
	len = (int)strlen(Replay_auto_dir);
	if ((len > 0) && ((Replay_auto_dir[len - 1] == '\\') || (Replay_auto_dir[len - 1] == '/'))) {
		Replay_auto_dir[len - 1] = 0;
	}
}

// -autorecord writes to "<name>.tmp" and only replaces the real files once the mission has ended
// properly, so an earlier recording survives a crash or the game being closed mid-mission
static void replay_auto_finish(int keep)
{
	char names[3][REPLAY_PATH_LEN + 32], tmp[REPLAY_PATH_LEN + 32];
	int i;

	if (Wfile) {
		fclose(Wfile);
		Wfile = NULL;
	}
	trace_close();
	if (Replay_log) {
		fclose(Replay_log);
		Replay_log = NULL;
	}

	strcpy(names[0], Replay_file);
	strcpy(names[1], Replay_trace_file);
	sprintf(names[2], "%s.record.log", Replay_file);
	for (i = 0; i < 3; i++) {
		sprintf(tmp, "%s.tmp", names[i]);
		if (keep) {
			MoveFileEx(tmp, names[i], MOVEFILE_REPLACE_EXISTING);
		} else {
			DeleteFile(tmp);
		}
	}
	Replay_auto_saved = keep;
}

// the command line as started.  Parsing writes into the process's own copy, so GetCommandLine()
// at shutdown returns only the first parameter.
static char Replay_cmdline[4096];

void replay_wait_for_restart()
{
	char pid[32];
	HANDLE proc;

	strncpy(Replay_cmdline, GetCommandLine(), sizeof(Replay_cmdline) - 1);
	if (!GetEnvironmentVariable(REPLAY_RESTART_ENV, pid, sizeof(pid))) {
		return;
	}
	SetEnvironmentVariable(REPLAY_RESTART_ENV, NULL);
	proc = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)atoi(pid));
	if (proc) {
		WaitForSingleObject(proc, 30000);
		CloseHandle(proc);
	}
}

void replay_restart()
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	char pid[32];

	if (!Replay_auto_restarting) {
		return;
	}

	// the same command line and directory.  The new instance waits for this one to close, since
	// only one may run.
	sprintf(pid, "%u", GetCurrentProcessId());
	SetEnvironmentVariable(REPLAY_RESTART_ENV, pid);
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	if (CreateProcess(NULL, Replay_cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

// after a mission, back in the main hall, the game restarts so that the next mission is the
// first one of its process, which is the only kind replay can reproduce
static void replay_auto_drive_state(int state)
{
	char *name;

	// an autotest runs its own missions and quits; restarting would run them again
	if (Replay_auto_restarting || Autotest_harness) {
		return;
	}

	if ((state == GS_STATE_MAIN_MENU) && (Replay_load_count > 0)) {
		// let the main hall draw once before the popup
		if (++Replay_auto_menu_frames < 2) {
			return;
		}
		if (!Replay_in_game) {
			// a mission was loaded but not flown
			replay_auto_finish(0);
		}
		if (Replay_auto_saved) {
			name = strrchr(Replay_file, '\\');
			popup(PF_USE_AFFIRMATIVE_ICON, 1, POPUP_OK, "Recording saved as %s\n\nThe game will now restart. A recording can only be replayed if its mission was the first one played since the game started.", name ? name + 1 : Replay_file);
		} else {
			popup(PF_USE_AFFIRMATIVE_ICON, 1, POPUP_OK, "No recording was saved.\n\nThe game will now restart, so the next mission is the first one since the game started and can be recorded.");
		}
		Replay_auto_restarting = 1;
		gameseq_post_event(GS_EVENT_QUIT_GAME);
		return;
	}

	if (((state == GS_STATE_CMD_BRIEF) || (state == GS_STATE_BRIEFING) || (state == GS_STATE_RED_ALERT)) && (Replay_load_count > 1) && !Replay_auto_told) {
		Replay_auto_told = 1;
		popup(PF_USE_AFFIRMATIVE_ICON, 1, POPUP_OK, "This mission will not be recorded.\n\nOnly the first mission since the game started can be recorded. Go back to the main hall and the game will restart.");
	}
}

// name with "_n" put in front of the extension
static void replay_numbered_name(char *out, char *name, int n)
{
	char *ext = strrchr(name, '.');
	char *slash = strrchr(name, '\\');

	if (!ext || (slash && (ext < slash))) {
		sprintf(out, "%s_%d", name, n);
	} else {
		sprintf(out, "%.*s_%d%s", (int)(ext - name), name, n, ext);
	}
}

static int replay_file_exists(char *name)
{
	return name[0] && (GetFileAttributes(name) != INVALID_FILE_ATTRIBUTES);
}

// never overwrite an earlier recording: if the names are taken, add the first free number
static void replay_pick_free_names()
{
	char rec[REPLAY_PATH_LEN + 32], trace[REPLAY_PATH_LEN + 32];
	int n;

	if (!replay_file_exists(Replay_file) && !replay_file_exists(Replay_trace_file)) {
		return;
	}
	for (n = 2; ; n++) {
		replay_numbered_name(rec, Replay_file, n);
		trace[0] = 0;
		if (Replay_trace_file[0]) {
			replay_numbered_name(trace, Replay_trace_file, n);
		}
		if (!replay_file_exists(rec) && !replay_file_exists(trace)) {
			break;
		}
	}
	strncpy(Replay_file, rec, REPLAY_PATH_LEN - 1);
	strncpy(Replay_trace_file, trace, REPLAY_PATH_LEN - 1);
}

void replay_startup()
{
	char filename[MAX_FILENAME_LEN * 2];

	if (Replay_mode == REPLAY_OFF) {
		return;
	}

	Replay_main_thread = GetCurrentThreadId();
	if (Replay_auto) {
		// the file names come from the mission, in replay_load_begin
		return;
	}
	if (Replay_mode == REPLAY_RECORD) {
		replay_pick_free_names();
	}
	replay_open_log();
	replay_logf("%s '%s'\n", (Replay_mode == REPLAY_RECORD) ? "Recording" : "Replaying", Replay_file);

	if (Replay_mode == REPLAY_PLAY) {
		char live[8];

		if (GetEnvironmentVariable(REPLAY_SOUND_LIVE_ENV, live, sizeof(live)) && (live[0] == '1')) {
			Replay_sound_live = 1;
			replay_logf("DIAGNOSTIC: %s=1, sound device read live, recorded sound entries skipped\n", REPLAY_SOUND_LIVE_ENV);
		}
		Replay_log_reads = GetEnvironmentVariable(REPLAY_LOG_READS_ENV, live, sizeof(live)) ? atoi(live) : 0;
		if (GetEnvironmentVariable(REPLAY_SOUND_MODEL_ENV, live, sizeof(live)) && (live[0] == '1')) {
			Replay_sound_model = 1;
			replay_logf("DIAGNOSTIC: %s=1, sound-state queries answered by the model, recorded sound and music entries skipped\n", REPLAY_SOUND_MODEL_ENV);
		}
		replay_read_file();
		replay_logf("Recorded by build '%s'\n", Hdr.build);
		replay_logf("Mission '%s', pilot '%s', campaign '%s', seed %d\n", Hdr.mission, Hdr.callsign, Hdr.campaign, Hdr.level_seed);
		if ((Hdr.language != LCL_ENGLISH) || Hdr.low_mem) {
			replay_logf("Not the reference configuration (D-056): language %d, low memory %d\n", Hdr.language, Hdr.low_mem);
		}

		// Claude: the mission plays under the rules it was recorded with, whatever the command line says
		autotest_set_from_recording(Hdr.autotest, Hdr.autotest_time, Hdr.autotest_speed);
		if (Hdr.autotest) {
			replay_logf("Autotest rules: %d s of mission time at %dx\n", Hdr.autotest_time, Hdr.autotest_speed);
		}

		// pilot and campaign files come from the recording, in their own directory, so the
		// player's own pilot is never read or written
		_mkdir("Data");			// a data set folder extracted on the first start has no players folder yet
		_mkdir("Data\\Players");
		_mkdir(REPLAY_PILOT_DIR);
		sprintf(filename, "%s.plr", Hdr.callsign);
		replay_write_pilot_blob(filename, Hdr_pilot, Hdr.pilot_size);
		if (Hdr.campaign_size) {
			replay_campaign_save_name(filename);
			replay_write_pilot_blob(filename, Hdr_campaign, Hdr.campaign_size);
		}
		Pathtypes[CF_TYPE_SINGLE_PLAYERS].path = REPLAY_PILOT_DIR;
	}
}

void replay_game_init_done()
{
}

// the FreeSpace 2 demo's own missions and campaign (its built-in list, FreeSpace.cpp, FS2_DEMO); no retail file has these names
static char *Replay_demo_missions[] = { "SPDemo-01", "SPDemo-02", "DemoTrain", "Demo", "MPDemo-01", "Demo-DOG-01" };

static int replay_is_demo_mission(char *filename)
{
	char base[_MAX_FNAME];
	int i;

	_splitpath(filename, NULL, NULL, base, NULL);
	for (i = 0; i < (int)(sizeof(Replay_demo_missions) / sizeof(Replay_demo_missions[0])); i++) {
		if (!stricmp(base, Replay_demo_missions[i])) {
			return 1;
		}
	}
	return 0;
}

// The data set a recording was made on, from what the header already holds (D-20261018-175706: no format
// change below GodotPrep): its campaign, else its mission.  A DATA_SET_* value, or DATA_SET_UNKNOWN (our
// own test missions play on either set).  Only the FreeSpace 2 pair is told apart so far.
static int replay_recorded_data_set(int (*is_volition_mission)(char *filename))
{
	char base[_MAX_FNAME];

	if (Hdr.campaign[0]) {
		_splitpath(Hdr.campaign, NULL, NULL, base, NULL);
		if (!stricmp(base, "demo")) {
			return DATA_SET_FS2_DEMO;
		}
		if (!stricmp(base, "freespace2")) {
			return DATA_SET_FS2_RETAIL;
		}
	}
	if (replay_is_demo_mission(Hdr.mission)) {
		return DATA_SET_FS2_DEMO;
	}
	if (is_volition_mission(Hdr.mission)) {	// this build's built-in list: retail's in a retail build
		return DATA_SET_FS2_RETAIL;
	}
	return DATA_SET_UNKNOWN;
}

void replay_check_data_set(int set, int (*is_volition_mission)(char *filename))
{
	int recorded;

	if ((Replay_mode != REPLAY_PLAY) || Replay_auto) {
		return;
	}
	recorded = replay_recorded_data_set(is_volition_mission);
	replay_logf("data set: %s (recording), %s (this run)\n", data_set_name(recorded), data_set_name(set));
	if ((recorded != DATA_SET_UNKNOWN) && (recorded != set)) {
		replay_fatal("'%s' was recorded on the %s data and this game has the %s data", Replay_file, data_set_name(recorded), data_set_name(set));
	}
}

// Claude: the data set a recording was made on, read from its header before the game picks its data
// folder (D-20261018-221704); replay_startup reads the whole file again later
int replay_peek_data_set(int (*is_volition_mission)(char *filename))
{
	FILE *fp;
	int got;

	if ((Replay_mode != REPLAY_PLAY) || Replay_auto || !Replay_file[0]) {
		return DATA_SET_UNKNOWN;
	}
	fp = fopen(Replay_file, "rb");
	if (!fp) {
		return DATA_SET_UNKNOWN;
	}
	memset(&Hdr, 0, sizeof(Hdr));
	got = (int)fread(&Hdr, 1, REPLAY_HEADER_V1_SIZE, fp);
	fclose(fp);
	if ((got != (int)REPLAY_HEADER_V1_SIZE) || memcmp(Hdr.magic, "FS2REC", 6)) {
		return DATA_SET_UNKNOWN;
	}
	return replay_recorded_data_set(is_volition_mission);
}

int replay_load_pilot()
{
	if (Replay_mode != REPLAY_PLAY) {
		return 0;
	}

	Player = &Players[0];
	if (read_pilot_file(Hdr.callsign, 1, Player)) {
		replay_fatal("Cannot load the recorded pilot '%s': %s", Hdr.callsign, pilot_file_guard_reason());	// Claude: with the guard's cause (D-20261023-201347 term 5)
	}
	Player->flags |= PLAYER_FLAGS_STRUCTURE_IN_USE;
	return 1;
}

int replay_level_seed(int seed)
{
	if (Replay_mode == REPLAY_RECORD) {
		Hdr.level_seed = seed;
	} else if (Replay_mode == REPLAY_PLAY) {
		seed = Hdr.level_seed;
	}
	return seed;
}

// Claude: the language and low-memory mode the recording was made with, in place of this machine's settings
// (D-056). lcl_init and game_init ask before they use them; replay_startup has read the header by then.
int replay_language(int lang)
{
	return (Replay_mode == REPLAY_PLAY) ? Hdr.language : lang;
}

int replay_low_mem(int low_mem)
{
	return (Replay_mode == REPLAY_PLAY) ? Hdr.low_mem : low_mem;
}

static uint replay_mission_checksum()
{
	char filename[MAX_FILENAME_LEN + 8], *ext;
	uint chksum = 0;

	strncpy(filename, Game_current_mission_filename, MAX_FILENAME_LEN - 1);
	filename[MAX_FILENAME_LEN - 1] = 0;
	ext = strchr(filename, '.');
	if (ext) {
		*ext = 0;
	}
	strcat(filename, FS_MISSION_FILE_EXT);
	cf_chksum_long(filename, &chksum);
	return chksum;
}

static void replay_check_int(char *what, int recorded, int now)
{
	if (recorded != now) {
		replay_fatal("%s differs: recorded %d, this run %d", what, recorded, now);
	}
}

static void replay_check_float(char *what, float recorded, float now)
{
	if (recorded != now) {
		replay_fatal("%s differs: recorded %f, this run %f", what, recorded, now);
	}
}

void replay_load_begin()
{
	int i;

	if (Replay_mode == REPLAY_OFF) {
		return;
	}

	Replay_load_count++;
	if (Replay_load_count > 1) {
		// function statics carry state from one mission to the next, so one process holds
		// exactly one recorded mission
		replay_mission_over();
		return;
	}

	if (Replay_mode == REPLAY_RECORD) {
		if (Replay_auto) {
			char base[_MAX_FNAME];

			_splitpath(Game_current_mission_filename, NULL, NULL, base, NULL);
			_snprintf(Replay_file, REPLAY_PATH_LEN - 1, "%s\\%s.fs2rec", Replay_auto_dir, base);
			_snprintf(Replay_trace_file, REPLAY_PATH_LEN - 1, "%s\\%s.trace", Replay_auto_dir, base);
			replay_open_log();
			replay_logf("Recording '%s'\n", Replay_file);
		}
		memset(&Hdr, 0, sizeof(Hdr));
		memcpy(Hdr.magic, "FS2REC", 6);
		Hdr.version = REPLAY_FILE_VERSION;
#ifdef NDEBUG
		sprintf(Hdr.build, "Release %s %s", __DATE__, __TIME__);
#else
		sprintf(Hdr.build, "Debug %s %s", __DATE__, __TIME__);
#endif
		strncpy(Hdr.mission, Game_current_mission_filename, sizeof(Hdr.mission) - 1);
		Hdr.mission_checksum = replay_mission_checksum();
		Hdr.game_mode = Game_mode;
		if (Game_mode & GM_CAMPAIGN_MODE) {
			strncpy(Hdr.campaign, Campaign.filename, sizeof(Hdr.campaign) - 1);
		}
		strncpy(Hdr.callsign, Player->callsign, sizeof(Hdr.callsign) - 1);
		Hdr.semirand_inited = Semirand_inited;
		for (i = 0; i < SEMIRAND_MAX; i++) {
			Hdr.semirand[i] = Semirand[i];
		}
		Hdr.skill_level = Game_skill_level;
		Hdr.detail_size = sizeof(Detail);
		Assert(sizeof(Detail) <= sizeof(Hdr.detail));
		memcpy(Hdr.detail, &Detail, sizeof(Detail));
		Hdr.screen_w = gr_screen.max_w;
		Hdr.screen_h = gr_screen.max_h;
		Hdr.sound_enabled = Sound_enabled;
		Hdr.snd_inited = snd_is_inited();
		Hdr.event_music_enabled = Event_music_enabled;
		Hdr.master_sound_volume = Master_sound_volume;
		Hdr.master_voice_volume = Master_voice_volume;
		Hdr.master_event_music_volume = Master_event_music_volume;
		autotest_get_settings(&Hdr.autotest, &Hdr.autotest_time, &Hdr.autotest_speed);	// Claude: so the replay plays under the same rules
		replay_logf("Mission '%s', pilot '%s', campaign '%s'\n", Hdr.mission, Hdr.callsign, Hdr.campaign);
		if (Hdr.autotest) {
			replay_logf("Autotest rules: %d s of mission time at %dx\n", Hdr.autotest_time, Hdr.autotest_speed);
		}
	} else {
		if (stricmp(Hdr.mission, Game_current_mission_filename)) {
			replay_fatal("Recorded mission is '%s', this run is loading '%s'", Hdr.mission, Game_current_mission_filename);
		}
		if ((Hdr.game_mode & GM_CAMPAIGN_MODE) != (Game_mode & GM_CAMPAIGN_MODE)) {
			replay_fatal("Campaign mode differs from the recording");
		}
		if (Hdr.mission_checksum != replay_mission_checksum()) {
			replay_fatal("Mission file '%s' differs from the recorded one", Hdr.mission);
		}
		if (Hdr.autotest) {
			Game_skill_level = Hdr.skill_level;	// autotest may have played at -at_skill, and does not save the pilot
		} else {
			replay_check_int("Skill level", Hdr.skill_level, Game_skill_level);
		}
		replay_check_int("Screen width", Hdr.screen_w, gr_screen.max_w);
		replay_check_int("Screen height", Hdr.screen_h, gr_screen.max_h);
		replay_check_int("Sound enabled", Hdr.sound_enabled, Sound_enabled);
		replay_check_int("Sound initialised", Hdr.snd_inited, snd_is_inited());
		replay_check_int("Event music enabled", Hdr.event_music_enabled, Event_music_enabled);
		replay_check_float("Sound volume", Hdr.master_sound_volume, Master_sound_volume);
		replay_check_float("Voice volume", Hdr.master_voice_volume, Master_voice_volume);
		replay_check_float("Music volume", Hdr.master_event_music_volume, Master_event_music_volume);
		replay_check_int("Detail settings size", Hdr.detail_size, sizeof(Detail));

		// these were set before the mission from the clock or the registry
		memcpy(&Detail, Hdr.detail, sizeof(Detail));
		Semirand_inited = Hdr.semirand_inited;
		for (i = 0; i < SEMIRAND_MAX; i++) {
			Semirand[i] = Hdr.semirand[i];
		}
	}
}

void replay_load_end()
{
	char filename[MAX_FILENAME_LEN * 2];

	if ((Replay_mode == REPLAY_OFF) || (Replay_load_count != 1) || Replay_in_game) {
		return;
	}

	if (Replay_mode == REPLAY_RECORD) {
		// mission_load() has just saved the pilot, so the files on disk match the pilot that
		// loaded this mission
		sprintf(filename, "%s.plr", Hdr.callsign);
		Hdr.pilot_size = replay_read_player_file(filename, &Hdr_pilot);
		if (!Hdr.pilot_size) {
			replay_fatal("Cannot read pilot file '%s'", filename);
		}
		if (Hdr.campaign[0]) {
			replay_campaign_save_name(filename);
			Hdr.campaign_size = replay_read_player_file(filename, &Hdr_campaign);
		}
	} else {
		// the loadout the player committed in the recorded run.  The briefing restores it
		// through the same path as a player re-flying a mission.
		Player_loadout = Hdr_loadout;
	}
}

void replay_enter_game()
{
	char name[REPLAY_PATH_LEN + 32];

	if ((Replay_mode == REPLAY_OFF) || (Replay_load_count != 1) || Replay_in_game) {
		return;
	}

	if (Replay_mode == REPLAY_RECORD) {
		Hdr_loadout = Player_loadout;
		Hdr.timestamp_ticker = timestamp_ticker;
		Hdr.last_time = Last_time;
		Hdr.last_delta_time = Last_delta_time;
		Hdr.saved_timestamp_ticker = saved_timestamp_ticker;
		Hdr.missiontime = Missiontime;
		Hdr.view_position[0] = View_position.x;
		Hdr.view_position[1] = View_position.y;
		Hdr.view_position[2] = View_position.z;
		Hdr.language = lcl_get_language();
		Hdr.low_mem = game_using_low_mem() ? 1 : 0;

		// the file is only created once there is a mission to put in it
		if (Replay_auto) {
			sprintf(name, "%s.tmp", Replay_file);
			Wfile = fopen(name, "wb");
		} else {
			Wfile = fopen(Replay_file, "wb");
		}
		if (!Wfile) {
			replay_fatal("Cannot create recording '%s'", Replay_file);
		}

		// header first, then the stream
		replay_write_header();
		wbuf_flush();
	} else {
		if (memcmp(&Player_loadout, &Hdr_loadout, sizeof(loadout_data))) {
			replay_fatal("Loadout differs from the recording");
		}

		// the time spent in the briefing advanced these
		timestamp_ticker = Hdr.timestamp_ticker;
		Last_time = Hdr.last_time;
		Last_delta_time = Hdr.last_delta_time;
		saved_timestamp_ticker = Hdr.saved_timestamp_ticker;
		Missiontime = Hdr.missiontime;

		// the menus left this, and it differs with the path taken through them.  Older recordings don't have it
		if (Hdr.version >= 3) {
			View_position.x = Hdr.view_position[0];
			View_position.y = Hdr.view_position[1];
			View_position.z = Hdr.view_position[2];
		}
	}

	// the briefing consumed random numbers for as long as the player stayed in it
	srand(Hdr.level_seed);
	cosmetic_srand(Hdr.level_seed);	// Claude: The briefing draws cosmetic numbers too, so reseed that stream as well.

	Replay_in_game = 1;
	Replay_active = 1;

	if (Replay_mode == REPLAY_RECORD) {
		w_tag(RT_ENTER_GAME);
		w_done();
	} else {
		r_tag(RT_ENTER_GAME);
	}

	// whether a joystick was in use changes which calls the joystick code makes, so replay takes it from
	// the recording, not from what is plugged in now.  Older recordings don't have it: live as before
	if (Replay_mode == REPLAY_RECORD) {
		w_tag(RT_JOY_PRESENT);
		w_varint(joy_num_sticks);
		w_done();
	} else if (r_peek_tag() == RT_JOY_PRESENT) {
		int sticks;

		r_tag(RT_JOY_PRESENT);
		sticks = r_varint();
		if (!joy_replay_set_sticks(sticks)) {
			replay_fatal("The recording used a joystick, and this machine has no joystick driver");
		}
	}

	if (Replay_auto) {
		sprintf(name, "%s.tmp", Replay_trace_file);
		trace_open(name, Replay_trace_full);
	} else if (Replay_trace_file[0]) {
		trace_open(Replay_trace_file, Replay_trace_full);
	}
	replay_logf("Entered mission\n");
}

void replay_mission_over()
{
	if ((Replay_mode == REPLAY_OFF) || Replay_done) {
		return;
	}
	if (!Replay_in_game) {
		return;
	}

	Replay_done = 1;
	if (Replay_mode == REPLAY_RECORD) {
		w_tag(RT_END);
		wbuf_flush();
		fclose(Wfile);
		Wfile = NULL;
	} else {
		r_tag(RT_END);
		if (Rpos != Rsize) {
			replay_desync("mission ended with %d bytes of the recording unused", Rsize - Rpos);
		}
	}
	Replay_active = 0;

	trace_close();
	replay_log_stats();
	if (Replay_sound_live || Replay_sound_model) {
		replay_logf("DIAGNOSTIC: %d recorded sound entries skipped\n", Replay_sound_skipped);
	}
	if (Replay_sound_model) {
		replay_logf("DIAGNOSTIC: %d sounds started in the model\n", Replay_snd_model_starts);
	}
	replay_logf((Replay_mode == REPLAY_RECORD) ? "RECORD OK\n" : "REPLAY OK\n");

	if (Replay_auto) {
		replay_auto_finish(1);
	}
	if (Replay_mode == REPLAY_PLAY) {
		gameseq_post_event(GS_EVENT_QUIT_GAME);
	}
}

// Claude: P1. The host got a fatal Error() handed back and is walking the game out: a replay cannot follow that (it wants the next
// recorded input), so it ends here, not as REPLAY OK and not as a desync. Recording is left to replay_mission_over.
void replay_abandon()
{
	if ((Replay_mode != REPLAY_PLAY) || Replay_done) {
		return;
	}
	replay_logf("REPLAY ABANDONED at frame %d: the host ended the game after a fatal error\n", Replay_frame);
	Replay_done = 1;
	Replay_active = 0;
}

void replay_shutdown()
{
	if (Replay_mode == REPLAY_OFF) {
		return;
	}

	if (Replay_auto && !Replay_done && (Replay_load_count > 0)) {
		// closed during the mission or its briefing: not a proper end, so keep any earlier recording
		replay_logf("The game was closed before the mission ended; the recording was not kept\n");
		Replay_done = 1;
		replay_auto_finish(0);
	}
	if (!Replay_done && Replay_in_game) {
		replay_mission_over();
	}
	if ((Replay_mode == REPLAY_RECORD) && !Replay_in_game) {
		// quit before the mission started; no file was created
		replay_logf("No mission was recorded\n");
	}
	trace_close();
	if (Replay_log) {
		fclose(Replay_log);
		Replay_log = NULL;
	}

	// Claude: back to the state a new process starts in, so game_host_init can replay again (D-055)
	if (Wfile) {
		fclose(Wfile);
		Wfile = NULL;
	}
	if (Wbuf) {
		free(Wbuf);
		Wbuf = NULL;
	}
	Wbuf_size = Wbuf_len = 0;
	Whead_written = 0;
	// recording, the pilot and campaign blobs were read into their own memory; replaying, they point into Rdata
	if (Hdr_pilot && !(Rdata && (Hdr_pilot >= Rdata) && (Hdr_pilot < Rdata + Rsize))) {
		free(Hdr_pilot);
	}
	Hdr_pilot = NULL;
	if (Hdr_campaign && !(Rdata && (Hdr_campaign >= Rdata) && (Hdr_campaign < Rdata + Rsize))) {
		free(Hdr_campaign);
	}
	Hdr_campaign = NULL;
	if (Rdata) {
		free(Rdata);
		Rdata = NULL;
	}
	Rsize = Rpos = 0;
	memset(&Hdr, 0, sizeof(Hdr));
	memset(&Hdr_loadout, 0, sizeof(Hdr_loadout));
	Replay_mode = REPLAY_OFF;
	memset(Replay_file, 0, sizeof(Replay_file));
	memset(Replay_trace_file, 0, sizeof(Replay_trace_file));
	Replay_trace_full = 0;
	Replay_auto_saved = 0;
	Replay_auto_told = 0;
	Replay_auto_menu_frames = 0;
	Replay_active = 0;
	Replay_done = 0;
	Replay_depth = 0;
	Replay_injecting = 0;
	Replay_load_count = 0;
	Replay_in_game = 0;
	Replay_frame = 0;
	Replay_entries = 0;
	memset(Replay_entries_by_tag, 0, sizeof(Replay_entries_by_tag));
	Replay_snd_mismatch = 0;
	Replay_sound_live = 0;
	Replay_sound_skipped = 0;
	Replay_sound_model = 0;
	memset(Replay_snd_models, 0, sizeof(Replay_snd_models));
	Replay_snd_models_next = 0;
	Replay_snd_model_handle = 0x40000000;
	Replay_snd_model_starts = 0;
	memset(Replay_last_int, 0, sizeof(Replay_last_int));
	memset(Replay_clock_offset, 0, sizeof(Replay_clock_offset));
	memset(Replay_clock_set, 0, sizeof(Replay_clock_set));
	Replay_started_mission = 0;
	Replay_briefing_started = 0;
	Replay_committed = 0;
}

void replay_enter_state(int state)
{
	if (Replay_mode == REPLAY_OFF) {
		return;
	}

	// back out of the mission: the recording ends here, before the new state's init reads
	// input (the main hall moves the cursor, which can read the focus)
	if (Replay_in_game && !Replay_done && ((state == GS_STATE_MAIN_MENU) || (state == GS_STATE_DEBRIEF))) {
		replay_mission_over();
	}
}

void replay_drive_state(int state)
{
	if (Replay_mode == REPLAY_OFF) {
		return;
	}

	if (Replay_auto) {
		replay_auto_drive_state(state);
		return;
	}
	if (Replay_mode != REPLAY_PLAY) {
		return;
	}

	switch (state) {
		case GS_STATE_MAIN_MENU:
			if (!Replay_started_mission) {
				Replay_started_mission = 1;
				if (Hdr.game_mode & GM_CAMPAIGN_MODE) {
					// the same path as the ready room's continue campaign
					gameseq_post_event(GS_EVENT_NEW_CAMPAIGN);
				} else {
					// the same path as the simulator room
					strncpy(Game_current_mission_filename, Hdr.mission, MAX_FILENAME_LEN - 1);
					Game_mode &= ~(GM_CAMPAIGN_MODE);
					gameseq_post_event(GS_EVENT_START_GAME);
				}
			}
			break;

		case GS_STATE_CMD_BRIEF:
			if (!Replay_briefing_started) {
				Replay_briefing_started = 1;
				gameseq_post_event(GS_EVENT_START_BRIEFING);
			}
			break;

		case GS_STATE_BRIEFING:
			if (!Replay_committed) {
				Replay_committed = 1;
				commit_pressed();
			}
			break;

		case GS_STATE_RED_ALERT:
			// a red alert mission has no briefing; its screen's continue button enters the game
			if (!Replay_committed) {
				Replay_committed = 1;
				gameseq_post_event(GS_EVENT_ENTER_GAME);
			}
			break;
	}
}

void replay_set_pace(int on)
{
	Replay_pace = on;
}

// -replay_pace: flFrametime is the game frame time after time compression, so a frame took flFrametime divided by
// the compression in real time.  The performance counter is read directly, not through the hooked timers, so the
// game sees nothing of the waiting.
static void replay_pace_wait()
{
	LARGE_INTEGER now;
	float compression = f2fl(Game_time_compression);

	if (Replay_pace_frames++ == 0) {
		QueryPerformanceFrequency(&Replay_pace_freq);
		QueryPerformanceCounter(&Replay_pace_start);
		Replay_pace_target = 0.0;
		return;
	}

	Replay_pace_target += (compression > 0.0f) ? (flFrametime / compression) : flFrametime;
	for (;;) {
		QueryPerformanceCounter(&now);
		double left = Replay_pace_target - (double)(now.QuadPart - Replay_pace_start.QuadPart) / (double)Replay_pace_freq.QuadPart;
		if (left <= 0.0) {
			break;
		}
		Sleep((left > 0.002) ? (DWORD)((left - 0.001) * 1000.0) : 0);
	}
}

void replay_sim_frame_end()
{
	if (!Replay_in_game || !Replay_active) {
		return;
	}

	trace_frame(Replay_frame);
	Replay_frame++;

	if (Replay_pace && (Replay_mode == REPLAY_PLAY)) {
		replay_pace_wait();
	}
}
