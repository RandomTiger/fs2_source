// HostHooks.h
// Claude: the platform seam (D-051). The live reads the game makes of the machine go through this table, which
// the host fills in. The recorder's wraps stay on the game side of it (Timer.cpp, ...), so a recording cannot tell
// which host filled it. No Windows or engine types here.

#ifndef _HOST_HOOKS_H
#define _HOST_HOOKS_H

#include "pstypes.h"

typedef struct host_hooks {
	// time: a counter that only goes up, and its ticks per second (less than 2^32)
	longlong	(*time_counter)();
	longlong	(*time_frequency)();
} host_hooks;

// The table in use. It starts as the Windows one (host_hooks_windows), so tools that never set it still run.
extern host_hooks Host_hooks;

// Fill the table before game_host_init. A null entry is an Error.
void host_hooks_set(const host_hooks *h);

// The Windows implementations (HostHooksWin32.cpp)
extern const host_hooks host_hooks_windows;

#endif
