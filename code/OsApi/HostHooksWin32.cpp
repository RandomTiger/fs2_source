// HostHooksWin32.cpp
// Claude: the Windows implementations of the platform seam (D-051), the code the game used directly before.

#include <windows.h>
#include <mmsystem.h>

#include "HostHooks.h"
#include "OsApi.h"

static longlong host_win32_time_counter()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

static longlong host_win32_time_frequency()
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	return f.QuadPart;
}

static void host_win32_mouse_get_pos(int *x, int *y)
{
	POINT pnt;
	GetCursorPos(&pnt);
	ScreenToClient((HWND)os_get_window(), &pnt);
	*x = pnt.x;
	*y = pnt.y;
}

static void host_win32_mouse_set_pos(int x, int y)
{
	POINT pnt;
	pnt.x = x;
	pnt.y = y;
	ClientToScreen((HWND)os_get_window(), &pnt);
	SetCursorPos(pnt.x, pnt.y);
}

static int host_win32_joy_count()
{
	return joyGetNumDevs();
}

static int host_win32_joy_caps(int stick, host_joy_caps *caps)
{
	JOYCAPS jc;
	if (joyGetDevCaps(stick, &jc, sizeof(jc)) != JOYERR_NOERROR) {
		return 1;
	}
	strncpy(caps->name, jc.szPname, sizeof(caps->name) - 1);
	caps->name[sizeof(caps->name) - 1] = 0;
	caps->has_z = (jc.wCaps & JOYCAPS_HASZ) ? 1 : 0;
	caps->has_r = (jc.wCaps & JOYCAPS_HASR) ? 1 : 0;
	caps->has_u = (jc.wCaps & JOYCAPS_HASU) ? 1 : 0;
	caps->has_v = (jc.wCaps & JOYCAPS_HASV) ? 1 : 0;
	return 0;
}

static int host_win32_joy_read(int stick, host_joy_state *state)
{
	JOYINFOEX ji;
	memset(&ji, 0, sizeof(ji));
	ji.dwSize = sizeof(ji);
	ji.dwFlags = JOY_RETURNALL;
	MMRESULT rs = joyGetPosEx(stick, &ji);
	if (rs != JOYERR_NOERROR) {
		return (int)rs;
	}
	state->axes[0] = ji.dwXpos;
	state->axes[1] = ji.dwYpos;
	state->axes[2] = ji.dwZpos;
	state->axes[3] = ji.dwRpos;
	state->axes[4] = ji.dwUpos;
	state->axes[5] = ji.dwVpos;
	state->buttons = ji.dwButtons;
	state->pov = ji.dwPOV;
	return 0;
}

// OsApi.cpp: the window procedure's pump and foreground flag
void os_win32_poll_events();
int os_win32_is_foreground();

const host_hooks host_hooks_windows = {
	host_win32_time_counter,
	host_win32_time_frequency,
	os_win32_poll_events,
	os_win32_is_foreground,
	host_win32_mouse_get_pos,
	host_win32_mouse_set_pos,
	host_win32_joy_count,
	host_win32_joy_caps,
	host_win32_joy_read,
};

static void host_headless_poll_events()
{
}

static int host_headless_is_foreground()
{
	return 1;
}

static void host_headless_mouse_get_pos(int *x, int *y)
{
	*x = 0;
	*y = 0;
}

static void host_headless_mouse_set_pos(int x, int y)
{
}

// the slots WinMM reports with no stick attached, so a replay that recorded a joystick still finds a driver
static int host_headless_joy_count()
{
	return 16;
}

static int host_headless_joy_caps(int stick, host_joy_caps *caps)
{
	return 1;
}

static int host_headless_joy_read(int stick, host_joy_state *state)
{
	return 1;
}

const host_hooks host_hooks_headless = {
	host_win32_time_counter,
	host_win32_time_frequency,
	host_headless_poll_events,
	host_headless_is_foreground,
	host_headless_mouse_get_pos,
	host_headless_mouse_set_pos,
	host_headless_joy_count,
	host_headless_joy_caps,
	host_headless_joy_read,
};
