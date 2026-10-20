// PadXInput.h
// Control pad backend: reads an XInput pad into a normalised pad_state.
// The core never sees which API or which make of pad the state came from.

#ifndef __PAD_XINPUT_H__
#define __PAD_XINPUT_H__

#include "PadState.h"

// Fills *out from the first connected pad. Returns out->connected.
// Loads XInput on the first call. Safe to call every frame.
int pad_backend_poll(pad_state *out);

#endif
