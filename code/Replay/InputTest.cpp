/*
 * Claude: scripted input tests.  See InputTest.h.
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "pstypes.h"
#include "fix.h"
#include "InputTest.h"
#include "Autotest.h"
#include "joy.h"
#include "ControlsConfig.h"
#include "MissionParse.h"
#include "Player.h"
#include "Mouse.h"

extern int joy_num_sticks;	// Joy.cpp

#define INPUT_TEST_JOY_NOTE		"autotest: joystick script"

#define JOY_TEST_STICK			0			// the stick id the scripted joystick answers to
#define JOY_TEST_CENTRE			32768
#define JOY_TEST_POV_CENTRED	65535		// JOY_POVCENTERED as the read returns it
#define JOY_TEST_BUTTON			10			// a button no control is bound to by default, for the exact counts
#define JOY_TEST_PRIMARY		0			// the default Fire Primary Weapon button (continuous)
#define JOY_TEST_SECONDARY		1			// the default Fire Secondary Weapon button (continuous)

// the script, in gameplay frames from the first one; a frame's state is what that frame's os_poll reads
#define JOY_TEST_TAP			20			// buttons 10 and 1 down for this frame only
#define JOY_TEST_HOLD_START		40			// buttons 10 and 0 down from here...
#define JOY_TEST_HOLD_END		60			// ...to here, where they come up
#define JOY_TEST_AXES1_START	80			// X full right, Y full forward, Rx full right
#define JOY_TEST_AXES2_START	120			// X and Y half way past the dead zone, Rx centred
#define JOY_TEST_AXES_END		160			// centred again
#define JOY_TEST_END			200			// the last check

static int Input_test_joy_inject = 0;		// -at_input joy
static int Input_test_joy_script = 0;		// this mission's notes ask for the joystick script
static int Input_test_joy_asked = 0;		// some mission asked for it
static int Input_test_joy_finished = 0;	// it reached JOY_TEST_END
static int Input_test_step = 0;				// gameplay frames since the mission started
static int Input_test_checks = 0;
static int Input_test_failures = 0;
static input_test_joy_state Input_test_joy;	// the scripted stick's state for the next polls

static void input_test_logf(char *format, ...)
{
	char text[512];
	va_list args;

	va_start(args, format);
	_vsnprintf(text, sizeof(text) - 1, format, args);
	va_end(args);
	text[sizeof(text) - 1] = 0;
	autotest_note(text);
}

void input_test_set_cmdline(char *what)
{
	if (what && !stricmp(what, "joy")) {
		Input_test_joy_inject = 1;
		input_test_logf("Input test: the scripted joystick stands in for the real one (-at_input joy)\n");
	} else {
		input_test_logf("FATAL: -at_input needs 'joy', got '%s'\n", what ? what : "");
		Input_test_failures++;
	}
}

// ---------------------------------------------------------------------------------------------------
// the scripted joystick, standing in for WinMM (Joy.cpp, through InputTestJoy.h)

static void joy_test_idle(input_test_joy_state *st)
{
	int i;

	for (i = 0; i < 6; i++) {
		st->axes[i] = JOY_TEST_CENTRE;
	}
	st->buttons = 0;
	st->pov = JOY_TEST_POV_CENTRED;
}

int input_test_joy_injected()
{
	return Input_test_joy_inject;
}

void input_test_joy_read(input_test_joy_state *st)
{
	*st = Input_test_joy;
}

UINT input_test_joyGetNumDevs()
{
	if (!Input_test_joy_inject) {
		return joyGetNumDevs();
	}
	return 1;
}

MMRESULT input_test_joyGetDevCaps(UINT_PTR id, JOYCAPS *caps, UINT size)
{
	if (!Input_test_joy_inject) {
		return joyGetDevCaps(id, caps, size);
	}
	if (id != JOY_TEST_STICK) {
		return JOYERR_PARMS;
	}
	memset(caps, 0, size);
	strcpy(caps->szPname, "Autotest scripted joystick");
	caps->wNumAxes = 4;
	caps->wNumButtons = 32;
	caps->wCaps = JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASPOV | JOYCAPS_POV4DIR;
	return JOYERR_NOERROR;
}

MMRESULT input_test_joyGetPosEx(UINT id, JOYINFOEX *ji)
{
	if (!Input_test_joy_inject) {
		return joyGetPosEx(id, ji);
	}
	if (id != JOY_TEST_STICK) {
		return JOYERR_UNPLUGGED;
	}
	ji->dwXpos = Input_test_joy.axes[0];
	ji->dwYpos = Input_test_joy.axes[1];
	ji->dwZpos = Input_test_joy.axes[2];
	ji->dwRpos = Input_test_joy.axes[3];
	ji->dwUpos = Input_test_joy.axes[4];
	ji->dwVpos = Input_test_joy.axes[5];
	ji->dwButtons = Input_test_joy.buttons;
	ji->dwPOV = Input_test_joy.pov;
	return JOYERR_NOERROR;
}

// the script: the stick state that frame 'step''s polls read
static void joy_test_script(int step, input_test_joy_state *st)
{
	joy_test_idle(st);

	if (step == JOY_TEST_TAP) {
		st->buttons |= (1 << JOY_TEST_BUTTON) | (1 << JOY_TEST_SECONDARY);
	}
	if ((step >= JOY_TEST_HOLD_START) && (step < JOY_TEST_HOLD_END)) {
		st->buttons |= (1 << JOY_TEST_BUTTON) | (1 << JOY_TEST_PRIMARY);
	}
	if ((step >= JOY_TEST_AXES1_START) && (step < JOY_TEST_AXES2_START)) {
		st->axes[0] = 65535;
		st->axes[1] = 0;
		st->axes[3] = 65535;
	}
	if ((step >= JOY_TEST_AXES2_START) && (step < JOY_TEST_AXES_END)) {
		st->axes[0] = JOY_TEST_CENTRE + 13107;
		st->axes[1] = JOY_TEST_CENTRE - 13107;
	}
}

// ---------------------------------------------------------------------------------------------------
// the checks

static void check_int(char *what, int got, int want)
{
	Input_test_checks++;
	if (got != want) {
		Input_test_failures++;
		input_test_logf("Joystick script: FAIL frame %d: %s is %d, wants %d\n", Input_test_step, what, got, want);
	}
}

static void check_float(char *what, float got, float want)
{
	Input_test_checks++;
	if (got != want) {
		Input_test_failures++;
		input_test_logf("Joystick script: FAIL frame %d: %s is %.6f, wants %.6f\n", Input_test_step, what, got, want);
	}
}

static void check_float_above(char *what, float got, float lo, float hi)
{
	Input_test_checks++;
	if ((got <= lo) || (got > hi)) {
		Input_test_failures++;
		input_test_logf("Joystick script: FAIL frame %d: %s is %.6f, wants above %.2f up to %.2f\n", Input_test_step, what, got, lo, hi);
	}
}

// control_get_axes_readings: after calibration (0, 32768, 65536), the 10% dead zone and sensitivity 9
static void check_axes(int h, int p, int b)
{
	int vals[5];

	control_get_axes_readings(&vals[0], &vals[1], &vals[2], &vals[3], &vals[4]);
	check_int("heading axis", vals[0], h);
	check_int("pitch axis", vals[1], p);
	check_int("bank axis", vals[2], b);
	check_int("absolute throttle axis", vals[3], 0);
	check_int("relative throttle axis", vals[4], 0);
}

static float clamp_ci(float v)
{
	return (v > 1.0f) ? 1.0f : ((v < -1.0f) ? -1.0f : v);
}

// the player's controls as read_keyboard_controls set them last frame, from the stick's last 10 Hz reading,
// then limited to -1..1 by physics_read_flying_controls
static void check_ci(int h, int p, int b)
{
	check_float("ci.heading", Player->ci.heading, clamp_ci(f2fl(h)));
	check_float("ci.pitch", Player->ci.pitch, clamp_ci(-f2fl(p)));
	check_float("ci.bank", Player->ci.bank, clamp_ci(-f2fl(b) * 1.5f));
}

// the script's answers depend on the pilot's controls being the defaults
static void joy_test_check_pilot()
{
	int i;

	check_int("joystick sensitivity", Joy_sensitivity, 9);
	check_int("dead zone", Dead_zone_size, 10);
	check_int("mouse flies the ship", Use_mouse_to_fly, 0);
	check_int("heading axis map", Axis_map_to[0], JOY_X_AXIS);
	check_int("pitch axis map", Axis_map_to[1], JOY_Y_AXIS);
	check_int("bank axis map", Axis_map_to[2], JOY_RX_AXIS);
	check_int("absolute throttle axis map", Axis_map_to[3], -1);
	check_int("relative throttle axis map", Axis_map_to[4], -1);
	for (i = 0; i < 5; i++) {
		check_int("axis inverted", Invert_axis[i], 0);
	}
	check_int("Fire Primary Weapon button", Control_config[FIRE_PRIMARY].joy_id, JOY_TEST_PRIMARY);
	check_int("Fire Secondary Weapon button", Control_config[FIRE_SECONDARY].joy_id, JOY_TEST_SECONDARY);
	for (i = 0; i < CCFG_MAX; i++) {
		if (Control_config[i].joy_id == JOY_TEST_BUTTON) {
			check_int("a control bound to the test button", i, -1);
		}
	}
	check_int("joysticks found", joy_num_sticks, 1);
}

// what the game read from the stick in frame 'step', checked at the start of frame 'step' after that frame's poll,
// and the player's controls from frame 'step' - 1
static void joy_test_check(int step)
{
	int b = JOY_TEST_BUTTON;

	// a one-frame tap
	if (step == JOY_TEST_TAP - 1) {
		check_int("joy_down(10)", joy_down(b), 0);
		check_int("joy_down_count(10)", joy_down_count(b), 0);
		check_int("joy_up_count(10)", joy_up_count(b), 0);
	}
	if (step == JOY_TEST_TAP) {
		check_int("joy_down(10)", joy_down(b), 1);
		check_int("joy_down_count(10)", joy_down_count(b), 1);
		check_int("joy_up_count(10)", joy_up_count(b), 0);
		check_int("ci.fire_secondary_count", Player->ci.fire_secondary_count, 0);
	}
	if (step == JOY_TEST_TAP + 1) {
		check_int("joy_down(10)", joy_down(b), 0);
		check_int("joy_down_count(10)", joy_down_count(b), 0);
		check_int("joy_up_count(10)", joy_up_count(b), 1);
		check_int("ci.fire_secondary_count", Player->ci.fire_secondary_count, 1);	// joy_down(1)
	}
	if (step == JOY_TEST_TAP + 2) {
		check_int("joy_up_count(10)", joy_up_count(b), 0);
		check_int("ci.fire_secondary_count", Player->ci.fire_secondary_count, 1);	// joy_down_count(1): a continuous control reads the count only when the button is up
	}
	if (step == JOY_TEST_TAP + 3) {
		check_int("ci.fire_secondary_count", Player->ci.fire_secondary_count, 0);
	}

	// a held button and its release
	if ((step >= JOY_TEST_HOLD_START - 5) && (step < JOY_TEST_HOLD_START)) {
		check_float("joy_down_time(10)", joy_down_time(b), 0.0f);
	}
	if (step == JOY_TEST_HOLD_START) {
		check_int("ci.fire_primary_count", Player->ci.fire_primary_count, 0);
		check_int("joy_down(10)", joy_down(b), 1);
		check_int("joy_down_count(10)", joy_down_count(b), 1);
		check_float_above("joy_down_time(10)", joy_down_time(b), 0.5f, 1.0f);
	}
	if ((step > JOY_TEST_HOLD_START) && (step < JOY_TEST_HOLD_END)) {
		check_int("ci.fire_primary_count", Player->ci.fire_primary_count, 1);
		check_int("joy_down(10)", joy_down(b), 1);
		check_int("joy_down_count(10)", joy_down_count(b), 0);
		check_float_above("joy_down_time(10)", joy_down_time(b), 0.5f, 1.0f);
	}
	if (step == JOY_TEST_HOLD_END) {
		check_int("ci.fire_primary_count", Player->ci.fire_primary_count, 1);
		check_int("joy_down(10)", joy_down(b), 0);
		check_int("joy_down_count(10)", joy_down_count(b), 0);
		check_int("joy_up_count(10)", joy_up_count(b), 1);
		check_float("joy_down_time(10)", joy_down_time(b), 0.0f);
	}
	if (step == JOY_TEST_HOLD_END + 1) {
		check_int("ci.fire_primary_count", Player->ci.fire_primary_count, 1);	// joy_down_count(0): the press, unread while the button was down, is read on release
		check_int("joy_up_count(10)", joy_up_count(b), 0);
		check_float("joy_down_time(10)", joy_down_time(b), 0.0f);
	}
	if (step == JOY_TEST_HOLD_END + 2) {
		check_int("ci.fire_primary_count", Player->ci.fire_primary_count, 0);
	}

	// the stick
	if (step == JOY_TEST_AXES1_START - 1) {
		check_axes(0, 0, 0);
		check_ci(0, 0, 0);
	}
	if (step == JOY_TEST_AXES2_START - 1) {
		check_axes(65533, -65536, 65533);
		check_ci(65533, -65536, 65533);
	}
	if (step == JOY_TEST_AXES_END - 1) {
		check_axes(16384, -16384, 0);
		check_ci(16384, -16384, 0);
	}
	if (step == JOY_TEST_END) {
		check_axes(0, 0, 0);
		check_ci(0, 0, 0);
	}
}

// ---------------------------------------------------------------------------------------------------

void input_test_mission_start()
{
	Input_test_step = 0;
	Input_test_joy_script = (strstr(The_mission.notes, INPUT_TEST_JOY_NOTE) != NULL);
	joy_test_idle(&Input_test_joy);
	if (Input_test_joy_script) {
		Input_test_joy_asked = 1;
		input_test_logf("Joystick script: started%s\n", Input_test_joy_inject ? "" : " (replay: the joystick reads come from the recording)");
		if (Input_test_joy_inject) {
			joy_test_check_pilot();
		}
	}
}

void input_test_frame()
{
	if (!Input_test_joy_script || Input_test_joy_finished) {
		return;
	}

	joy_test_check(Input_test_step);
	if (Input_test_step == JOY_TEST_END) {
		Input_test_joy_finished = 1;
		input_test_logf("Joystick script: %d checks, %d failed: %s\n", Input_test_checks, Input_test_failures, Input_test_failures ? "FAIL" : "PASS");
	}
	Input_test_step++;
	joy_test_script(Input_test_step, &Input_test_joy);
}

int input_test_failed()
{
	if (Input_test_joy_asked && !Input_test_joy_finished) {
		input_test_logf("Joystick script: FAIL the mission ended at frame %d, before the script's end at frame %d\n", Input_test_step, JOY_TEST_END);
		return 1;
	}
	return Input_test_failures ? 1 : 0;
}
