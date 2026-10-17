// HostHooksWin32.cpp
// Claude: the Windows implementations of the platform seam (D-051), the code the game used directly before.

#include <windows.h>

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

const host_hooks host_hooks_headless = {
	host_win32_time_counter,
	host_win32_time_frequency,
	host_headless_poll_events,
	host_headless_is_foreground,
	host_headless_mouse_get_pos,
	host_headless_mouse_set_pos,
};
