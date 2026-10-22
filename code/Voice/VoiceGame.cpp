/*
 * Voice control for squad orders: the game side.  See VoiceGame.h and docs/VOICE_CONTROL.md.
 *
 * Live, each frame: while push-to-talk is held the microphone's audio streams to the engine
 * with the phrases for the current step.  The engine answers at each pause and on release; the
 * text is matched against the targets and menu rows as they are now, and the result is
 * recorded (replay_input_device) and applied.  All of that live work runs inside
 * replay_push()/replay_pop() and times itself with GetTickCount, so it adds nothing to a
 * recording but the result.
 *
 * In replay the engine and microphone are never started: each frame reads a recorded result,
 * if there is one at this point, and applies it.  Applying re-checks the game state, so a
 * replay does exactly what the recorded run did.
 */

#include <windows.h>

#include "pstypes.h"
#include "linklist.h"
#include "osregistry.h"
#include "key.h"
#include "systemvars.h"
#include "player.h"
#include "ship.h"
#include "gamesnd.h"
#include "KeyControl.h"
#include "ControlsConfig.h"
#include "../Replay/Replay.h"

#include "VoiceCore.h"
#include "VoiceEngine.h"
#include "VoiceCapture.h"
#include "VoiceGame.h"

// HUDsquadmsg.cpp
extern int hud_squadmsg_voice_ship_known(int shipnum);
extern int hud_squadmsg_voice_wing_known(int wingnum);
extern int hud_squadmsg_voice_open_ship(int shipnum);
extern int hud_squadmsg_voice_open_wing(int wingnum);
extern int hud_squadmsg_voice_open_all_fighters();
extern int hud_squadmsg_voice_in_command();
extern int hud_squadmsg_voice_num_rows();
extern int hud_squadmsg_voice_row(int row, char **text, int *order, int *active);
extern int hud_squadmsg_voice_select_row(int row, int order);
extern int hud_squadmsg_voice_held_order();
extern int hud_squadmsg_voice_shortcut(int order);
extern int hud_squadmsg_voice_send_ship(int shipnum, int order);
extern int hud_squadmsg_voice_send_wing(int wingnum, int order);
extern int hud_squadmsg_voice_send_all_fighters(int order);
extern void hud_squadmsg_rearm_shortcut();

#define VOICE_INI_SECTION			"Voice"
#define VOICE_DEFAULT_PTT			KEY_CAPSLOCK	// unbound by default.  Not backtick: that is the debug key, and the ship keys are ignored while it is held
#define VOICE_DEFAULT_MIN_CONF	"0.6"

// a recorded result: VOICE_MAGIC, kind, index, order
#define VOICE_MAGIC					0x56434531		// "VCE1"
#define VOICE_NUM_VALS				4
#define VOICE_MAX_VALS				16					// a voice entry is 4; read after the pad's, never instead of it

static int Voice_enabled = 0;			// engine opened; live voice runs
static int Voice_ptt_key = VOICE_DEFAULT_PTT;
static float Voice_min_conf = 0.6f;
static int Voice_failed_logged = 0;
static int Voice_capture_failed_logged = 0;

static voice_core Voice_core;
static int Voice_talking = 0;			// push-to-talk held and recording
static int Voice_busy = 0;				// a stream is open in the engine
static int Voice_need_next = 0;		// a phrase was acted on; the engine waits for the next step's phrases
static int Voice_job_step = VOICE_STEP_TARGET;

static voice_target Voice_targets[VOICE_MAX_TARGETS];
static voice_row Voice_rows[VOICE_MAX_ROWS];
static char Voice_grammar[VOICE_GRAMMAR_LEN];
static char Voice_json[VOICE_RESULT_LEN];

void voice_game_init()
{
	char *ptr;
	char dll_dir[MAX_PATH], model_dir[MAX_PATH];
	uint key;

	voice_core_init(&Voice_core);

	// replays apply recorded results and never hear anything
	if (Replay_mode == REPLAY_PLAY) {
		return;
	}

	if (!os_config_read_uint(VOICE_INI_SECTION, "Enabled", 0)) {
		return;
	}

	ptr = os_config_read_string(VOICE_INI_SECTION, "Dll", "");
	strncpy(dll_dir, ptr ? ptr : "", MAX_PATH - 1);
	dll_dir[MAX_PATH - 1] = 0;

	ptr = os_config_read_string(VOICE_INI_SECTION, "Model", "");
	strncpy(model_dir, ptr ? ptr : "", MAX_PATH - 1);
	model_dir[MAX_PATH - 1] = 0;

	key = os_config_read_uint(VOICE_INI_SECTION, "PushToTalk", VOICE_DEFAULT_PTT);
	Voice_ptt_key = ((key > 0) && (key < NUM_KEYS)) ? (int)key : VOICE_DEFAULT_PTT;

	ptr = os_config_read_string(VOICE_INI_SECTION, "MinConfidence", VOICE_DEFAULT_MIN_CONF);
	Voice_min_conf = (float)atof(ptr ? ptr : VOICE_DEFAULT_MIN_CONF);

	if (!voice_engine_open(dll_dir, model_dir)) {
		mprintf(("Voice: off: %s\n", voice_engine_error()));
		return;
	}

	mprintf(("Voice: loading model '%s', push-to-talk key 0x%x, min confidence %.2f\n", model_dir, Voice_ptt_key, Voice_min_conf));
	Voice_enabled = 1;
}

void voice_game_close()
{
	short *samples;

	if (Voice_talking) {
		voice_capture_stop(&samples);
		Voice_talking = 0;
	}
	voice_capture_close();
	voice_engine_close();
	Voice_enabled = 0;
	Voice_busy = 0;
	Voice_need_next = 0;
}

// the ships and wings the player can name now
static int voice_build_targets()
{
	int i, n = 0;
	ship_obj *so;
	ship *shipp;

	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
		if (n >= VOICE_MAX_TARGETS) {
			break;
		}
		shipp = &Ships[Objects[so->objnum].instance];
		if (!hud_squadmsg_voice_ship_known(SHIP_INDEX(shipp))) {
			continue;
		}
		strncpy(Voice_targets[n].name, shipp->ship_name, VOICE_TEXT_LEN - 1);
		Voice_targets[n].name[VOICE_TEXT_LEN - 1] = 0;
		Voice_targets[n].kind = VOICE_TARGET_SHIP;
		Voice_targets[n].index = SHIP_INDEX(shipp);
		n++;
	}

	for (i = 0; (i < num_wings) && (n < VOICE_MAX_TARGETS); i++) {
		if (!hud_squadmsg_voice_wing_known(i)) {
			continue;
		}
		strncpy(Voice_targets[n].name, Wings[i].name, VOICE_TEXT_LEN - 1);
		Voice_targets[n].name[VOICE_TEXT_LEN - 1] = 0;
		Voice_targets[n].kind = VOICE_TARGET_WING;
		Voice_targets[n].index = i;
		n++;
	}

	return n;
}

// the rows the menu shows now
static int voice_build_rows()
{
	int i, n, order, active;
	char *text;

	n = hud_squadmsg_voice_num_rows();
	if (n > VOICE_MAX_ROWS) {
		n = VOICE_MAX_ROWS;
	}
	for (i = 0; i < n; i++) {
		if (!hud_squadmsg_voice_row(i, &text, &order, &active)) {
			break;
		}
		strncpy(Voice_rows[i].text, text, VOICE_TEXT_LEN - 1);
		Voice_rows[i].text[VOICE_TEXT_LEN - 1] = 0;
		Voice_rows[i].order = ((order >= 0) && (order < VOICE_NUM_ORDERS)) ? order : -1;
		Voice_rows[i].active = active;
	}
	return i;
}

// do what a result says.  Runs in the recorded run and in its replay at the same point.
static void voice_apply(int kind, int index, int order)
{
	switch (kind) {
	case VOICE_RESULT_SHIP:
		hud_squadmsg_voice_open_ship(index);
		break;

	case VOICE_RESULT_WING:
		hud_squadmsg_voice_open_wing(index);
		break;

	case VOICE_RESULT_ALL_FIGHTERS:
		hud_squadmsg_voice_open_all_fighters();
		break;

	case VOICE_RESULT_ORDER:
	case VOICE_RESULT_REFUSED:
		// the menu sends the order, or refuses a greyed-out row with the fail sound
		if (!hud_squadmsg_voice_select_row(index, order)) {
			gamesnd_play_iface(SND_GENERAL_FAIL);
		}
		break;

	case VOICE_RESULT_FUNCTION:
		// as the keys do: CYCLE_NUM_MISSLES toggles dual fire, and the rearm key calls in support
		if ((Player == NULL) || (Player_ship == NULL)) {
			break;
		}
		if (index == VOICE_FUNC_SINGLE_MISSILE) {
			if (Player_ship->flags & SF_SECONDARY_DUAL_FIRE) {
				button_info_set(&Player->bi, CYCLE_NUM_MISSLES);
			}
		} else if (index == VOICE_FUNC_DUAL_MISSILE) {
			if (!(Player_ship->flags & SF_SECONDARY_DUAL_FIRE)) {
				button_info_set(&Player->bi, CYCLE_NUM_MISSLES);
			}
		} else if (index == VOICE_FUNC_CYCLE_MISSILE) {
			button_info_set(&Player->bi, CYCLE_SECONDARY);
		} else if (index == VOICE_FUNC_REARM) {
			hud_squadmsg_rearm_shortcut();
		}
		break;

	case VOICE_RESULT_SHORTCUT:
		// the menu opens holding the order, as its shortcut key does
		hud_squadmsg_voice_shortcut(order);
		break;

	case VOICE_RESULT_SEND_SHIP:
		hud_squadmsg_voice_send_ship(index, order);
		break;

	case VOICE_RESULT_SEND_WING:
		hud_squadmsg_voice_send_wing(index, order);
		break;

	case VOICE_RESULT_SEND_ALL_FIGHTERS:
		hud_squadmsg_voice_send_all_fighters(order);
		break;

	default:
		break;
	}
}

// the phrases for a step, as the game is now, into Voice_grammar.  0 if there are none yet.
static int voice_build_grammar(int step)
{
	int num;

	if (step == VOICE_STEP_ORDER) {
		num = voice_build_rows();
		return voice_order_phrases(Voice_rows, num, Voice_grammar, VOICE_GRAMMAR_LEN);
	}
	num = voice_build_targets();
	if (step == VOICE_STEP_SEND) {
		return voice_send_phrases(Voice_targets, num, Voice_grammar, VOICE_GRAMMAR_LEN);
	}
	return voice_target_phrases(Voice_targets, num, Voice_grammar, VOICE_GRAMMAR_LEN);
}

// push-to-talk and the engine.  Returns 1 and fills r when something was recognised.
//
// One press can carry a target and then an order: the engine ends a phrase at the pause after
// it, the result is acted on (the menu opens at the target), and the next frame gives the
// engine the next step's phrases while it holds on to the audio that came after the pause.
static int voice_live(voice_result *r)
{
	int now, step, count, num, final;
	short *samples;
	char text[VOICE_TEXT_LEN];
	float conf;

	now = (int)GetTickCount();

	if (voice_engine_state() == VOICE_ENGINE_FAILED) {
		if (!Voice_failed_logged) {
			mprintf(("Voice: off: %s\n", voice_engine_error()));
			Voice_failed_logged = 1;
		}
		Voice_enabled = 0;
		return 0;
	}

	step = voice_core_step(&Voice_core, hud_squadmsg_voice_in_command(), hud_squadmsg_voice_held_order(), now);

	// the last phrase has been acted on: give the engine the phrases for the step it led to.
	// The menu fills its rows in its own frame, so an order step may have to wait one.
	if (Voice_need_next && voice_build_grammar(step)) {
		voice_engine_stream_next(Voice_grammar);
		Voice_job_step = step;
		Voice_need_next = 0;
	}

	if (voice_engine_state() == VOICE_ENGINE_READY) {
		if (keyd_pressed[Voice_ptt_key]) {
			if (Voice_talking) {
				count = voice_capture_poll(&samples);
				voice_engine_stream_feed(samples, count);
			} else if (!Voice_busy && voice_build_grammar(step)) {
				if (!voice_capture_start()) {
					if (!Voice_capture_failed_logged) {
						mprintf(("Voice: cannot open the microphone\n"));
						Voice_capture_failed_logged = 1;
					}
				} else if (voice_engine_stream_start(Voice_grammar)) {
					Voice_talking = 1;
					Voice_busy = 1;
					Voice_job_step = step;
				} else {
					voice_capture_stop(&samples);
				}
			}
		} else if (Voice_talking) {
			Voice_talking = 0;
			count = voice_capture_stop(&samples);
			voice_engine_stream_feed(samples, count);
			voice_engine_stream_end();
		}
	}

	if (!Voice_busy || !voice_engine_stream_result(Voice_json, VOICE_RESULT_LEN, &final)) {
		return 0;
	}
	if (final) {
		Voice_busy = 0;
		Voice_need_next = 0;
	} else {
		Voice_need_next = 1;
	}

	if (!voice_parse_result(Voice_json, text, VOICE_TEXT_LEN, &conf)) {
		return 0;
	}

	// match against the game as it is now; the menu may have changed while the engine worked
	if (Voice_job_step == VOICE_STEP_ORDER) {
		num = voice_build_rows();
		voice_core_dispatch(&Voice_core, Voice_job_step, text, conf, Voice_min_conf, NULL, 0, Voice_rows, num, now, r);
	} else {
		num = voice_build_targets();
		voice_core_dispatch(&Voice_core, Voice_job_step, text, conf, Voice_min_conf, Voice_targets, num, NULL, 0, now, r);
	}

	mprintf(("Voice: heard '%s' (%.2f): result %d, index %d, order %d\n", text, conf, r->kind, r->index, r->order));
	return r->kind != VOICE_RESULT_NONE;
}

void voice_frame()
{
	int vals[VOICE_MAX_VALS];
	int n, have;
	voice_result r;

	if (Replay_mode == REPLAY_PLAY) {
		if (replay_should_read()) {
			n = replay_input_device(vals, 0, VOICE_MAX_VALS);
			if ((n == VOICE_NUM_VALS) && (vals[0] == VOICE_MAGIC)) {
				voice_apply(vals[1], vals[2], vals[3]);
			}
		}
		return;
	}

	if (!Voice_enabled || (Game_mode & GM_MULTIPLAYER) || (Player == NULL)) {
		return;
	}

	replay_push();
	have = voice_live(&r);
	replay_pop();

	if (!have) {
		return;
	}

	vals[0] = VOICE_MAGIC;
	vals[1] = r.kind;
	vals[2] = r.index;
	vals[3] = r.order;
	replay_input_device(vals, VOICE_NUM_VALS, VOICE_MAX_VALS);
	voice_apply(r.kind, r.index, r.order);
}
