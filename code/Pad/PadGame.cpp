// PadGame.cpp
// Control pad glue for FS2. See PadGame.h, and docs/PAD_CONTROL.md for the layout.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "PadGame.h"
#include "PadCore.h"
#include "PadXInput.h"
#include "PadMenuGame.h"

#include "Physics.h"
#include "Player.h"
#include "ControlsConfig.h"
#include "KeyControl.h"
#include "Timer.h"
#include "Replay.h"
#include "Autotest.h"
#include "Ship.h"
#include "ai.h"
#include "AiGoals.h"
#include "HUDtarget.h"
#include "Object.h"

extern int joy_num_sticks;					// Joy.cpp: 1 if the legacy joystick code is reading a device

#define PAD_SETTINGS_FILE		"pad.cfg"		// in the game folder; optional
#define PAD_ACTION_PAUSE		1000				// not a Control_config id
#define PAD_ACTION_SLOW		1001				// held: lower the throttle
#define PAD_ACTION_MENU_OPEN		1002			// squad message menu, driven by the pad
#define PAD_ACTION_MENU_CLOSE		1003
#define PAD_ACTION_MENU_UP			1004
#define PAD_ACTION_MENU_DOWN		1005
#define PAD_ACTION_MENU_SELECT	1006
#define PAD_ACTION_MENU_BACK		1007
#define PAD_ACTION_TALK			1008			// held: voice push-to-talk
#define PAD_ACTION_TARGET			1009			// South: ship in reticle, then its subsystem, then no subsystem
#define PAD_ACTION_SECONDARY_STEP	1010		// RB + North: one step of bank 1 single, double, bank 2 single, double
#define PAD_ACTION_ETS_SHOW		1011			// held: draw the selected system on the ETS gauge
#define PAD_ACTION_ETS_PREV		1012			// RB + D-pad: the energy system to change, and change it
#define PAD_ACTION_ETS_NEXT		1013
#define PAD_ACTION_ETS_UP			1014
#define PAD_ACTION_ETS_DOWN		1015
#define PAD_ACTION_TARGET_NEAREST	1016		// East: the nearest hostile
#define PAD_ACTION_TARGET_NEXT	1017			// East again within PAD_REPRESS_MS of its release: the next hostile

// HUDsquadmsg.cpp
extern void hud_squadmsg_pad_open();
extern void hud_squadmsg_pad_close();
extern void hud_squadmsg_pad_move(int dir);
extern void hud_squadmsg_pad_select();
extern void hud_squadmsg_pad_back();
extern int hud_squadmsg_voice_in_command();
extern int hud_squadmsg_voice_row(int row, char **text, int *order, int *active);

// HUDtarget.cpp
extern object *hud_pad_reticle_top();
extern void hud_pad_reticle_fresh();
extern ship_subsys *hud_pad_subsystem_in_reticle();
extern void hud_pad_target_hostile(int next);

// Flight layout (#151). Everything else stays on the keyboard for now. Layers keep a press to the
// layer it went down in (PadCore.h), so the base action of a button pressed with RB never happens.
static const pad_binding Pad_game_bindings[] = {
	// RB: axes, and so the left trigger, carry on under it
	{ PAD_RSHOULDER,			PAD_G_HOLD,					PAD_LAYER_ANY,		PAD_ACTION_ETS_SHOW },
	{ PAD_INPUT_LTRIGGER,	PAD_G_DOUBLE_TAP_HOLD,	PAD_LAYER_ANY,		AFTERBURNER },

	{ PAD_INPUT_RTRIGGER,	PAD_G_HOLD,					PAD_LAYER_BASE,	FIRE_PRIMARY },
	{ PAD_NORTH,				PAD_G_HOLD,					PAD_LAYER_BASE,	FIRE_SECONDARY },
	{ PAD_DPAD_UP,				PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_TOP },
	{ PAD_DPAD_DOWN,			PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_BOTTOM },
	{ PAD_DPAD_LEFT,			PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_LEFT },
	{ PAD_DPAD_RIGHT,			PAD_G_PRESS,				PAD_LAYER_BASE,	SHIELD_XFER_RIGHT },
	{ PAD_LSHOULDER,			PAD_G_HOLD_PAST_TAP,		PAD_LAYER_BASE,	PAD_CORE_ACTION_SPEED_MODE },
	{ PAD_LSHOULDER,			PAD_G_DOUBLE_TAP,			PAD_LAYER_BASE,	ZERO_THROTTLE },
	{ PAD_RSTICK,				PAD_G_PRESS,				PAD_LAYER_BASE,	LAUNCH_COUNTERMEASURE,					PAD_CHORD_WITHOUT(PAD_LSHOULDER) },
	{ PAD_RSTICK,				PAD_G_PRESS,				PAD_LAYER_BASE,	TOGGLE_AUTO_MATCH_TARGET_SPEED,		PAD_CHORD_WITH(PAD_LSHOULDER) },
	{ PAD_SOUTH,				PAD_G_PRESS,				PAD_LAYER_BASE,	PAD_ACTION_TARGET },
	{ PAD_EAST,					PAD_G_PRESS_FRESH,		PAD_LAYER_BASE,	PAD_ACTION_TARGET_NEAREST },
	{ PAD_EAST,					PAD_G_PRESS_AGAIN,		PAD_LAYER_BASE,	PAD_ACTION_TARGET_NEXT },
	{ PAD_WEST,					PAD_G_HOLD,					PAD_LAYER_BASE,	PAD_ACTION_TALK },	// voice push-to-talk, also while the squad menu is up
	{ PAD_START,				PAD_G_PRESS,				PAD_LAYER_BASE,	PAD_ACTION_PAUSE },

	{ PAD_NORTH,				PAD_G_PRESS,				PAD_LAYER_MOD,		PAD_ACTION_SECONDARY_STEP },
	{ PAD_INPUT_RTRIGGER,	PAD_G_PRESS,				PAD_LAYER_MOD,		CYCLE_NEXT_PRIMARY },
	{ PAD_LSHOULDER,			PAD_G_LONG_HOLD,			PAD_LAYER_MOD,		END_MISSION },
	{ PAD_DPAD_LEFT,			PAD_G_PRESS,				PAD_LAYER_MOD,		PAD_ACTION_ETS_PREV },
	{ PAD_DPAD_RIGHT,			PAD_G_PRESS,				PAD_LAYER_MOD,		PAD_ACTION_ETS_NEXT },
	{ PAD_DPAD_UP,				PAD_G_PRESS,				PAD_LAYER_MOD,		PAD_ACTION_ETS_UP },
	{ PAD_DPAD_DOWN,			PAD_G_PRESS,				PAD_LAYER_MOD,		PAD_ACTION_ETS_DOWN },
};

static const pad_modifier Pad_game_modifiers[] = {
	{ PAD_RSHOULDER,	PAD_LAYER_MOD },
};

static int				Pad_game_inited = 0;
static pad_output		Pad_game_out;

static void pad_game_init()
{
	pad_settings s;
	FILE *fp;

	Pad_game_inited = 1;

	pad_core_default_settings(&s);

	fp = fopen(PAD_SETTINGS_FILE, "rb");
	if (fp) {
		static char text[8192];
		int len = fread(text, 1, sizeof(text) - 1, fp);
		fclose(fp);
		text[len > 0 ? len : 0] = 0;
		mprintf(("Pad: read %d settings from %s\n", pad_core_parse_settings(&s, text), PAD_SETTINGS_FILE));
	}

	pad_core_set_settings(&s);
	pad_core_set_bindings(Pad_game_bindings, sizeof(Pad_game_bindings) / sizeof(Pad_game_bindings[0]));
	pad_core_set_modifiers(Pad_game_modifiers, sizeof(Pad_game_modifiers) / sizeof(Pad_game_modifiers[0]));
}

// The recorder keeps the pad's output, not its raw state, so a replay does not depend on
// pad.cfg or the core. Floats are kept as fixed point; live play uses the same rounded values.
// Slots 8 to 11 held the old pad tree: always these values now, ignored on read, so recordings stay valid.
#define PAD_REPLAY_HEADER		12
#define PAD_REPLAY_MAX_INTS	(PAD_REPLAY_HEADER + 2 * PAD_MAX_OUT)
#define PAD_REPLAY_SCALE		10000.0f

static int pad_game_fixed(float v)
{
	return (int) floor(v * PAD_REPLAY_SCALE + 0.5f);
}

static int pad_game_pack(const pad_output *out, int *vals)
{
	int i, n;

	if (!out->connected) {
		vals[0] = 0;
		return 1;
	}

	vals[0] = 1;
	vals[1] = out->num_fired;
	vals[2] = out->num_held;
	vals[3] = pad_game_fixed(out->yaw);
	vals[4] = pad_game_fixed(out->pitch);
	vals[5] = pad_game_fixed(out->roll);
	vals[6] = pad_game_fixed(out->throttle_rate);
	vals[7] = pad_game_fixed(out->throttle_max_rate);
	vals[8] = -1;
	vals[9] = -1;
	vals[10] = 0;
	vals[11] = 0;

	n = PAD_REPLAY_HEADER;
	for (i = 0; i < out->num_fired; i++) {
		vals[n++] = out->fired[i];
	}
	for (i = 0; i < out->num_held; i++) {
		vals[n++] = out->held[i];
	}
	return n;
}

static void pad_game_unpack(const int *vals, int n, pad_output *out)
{
	int i, p;

	memset(out, 0, sizeof(*out));

	if ((n < PAD_REPLAY_HEADER) || !vals[0]) {
		return;
	}
	if ((vals[1] < 0) || (vals[1] > PAD_MAX_OUT) || (vals[2] < 0) || (vals[2] > PAD_MAX_OUT) || (n != PAD_REPLAY_HEADER + vals[1] + vals[2])) {
		return;
	}

	out->connected = 1;
	out->num_fired = vals[1];
	out->num_held = vals[2];
	out->yaw = vals[3] / PAD_REPLAY_SCALE;
	out->pitch = vals[4] / PAD_REPLAY_SCALE;
	out->roll = vals[5] / PAD_REPLAY_SCALE;
	out->throttle_rate = vals[6] / PAD_REPLAY_SCALE;
	out->throttle_max_rate = vals[7] / PAD_REPLAY_SCALE;

	p = PAD_REPLAY_HEADER;
	for (i = 0; i < out->num_fired; i++) {
		out->fired[i] = vals[p++];
	}
	for (i = 0; i < out->num_held; i++) {
		out->held[i] = vals[p++];
	}
}

// The pad's squad menu test (#119).  A test mission whose notes have PAD_TEST_NOTE gets its pad output from this
// script instead of the pad (AutotestSquadPad); a replay reads it back from the recording.  When a step's mission
// time comes, it first checks what the steps before it left (the same in the recording and in each replay), then
// fires its pad action.  The pad opens the menu and presses rows through hud_squadmsg_menu_open/_press_row.
#define PAD_TEST_NOTE			"autotest: squad pad"
#define PAD_TEST_TALK_ON		-2		// start holding push-to-talk (West)
#define PAD_TEST_TALK_OFF		-3

enum { PT_CLOSED, PT_TOP, PT_ORDERS, PT_FORM_ROW, PT_FORMED, PT_TALK, PT_RELEASED };

struct pad_test_step { float time; int action; int expect; char *what; };
static pad_test_step Pad_test_steps[] = {
	{ 10.0f, PAD_ACTION_MENU_OPEN,	PT_CLOSED,		"menu closed before the test" },
	{ 11.0f, PAD_ACTION_MENU_DOWN,	PT_TOP,			"pad open: the top menu is up" },
	{ 11.5f, PAD_ACTION_MENU_SELECT,	PT_TOP,			"down: still the top menu" },
	{ 12.5f, PAD_ACTION_MENU_SELECT,	PT_TOP,			"row 2 (Wings): the wing list is up" },
	{ 13.5f, PAD_ACTION_MENU_DOWN,	PT_FORM_ROW,	"row 1 (Beta): its orders are up, row 7 is Form on my wing" },
	{ 13.7f, PAD_ACTION_MENU_DOWN,	PT_ORDERS,		"down 1: still the orders" },
	{ 13.9f, PAD_ACTION_MENU_DOWN,	PT_ORDERS,		"down 2: still the orders" },
	{ 14.1f, PAD_ACTION_MENU_DOWN,	PT_ORDERS,		"down 3: still the orders" },
	{ 14.3f, PAD_ACTION_MENU_DOWN,	PT_ORDERS,		"down 4: still the orders" },
	{ 14.5f, PAD_ACTION_MENU_DOWN,	PT_ORDERS,		"down 5: still the orders" },
	{ 15.0f, PAD_ACTION_MENU_SELECT,	PT_ORDERS,		"down 6: still the orders" },
	{ 20.0f, PAD_ACTION_MENU_OPEN,	PT_FORMED,		"row 7 (Form on my wing): sent, the menu closed, Beta 1 forms on the player" },
	{ 21.0f, PAD_ACTION_MENU_BACK,	PT_TOP,			"pad open: the top menu is up" },
	{ 22.0f, PAD_ACTION_MENU_CLOSE,	PT_TOP,			"back on the top menu: it stays up" },
	{ 26.0f, PAD_ACTION_MENU_OPEN,	PT_CLOSED,		"pad close: the menu closed" },
	{ 27.0f, PAD_ACTION_MENU_DOWN,	PT_TOP,			"pad open: the top menu is up" },
	{ 27.5f, PAD_ACTION_MENU_DOWN,	PT_TOP,			"down: still the top menu" },
	{ 28.0f, PAD_ACTION_MENU_SELECT,	PT_TOP,			"down: still the top menu" },
	{ 29.0f, PAD_ACTION_MENU_BACK,	PT_ORDERS,		"row 3 (All fighters): the orders are up" },
	{ 30.0f, PAD_ACTION_MENU_CLOSE,	PT_TOP,			"back from All fighters: the top menu" },
	{ 32.0f, PAD_TEST_TALK_ON,		PT_CLOSED,		"pad close: the menu closed" },
	{ 33.0f, -1,						PT_TALK,			"West held: push-to-talk held, the menu still closed" },
	{ 34.0f, PAD_TEST_TALK_OFF,		PT_TALK,			"West still held" },
	{ 35.0f, -1,						PT_RELEASED,	"West released: push-to-talk off, the menu still closed" },
};
#define PAD_TEST_STEPS	((int)(sizeof(Pad_test_steps) / sizeof(Pad_test_steps[0])))

static int Pad_test_next = 0;
static int Pad_test_talk = 0;

static int pad_test_beta1_formed()
{
	int shipnum;
	ai_info *aip;

	// The order lands as an AI_GOAL_FORM_ON_WING goal, but the goal code clears every goal
	// once it acts on it (ai_form_on_wing), so what lasts is the formation on the player.
	shipnum = ship_name_lookup("Beta 1");
	if ((shipnum < 0) || (Ships[shipnum].ai_index < 0) || (Player_obj == NULL)) {
		return 0;
	}
	aip = &Ai_info[Ships[shipnum].ai_index];
	return (aip->ai_flags & AIF_FORMATION_OBJECT) && (aip->goal_objnum == OBJ_INDEX(Player_obj));
}

static int pad_test_expect(int expect)
{
	int open = (Player->flags & PLAYER_FLAGS_MSG_MODE) ? 1 : 0;
	int order, active;
	char *text;

	switch (expect) {
	case PT_CLOSED:		return !open;
	case PT_TOP:			return open && !hud_squadmsg_voice_in_command();
	case PT_ORDERS:		return hud_squadmsg_voice_in_command();
	case PT_FORM_ROW:		return hud_squadmsg_voice_row(6, &text, &order, &active) && active && !stricmp(text, "Form on my wing");
	case PT_FORMED:		return !open && pad_test_beta1_formed();
	case PT_TALK:			return !open && pad_voice_talk_held();
	case PT_RELEASED:		return !open && !pad_voice_talk_held();
	}
	return 0;
}

// live: the output to fill while recording (or in a plain run); NULL in a replay, which only checks
static void pad_test_frame(pad_output *live)
{
	pad_test_step *s;

	if (!autotest_mission_note(PAD_TEST_NOTE)) {
		Pad_test_next = 0;
		Pad_test_talk = 0;
		return;
	}

	if (live) {
		memset(live, 0, sizeof(*live));
		live->connected = 1;
	}

	if ((Pad_test_next < PAD_TEST_STEPS) && (autotest_mission_seconds() >= Pad_test_steps[Pad_test_next].time)) {
		s = &Pad_test_steps[Pad_test_next++];
		autotest_check(pad_test_expect(s->expect), s->what);
		if (s->action == PAD_TEST_TALK_ON) {
			Pad_test_talk = 1;
		} else if (s->action == PAD_TEST_TALK_OFF) {
			Pad_test_talk = 0;
		} else if (live && (s->action >= 0)) {
			live->fired[live->num_fired++] = s->action;
		}
	}

	if (live && Pad_test_talk) {
		live->held[live->num_held++] = PAD_ACTION_TALK;
	}
}

void pad_game_settings_read()
{
	if (!Pad_game_inited) {
		pad_game_init();
	}
}

// from_frame: pad_frame's update, the one the test script drives (pad_pause_pressed updates too, while paused)
static void pad_game_update(int from_frame)
{
	int vals[PAD_REPLAY_MAX_INTS];
	int n = 0;

	if (!Pad_game_inited) {
		pad_game_init();
	}

	if (replay_should_read()) {
		if (from_frame) {
			pad_test_frame(NULL);
		}
	} else {
		pad_state st;
		pad_output live;

		// the clock read in here is covered by the pad's own entry
		replay_push();

		pad_menu_read_for_flight(&st);
		pad_core_update(&st, timer_get_milliseconds(), &live);

		replay_pop();
		if (from_frame) {
			pad_test_frame(&live);
		}
		n = pad_game_pack(&live, vals);
	}

	n = replay_input_device(vals, n, PAD_REPLAY_MAX_INTS);
	pad_game_unpack(vals, n, &Pad_game_out);
}

// South: the game's own controls do the work. Reticle targeting changes the target, so what South means is
// worked out first, from the object a press would take. Repeat presses cycling through what is under the
// reticle is the game's way; South means the top one, or it could never get to the subsystem.
static int pad_game_south_control()
{
	object *top;
	ship_subsys *subsys;

	if ((Player_obj == NULL) || (Player_obj->type != OBJ_SHIP) || (Player_ship == NULL) || (Player_ai == NULL)) {
		return -1;
	}
	if (!hud_sensors_ok(Player_ship, 0)) {
		return TARGET_SHIP_IN_RETICLE;		// the game refuses it, with its own message
	}

	top = hud_pad_reticle_top();
	if ((top == NULL) || (OBJ_INDEX(top) != Player_ai->target_objnum)) {
		hud_pad_reticle_fresh();
		return TARGET_SHIP_IN_RETICLE;
	}

	subsys = hud_pad_subsystem_in_reticle();
	if ((subsys != NULL) && (subsys == Player_ai->targeted_subsys)) {
		return STOP_TARGETING_SUBSYSTEM;
	}
	return TARGET_SUBOBJECT_IN_RETICLE;
}

// RB + North: bank 1 single, bank 1 double, bank 2 single, bank 2 double, back to the start. The game's
// controls change the bank and the link, and refuse with their own messages. With no banks, CYCLE_SECONDARY
// says so; with one bank it only alternates single and double.
static void pad_game_secondary_step()
{
	int banks;

	if ((Player_obj == NULL) || (Player_obj->type != OBJ_SHIP) || (Player_ship == NULL)) {
		return;
	}

	banks = Player_ship->weapons.num_secondary_banks;
	if (banks <= 0) {
		button_info_set(&Player->bi, CYCLE_SECONDARY);
		return;
	}
	if ((Player_ship->flags & SF_SECONDARY_DUAL_FIRE) && (banks > 1)) {
		button_info_set(&Player->bi, CYCLE_SECONDARY);
	}
	button_info_set(&Player->bi, CYCLE_NUM_MISSLES);
}

// The energy system RB + D-pad changes. Only the pad's recorded actions change it, so it replays,
// and only those controls read it; the gauge just draws it.
enum { PAD_ETS_SHIELDS, PAD_ETS_WEAPONS, PAD_ETS_ENGINES, PAD_ETS_NUM };

static int	Pad_ets_selected = PAD_ETS_SHIELDS;
static fix	Pad_ets_last_time = 0;

static int pad_ets_no_shields()
{
	return (Player_obj != NULL) && (Player_obj->flags & OF_NO_SHIELDS);
}

// a ship may lose its shields after they were selected
static int pad_ets_system()
{
	return ((Pad_ets_selected == PAD_ETS_SHIELDS) && pad_ets_no_shields()) ? PAD_ETS_WEAPONS : Pad_ets_selected;
}

static void pad_ets_step(int dir)
{
	int sel = pad_ets_system();

	do {
		sel = (sel + dir + PAD_ETS_NUM) % PAD_ETS_NUM;
	} while ((sel == PAD_ETS_SHIELDS) && pad_ets_no_shields());
	Pad_ets_selected = sel;
}

static void pad_ets_change(int up)
{
	switch (pad_ets_system()) {
	case PAD_ETS_SHIELDS:	button_info_set(&Player->bi, up ? INCREASE_SHIELD : DECREASE_SHIELD);	break;
	case PAD_ETS_WEAPONS:	button_info_set(&Player->bi, up ? INCREASE_WEAPON : DECREASE_WEAPON);	break;
	case PAD_ETS_ENGINES:	button_info_set(&Player->bi, up ? INCREASE_ENGINE : DECREASE_ENGINE);	break;
	}
}

int pad_ets_highlight(int gauge)
{
	static const int system[3] = { PAD_ETS_WEAPONS, PAD_ETS_SHIELDS, PAD_ETS_ENGINES };

	if (!pad_output_is_held(&Pad_game_out, PAD_ACTION_ETS_SHOW) || (gauge < 0) || (gauge > 2)) {
		return 0;
	}
	return system[gauge] == pad_ets_system();
}

static int Pad_game_south_presses = 0;
static int Pad_game_east_presses[2] = { 0, 0 };

// East: the nearest hostile (next 0), or the next one after the current target (next 1). As the game's targeting
// keys: a damaged sensor refuses it with the game's message. Not while dead.
static void pad_game_target_hostile(int next)
{
	if ((Player_obj == NULL) || (Player_obj->type != OBJ_SHIP) || (Player_ship == NULL) || (Player_ai == NULL) || (Game_mode & GM_DEAD)) {
		return;
	}
	Pad_game_east_presses[next]++;
	control_used(TARGET_NEXT_CLOSEST_HOSTILE);
	if (hud_sensors_ok(Player_ship)) {
		hud_pad_target_hostile(next);
	}
}

int pad_game_east_count(int next)
{
	return Pad_game_east_presses[next];
}

int pad_game_south_count()
{
	return Pad_game_south_presses;
}

void pad_frame()
{
	int i, control;

	pad_game_update(1);

	// a new mission starts from the same selection, so a recording made after another mission replays
	if (Missiontime < Pad_ets_last_time) {
		Pad_ets_selected = PAD_ETS_SHIELDS;
	}
	Pad_ets_last_time = Missiontime;

	for (i = 0; i < Pad_game_out.num_fired; i++) {
		int action = Pad_game_out.fired[i];

		switch (action) {
		case PAD_ACTION_PAUSE:			game_do_pad_pause_popup();		break;	// Claude: #199
		case PAD_ACTION_MENU_OPEN:		hud_squadmsg_pad_open();		break;
		case PAD_ACTION_MENU_CLOSE:	hud_squadmsg_pad_close();		break;
		case PAD_ACTION_MENU_UP:		hud_squadmsg_pad_move(-1);		break;
		case PAD_ACTION_MENU_DOWN:		hud_squadmsg_pad_move(1);		break;
		case PAD_ACTION_MENU_SELECT:	hud_squadmsg_pad_select();		break;
		case PAD_ACTION_MENU_BACK:		hud_squadmsg_pad_back();		break;
		case PAD_ACTION_SECONDARY_STEP:	pad_game_secondary_step();	break;
		case PAD_ACTION_TARGET_NEAREST:	pad_game_target_hostile(0);	break;
		case PAD_ACTION_TARGET_NEXT:		pad_game_target_hostile(1);	break;
		case PAD_ACTION_ETS_PREV:		pad_ets_step(1);					break;	// reversed at Tom's request: left steps up the list (#151)
		case PAD_ACTION_ETS_NEXT:		pad_ets_step(-1);					break;
		case PAD_ACTION_ETS_UP:			pad_ets_change(1);				break;
		case PAD_ACTION_ETS_DOWN:		pad_ets_change(0);				break;
		case PAD_ACTION_TARGET:
			Pad_game_south_presses++;
			control = pad_game_south_control();
			if (control >= 0) {
				button_info_set(&Player->bi, control);
			}
			break;
		default:
			if ((action >= 0) && (action < CCFG_MAX)) {
				button_info_set(&Player->bi, action);
			}
			break;
		}
	}
}

const pad_output *pad_game_output()
{
	return &Pad_game_out;
}

int pad_control_held(int id)
{
	return pad_output_is_held(&Pad_game_out, id);
}

int pad_voice_talk_held()
{
	return pad_output_is_held(&Pad_game_out, PAD_ACTION_TALK);
}

void pad_read_flight(control_info *ci, float frame_time)
{
	if (!Pad_game_out.connected) {
		return;
	}

	// FS2 signs: heading +right, pitch +nose down, bank +left
	ci->heading += Pad_game_out.yaw;
	ci->pitch -= Pad_game_out.pitch;
	ci->bank -= Pad_game_out.roll;

	if (Pad_game_out.throttle_rate > 0.0f) {
		// raising the throttle by hand ends speed matching, or matching would undo it next frame
		Player->flags &= ~PLAYER_FLAGS_MATCH_TARGET;
		ci->forward_cruise_percent += Pad_game_out.throttle_rate * frame_time;
	} else if (Pad_game_out.throttle_rate < 0.0f) {
		Player->flags &= ~PLAYER_FLAGS_MATCH_TARGET;
		ci->forward_cruise_percent += Pad_game_out.throttle_rate * frame_time;
	}

	// only old recordings still hold this; LB is the speed mode now (#151)
	if (pad_output_is_held(&Pad_game_out, PAD_ACTION_SLOW)) {
		Player->flags &= ~PLAYER_FLAGS_MATCH_TARGET;
		ci->forward_cruise_percent -= Pad_game_out.throttle_max_rate * frame_time;
	}
}

int pad_owns_joystick()
{
	pad_state st;

	return pad_backend_poll(&st);
}

int pad_pause_pressed()
{
	int i;

	pad_game_update(0);

	for (i = 0; i < Pad_game_out.num_fired; i++) {
		if (Pad_game_out.fired[i] == PAD_ACTION_PAUSE) {
			return 1;
		}
	}
	return 0;
}
