// HostHooksWin32.cpp
// Claude: the Windows implementations of the platform seam (D-051), the code the game used directly before.

#include <windows.h>

#include "HostHooks.h"

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

// OsApi.cpp: the window procedure's pump and foreground flag
void os_win32_poll_events();
int os_win32_is_foreground();

const host_hooks host_hooks_windows = {
	host_win32_time_counter,
	host_win32_time_frequency,
	os_win32_poll_events,
	os_win32_is_foreground,
};

static void host_headless_poll_events()
{
}

static int host_headless_is_foreground()
{
	return 1;
}

const host_hooks host_hooks_headless = {
	host_win32_time_counter,
	host_win32_time_frequency,
	host_headless_poll_events,
	host_headless_is_foreground,
};
