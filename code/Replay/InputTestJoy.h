/*
 * Claude: included by Joy.cpp only, after windows.h.  Sends its WinMM joystick calls to InputTest.cpp,
 * which passes them straight on to WinMM unless -at_input joy is on (see InputTest.h).
 */

#ifndef _FS2_INPUT_TEST_JOY_H
#define _FS2_INPUT_TEST_JOY_H

UINT input_test_joyGetNumDevs();
MMRESULT input_test_joyGetDevCaps(UINT_PTR id, JOYCAPS *caps, UINT size);
MMRESULT input_test_joyGetPosEx(UINT id, JOYINFOEX *ji);

#undef joyGetDevCaps
#define joyGetNumDevs	input_test_joyGetNumDevs
#define joyGetDevCaps	input_test_joyGetDevCaps
#define joyGetPosEx		input_test_joyGetPosEx

#endif
