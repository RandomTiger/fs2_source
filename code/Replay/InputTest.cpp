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
#include "../Pad/PadCore.h"
#include "../Pad/PadXInput.h"
#include "../Pad/PadGame.h"

extern int joy_num_sticks;	// Joy.cpp

#define INPUT_TEST_JOY_NOTE		"autotest: joystick script"
#define INPUT_TEST_PAD_NOTE		"autotest: pad script"

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
static int Input_test_pad_inject = 0;		// -at_input pad
static int Input_test_pad_script = 0;		// this mission's notes ask for the pad script
static int Input_test_pad_asked = 0;		// some mission asked for it
static int Input_test_pad_finished = 0;	// it reached PAD_TEST_END
static int Input_test_step = 0;				// gameplay frames since the mission started
static int Input_test_checks = 0;
static int Input_test_failures = 0;
static char *Input_test_name = "Joystick script";	// for the log
static input_test_joy_state Input_test_joy;	// the scripted stick's state for the next polls
static pad_state Input_test_pad;			// the scripted pad's state for this frame's poll

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
	} else if (what && !stricmp(what, "pad")) {
		Input_test_pad_inject = 1;
		Input_test_pad.connected = 1;		// from game start, so joy_init leaves the stick to the pad
		input_test_logf("Input test: the scripted pad stands in for the real one (-at_input pad)\n");
	} else {
		input_test_logf("FATAL: -at_input needs 'joy' or 'pad', got '%s'\n", what ? what : "");
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
		input_test_logf("%s: FAIL frame %d: %s is %d, wants %d\n", Input_test_name, Input_test_step, what, got, want);
	}
}

static void check_float(char *what, float got, float want)
{
	Input_test_checks++;
	if (got != want) {
		Input_test_failures++;
		input_test_logf("%s: FAIL frame %d: %s is %.6f, wants %.6f\n", Input_test_name, Input_test_step, what, got, want);
	}
}

static void check_float_above(char *what, float got, float lo, float hi)
{
	Input_test_checks++;
	if ((got <= lo) || (got > hi)) {
		Input_test_failures++;
		input_test_logf("%s: FAIL frame %d: %s is %.6f, wants above %.2f up to %.2f\n", Input_test_name, Input_test_step, what, got, lo, hi);
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

// the scripted pad, standing in for the pad backend (PadGame.cpp, through InputTestPad.h)

// the script, in gameplay frames from the first one; a frame's state is what that frame's pad_frame polls
#define PAD_TEST_TAP			20			// South (Target next hostile, press) and the right shoulder (Fire secondary, hold) down for this frame only
#define PAD_TEST_LOW_START		35			// the right trigger at 0.2, under the 0.3 threshold...
#define PAD_TEST_HOLD_START		40			// ...then fully down (Fire primary, hold) from here...
#define PAD_TEST_HOLD_END		60			// ...to here, where it comes up
#define PAD_TEST_AXES1_START	80			// left stick full right, right stick full left, left trigger 0.525
#define PAD_TEST_AXES2_START	120			// left stick at (0.3, 0.4), past the dead zone
#define PAD_TEST_DEAD_START		160			// left stick at (0.1, 0.1) and right at (0.1, 0), inside the dead zones
#define PAD_TEST_DEAD_END		180			// centred again
#define PAD_TEST_END			200			// the last check

int input_test_pad_poll(pad_state *out)
{
	if (!Input_test_pad_inject) {
		return pad_backend_poll(out);
	}
	*out = Input_test_pad;
	return 1;
}

static void pad_test_idle(pad_state *st)
{
	memset(st, 0, sizeof(*st));
	st->connected = 1;
}

static void pad_test_script(int step, pad_state *st)
{
	pad_test_idle(st);

	if (step == PAD_TEST_TAP) {
		st->button[PAD_SOUTH] = 1;
		st->button[PAD_RSHOULDER] = 1;
	}
	if ((step >= PAD_TEST_LOW_START) && (step < PAD_TEST_HOLD_START)) {
		st->axis[PAD_TRIGGER_RIGHT] = 0.2f;
	}
	if ((step >= PAD_TEST_HOLD_START) && (step < PAD_TEST_HOLD_END)) {
		st->axis[PAD_TRIGGER_RIGHT] = 1.0f;
	}
	if ((step >= PAD_TEST_AXES1_START) && (step < PAD_TEST_AXES2_START)) {
		st->axis[PAD_LEFT_X] = 1.0f;
		st->axis[PAD_RIGHT_X] = -1.0f;
		st->axis[PAD_TRIGGER_LEFT] = 0.525f;
	}
	if ((step >= PAD_TEST_AXES2_START) && (step < PAD_TEST_DEAD_START)) {
		st->axis[PAD_LEFT_X] = 0.3f;
		st->axis[PAD_LEFT_Y] = 0.4f;
	}
	if ((step >= PAD_TEST_DEAD_START) && (step < PAD_TEST_DEAD_END)) {
		st->axis[PAD_LEFT_X] = 0.1f;
		st->axis[PAD_LEFT_Y] = 0.1f;
		st->axis[PAD_RIGHT_X] = 0.1f;
	}
}

// the recorder keeps the pad's flight values to 1/10000 (PadGame.cpp), so they are checked to that
static void check_float_near(char *what, float got, float want)
{
	Input_test_checks++;
	if ((got < want - 0.0002f) || (got > want + 0.0002f)) {
		Input_test_failures++;
		input_test_logf("%s: FAIL frame %d: %s is %.6f, wants %.6f\n", Input_test_name, Input_test_step, what, got, want);
	}
}

// the pad's output for frame 'step' - 1 (pad_game_output), and the player's controls read from it
static void check_pad(int fired, int held, float yaw, float pitch, float roll, float throttle)
{
	const pad_output *out = pad_game_output();

	check_int("pad connected", out->connected, 1);
	check_int("pad actions fired", out->num_fired, (fired >= 0) ? 1 : 0);
	if ((fired >= 0) && (out->num_fired == 1)) {
		check_int("pad action fired", out->fired[0], fired);
	}
	check_int("pad actions held", out->num_held, (held >= 0) ? 1 : 0);
	if ((held >= 0) && (out->num_held == 1)) {
		check_int("pad action held", out->held[0], held);
	}
	check_float_near("pad yaw", out->yaw, yaw);
	check_float_near("pad pitch", out->pitch, pitch);
	check_float_near("pad roll", out->roll, roll);
	check_float_near("pad throttle rate", out->throttle_rate, throttle);

	// pad_read_flight: heading += yaw, pitch -= pitch, bank -= roll; then limited to -1..1
	check_float_near("ci.heading", Player->ci.heading, clamp_ci(yaw));
	check_float_near("ci.pitch", Player->ci.pitch, clamp_ci(-pitch));
	check_float_near("ci.bank", Player->ci.bank, clamp_ci(-roll));
	check_int("ci.fire_primary_count", Player->ci.fire_primary_count, (held == FIRE_PRIMARY) ? 1 : 0);
	check_int("ci.fire_secondary_count", Player->ci.fire_secondary_count, (held == FIRE_SECONDARY) ? 1 : 0);
}

// the script's answers depend on the default pad settings and controls
static void pad_test_check_setup()
{
	FILE *fp = fopen("pad.cfg", "rb");

	check_int("pad.cfg in the game folder", fp ? 1 : 0, 0);
	if (fp) {
		fclose(fp);
	}
	check_int("mouse flies the ship", Use_mouse_to_fly, 0);
	check_int("joysticks found", joy_num_sticks, 0);	// joy_init leaves the stick to a pad
}

// stick shaping (PadCore.cpp, default settings): radial dead zone 0.15 rescaled to 0..1, then squared
//   (1, 0)     -> magnitude 1, shaped 1
//   (0.3, 0.4) -> magnitude 0.5, shaped ((0.5 - 0.15) / 0.85)^2 = 49/289, so (0.6, 0.8) * 49/289
//   left trigger 0.525 -> (0.525 - 0.05) / 0.95 = 0.5 of the 100 percent per second throttle rate
#define PAD_TEST_SHAPED		(49.0f / 289.0f)

static void pad_test_check(int step)
{
	if ((step == PAD_TEST_TAP) || (step == PAD_TEST_TAP + 2)) {
		check_pad(-1, -1, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	if (step == PAD_TEST_TAP + 1) {
		check_pad(TARGET_NEXT_CLOSEST_HOSTILE, FIRE_SECONDARY, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	if ((step > PAD_TEST_LOW_START) && (step <= PAD_TEST_HOLD_START)) {
		check_pad(-1, -1, 0.0f, 0.0f, 0.0f, 0.0f);	// under the trigger threshold
	}
	if ((step > PAD_TEST_HOLD_START) && (step <= PAD_TEST_HOLD_END)) {
		check_pad(-1, FIRE_PRIMARY, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	if ((step == PAD_TEST_HOLD_END + 1) || (step == PAD_TEST_HOLD_END + 2)) {
		check_pad(-1, -1, 0.0f, 0.0f, 0.0f, 0.0f);	// a pad hold has no count left over on release
	}
	if ((step == PAD_TEST_AXES1_START) || (step == PAD_TEST_AXES2_START + 1)) {
		check_pad(-1, -1, (step == PAD_TEST_AXES1_START) ? 0.0f : 0.6f * PAD_TEST_SHAPED, (step == PAD_TEST_AXES1_START) ? 0.0f : 0.8f * PAD_TEST_SHAPED, 0.0f, 0.0f);
	}
	if ((step == PAD_TEST_AXES1_START + 1) || (step == PAD_TEST_AXES2_START)) {
		check_pad(-1, -1, 1.0f, 0.0f, -1.0f, 50.0f);
	}
	if (step == PAD_TEST_DEAD_START) {
		check_pad(-1, -1, 0.6f * PAD_TEST_SHAPED, 0.8f * PAD_TEST_SHAPED, 0.0f, 0.0f);
	}
	if ((step == PAD_TEST_DEAD_START + 1) || (step == PAD_TEST_DEAD_END) || (step == PAD_TEST_END)) {
		check_pad(-1, -1, 0.0f, 0.0f, 0.0f, 0.0f);
	}
}

// ---------------------------------------------------------------------------------------------------

void input_test_mission_start()
{
	Input_test_step = 0;
	Input_test_joy_script = (strstr(The_mission.notes, INPUT_TEST_JOY_NOTE) != NULL);
	Input_test_pad_script = (strstr(The_mission.notes, INPUT_TEST_PAD_NOTE) != NULL);
	joy_test_idle(&Input_test_joy);
	pad_test_idle(&Input_test_pad);
	if (Input_test_joy_script) {
		Input_test_name = "Joystick script";
		Input_test_joy_asked = 1;
		input_test_logf("Joystick script: started%s\n", Input_test_joy_inject ? "" : " (replay: the joystick reads come from the recording)");
		if (Input_test_joy_inject) {
			joy_test_check_pilot();
		}
	}
	if (Input_test_pad_script) {
		Input_test_name = "Pad script";
		Input_test_pad_asked = 1;
		input_test_logf("Pad script: started%s\n", Input_test_pad_inject ? "" : " (replay: the pad's output comes from the recording)");
		if (Input_test_pad_inject) {
			pad_test_check_setup();
		}
	}
}

void input_test_frame()
{
	int joy = Input_test_joy_script && !Input_test_joy_finished;
	int pad = Input_test_pad_script && !Input_test_pad_finished;

	if (!joy && !pad) {
		return;
	}

	if (joy) {
		joy_test_check(Input_test_step);
		if (Input_test_step == JOY_TEST_END) {
			Input_test_joy_finished = 1;
			input_test_logf("Joystick script: %d checks, %d failed: %s\n", Input_test_checks, Input_test_failures, Input_test_failures ? "FAIL" : "PASS");
		}
	}
	if (pad) {
		pad_test_check(Input_test_step);
		if (Input_test_step == PAD_TEST_END) {
			Input_test_pad_finished = 1;
			input_test_logf("Pad script: %d checks, %d failed: %s\n", Input_test_checks, Input_test_failures, Input_test_failures ? "FAIL" : "PASS");
		}
	}

	pad_test_script(Input_test_step, &Input_test_pad);		// this frame's pad_frame polls it
	Input_test_step++;
	joy_test_script(Input_test_step, &Input_test_joy);		// the next frame's os_poll reads it
}

int input_test_failed()
{
	if (Input_test_joy_asked && !Input_test_joy_finished) {
		input_test_logf("Joystick script: FAIL the mission ended at frame %d, before the script's end at frame %d\n", Input_test_step, JOY_TEST_END);
		return 1;
	}
	if (Input_test_pad_asked && !Input_test_pad_finished) {
		input_test_logf("Pad script: FAIL the mission ended at frame %d, before the script's end at frame %d\n", Input_test_step, PAD_TEST_END);
		return 1;
	}
	return Input_test_failures ? 1 : 0;
}
