// PadXInput.cpp
// Control pad backend for Windows XInput (360, One, Series and most third-party pads).
//
// XInput is loaded at run time, so the game still starts on a machine without it and
// needs no import library. XInputGetStateEx (ordinal 100) is used when present, because
// it is the only way to read the Guide button.

#include <windows.h>
#include <string.h>

#include "PadXInput.h"

// Copies of the XInput structs, so no particular SDK header is needed. Their layout is
// a fixed ABI.
typedef struct pad_xinput_gamepad {
	WORD	wButtons;
	BYTE	bLeftTrigger;
	BYTE	bRightTrigger;
	SHORT	sThumbLX;
	SHORT	sThumbLY;
	SHORT	sThumbRX;
	SHORT	sThumbRY;
} pad_xinput_gamepad;

typedef struct pad_xinput_state {
	DWORD						dwPacketNumber;
	pad_xinput_gamepad	Gamepad;
} pad_xinput_state;

typedef DWORD (WINAPI *pad_xinput_get_state_fn)(DWORD user_index, pad_xinput_state *state);

#define PAD_XINPUT_MAX_USERS		4
#define PAD_XINPUT_RESCAN_MS		1000		// how often to look for a pad when none is connected

#define XI_DPAD_UP			0x0001
#define XI_DPAD_DOWN			0x0002
#define XI_DPAD_LEFT			0x0004
#define XI_DPAD_RIGHT		0x0008
#define XI_START				0x0010
#define XI_BACK				0x0020
#define XI_LEFT_THUMB		0x0040
#define XI_RIGHT_THUMB		0x0080
#define XI_LEFT_SHOULDER	0x0100
#define XI_RIGHT_SHOULDER	0x0200
#define XI_GUIDE				0x0400		// only from XInputGetStateEx
#define XI_A					0x1000
#define XI_B					0x2000
#define XI_X					0x4000
#define XI_Y					0x8000

static int								Pad_xinput_loaded = 0;		// 0 = not tried, 1 = ok, -1 = failed
static pad_xinput_get_state_fn	Pad_xinput_get_state = NULL;
static int								Pad_xinput_user = -1;		// slot in use, -1 = none
static DWORD							Pad_xinput_next_scan = 0;

static void pad_xinput_load()
{
	static const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
	int i;

	Pad_xinput_loaded = -1;

	for (i = 0; i < sizeof(dlls) / sizeof(dlls[0]); i++) {
		HMODULE lib = LoadLibraryA(dlls[i]);
		if (lib == NULL) {
			continue;
		}

		// ordinal 100 = XInputGetStateEx (not in xinput9_1_0)
		Pad_xinput_get_state = (pad_xinput_get_state_fn) GetProcAddress(lib, (LPCSTR) 100);
		if (Pad_xinput_get_state == NULL) {
			Pad_xinput_get_state = (pad_xinput_get_state_fn) GetProcAddress(lib, "XInputGetState");
		}

		if (Pad_xinput_get_state != NULL) {
			mprintf(("Pad: using %s\n", dlls[i]));
			Pad_xinput_loaded = 1;
			return;
		}

		FreeLibrary(lib);
	}

	mprintf(("Pad: XInput not available, pad disabled\n"));
}

static float pad_xinput_stick(SHORT v)
{
	float f = (float) v / 32767.0f;
	return (f < -1.0f) ? -1.0f : f;
}

int pad_xinput_poll(pad_state *out)
{
	pad_xinput_state xs;
	WORD b;
	int i;

	memset(out, 0, sizeof(*out));
	memset(&xs, 0, sizeof(xs));

	if (Pad_xinput_loaded == 0) {
		pad_xinput_load();
	}

	if (Pad_xinput_loaded < 0) {
		return 0;
	}

	// keep reading the slot in use; probing empty slots is slow, so only rescan now and then
	if (Pad_xinput_user >= 0) {
		if (Pad_xinput_get_state(Pad_xinput_user, &xs) != ERROR_SUCCESS) {
			mprintf(("Pad: pad %d disconnected\n", Pad_xinput_user));
			Pad_xinput_user = -1;
			Pad_xinput_next_scan = 0;
		}
	}

	if (Pad_xinput_user < 0) {
		DWORD now = GetTickCount();
		if (now < Pad_xinput_next_scan) {
			return 0;
		}

		Pad_xinput_next_scan = now + PAD_XINPUT_RESCAN_MS;
		for (i = 0; i < PAD_XINPUT_MAX_USERS; i++) {
			if (Pad_xinput_get_state(i, &xs) == ERROR_SUCCESS) {
				Pad_xinput_user = i;
				mprintf(("Pad: pad %d connected\n", i));
				break;
			}
		}

		if (Pad_xinput_user < 0) {
			return 0;
		}
	}

	b = xs.Gamepad.wButtons;

	out->connected = 1;
	out->button[PAD_SOUTH]			= (b & XI_A) != 0;
	out->button[PAD_EAST]			= (b & XI_B) != 0;
	out->button[PAD_WEST]			= (b & XI_X) != 0;
	out->button[PAD_NORTH]			= (b & XI_Y) != 0;
	out->button[PAD_BACK]			= (b & XI_BACK) != 0;
	out->button[PAD_GUIDE]			= (b & XI_GUIDE) != 0;
	out->button[PAD_START]			= (b & XI_START) != 0;
	out->button[PAD_LSTICK]			= (b & XI_LEFT_THUMB) != 0;
	out->button[PAD_RSTICK]			= (b & XI_RIGHT_THUMB) != 0;
	out->button[PAD_LSHOULDER]		= (b & XI_LEFT_SHOULDER) != 0;
	out->button[PAD_RSHOULDER]		= (b & XI_RIGHT_SHOULDER) != 0;
	out->button[PAD_DPAD_UP]		= (b & XI_DPAD_UP) != 0;
	out->button[PAD_DPAD_DOWN]		= (b & XI_DPAD_DOWN) != 0;
	out->button[PAD_DPAD_LEFT]		= (b & XI_DPAD_LEFT) != 0;
	out->button[PAD_DPAD_RIGHT]	= (b & XI_DPAD_RIGHT) != 0;

	// XInput has +y up; the normalised state has +y down
	out->axis[PAD_LEFT_X]			= pad_xinput_stick(xs.Gamepad.sThumbLX);
	out->axis[PAD_LEFT_Y]			= -pad_xinput_stick(xs.Gamepad.sThumbLY);
	out->axis[PAD_RIGHT_X]			= pad_xinput_stick(xs.Gamepad.sThumbRX);
	out->axis[PAD_RIGHT_Y]			= -pad_xinput_stick(xs.Gamepad.sThumbRY);
	out->axis[PAD_TRIGGER_LEFT]	= (float) xs.Gamepad.bLeftTrigger / 255.0f;
	out->axis[PAD_TRIGGER_RIGHT]	= (float) xs.Gamepad.bRightTrigger / 255.0f;

	return 1;
}
