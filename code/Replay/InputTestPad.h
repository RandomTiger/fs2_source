/*
 * Claude: included by PadGame.cpp only, after PadXInput.h.  Sends its pad backend reads to InputTest.cpp,
 * which passes them straight on to the backend unless -at_input pad is on (see InputTest.h).
 */

#ifndef _FS2_INPUT_TEST_PAD_H
#define _FS2_INPUT_TEST_PAD_H

int input_test_pad_poll(pad_state *out);

#define pad_backend_poll	input_test_pad_poll

#endif
