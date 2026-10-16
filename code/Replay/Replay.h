/*
 * Gameplay recorder / replayer.
 *
 * Records every value the game reads from the OS during one mission (clock reads on the
 * main thread, keyboard / mouse / focus events, joystick reads, sound device state) as one
 * ordered stream, and feeds the same values back on replay.  With no -record / -replay
 * flag every hook is a pass-through and the game behaves exactly as before.
 *
 * Usage:
 *   -record <file.fs2rec> [-trace <file.trace>]
 *   -replay <file.fs2rec> [-trace <file.trace>] [-trace_full]
 *   -autorecord <directory>
 *
 * -autorecord records each mission played to <directory>\<mission>.fs2rec and .trace, replacing
 * any earlier recording of it, but only if the mission ends properly (debriefing, quit to the
 * main hall, or restart from the death popup).  Only the first mission since the game started
 * can be replayed, so back in the main hall the game restarts itself.
 *
 * See docs/REPLAY.md for the file format and workflow.
 */

#ifndef _FS2_REPLAY_H
#define _FS2_REPLAY_H

#define REPLAY_OFF		0
#define REPLAY_RECORD	1
#define REPLAY_PLAY		2

extern int Replay_mode;

// entry tags in the value stream
#define RT_TIMER_FIX				1
#define RT_TIMER_MS				2
#define RT_TIMER_US				3
#define RT_OSPOLL					4
#define RT_FOREGROUND			5
#define RT_EV_KEY					6
#define RT_EV_MOUSE				7
#define RT_EV_LOSTFOCUS			8
#define RT_EV_GOTFOCUS			9
#define RT_MOUSE_POS				10
#define RT_JOY_DOWN				11
#define RT_JOY_DOWN_COUNT		12
#define RT_JOY_UP_COUNT			13
#define RT_JOY_DOWN_TIME		14
#define RT_JOY_RAW_AXIS			15
#define RT_JOY_POS				16
#define RT_AXES					17
#define RT_SND_PLAY				18
#define RT_SND_PLAY_3D			19
#define RT_SND_PLAY_LOOPING	20
#define RT_SND_IS_PLAYING		21
#define RT_SND_TIME_REMAINING	22
#define RT_SND_PITCH				23
#define RT_SND_NUM_PLAYING		24
#define RT_STREAM_IS_PLAYING	25
#define RT_STREAM_IS_PAUSED	26
#define RT_STREAM_DONE_READING	27
#define RT_ENTER_GAME			28
#define RT_END						29
#define RT_STREAM_BYTES			30
#define RT_INPUT_DEVICE			31
#define RT_JOY_PRESENT			32
#define RT_NUM_TAGS				33

// command line (called from parse_cmdline)
void replay_set_cmdline(char *record_file, char *replay_file, char *trace_file, int trace_full);
void replay_set_autorecord(char *dir);

// -autorecord restart, called from WinMainSub
void replay_wait_for_restart();		// first thing: wait for the instance that started this one to close
void replay_restart();					// after game_shutdown: start a new instance if one is due

// lifecycle, called from FreeSpace.cpp
void replay_startup();					// after parse_cmdline, before game_init
void replay_game_init_done();			// end of game_init
int  replay_level_seed(int seed);	// game_level_init: returns the seed to use
void replay_load_begin();				// start of game_start_mission
void replay_load_end();					// end of game_start_mission
void replay_enter_game();				// GS_EVENT_ENTER_GAME
void replay_mission_over();			// left gameplay (main menu, debrief, quit)
void replay_shutdown();					// game_shutdown
int  replay_load_pilot();				// GS_EVENT_GAME_INIT: 1 if replay loaded its own pilot
void replay_enter_state(int state);	// called from game_enter_state; ends the recording when the mission is left
void replay_drive_state(int state);	// called each frame from game_do_state; drives menus in replay
void replay_sim_frame_end();			// end of game_simulation_frame: writes the state trace

// nesting: hooked functions called from inside other hooked functions are not recorded
void replay_push();
void replay_pop();

// value hooks.  replay_should_read() is true when the value must come from the recording.
int   replay_should_read();
int   replay_should_read_tag(int tag);	// as replay_should_read, but false for a tag the diagnostic FS2_REPLAY_SOUND_LIVE answers live
int   replay_read_int(int tag);
float replay_read_float(int tag);
int   replay_write_int(int tag, int val);
float replay_write_float(int tag, float val);
void  replay_read_ints(int tag, int *vals, int count);
void  replay_write_ints(int tag, int *vals, int count);

// The state of an input device the original game did not have (for example a control pad), as
// up to max ints.  In replay, fills vals from the recording and returns the count, or 0 when the
// recording has no entry here (made by a build without the device): it then reads as not connected.  Otherwise
// records vals[0..count-1] and returns count.  Pack the live state into vals only when
// replay_should_read() is 0.
int   replay_input_device(int *vals, int count, int max);

// sound handles: the live handle is kept unless the recorded call failed or succeeded differently
int replay_snd_handle(int tag, int live_handle);

// events.  Return 0 if the live event must be ignored (during replay).
int  replay_event_key(unsigned int code, int state, unsigned int latency);
int  replay_event_mouse(unsigned int flags, int set);
int  replay_event_focus(int got_focus);
int  replay_live_focus();		// 0 while a replay is feeding the mission: live focus changes must not change game state
void replay_os_poll();

// wrap a function returning int / float.  The live expression is evaluated only when the
// value is not coming from the recording.
#define REPLAY_WRAP_INT(tag, expr)	{ if (replay_should_read_tag(tag)) return replay_read_int(tag); replay_push(); int replay_r_ = (expr); replay_pop(); return replay_write_int(tag, replay_r_); }
#define REPLAY_WRAP_FLOAT(tag, expr)	{ if (replay_should_read()) return replay_read_float(tag); replay_push(); float replay_r_ = (expr); replay_pop(); return replay_write_float(tag, replay_r_); }

#endif
