/*
 * Claude: scripted input tests for the joystick and the control pad (D-20261028-094624).
 *
 * Usage, when recording:
 *   -autotest -at_mission AutotestJoy -at_input joy -at_pilot AutotestPilot -record ...
 *   -autotest -at_mission AutotestPad -at_input pad -at_pilot AutotestPilot -record ...
 *
 * The checks assume the default controls and options of a new pilot; a new scratch pilot has them, a
 * player's own pilot may not (the checks say so).
 *
 * -at_input joy puts a scripted joystick in the host hook table (HostHooks.h: joy_count, joy_caps, joy_read)
 * in place of the real one, from game start, so joy_init finds
 * it and the recording says a joystick was in use.  It does nothing without -autotest.  A replay needs no
 * flag: the joystick calls come from the recording.
 *
 * A mission whose notes contain "autotest: joystick script" runs the script and its checks, in a
 * recording and in its replays alike, so both make the same recorded calls.  The script is a fixed
 * sequence over gameplay frames: a one-frame tap, a held button and its release, and two stick positions.
 * Each check compares what the game read (joy_down, joy_down_count, joy_up_count, joy_down_time,
 * control_get_axes_readings, and the player's control_info) with a known answer, and logs
 * "Joystick script: FAIL ..." on a mismatch.  A replay reads the recorded values, so it checks the
 * recording.  Any failure, or a script that did not finish, fails the autotest exit code.
 *
 * -at_input pad does the same for the control pad: the host table's pad_poll returns the scripted pad, which joy_init then takes for an XInput pad.  A
 * mission whose notes contain "autotest: pad script" runs the pad script: a one-frame tap of South and the
 * right shoulder, the right trigger under and over its threshold, and stick and trigger positions in and
 * out of the dead zones.  Each check compares the pad's output (pad_game_output) and the player's
 * control_info with a known answer for the default pad settings, and logs "Pad script: FAIL ...".  The
 * recording keeps the pad's output (RT_INPUT_DEVICE), so a replay checks that.
 *
 * -at_input joyhost and -at_input padhost run the same two scripts and checks, but leave the host table alone: each
 * frame the script's state goes to a sink the host registered (fs2_set_input_script_sink), the host feeds it into its own
 * joystick or pad input, and the game reads it back through the host's real joy_* or pad_poll entries. The known answers are
 * the same, so a host's reader is right when its answers match the scripted-table run (D-20261028-103747).
 *
 * The script's stick state has the shape of the host layer's joystick read (axes 0..65535, centre 32768,
 * a button bit mask, the POV in hundredths of a degree or 65535 centred), and its pad state is a pad_state,
 * so a host on any layer can return them from its own table.
 *
 * -at_flight pad|joystick|mouse sets the Game Options flight choice in-process (GameOptions.h, D-20261102-200107) and
 * checks the readers it turns off are ignored: with Pad or Mouse the joystick script's stick and buttons move no
 * control (the axes read centred, ci and the fire counts stay 0; joy_down and the like still see the stick), with
 * Joystick or Mouse the pad script's pad still makes its output but moves no control.  -at_input both injects the
 * scripted joystick and pad together (the pad connected from start), and a mission with either note then runs both
 * scripts.  Run live only: -at_flight in a recording or replay is a FATAL failure.
 *   -autotest -at_mission AutotestJoy -at_input joy -at_flight pad ...
 *   -autotest -at_mission AutotestPad -at_input pad -at_flight joystick ...
 *   -autotest -at_mission AutotestJoy -at_input both -at_flight mouse ...
 *
 * -at_flight cycle, with -at_input both, takes the six live device switches (D-20261102-204804) in one run in place of the
 * two scripts: Pad -> Joystick -> Mouse -> Pad -> Mouse -> Joystick -> Pad, one segment each.  A segment switches through
 * game_options_set_flight_device (the choice, Use_mouse_to_fly and joy_reassign, as the Options screen's row), checks the
 * ownership it left (joy_num_sticks, Use_mouse_to_fly, which reader flies), then moves the stick with its fire button down,
 * then the pad with its fire trigger down, and checks only the chosen device reached the controls.  One line per
 * transition, "Flight switch: <from> -> <to> live: ... PASS", and a last "Flight switch: 6 of 6 transitions passed ... PASS".
 *   -autotest -at_mission AutotestJoy -at_input both -at_flight cycle -at_pilot AutotestPilot -at_time 60 (tools\flight_switch_check.ps1)
 */

#ifndef _FS2_INPUT_TEST_H
#define _FS2_INPUT_TEST_H

#include "../Pad/PadState.h"

typedef struct input_test_joy_state {
	unsigned int axes[6];
	unsigned int buttons;
	unsigned int pov;
} input_test_joy_state;

// -at_input joyhost / padhost: the script's state for the host to feed into its own input (see the file comment)
typedef struct input_test_script {
	int kind;							// 1 stick (joy), 2 pad (pad)
	input_test_joy_state joy;
	pad_state pad;
} input_test_script;
void input_test_set_host_sink(void (*sink)(const input_test_script *script));	// the host's sink, before the game starts

void input_test_set_cmdline(char *what);	// -at_input (called from parse_cmdline, with -autotest)
void input_test_set_flight(char *what);		// -at_flight (called from parse_cmdline, with -autotest)
void input_test_joy_read(input_test_joy_state *st);	// the scripted stick's state now

void input_test_mission_start();			// first gameplay frame under autotest rules (autotest_frame)
void input_test_frame();					// each gameplay frame under autotest rules, after autotest_frame's own work
int  input_test_failed();					// 1 if a check failed, or a script did not finish

#endif
