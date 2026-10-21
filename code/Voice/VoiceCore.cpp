/*
 * Voice control core.  See VoiceCore.h and docs/VOICE_CONTROL.md.
 *
 * Phrases follow the 2009-2014 SAPI implementation (voicerecognition.cpp, phrases.xml):
 * ship and wing names, "all fighters", the menu's order text, and three missile functions.
 * Changes from it: an order matches a live menu row (so the menu's greyed-out checks apply),
 * an unmatched phrase does nothing, and targets come from the mission instead of a fixed list.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "VoiceCore.h"

// phrases for each order; the first is the menu's own text.  "attack" is the old grammar's
// word for engage enemy.
static const char *Voice_order_phrases[VOICE_NUM_ORDERS][3] = {
	{ "destroy my target", NULL },
	{ "disable my target", NULL },
	{ "disarm my target", NULL },
	{ "destroy subsystem", NULL },
	{ "protect my target", NULL },
	{ "ignore my target", NULL },
	{ "form on my wing", NULL },
	{ "cover me", NULL },
	{ "engage enemy", "attack", NULL },
	{ "capture my target", NULL },
	{ "rearm me", NULL },
	{ "abort rearm", NULL },
	{ "depart", NULL },
};

static const char *Voice_func_phrases[VOICE_NUM_FUNCS] = {
	"single missile",
	"dual missile",
	"cycle missile",
};

#define VOICE_ALL_FIGHTERS_PHRASE	"all fighters"
#define VOICE_WING_SUFFIX				" wing"

static const char *Voice_ones[20] = {
	"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
	"eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"
};

static const char *Voice_tens[10] = {
	"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
};

// append s to out (length *len, size max), with a space before it unless out is empty
static void voice_append_word(char *out, int *len, int max, const char *s)
{
	int n = (int)strlen(s);

	if (*len > 0) {
		if (*len + 1 >= max) {
			return;
		}
		out[(*len)++] = ' ';
	}
	if (*len + n >= max) {
		return;
	}
	memcpy(out + *len, s, n);
	*len += n;
	out[*len] = 0;
}

static void voice_append_number(char *out, int *len, int max, const char *digits, int count)
{
	int i, val;

	if (count <= 2) {
		val = 0;
		for (i = 0; i < count; i++) {
			val = (val * 10) + (digits[i] - '0');
		}
		if ((val < 20) && !((count == 2) && (digits[0] == '0'))) {
			voice_append_word(out, len, max, Voice_ones[val]);
			return;
		}
		if (count == 2 && digits[0] != '0') {
			voice_append_word(out, len, max, Voice_tens[val / 10]);
			if (val % 10) {
				voice_append_word(out, len, max, Voice_ones[val % 10]);
			}
			return;
		}
	}

	// longer numbers, or a leading zero: digit by digit
	for (i = 0; i < count; i++) {
		voice_append_word(out, len, max, Voice_ones[digits[i] - '0']);
	}
}

char *voice_normalize(const char *in, char *out, int max)
{
	int len = 0;
	char word[VOICE_TEXT_LEN];
	int wlen;

	if (max <= 0) {
		return out;
	}
	out[0] = 0;

	while (*in) {
		unsigned char c = (unsigned char)*in;

		if ((c >= '0') && (c <= '9')) {
			const char *start = in;

			while ((*in >= '0') && (*in <= '9')) {
				in++;
			}
			voice_append_number(out, &len, max, start, (int)(in - start));
			continue;
		}

		// a word: ASCII letters lower-cased, Latin-1 letters kept as they are
		if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c >= 0xC0) || (c == '\'')) {
			wlen = 0;
			while (*in) {
				c = (unsigned char)*in;
				if ((c >= 'A') && (c <= 'Z')) {
					c = (unsigned char)(c - 'A' + 'a');
				} else if (!(((c >= 'a') && (c <= 'z')) || (c >= 0xC0) || (c == '\''))) {
					break;
				}
				if (wlen < VOICE_TEXT_LEN - 1) {
					word[wlen++] = (char)c;
				}
				in++;
			}
			word[wlen] = 0;
			voice_append_word(out, &len, max, word);
			continue;
		}

		// anything else separates words
		in++;
	}

	return out;
}

// add phrase to a newline-separated list unless it is already there
static int voice_add_phrase(char *out, int *len, int max, const char *phrase)
{
	const char *p;
	int n = (int)strlen(phrase);

	if (n == 0) {
		return 0;
	}

	p = out;
	while (*p) {
		const char *e = strchr(p, '\n');
		int plen = e ? (int)(e - p) : (int)strlen(p);

		if ((plen == n) && !strncmp(p, phrase, n)) {
			return 0;
		}
		p += plen;
		if (*p == '\n') {
			p++;
		}
	}

	if (*len + n + 1 >= max) {
		return 0;
	}
	memcpy(out + *len, phrase, n);
	*len += n;
	out[(*len)++] = '\n';
	out[*len] = 0;
	return 1;
}

int voice_target_phrases(const voice_target *targets, int count, char *out, int max)
{
	int i, len = 0, num = 0;
	char name[VOICE_TEXT_LEN];
	char wing[VOICE_TEXT_LEN + 8];

	if (max <= 0) {
		return 0;
	}
	out[0] = 0;

	for (i = 0; i < count; i++) {
		voice_normalize(targets[i].name, name, VOICE_TEXT_LEN);
		num += voice_add_phrase(out, &len, max, name);
		if (targets[i].kind == VOICE_TARGET_WING) {
			sprintf(wing, "%s%s", name, VOICE_WING_SUFFIX);
			num += voice_add_phrase(out, &len, max, wing);
		}
	}
	num += voice_add_phrase(out, &len, max, VOICE_ALL_FIGHTERS_PHRASE);
	for (i = 0; i < VOICE_NUM_FUNCS; i++) {
		num += voice_add_phrase(out, &len, max, Voice_func_phrases[i]);
	}
	return num;
}

int voice_order_phrases(const voice_row *rows, int count, char *out, int max)
{
	int i, j, len = 0, num = 0;
	char text[VOICE_TEXT_LEN];

	if (max <= 0) {
		return 0;
	}
	out[0] = 0;

	// greyed-out rows too, so they are heard and refused instead of heard as another order
	for (i = 0; i < count; i++) {
		voice_normalize(rows[i].text, text, VOICE_TEXT_LEN);
		num += voice_add_phrase(out, &len, max, text);
		if ((rows[i].order >= 0) && (rows[i].order < VOICE_NUM_ORDERS)) {
			for (j = 0; Voice_order_phrases[rows[i].order][j]; j++) {
				num += voice_add_phrase(out, &len, max, Voice_order_phrases[rows[i].order][j]);
			}
		}
	}
	return num;
}

int voice_match_target(const char *heard, const voice_target *targets, int count, voice_result *r)
{
	int i;
	char name[VOICE_TEXT_LEN];
	char wing[VOICE_TEXT_LEN + 8];

	r->kind = VOICE_RESULT_NONE;
	r->index = -1;
	r->order = -1;

	if (!heard[0]) {
		return r->kind;
	}

	if (!strcmp(heard, VOICE_ALL_FIGHTERS_PHRASE)) {
		r->kind = VOICE_RESULT_ALL_FIGHTERS;
		return r->kind;
	}

	for (i = 0; i < VOICE_NUM_FUNCS; i++) {
		if (!strcmp(heard, Voice_func_phrases[i])) {
			r->kind = VOICE_RESULT_FUNCTION;
			r->index = i;
			return r->kind;
		}
	}

	// ships first: a ship's full name wins over a wing of the same name
	for (i = 0; i < count; i++) {
		if (targets[i].kind != VOICE_TARGET_SHIP) {
			continue;
		}
		voice_normalize(targets[i].name, name, VOICE_TEXT_LEN);
		if (!strcmp(heard, name)) {
			r->kind = VOICE_RESULT_SHIP;
			r->index = targets[i].index;
			return r->kind;
		}
	}

	for (i = 0; i < count; i++) {
		if (targets[i].kind != VOICE_TARGET_WING) {
			continue;
		}
		voice_normalize(targets[i].name, name, VOICE_TEXT_LEN);
		sprintf(wing, "%s%s", name, VOICE_WING_SUFFIX);
		if (!strcmp(heard, name) || !strcmp(heard, wing)) {
			r->kind = VOICE_RESULT_WING;
			r->index = targets[i].index;
			return r->kind;
		}
	}

	return r->kind;
}

static int voice_row_result(const voice_row *rows, int row, voice_result *r)
{
	r->kind = rows[row].active ? VOICE_RESULT_ORDER : VOICE_RESULT_REFUSED;
	r->index = row;
	r->order = rows[row].order;
	return r->kind;
}

int voice_match_order(const char *heard, const voice_row *rows, int count, voice_result *r)
{
	int i, j;
	char text[VOICE_TEXT_LEN];

	r->kind = VOICE_RESULT_NONE;
	r->index = -1;
	r->order = -1;

	if (!heard[0]) {
		return r->kind;
	}

	// the row's own text
	for (i = 0; i < count; i++) {
		voice_normalize(rows[i].text, text, VOICE_TEXT_LEN);
		if (!strcmp(heard, text)) {
			return voice_row_result(rows, i, r);
		}
	}

	// another phrase for a row's order
	for (i = 0; i < count; i++) {
		if ((rows[i].order < 0) || (rows[i].order >= VOICE_NUM_ORDERS)) {
			continue;
		}
		for (j = 0; Voice_order_phrases[rows[i].order][j]; j++) {
			if (!strcmp(heard, Voice_order_phrases[rows[i].order][j])) {
				return voice_row_result(rows, i, r);
			}
		}
	}

	return r->kind;
}

// find "key" in json and return a pointer just past the following colon, or NULL
static const char *voice_json_value(const char *json, const char *key)
{
	char pattern[32];
	const char *p;

	sprintf(pattern, "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p) {
		return NULL;
	}
	p += strlen(pattern);
	while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')) {
		p++;
	}
	if (*p != ':') {
		return NULL;
	}
	return p + 1;
}

int voice_parse_result(const char *json, char *text, int max, float *min_conf)
{
	const char *p;
	int len = 0;

	if (max <= 0) {
		return 0;
	}
	text[0] = 0;
	*min_conf = 1.0f;

	if (!json) {
		return 0;
	}

	p = voice_json_value(json, "text");
	if (p) {
		while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')) {
			p++;
		}
		if (*p == '"') {
			p++;
			while (*p && (*p != '"') && (len < max - 1)) {
				if ((*p == '\\') && p[1]) {
					p++;
				}
				text[len++] = *p++;
			}
			text[len] = 0;
		}
	}

	// every word's confidence, when the recogniser was asked for words
	p = json;
	while ((p = voice_json_value(p, "conf")) != NULL) {
		float c = (float)atof(p);

		if (c < *min_conf) {
			*min_conf = c;
		}
	}

	return text[0] != 0;
}

void voice_core_init(voice_core *vc)
{
	vc->step = VOICE_STEP_TARGET;
	vc->order_deadline = 0;
}

int voice_core_step(voice_core *vc, int menu_in_command, int now_ms)
{
	if (!menu_in_command) {
		vc->step = VOICE_STEP_TARGET;
		return VOICE_STEP_TARGET;
	}

	// the menu was opened at a target by the keyboard
	if (vc->step != VOICE_STEP_ORDER) {
		vc->step = VOICE_STEP_ORDER;
		vc->order_deadline = now_ms + VOICE_ORDER_TIMEOUT_MS;
	}

	// lapsed: a new target can be said while the menu is still up
	if ((now_ms - vc->order_deadline) >= 0) {
		return VOICE_STEP_TARGET;
	}

	return VOICE_STEP_ORDER;
}

int voice_core_dispatch(voice_core *vc, int step, const char *heard, float conf, float min_conf,
		const voice_target *targets, int num_targets, const voice_row *rows, int num_rows, int now_ms, voice_result *r)
{
	char text[VOICE_TEXT_LEN];

	r->kind = VOICE_RESULT_NONE;
	r->index = -1;
	r->order = -1;

	if (conf < min_conf) {
		return r->kind;
	}

	voice_normalize(heard, text, VOICE_TEXT_LEN);

	if (step == VOICE_STEP_ORDER) {
		voice_match_order(text, rows, num_rows, r);
	} else {
		voice_match_target(text, targets, num_targets, r);
	}

	switch (r->kind) {
	case VOICE_RESULT_SHIP:
	case VOICE_RESULT_WING:
	case VOICE_RESULT_ALL_FIGHTERS:
		vc->step = VOICE_STEP_ORDER;
		vc->order_deadline = now_ms + VOICE_ORDER_TIMEOUT_MS;
		break;

	case VOICE_RESULT_ORDER:
	case VOICE_RESULT_FUNCTION:
		vc->step = VOICE_STEP_TARGET;
		break;

	default:
		// refused or unmatched: the step stays, so another order can be said
		break;
	}

	return r->kind;
}

const char *voice_order_name(int order)
{
	if ((order < 0) || (order >= VOICE_NUM_ORDERS)) {
		return "?";
	}
	return Voice_order_phrases[order][0];
}

const char *voice_func_name(int func)
{
	if ((func < 0) || (func >= VOICE_NUM_FUNCS)) {
		return "?";
	}
	return Voice_func_phrases[func];
}
