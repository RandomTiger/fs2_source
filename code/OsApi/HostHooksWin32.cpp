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

const host_hooks host_hooks_windows = {
	host_win32_time_counter,
	host_win32_time_frequency,
};
