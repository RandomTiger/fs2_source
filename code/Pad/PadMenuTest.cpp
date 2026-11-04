// PadMenuTest.cpp
// The menu layer's test: -at_padmenu, mission AutotestPadMenu. A script feeds pad_state below the
// layer, and a cursor below mouse_get_cursor_pos, and checks what the game did. It runs when the
// recording is made and never in a replay, which gets its events from the stream.
//
// The script reads as a list of steps: each WAIT returns to the game until its condition holds.

#include <math.h>
#include <string.h>

#include "pstypes.h"
#include "PadMenuGame.h"
#include "PadGame.h"

#include "2d.h"
#include "Autotest.h"
#include "ControlsConfig.h"
#include "GameSequence.h"
#include "key.h"
#include "Player.h"
#include "Popup.h"
#include "PopupDead.h"
#include "Replay.h"

extern int Popup_default_choice;
extern int Popupdead_default_choice;

// small marked getters in the screens' own files
int sim_room_test_selected_line();
int sim_room_test_missions_tab_xy(int *x, int *y);
int techroom_test_cur_entry();
int techroom_test_sim_tab_xy(int *x, int *y);
int main_hall_test_region_xy(int region, int *x, int *y);
int cmd_brief_test_stage();
int brief_test_stage();

#define TEST_TIMEOUT		20000		// polls
#define TEST_HOLD			6			// polls a tap stays down, and up

static int Test_set = 0;			// -at_padmenu
static int Test_front_done = 0;
static int Test_cmd_brief_done = 0;
static int Test_brief_done = 0;
static int Test_auto_advance = 0;	// the pilot's auto-advance, off while the script counts stages
static int Test_done = 0;
static float Test_last_press = -1.0f;	// mission time of the press that should close the death popup
static int Test_line = 0;
static int Test_polls = 0;
static int Test_since = 0;
static int Test_until = 0;
static int Test_bad = 0;
static int Test_events = 0;
static int Test_v0, Test_v1;
static int Test_south = 0;				// pad_game_south_count() before what is checked (South targets in flight, West talks: #151)
static int Test_x = 0, Test_y = 0;
static int Test_key = 0;
static pad_state Test_pad;

void pad_menu_test_set()
{
	Test_set = 1;
}

int pad_menu_test_on()
{
	return Test_set && (Replay_mode != REPLAY_PLAY);
}

int pad_menu_test_front_end()
{
	return pad_menu_test_on() && !Test_front_done;
}

int pad_menu_test_holds_briefing(int state)
{
	if (!pad_menu_test_on()) {
		return 0;
	}
	return ((state == GS_STATE_CMD_BRIEF) && !Test_cmd_brief_done) || ((state == GS_STATE_BRIEFING) && !Test_brief_done);
}

void pad_menu_test_state(pad_state *st)
{
	*st = Test_pad;
}

void pad_menu_test_move_cursor(int x, int y)
{
	Test_x = x;
	Test_y = y;
}

void pad_menu_test_cursor(long *x, long *y)
{
	if (pad_menu_test_on()) {
		*x = Test_x;
		*y = Test_y;
	}
}

int pad_menu_test_take_key()
{
	int k = Test_key;

	Test_key = 0;
	return k;
}

void pad_menu_test_check_finished()
{
	if (pad_menu_test_on()) {
		autotest_check(Test_done, "pad menu script got to its end");
		autotest_check((Test_last_press >= 0.0f) && (autotest_mission_seconds() - Test_last_press < 5.0f), "death popup: South closes it");
	}
}

// the nearest pixel of value to the middle of all the pixels of value, so it is on them
int pad_menu_test_find_pixel(const unsigned char *mask, int w, int h, int value, int *x, int *y)
{
	int i, j, count = 0, best = -1;
	float cx = 0.0f, cy = 0.0f;

	if (mask == NULL) {
		return 0;
	}
	for (j = 0; j < h; j++) {
		for (i = 0; i < w; i++) {
			if (mask[j * w + i] == value) {
				cx += i;
				cy += j;
				count++;
			}
		}
	}
	if (!count) {
		return 0;
	}
	cx /= count;
	cy /= count;
	for (j = 0; j < h; j++) {
		for (i = 0; i < w; i++) {
			if (mask[j * w + i] == value) {
				int d = (int) ((i - cx) * (i - cx) + (j - cy) * (j - cy));
				if ((best < 0) || (d < best)) {
					best = d;
					*x = i;
					*y = j;
				}
			}
		}
	}
	return 1;
}

static void test_release_all()
{
	memset(&Test_pad, 0, sizeof(Test_pad));
	Test_pad.connected = 1;
}

static void test_check(int ok, char *what)
{
	autotest_check(ok, what);
}

// the stick, as a person would: full over a distance, gently near. 1 when there.
static int test_steer(int tx, int ty)
{
	float dx = (float) (tx - Test_x);
	float dy = (float) (ty - Test_y);
	float d = (float) sqrt(dx * dx + dy * dy);
	float mag;

	if (d <= 2.0f) {
		Test_pad.axis[PAD_LEFT_X] = 0.0f;
		Test_pad.axis[PAD_LEFT_Y] = 0.0f;
		return 1;
	}
	mag = (d > 40.0f) ? 1.0f : ((d > 10.0f) ? 0.45f : 0.3f);
	Test_pad.axis[PAD_LEFT_X] = dx / d * mag;
	Test_pad.axis[PAD_LEFT_Y] = dy / d * mag;
	return 0;
}

static void test_fail(char *what)
{
	test_check(0, what);
	test_release_all();
	Test_front_done = 1;
	Test_cmd_brief_done = 1;
	Test_brief_done = 1;
	Test_done = 1;
}

#define WAIT_(n, c)			do { Test_line = (n); Test_since = Test_polls; case (n): if (!(c)) { if (Test_polls - Test_since > TEST_TIMEOUT) { test_fail("pad menu script timed out: " #c); } return; } } while (0)
#define WAIT(c)				WAIT_(__COUNTER__ + 1, c)
#define POLLS(p)				do { Test_until = Test_polls + (p); WAIT(Test_polls >= Test_until); } while (0)
#define PRESS(b)				do { Test_pad.button[b] = 1; POLLS(TEST_HOLD); } while (0)
#define RELEASE(b)			do { Test_pad.button[b] = 0; POLLS(TEST_HOLD); } while (0)
#define TAP(b)					do { PRESS(b); RELEASE(b); } while (0)
#define ALWAYS_(n, p, c, m)	do { Test_bad = 0; Test_until = Test_polls + (p); Test_line = (n); case (n): if (!(c)) { Test_bad++; } if (Test_polls < Test_until) { return; } test_check(!Test_bad, m); } while (0)
#define ALWAYS(p, c, m)		ALWAYS_(__COUNTER__ + 1, p, c, m)

#define TALK		pad_voice_talk_held()
#define SOUTHS		pad_game_south_count()
#define MISSILE	pad_control_held(FIRE_SECONDARY)
#define STATE		gameseq_get_state()
#define DEAD		((STATE == GS_STATE_DEATH_DIED) || (STATE == GS_STATE_DEATH_BLEW_UP))

void pad_menu_test_frame()
{
	if (!pad_menu_test_on() || Test_done) {
		return;
	}

	Test_polls++;

	switch (Test_line) {
	case 0:
		test_release_all();

		// the main hall: the stick moves the cursor onto a region, South goes in
		WAIT(STATE == GS_STATE_MAIN_MENU);
		POLLS(10);
		test_check(pad_menu_region_under_cursor() == -1, "main hall: no region under the parked cursor");
		test_check(main_hall_test_region_xy(3, &Test_v0, &Test_v1), "main hall: Tech Room region found");
		WAIT(test_steer(Test_v0, Test_v1));
		POLLS(4);
		test_check(pad_menu_region_under_cursor() == 3, "main hall: the stick puts the cursor on Tech Room");
		TAP(PAD_SOUTH);
		WAIT(STATE == GS_STATE_TECH_MENU);
		POLLS(10);

		// a window with hotkeys: left and right, from the D-pad and the shoulders, go through the entries
		Test_v0 = techroom_test_cur_entry();
		TAP(PAD_DPAD_RIGHT);
		test_check(techroom_test_cur_entry() != Test_v0, "tech room: D-pad right moves to the next entry");
		Test_v1 = techroom_test_cur_entry();
		TAP(PAD_RSHOULDER);
		test_check(techroom_test_cur_entry() != Test_v1, "tech room: RB moves to the next entry");
		TAP(PAD_LSHOULDER);
		test_check(techroom_test_cur_entry() == Test_v1, "tech room: LB moves back an entry");
		TAP(PAD_DPAD_LEFT);
		test_check(techroom_test_cur_entry() == Test_v0, "tech room: D-pad left moves back an entry");

		test_check(techroom_test_sim_tab_xy(&Test_v0, &Test_v1), "tech room: Simulator tab found");
		WAIT(test_steer(Test_v0, Test_v1));
		POLLS(4);
		test_check(pad_menu_under_cursor(), "tech room: the stick puts the cursor on the Simulator tab");
		TAP(PAD_SOUTH);
		WAIT(STATE == GS_STATE_SIMULATOR_ROOM);
		POLLS(10);
		WAIT(!popup_active());		// it loads its list under a popup

		// a tab, then a list: up and down move one line for each press, however long it is held
		test_check(sim_room_test_missions_tab_xy(&Test_v0, &Test_v1), "sim room: Single Missions tab found");
		WAIT(test_steer(Test_v0, Test_v1));
		POLLS(4);
		TAP(PAD_SOUTH);
		POLLS(10);
		WAIT(!popup_active());
		POLLS(10);
		Test_v0 = sim_room_test_selected_line();
		Test_pad.button[PAD_DPAD_DOWN] = 1;
		POLLS(30);
		test_check(sim_room_test_selected_line() == Test_v0 + 1, "sim room: D-pad down held moves down one line");
		RELEASE(PAD_DPAD_DOWN);
		TAP(PAD_DPAD_UP);
		test_check(sim_room_test_selected_line() == Test_v0, "sim room: D-pad up moves back up");

		// South with nothing under the cursor is a click and Enter, and Enter is not a commit
		WAIT(test_steer(0, 0));
		POLLS(4);
		test_check(!pad_menu_under_cursor(), "sim room: nothing under the cursor in the corner");
		Test_events = pad_menu_events();
		TAP(PAD_SOUTH);
		test_check(pad_menu_events() == Test_events + 4, "sim room: South on nothing is a click and Enter");
		test_check(STATE == GS_STATE_SIMULATOR_ROOM, "sim room: South on nothing commits nothing");
		TAP(PAD_EAST);
		WAIT(STATE == GS_STATE_MAIN_MENU);
		POLLS(10);

		TAP(PAD_SOUTH);
		test_check(STATE == GS_STATE_MAIN_MENU, "main hall: South on nothing stays in the main hall");
		Test_front_done = 1;

		// the command briefing, three stages: left and right, from the D-pad and the shoulders, move one stage
		// and stop at each end. Auto-advance would move them too, so it is off until the script has counted.
		WAIT(STATE == GS_STATE_CMD_BRIEF);
		POLLS(10);
		Test_auto_advance = Player->auto_advance;
		Player->auto_advance = 0;
		test_check(cmd_brief_test_stage() == 0, "command briefing: opens on stage 1");
		TAP(PAD_DPAD_RIGHT);
		test_check(cmd_brief_test_stage() == 1, "command briefing: D-pad right moves to stage 2");
		TAP(PAD_RSHOULDER);
		test_check(cmd_brief_test_stage() == 2, "command briefing: RB moves to stage 3");
		TAP(PAD_DPAD_RIGHT);
		test_check(cmd_brief_test_stage() == 2, "command briefing: D-pad right stops at the last stage");
		TAP(PAD_LSHOULDER);
		test_check(cmd_brief_test_stage() == 1, "command briefing: LB moves back to stage 2");
		TAP(PAD_DPAD_LEFT);
		test_check(cmd_brief_test_stage() == 0, "command briefing: D-pad left moves back to stage 1");
		TAP(PAD_LSHOULDER);
		test_check(cmd_brief_test_stage() == 0, "command briefing: LB stops at the first stage");
		Test_cmd_brief_done = 1;

		// the tactical briefing, the same
		WAIT(STATE == GS_STATE_BRIEFING);
		POLLS(10);
		test_check(brief_test_stage() == 0, "tactical briefing: opens on stage 1");
		TAP(PAD_DPAD_RIGHT);
		test_check(brief_test_stage() == 1, "tactical briefing: D-pad right moves to stage 2");
		TAP(PAD_RSHOULDER);
		test_check(brief_test_stage() == 2, "tactical briefing: RB moves to stage 3");
		TAP(PAD_DPAD_RIGHT);
		test_check(brief_test_stage() == 2, "tactical briefing: D-pad right stops at the last stage");
		TAP(PAD_LSHOULDER);
		test_check(brief_test_stage() == 1, "tactical briefing: LB moves back to stage 2");
		TAP(PAD_DPAD_LEFT);
		test_check(brief_test_stage() == 0, "tactical briefing: D-pad left moves back to stage 1");
		TAP(PAD_LSHOULDER);
		test_check(brief_test_stage() == 0, "tactical briefing: LB stops at the first stage");

		// South and RB go down on a menu and are held into the mission
		Test_pad.button[PAD_SOUTH] = 1;
		Test_pad.button[PAD_RSHOULDER] = 1;
		POLLS(5);
		test_check(STATE == GS_STATE_BRIEFING, "tactical briefing: South on nothing stays in the briefing");
		Test_south = SOUTHS;
		Player->auto_advance = Test_auto_advance;
		Test_brief_done = 1;

		WAIT(STATE == GS_STATE_GAME_PLAY);
		POLLS(3);
		ALWAYS(30, (SOUTHS == Test_south) && !MISSILE, "flight: South and RB held from a menu do nothing");
		Test_events = pad_menu_events();
		test_release_all();
		POLLS(TEST_HOLD);
		test_check(pad_menu_events() == Test_events, "flight: nothing sent when they are released");

		// a fresh press in flight is the flight action again
		Test_south = SOUTHS;
		Test_pad.button[PAD_SOUTH] = 1;
		WAIT(SOUTHS != Test_south);
		RELEASE(PAD_SOUTH);
		Test_pad.button[PAD_WEST] = 1;
		WAIT(TALK);
		RELEASE(PAD_WEST);
		WAIT(!TALK);

		// in flight the layer sends nothing and leaves the cursor alone
		Test_events = pad_menu_events();
		Test_v0 = Test_x;
		Test_v1 = Test_y;
		Test_pad.button[PAD_WEST] = 1;
		Test_pad.button[PAD_RSHOULDER] = 1;
		Test_pad.button[PAD_DPAD_LEFT] = 1;
		Test_pad.axis[PAD_LEFT_X] = 1.0f;
		POLLS(3);
		ALWAYS(10, TALK && MISSILE && (pad_menu_events() == Test_events) && (Test_x == Test_v0) && (Test_y == Test_v1), "flight: the buttons fly, no menu events, the cursor stays");
		test_release_all();
		POLLS(TEST_HOLD);
		test_check(pad_menu_events() == Test_events, "flight: release sends nothing either");

		// pause: RB, down from flight, still does its flight action after; East is Back; South is inert
		Test_pad.button[PAD_RSHOULDER] = 1;
		WAIT(MISSILE);
		TAP(PAD_START);
		WAIT(STATE == GS_STATE_GAME_PAUSED);
		POLLS(TEST_HOLD);
		Test_events = pad_menu_events();
		TAP(PAD_SOUTH);
		test_check(STATE == GS_STATE_GAME_PAUSED, "pause: South changes nothing");
		test_check(pad_menu_events() == Test_events + 4, "pause: South is sent, RB held from flight is not");
		TAP(PAD_EAST);
		WAIT(STATE == GS_STATE_GAME_PLAY);
		POLLS(3);
		WAIT(MISSILE);
		test_check(1, "pause: RB held from flight still fires after");
		test_release_all();
		POLLS(TEST_HOLD);

		// a popup: up and down move its default, East aborts it, South held closes it and needs a release
		Test_key = KEY_ESC;
		WAIT(popup_active());
		POLLS(TEST_HOLD);
		Test_v0 = Popup_default_choice;
		TAP(PAD_DPAD_DOWN);
		test_check(Popup_default_choice != Test_v0, "popup: D-pad down moves the default");
		TAP(PAD_DPAD_UP);
		test_check(Popup_default_choice == Test_v0, "popup: D-pad up moves it back");
		PRESS(PAD_EAST);
		WAIT(!popup_active());
		RELEASE(PAD_EAST);
		test_check(STATE == GS_STATE_GAME_PLAY, "popup: East aborts it, back in flight");
		Test_key = KEY_ESC;
		WAIT(popup_active());
		POLLS(TEST_HOLD);
		Test_south = SOUTHS;
		Test_pad.button[PAD_SOUTH] = 1;
		WAIT(!popup_active());
		test_check(STATE == GS_STATE_GAME_PLAY, "popup: South takes the default, No, back in flight");
		ALWAYS(30, SOUTHS == Test_south, "popup: South held after it closed does not target");
		RELEASE(PAD_SOUTH);
		Test_pad.button[PAD_SOUTH] = 1;
		WAIT(SOUTHS != Test_south);
		RELEASE(PAD_SOUTH);

		// death: the view takes nothing, the popup works
		WAIT(DEAD);
		Test_events = pad_menu_events();
		PRESS(PAD_SOUTH);
		RELEASE(PAD_SOUTH);
		test_check(popupdead_is_active() || (pad_menu_events() == Test_events), "death view: South sends nothing");
		Test_south = SOUTHS;
		Test_pad.button[PAD_SOUTH] = 1;
		WAIT(popupdead_is_active());
		POLLS(TEST_HOLD);
		test_check(pad_menu_events() == Test_events, "death popup: South held into it sends nothing");
		test_check(1, (SOUTHS != Test_south) ? "death popup: fact: South down as it opened targeted first" : "death popup: fact: South held into it does not reach flight");
		RELEASE(PAD_SOUTH);

		Test_pad.axis[PAD_LEFT_X] = 1.0f;
		POLLS(20);
		test_check(1, (Player->ci.heading != 0.0f) ? "death popup: fact: the stick reaches the player's heading" : "death popup: fact: the stick does not reach the player's heading");
		test_release_all();
		POLLS(TEST_HOLD);

		TAP(PAD_DPAD_DOWN);
		test_check(Popupdead_default_choice == 1, "death popup: D-pad down moves the default");

		// this ends the mission, and autotest moves on from the main hall before the script's next poll
		Test_pad.button[PAD_SOUTH] = 1;
		Test_last_press = autotest_mission_seconds();
		Test_done = 1;
		break;
	}
}
