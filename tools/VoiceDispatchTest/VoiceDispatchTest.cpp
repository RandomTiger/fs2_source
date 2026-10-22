/*
 * VoiceDispatchTest: the no-microphone check for voice control.  Links VoiceCore and
 * VoiceEngine without the game, feeds recognised text against stubbed targets and menu rows,
 * and checks what voice would do.  Never starts the game and needs no microphone, no Vosk
 * DLL and no model.  See docs/VOICE_CONTROL.md.
 *
 * Usage: VoiceDispatchTest            prints each failure, then PASS or FAIL; exit code 0 on PASS
 *        VoiceDispatchTest <dll_dir>  also loads libvosk.dll from dll_dir (same bitness as this
 *                                     exe) against an empty model folder: the DLL and every
 *                                     function load, and the model failure is reported cleanly
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../../code/Voice/VoiceCore.h"
#include "../../code/Voice/VoiceEngine.h"

static int Checks = 0;
static int Failures = 0;

static void check(int ok, const char *what, const char *detail)
{
	Checks++;
	if (!ok) {
		Failures++;
		printf("FAIL: %s%s%s\n", what, detail ? " -- " : "", detail ? detail : "");
	}
}

static void check_normalize(const char *in, const char *expect)
{
	char out[VOICE_TEXT_LEN];
	char detail[256];

	voice_normalize(in, out, VOICE_TEXT_LEN);
	sprintf(detail, "'%s' gave '%s', expected '%s'", in, out, expect);
	check(!strcmp(out, expect), "normalize", detail);
}

// stub mission: two ships, one of which shares a name with a wing, and two wings
static const voice_target Targets[] = {
	{ "Alpha 2", VOICE_TARGET_SHIP, 11 },
	{ "GTC Aquitaine", VOICE_TARGET_SHIP, 12 },
	{ "Beta", VOICE_TARGET_SHIP, 13 },
	{ "Alpha", VOICE_TARGET_WING, 0 },
	{ "Beta", VOICE_TARGET_WING, 1 },
	{ "Epsilon", VOICE_TARGET_WING, 4 },
};
#define NUM_TARGETS	((int)(sizeof(Targets) / sizeof(Targets[0])))

// stub ship command menu: every order, as the game shows it, with two greyed out and one
// flagged "(*)" as the default
static voice_row Rows[VOICE_NUM_ORDERS];
static const char *Row_text[VOICE_NUM_ORDERS] = {
	"Destroy my target", "Disable my target", "Disarm my target", "Destroy subsystem",
	"Protect my target", "Ignore my target", "Form on my wing", "Cover me(*)", "Engage enemy",
	"Capture my target", "Rearm me", "Abort rearm", "Depart"
};
#define GREY_ROW_1	VOICE_ORDER_CAPTURE_TARGET
#define GREY_ROW_2	VOICE_ORDER_ABORT_REARM

static void build_rows()
{
	int i;

	for (i = 0; i < VOICE_NUM_ORDERS; i++) {
		strcpy(Rows[i].text, Row_text[i]);
		Rows[i].order = i;
		Rows[i].active = (i != GREY_ROW_1) && (i != GREY_ROW_2);
	}
}

// the phrase list contains phrase as a whole line
static int has_phrase(const char *list, const char *phrase)
{
	const char *p = list;
	int n = (int)strlen(phrase);

	while (*p) {
		const char *e = strchr(p, '\n');
		int len = e ? (int)(e - p) : (int)strlen(p);

		if ((len == n) && !strncmp(p, phrase, n)) {
			return 1;
		}
		p += len;
		if (*p == '\n') {
			p++;
		}
	}
	return 0;
}

static void test_normalize()
{
	check_normalize("Alpha 2(*)", "alpha two");
	check_normalize("Cover me(*)", "cover me");
	check_normalize("GTC Aquitaine", "gtc aquitaine");
	check_normalize("  Destroy   my-target  ", "destroy my target");
	check_normalize("2", "two");
	check_normalize("12", "twelve");
	check_normalize("19", "nineteen");
	check_normalize("20", "twenty");
	check_normalize("21", "twenty one");
	check_normalize("07", "zero seven");
	check_normalize("100", "one zero zero");
	check_normalize("Alpha12", "alpha twelve");
	check_normalize("O'Brien", "o'brien");
	check_normalize("\xC9tienne", "\xC9tienne");	// Latin-1 kept as it is
	check_normalize("", "");
	check_normalize("(*)", "");
}

static void test_phrases()
{
	static char list[VOICE_GRAMMAR_LEN];
	char detail[128];
	int i, j, n;
	const char *expect_targets[] = {
		"alpha two", "gtc aquitaine", "beta", "alpha", "alpha wing", "beta wing", "epsilon",
		"epsilon wing", "all fighters", "single missile", "dual missile", "cycle missile", "support", "rearm", "repair",
		"destroy my target", "disable my target", "disarm my target", "destroy subsystem", "protect my target",
		"ignore my target", "form on my wing", "cover me", "engage enemy", "attack", "capture my target", "depart"
	};
	const char *expect_orders[] = {
		"destroy my target", "disable my target", "disarm my target", "destroy subsystem",
		"protect my target", "ignore my target", "form on my wing", "cover me", "engage enemy",
		"attack", "capture my target", "rearm me", "abort rearm", "depart"
	};

	// "beta" is both a ship and a wing: listed once
	n = voice_target_phrases(Targets, NUM_TARGETS, list, VOICE_GRAMMAR_LEN);
	sprintf(detail, "%d target phrases, expected 27", n);
	check(n == 27, "target phrase count", detail);
	for (i = 0; i < (int)(sizeof(expect_targets) / sizeof(expect_targets[0])); i++) {
		check(has_phrase(list, expect_targets[i]), "target phrase listed", expect_targets[i]);
	}
	// "rearm me" and "abort rearm" have no shortcut key, so they are not said first
	check(!has_phrase(list, "rearm me") && !has_phrase(list, "abort rearm"), "no order first without a key", NULL);

	// the send step: the targets only
	n = voice_send_phrases(Targets, NUM_TARGETS, list, VOICE_GRAMMAR_LEN);
	sprintf(detail, "%d send phrases, expected 9", n);
	check(n == 9, "send phrase count", detail);
	for (i = 0; i < 9; i++) {
		check(has_phrase(list, expect_targets[i]), "send phrase listed", expect_targets[i]);
	}
	check(!has_phrase(list, "support") && !has_phrase(list, "cover me"), "send phrases are targets only", NULL);

	// greyed-out rows are listed too, so they can be heard and refused
	n = voice_order_phrases(Rows, VOICE_NUM_ORDERS, list, VOICE_GRAMMAR_LEN);
	sprintf(detail, "%d order phrases, expected 14", n);
	check(n == 14, "order phrase count", detail);
	for (i = 0; i < (int)(sizeof(expect_orders) / sizeof(expect_orders[0])); i++) {
		check(has_phrase(list, expect_orders[i]), "order phrase listed", expect_orders[i]);
	}

	// every listed order phrase is the first phrase of exactly one order, or a synonym
	for (i = 0; i < VOICE_NUM_ORDERS; i++) {
		for (j = 0; j < VOICE_NUM_ORDERS; j++) {
			if ((i != j) && !strcmp(voice_order_name(i), voice_order_name(j))) {
				check(0, "order names distinct", voice_order_name(i));
			}
		}
	}

	// a tiny buffer does not overflow
	n = voice_target_phrases(Targets, NUM_TARGETS, list, 12);
	check((int)strlen(list) < 12, "target phrases fit a small buffer", NULL);
}

// dispatch heard in step, at confidence 0.9 against 0.6, and check the result
static void expect(voice_core *vc, int step, const char *heard, int kind, int index, int order, int next_step)
{
	voice_result r;
	char detail[256];

	voice_core_dispatch(vc, step, heard, 0.9f, 0.6f, Targets, NUM_TARGETS, Rows, VOICE_NUM_ORDERS, 1000, &r);
	sprintf(detail, "'%s' in step %d gave kind %d index %d order %d step %d; expected %d %d %d step %d",
		heard, step, r.kind, r.index, r.order, vc->step, kind, index, order, next_step);
	check((r.kind == kind) && (r.index == index) && (r.order == order) && (vc->step == next_step), "dispatch", detail);
}

static void test_targets()
{
	voice_core vc;
	char heard[VOICE_TEXT_LEN];

	voice_core_init(&vc);

	// a ship's name wins over a wing of the same name; "name wing" is always the wing
	expect(&vc, VOICE_STEP_TARGET, "alpha two", VOICE_RESULT_SHIP, 11, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "Alpha 2", VOICE_RESULT_SHIP, 11, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "gtc aquitaine", VOICE_RESULT_SHIP, 12, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "beta", VOICE_RESULT_SHIP, 13, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "beta wing", VOICE_RESULT_WING, 1, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "alpha", VOICE_RESULT_WING, 0, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "alpha wing", VOICE_RESULT_WING, 0, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "epsilon", VOICE_RESULT_WING, 4, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "epsilon wing", VOICE_RESULT_WING, 4, -1, VOICE_STEP_ORDER);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "all fighters", VOICE_RESULT_ALL_FIGHTERS, -1, -1, VOICE_STEP_ORDER);

	// functions do their work at once and leave the step at target
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "single missile", VOICE_RESULT_FUNCTION, VOICE_FUNC_SINGLE_MISSILE, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "dual missile", VOICE_RESULT_FUNCTION, VOICE_FUNC_DUAL_MISSILE, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "cycle missile", VOICE_RESULT_FUNCTION, VOICE_FUNC_CYCLE_MISSILE, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "support", VOICE_RESULT_FUNCTION, VOICE_FUNC_REARM, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "rearm", VOICE_RESULT_FUNCTION, VOICE_FUNC_REARM, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "repair", VOICE_RESULT_FUNCTION, VOICE_FUNC_REARM, -1, VOICE_STEP_TARGET);

	// unmatched: nothing, and the step stays
	expect(&vc, VOICE_STEP_TARGET, "gamma wing", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "alpha two wing", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_TARGET);
	// "rearm me" and "abort rearm" are not said first
	expect(&vc, VOICE_STEP_TARGET, "rearm me", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_TARGET);
	expect(&vc, VOICE_STEP_TARGET, "abort rearm", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_TARGET);

	// every target name the stub offers resolves to itself
	for (int i = 0; i < NUM_TARGETS; i++) {
		voice_result r;

		voice_normalize(Targets[i].name, heard, VOICE_TEXT_LEN);
		voice_match_target(heard, Targets, NUM_TARGETS, &r);
		if (Targets[i].kind == VOICE_TARGET_WING) {
			strcat(heard, " wing");
			voice_match_target(heard, Targets, NUM_TARGETS, &r);
			check((r.kind == VOICE_RESULT_WING) && (r.index == Targets[i].index), "wing resolves", heard);
		} else {
			check((r.kind == VOICE_RESULT_SHIP) && (r.index == Targets[i].index), "ship resolves", heard);
		}
	}
}

static void test_orders()
{
	voice_core vc;
	int i;

	voice_core_init(&vc);

	// each row by its own text (as the menu shows it, then normalised), back to the target step
	for (i = 0; i < VOICE_NUM_ORDERS; i++) {
		int kind = Rows[i].active ? VOICE_RESULT_ORDER : VOICE_RESULT_REFUSED;
		int next = Rows[i].active ? VOICE_STEP_TARGET : VOICE_STEP_ORDER;

		vc.step = VOICE_STEP_ORDER;
		expect(&vc, VOICE_STEP_ORDER, Rows[i].text, kind, i, i, next);
		vc.step = VOICE_STEP_ORDER;
		expect(&vc, VOICE_STEP_ORDER, voice_order_name(i), kind, i, i, next);
	}

	// the synonym
	vc.step = VOICE_STEP_ORDER;
	expect(&vc, VOICE_STEP_ORDER, "attack", VOICE_RESULT_ORDER, VOICE_ORDER_ENGAGE_ENEMY, VOICE_ORDER_ENGAGE_ENEMY, VOICE_STEP_TARGET);

	// a greyed-out row is refused and the step stays, so another order can be said
	vc.step = VOICE_STEP_ORDER;
	expect(&vc, VOICE_STEP_ORDER, "capture my target", VOICE_RESULT_REFUSED, GREY_ROW_1, GREY_ROW_1, VOICE_STEP_ORDER);
	expect(&vc, VOICE_STEP_ORDER, "abort rearm", VOICE_RESULT_REFUSED, GREY_ROW_2, GREY_ROW_2, VOICE_STEP_ORDER);
	expect(&vc, VOICE_STEP_ORDER, "cover me", VOICE_RESULT_ORDER, VOICE_ORDER_COVER_ME, VOICE_ORDER_COVER_ME, VOICE_STEP_TARGET);

	// unmatched in the order step: nothing, and the step stays
	vc.step = VOICE_STEP_ORDER;
	expect(&vc, VOICE_STEP_ORDER, "do a barrel roll", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_ORDER);
	// a target said in the order step is not an order
	expect(&vc, VOICE_STEP_ORDER, "alpha two", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_ORDER);

	// an order the menu does not show is not heard: the wing menu has no "rearm me"
	{
		voice_row wing_rows[2];
		voice_result r;

		strcpy(wing_rows[0].text, "Destroy my target");
		wing_rows[0].order = VOICE_ORDER_DESTROY_TARGET;
		wing_rows[0].active = 1;
		strcpy(wing_rows[1].text, "Engage enemy");
		wing_rows[1].order = VOICE_ORDER_ENGAGE_ENEMY;
		wing_rows[1].active = 1;

		vc.step = VOICE_STEP_ORDER;
		voice_core_dispatch(&vc, VOICE_STEP_ORDER, "rearm me", 0.9f, 0.6f, Targets, NUM_TARGETS, wing_rows, 2, 0, &r);
		check(r.kind == VOICE_RESULT_NONE, "order not on the menu", "rearm me");
		voice_core_dispatch(&vc, VOICE_STEP_ORDER, "attack", 0.9f, 0.6f, Targets, NUM_TARGETS, wing_rows, 2, 0, &r);
		check((r.kind == VOICE_RESULT_ORDER) && (r.index == 1) && (r.order == VOICE_ORDER_ENGAGE_ENEMY), "synonym on a short menu", "attack");
	}

	// a row voice does not know (order -1) matches by its text only
	{
		voice_row odd;
		voice_result r;

		strcpy(odd.text, "Rearm/Repair Subsys");
		odd.order = -1;
		odd.active = 1;
		voice_match_order("rearm repair subsys", &odd, 1, &r);
		check((r.kind == VOICE_RESULT_ORDER) && (r.index == 0) && (r.order == -1), "unknown row by text", NULL);
	}
}

static void test_send()
{
	voice_core vc;
	int i;

	voice_core_init(&vc);

	// an order said first: the menu holds it, and the next step names who gets it
	for (i = 0; i < VOICE_NUM_ORDERS; i++) {
		int first = (i != VOICE_ORDER_REARM_ME) && (i != VOICE_ORDER_ABORT_REARM);

		vc.step = VOICE_STEP_TARGET;
		vc.held_order = -1;
		if (first) {
			expect(&vc, VOICE_STEP_TARGET, voice_order_name(i), VOICE_RESULT_SHORTCUT, -1, i, VOICE_STEP_SEND);
			check(vc.held_order == i, "shortcut holds the order", voice_order_name(i));
		} else {
			expect(&vc, VOICE_STEP_TARGET, voice_order_name(i), VOICE_RESULT_NONE, -1, -1, VOICE_STEP_TARGET);
		}
	}
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "attack", VOICE_RESULT_SHORTCUT, -1, VOICE_ORDER_ENGAGE_ENEMY, VOICE_STEP_SEND);
	vc.step = VOICE_STEP_TARGET;
	expect(&vc, VOICE_STEP_TARGET, "Destroy my target", VOICE_RESULT_SHORTCUT, -1, VOICE_ORDER_DESTROY_TARGET, VOICE_STEP_SEND);

	// then a ship, a wing or all fighters gets the held order, and the step is target again
	expect(&vc, VOICE_STEP_SEND, "beta wing", VOICE_RESULT_SEND_WING, 1, VOICE_ORDER_DESTROY_TARGET, VOICE_STEP_TARGET);
	check(vc.held_order == -1, "send clears the held order", NULL);
	vc.step = VOICE_STEP_SEND;
	vc.held_order = VOICE_ORDER_COVER_ME;
	expect(&vc, VOICE_STEP_SEND, "alpha two", VOICE_RESULT_SEND_SHIP, 11, VOICE_ORDER_COVER_ME, VOICE_STEP_TARGET);
	vc.step = VOICE_STEP_SEND;
	vc.held_order = VOICE_ORDER_COVER_ME;
	expect(&vc, VOICE_STEP_SEND, "beta", VOICE_RESULT_SEND_SHIP, 13, VOICE_ORDER_COVER_ME, VOICE_STEP_TARGET);
	vc.step = VOICE_STEP_SEND;
	vc.held_order = VOICE_ORDER_ENGAGE_ENEMY;
	expect(&vc, VOICE_STEP_SEND, "epsilon", VOICE_RESULT_SEND_WING, 4, VOICE_ORDER_ENGAGE_ENEMY, VOICE_STEP_TARGET);
	vc.step = VOICE_STEP_SEND;
	vc.held_order = VOICE_ORDER_ENGAGE_ENEMY;
	expect(&vc, VOICE_STEP_SEND, "all fighters", VOICE_RESULT_SEND_ALL_FIGHTERS, -1, VOICE_ORDER_ENGAGE_ENEMY, VOICE_STEP_TARGET);

	// in the send step, functions, orders and unknown names do nothing, and the step stays
	vc.step = VOICE_STEP_SEND;
	vc.held_order = VOICE_ORDER_DEPART;
	expect(&vc, VOICE_STEP_SEND, "support", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_SEND);
	expect(&vc, VOICE_STEP_SEND, "single missile", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_SEND);
	expect(&vc, VOICE_STEP_SEND, "cover me", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_SEND);
	expect(&vc, VOICE_STEP_SEND, "gamma wing", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_SEND);
	check(vc.held_order == VOICE_ORDER_DEPART, "unmatched keeps the held order", NULL);

	// no held order: a name in the send step does nothing
	vc.held_order = -1;
	expect(&vc, VOICE_STEP_SEND, "beta wing", VOICE_RESULT_NONE, -1, -1, VOICE_STEP_SEND);
}

static void test_confidence()
{
	voice_core vc;
	voice_result r;

	voice_core_init(&vc);

	// below the minimum: nothing, whatever was heard, and the step stays
	voice_core_dispatch(&vc, VOICE_STEP_TARGET, "alpha two", 0.59f, 0.6f, Targets, NUM_TARGETS, Rows, VOICE_NUM_ORDERS, 0, &r);
	check((r.kind == VOICE_RESULT_NONE) && (vc.step == VOICE_STEP_TARGET), "low confidence ignored", NULL);

	// at the minimum: accepted
	voice_core_dispatch(&vc, VOICE_STEP_TARGET, "alpha two", 0.6f, 0.6f, Targets, NUM_TARGETS, Rows, VOICE_NUM_ORDERS, 0, &r);
	check(r.kind == VOICE_RESULT_SHIP, "confidence at the minimum accepted", NULL);
}

static void test_steps()
{
	voice_core vc;
	voice_result r;
	int now = 50000;

	voice_core_init(&vc);
	check(vc.step == VOICE_STEP_TARGET, "starts at target", NULL);

	// no menu: target
	check(voice_core_step(&vc, 0, -1, now) == VOICE_STEP_TARGET, "no menu is target", NULL);

	// a target said: order until the deadline, while the menu is in its command mode
	voice_core_dispatch(&vc, VOICE_STEP_TARGET, "beta wing", 1.0f, 0.6f, Targets, NUM_TARGETS, Rows, VOICE_NUM_ORDERS, now, &r);
	check((vc.step == VOICE_STEP_ORDER) && (vc.order_deadline == now + VOICE_ORDER_TIMEOUT_MS), "target sets the deadline", NULL);
	check(voice_core_step(&vc, 1, -1, now + 1) == VOICE_STEP_ORDER, "order step while the menu is up", NULL);
	check(voice_core_step(&vc, 1, -1, now + VOICE_ORDER_TIMEOUT_MS - 1) == VOICE_STEP_ORDER, "order step just before the deadline", NULL);
	check(voice_core_step(&vc, 1, -1, now + VOICE_ORDER_TIMEOUT_MS) == VOICE_STEP_TARGET, "target step at the deadline", NULL);

	// the menu closed (order given by key, or menu timed out): back to target
	voice_core_dispatch(&vc, VOICE_STEP_TARGET, "beta wing", 1.0f, 0.6f, Targets, NUM_TARGETS, Rows, VOICE_NUM_ORDERS, now, &r);
	check(voice_core_step(&vc, 0, -1, now + 1) == VOICE_STEP_TARGET, "menu closed is target", NULL);
	check(vc.step == VOICE_STEP_TARGET, "menu closed resets the step", NULL);

	// the menu opened at a target by the keyboard: order step, with a fresh deadline
	check(voice_core_step(&vc, 1, -1, now + 100) == VOICE_STEP_ORDER, "keyboard-opened menu is order", NULL);
	check(vc.order_deadline == now + 100 + VOICE_ORDER_TIMEOUT_MS, "keyboard-opened menu sets the deadline", NULL);

	// an order given: target again, though the menu may still show this frame
	voice_core_dispatch(&vc, VOICE_STEP_ORDER, "engage enemy", 1.0f, 0.6f, Targets, NUM_TARGETS, Rows, VOICE_NUM_ORDERS, now + 200, &r);
	check((r.kind == VOICE_RESULT_ORDER) && (vc.step == VOICE_STEP_TARGET), "order returns to target", NULL);

	// a lapsed order step takes a new target while the menu is still up
	vc.step = VOICE_STEP_ORDER;
	vc.order_deadline = now;
	check(voice_core_step(&vc, 1, -1, now + 5) == VOICE_STEP_TARGET, "lapsed step takes a target", NULL);
	voice_core_dispatch(&vc, VOICE_STEP_TARGET, "alpha two", 1.0f, 0.6f, Targets, NUM_TARGETS, Rows, VOICE_NUM_ORDERS, now + 5, &r);
	check((r.kind == VOICE_RESULT_SHIP) && (voice_core_step(&vc, 1, -1, now + 6) == VOICE_STEP_ORDER), "new target after a lapse", NULL);

	// the menu holds an order (said first, or its key): send step, whatever came before
	vc.step = VOICE_STEP_ORDER;
	check(voice_core_step(&vc, 0, VOICE_ORDER_COVER_ME, now) == VOICE_STEP_SEND, "held order is send", NULL);
	check(vc.held_order == VOICE_ORDER_COVER_ME, "step takes the held order", NULL);
	check(voice_core_step(&vc, 0, -1, now + 1) == VOICE_STEP_TARGET, "held order gone is target", NULL);
	check(vc.held_order == -1, "step clears the held order", NULL);

	// the deadline survives the millisecond clock wrapping
	vc.step = VOICE_STEP_ORDER;
	vc.order_deadline = (int)0x80000005;
	check(voice_core_step(&vc, 1, -1, 0x7FFFFFFD) == VOICE_STEP_ORDER, "before a deadline past the wrap", NULL);
	check(voice_core_step(&vc, 1, -1, (int)0x80000005) == VOICE_STEP_TARGET, "at a deadline past the wrap", NULL);
}

static void test_parse()
{
	char text[VOICE_TEXT_LEN];
	char detail[128];
	float conf;
	int ok;

	ok = voice_parse_result("{\n  \"text\" : \"alpha two\"\n}", text, VOICE_TEXT_LEN, &conf);
	check(ok && !strcmp(text, "alpha two") && (conf == 1.0f), "parse text, no conf", text);

	ok = voice_parse_result(
		"{\n  \"result\" : [{\n      \"conf\" : 0.912345,\n      \"end\" : 1.2,\n      \"start\" : 0.6,\n      \"word\" : \"cover\"\n    }, {\n"
		"      \"conf\" : 0.45,\n      \"end\" : 1.5,\n      \"start\" : 1.2,\n      \"word\" : \"me\"\n    }],\n  \"text\" : \"cover me\"\n}",
		text, VOICE_TEXT_LEN, &conf);
	sprintf(detail, "text '%s' conf %f", text, conf);
	check(ok && !strcmp(text, "cover me") && (conf > 0.449f) && (conf < 0.451f), "parse text and the lowest conf", detail);

	ok = voice_parse_result("{\n  \"text\" : \"\"\n}", text, VOICE_TEXT_LEN, &conf);
	check(!ok && !text[0], "parse empty text", NULL);

	ok = voice_parse_result("", text, VOICE_TEXT_LEN, &conf);
	check(!ok && !text[0] && (conf == 1.0f), "parse empty json", NULL);

	ok = voice_parse_result(NULL, text, VOICE_TEXT_LEN, &conf);
	check(!ok && !text[0], "parse NULL json", NULL);

	ok = voice_parse_result("{\"text\":\"say \\\"hi\\\"\"}", text, VOICE_TEXT_LEN, &conf);
	check(ok && !strcmp(text, "say \"hi\""), "parse escaped quotes", text);

	ok = voice_parse_result("{\"text\":\"a very long phrase\"}", text, 7, &conf);
	check(ok && !strcmp(text, "a very"), "parse truncates", text);
}

static void test_engine()
{
	char dir[MAX_PATH];
	char json[VOICE_RESULT_LEN];
	short samples[160];
	int final;
	DWORD n;

	memset(samples, 0, sizeof(samples));

	// before open: off, and nothing runs
	check(voice_engine_state() == VOICE_ENGINE_OFF, "engine starts off", NULL);
	check(!voice_engine_stream_start("cover me\n"), "no stream when off", NULL);
	voice_engine_stream_feed(samples, 160);
	voice_engine_stream_end();
	voice_engine_stream_next("cover me\n");
	check(!voice_engine_stream_result(json, VOICE_RESULT_LEN, &final), "no result when off", NULL);

	// no model folder set
	check(!voice_engine_open("", ""), "open with no model fails", NULL);
	check(voice_engine_state() == VOICE_ENGINE_FAILED, "no model is failed", voice_engine_error());
	check(voice_engine_error()[0] != 0, "no model has an error", NULL);
	voice_engine_close();
	check(voice_engine_state() == VOICE_ENGINE_OFF, "close resets", NULL);

	// a model folder that does not exist
	check(!voice_engine_open("", "Z:\\no\\such\\voice\\model"), "open with a missing model fails", NULL);
	check(voice_engine_state() == VOICE_ENGINE_FAILED, "missing model is failed", voice_engine_error());
	voice_engine_close();

	// a model folder that exists (an empty temp folder), but no DLL in the folder given
	n = GetTempPathA(MAX_PATH - 32, dir);
	check((n > 0) && (n < MAX_PATH - 32), "temp path", NULL);
	if ((n > 0) && (n < MAX_PATH - 32)) {
		strcat(dir, "VoiceDispatchTest_empty");
		CreateDirectoryA(dir, NULL);
		check(!voice_engine_open(dir, dir), "open with no DLL fails", NULL);
		check(voice_engine_state() == VOICE_ENGINE_FAILED, "no DLL is failed", voice_engine_error());
		check(strstr(voice_engine_error(), "libvosk.dll") != NULL, "no DLL names the DLL", voice_engine_error());
		check(!voice_engine_stream_start("cover me\n"), "no stream when failed", NULL);
		voice_engine_stream_feed(samples, 160);
		voice_engine_stream_end();
		check(!voice_engine_stream_result(json, VOICE_RESULT_LEN, &final), "no result when failed", NULL);
		voice_engine_close();
		check(voice_engine_state() == VOICE_ENGINE_OFF, "close after a failed open", NULL);
		RemoveDirectoryA(dir);
	}

	// close twice is safe
	voice_engine_close();
	check(voice_engine_state() == VOICE_ENGINE_OFF, "close twice", NULL);
}

// a real libvosk.dll with an empty model folder: open succeeds, then the model load fails
static void test_engine_dll(const char *dll_dir)
{
	char dir[MAX_PATH];
	DWORD n;
	int i;

	n = GetTempPathA(MAX_PATH - 32, dir);
	if ((n == 0) || (n >= MAX_PATH - 32)) {
		check(0, "temp path", NULL);
		return;
	}
	strcat(dir, "VoiceDispatchTest_model");
	CreateDirectoryA(dir, NULL);

	check(voice_engine_open(dll_dir, dir) == 1, "open with the DLL", voice_engine_error());
	for (i = 0; (i < 200) && (voice_engine_state() == VOICE_ENGINE_LOADING); i++) {
		Sleep(50);
	}
	check(voice_engine_state() == VOICE_ENGINE_FAILED, "empty model is failed", voice_engine_error());
	check(strstr(voice_engine_error(), "could not load the model") != NULL, "empty model error", voice_engine_error());
	voice_engine_close();
	check(voice_engine_state() == VOICE_ENGINE_OFF, "close after a model failure", NULL);

	RemoveDirectoryA(dir);
}

// a real libvosk.dll and model: a stream of silence ends with one empty final result, and the
// engine then takes the next stream
static void test_engine_stream(const char *dll_dir, const char *model_dir)
{
	static short silence[VOICE_ENGINE_RATE / 60];	// one frame
	char json[VOICE_RESULT_LEN];
	char text[VOICE_TEXT_LEN];
	float conf;
	int i, j, ok, final, results;

	memset(silence, 0, sizeof(silence));

	check(voice_engine_open(dll_dir, model_dir) == 1, "open with the model", voice_engine_error());
	for (i = 0; (i < 600) && (voice_engine_state() == VOICE_ENGINE_LOADING); i++) {
		Sleep(50);
	}
	check(voice_engine_state() == VOICE_ENGINE_READY, "model loads", voice_engine_error());
	if (voice_engine_state() != VOICE_ENGINE_READY) {
		voice_engine_close();
		return;
	}

	for (j = 0; j < 2; j++) {
		check(voice_engine_stream_start("alpha two\nbeta wing\nall fighters\n") == 1, "stream starts", NULL);
		check(!voice_engine_stream_start("cover me\n"), "one stream at a time", NULL);
		for (i = 0; i < 120; i++) {
			voice_engine_stream_feed(silence, VOICE_ENGINE_RATE / 60);
		}
		voice_engine_stream_end();

		ok = 0;
		results = 0;
		final = 0;
		for (i = 0; (i < 200) && !final; i++) {
			if (voice_engine_stream_result(json, VOICE_RESULT_LEN, &final)) {
				results++;
				ok = !voice_parse_result(json, text, VOICE_TEXT_LEN, &conf);
				if (!final) {
					voice_engine_stream_next("cover me\n");
				}
			} else {
				Sleep(10);
			}
		}
		check(final, "silence gives a final result", json);
		check(ok, "silence is heard as nothing", json);
		check(results == 1, "silence gives one result", NULL);
	}

	voice_engine_close();
	check(voice_engine_state() == VOICE_ENGINE_OFF, "close after streams", NULL);
}

int main(int argc, char **argv)
{
	build_rows();

	test_normalize();
	test_phrases();
	test_targets();
	test_orders();
	test_send();
	test_confidence();
	test_steps();
	test_parse();
	test_engine();
	if (argc > 1) {
		test_engine_dll(argv[1]);
	}
	if (argc > 2) {
		test_engine_stream(argv[1], argv[2]);
	}

	printf("%s: %d checks, %d failed\n", Failures ? "FAIL" : "PASS", Checks, Failures);
	return Failures ? 1 : 0;
}
