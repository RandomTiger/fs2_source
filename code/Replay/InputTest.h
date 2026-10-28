/*
 * Claude: scripted input tests for the joystick (D-20261028-094624).
 *
 * Usage, when recording:
 *   -autotest -at_mission AutotestJoy -at_input joy -at_pilot AutotestPilot -record ...
 *
 * The checks assume the default controls and options of a new pilot; a new scratch pilot has them, a
 * player's own pilot may not (the checks say so).
 *
 * -at_input joy puts a scripted joystick in place of the WinMM one, from game start, so joy_init finds
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
 * The script's stick state has the shape of the host layer's joystick read (axes 0..65535, centre 32768,
 * a button bit mask, the POV in hundredths of a degree or 65535 centred), so a host table can return it
 * on the layers that read the stick through the host.
 */

#ifndef _FS2_INPUT_TEST_H
#define _FS2_INPUT_TEST_H

typedef struct input_test_joy_state {
	unsigned int axes[6];
	unsigned int buttons;
	unsigned int pov;
} input_test_joy_state;

void input_test_set_cmdline(char *what);	// -at_input (called from parse_cmdline, with -autotest)
int  input_test_joy_injected();				// -at_input joy: the scripted stick stands in for the real one
void input_test_joy_read(input_test_joy_state *st);	// the scripted stick's state now

void input_test_mission_start();			// first gameplay frame under autotest rules (autotest_frame)
void input_test_frame();					// each gameplay frame under autotest rules, after autotest_frame's own work
int  input_test_failed();					// 1 if a check failed, or a script did not finish

#endif
