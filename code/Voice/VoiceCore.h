/*
 * Voice control core: phrase normalisation, grammars, dispatch and the two-step state
 * machine.  Plain C++ with no FreeSpace types, so the dispatch test (tools/VoiceDispatchTest)
 * links it without the game.  See docs/VOICE_CONTROL.md.
 */

#ifndef _FS2_VOICECORE_H
#define _FS2_VOICECORE_H

#define VOICE_TEXT_LEN			64		// one phrase or menu row, normalised
#define VOICE_MAX_TARGETS		256	// ships and wings offered in step 1
#define VOICE_MAX_ROWS			16		// menu rows offered in step 2
#define VOICE_GRAMMAR_LEN		16384	// phrases, one per line

// steps
#define VOICE_STEP_TARGET		0		// say a ship, a wing, "all fighters", a ship function or an order
#define VOICE_STEP_ORDER		1		// say an order the menu shows
#define VOICE_STEP_SEND			2		// the menu holds an order (said first, or its key): say who gets it

// target kinds
#define VOICE_TARGET_SHIP		0
#define VOICE_TARGET_WING		1

// result kinds.  These values are recorded, so never renumber them.
#define VOICE_RESULT_NONE			0	// nothing matched: no effect
#define VOICE_RESULT_SHIP			1	// index = ship
#define VOICE_RESULT_WING			2	// index = wing
#define VOICE_RESULT_ALL_FIGHTERS	3
#define VOICE_RESULT_ORDER			4	// index = menu row, order = VOICE_ORDER_*
#define VOICE_RESULT_REFUSED		5	// index = menu row (greyed out), order = VOICE_ORDER_*
#define VOICE_RESULT_FUNCTION		6	// index = VOICE_FUNC_*
#define VOICE_RESULT_SHORTCUT		7	// order = VOICE_ORDER_*: the menu opens holding it, as its key does
#define VOICE_RESULT_SEND_SHIP		8	// index = ship, order = the held VOICE_ORDER_*
#define VOICE_RESULT_SEND_WING		9	// index = wing, order = the held VOICE_ORDER_*
#define VOICE_RESULT_SEND_ALL_FIGHTERS	10	// order = the held VOICE_ORDER_*

// squad orders, in the menu's order.  Recorded, so never renumber.
#define VOICE_ORDER_DESTROY_TARGET		0
#define VOICE_ORDER_DISABLE_TARGET		1
#define VOICE_ORDER_DISARM_TARGET		2
#define VOICE_ORDER_DESTROY_SUBSYSTEM	3
#define VOICE_ORDER_PROTECT_TARGET		4
#define VOICE_ORDER_IGNORE_TARGET		5
#define VOICE_ORDER_FORM_ON_WING			6
#define VOICE_ORDER_COVER_ME				7
#define VOICE_ORDER_ENGAGE_ENEMY			8
#define VOICE_ORDER_CAPTURE_TARGET		9
#define VOICE_ORDER_REARM_ME				10
#define VOICE_ORDER_ABORT_REARM			11
#define VOICE_ORDER_DEPART					12
#define VOICE_NUM_ORDERS					13

// ship functions said in step 1.  Recorded, so never renumber.
#define VOICE_FUNC_SINGLE_MISSILE		0
#define VOICE_FUNC_DUAL_MISSILE			1
#define VOICE_FUNC_CYCLE_MISSILE			2
#define VOICE_FUNC_REARM					3	// call in the support ship, as the rearm key does
#define VOICE_NUM_FUNCS						4

#define VOICE_ORDER_TIMEOUT_MS			8000	// as the squad menu's own timeout

typedef struct voice_target {
	char	name[VOICE_TEXT_LEN];	// as the game shows it: "Alpha 2", "Beta"
	int	kind;							// VOICE_TARGET_*
	int	index;						// ship or wing index
} voice_target;

typedef struct voice_row {
	char	text[VOICE_TEXT_LEN];	// as the menu shows it: "Destroy my target", "Cover me(*)"
	int	order;						// VOICE_ORDER_*, or -1 for a row voice does not know
	int	active;						// 0 = greyed out
} voice_row;

typedef struct voice_result {
	int	kind;							// VOICE_RESULT_*
	int	index;
	int	order;
} voice_result;

typedef struct voice_core {
	int	step;							// VOICE_STEP_*
	int	order_deadline;			// ms; the order step lapses after this
	int	held_order;					// VOICE_ORDER_* the menu holds for VOICE_STEP_SEND, or -1
} voice_core;

// "Alpha 2(*)" -> "alpha two".  Lower case, digits to words, anything else that is not a
// letter to a single space.  Returns out.
char *voice_normalize(const char *in, char *out, int max);

// one phrase per line, normalised and without duplicates.  Return the count.
int voice_target_phrases(const voice_target *targets, int count, char *out, int max);
int voice_order_phrases(const voice_row *rows, int count, char *out, int max);
int voice_send_phrases(const voice_target *targets, int count, char *out, int max);	// targets only

// matching on normalised text.  Return the result kind.
int voice_match_target(const char *heard, const voice_target *targets, int count, voice_result *r);
int voice_match_order(const char *heard, const voice_row *rows, int count, voice_result *r);
int voice_match_send(const char *heard, const voice_target *targets, int count, int held_order, voice_result *r);

// Vosk result JSON -> text and the lowest word confidence (1.0 when it has none).  Returns 1
// when the text is not empty.
int voice_parse_result(const char *json, char *text, int max, float *min_conf);

// state machine
void voice_core_init(voice_core *vc);
// held_order: the VOICE_ORDER_* the menu holds for a target, or -1
int  voice_core_step(voice_core *vc, int menu_in_command, int held_order, int now_ms);
int  voice_core_dispatch(voice_core *vc, int step, const char *heard, float conf, float min_conf,
		const voice_target *targets, int num_targets, const voice_row *rows, int num_rows, int now_ms, voice_result *r);

const char *voice_order_name(int order);		// first phrase of the order, for logs
const char *voice_func_name(int func);

#endif
