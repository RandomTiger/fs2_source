// HostFatal.cpp
// Claude: fatal errors to the host (P1), see HostFatal.h.

#include "HostFatal.h"
#include <stddef.h>

static host_fatal_fn Host_fatal_fn = NULL;
static int Host_fatal_busy = 0;	// the callback raised another one: do not call it again

host_fatal_fn host_fatal_set(host_fatal_fn fn)
{
	host_fatal_fn old = Host_fatal_fn;
	Host_fatal_fn = fn;	// a longjmp out of the callback leaves busy set
	Host_fatal_busy = 0;
	return old;
}

int host_fatal_notify(int kind, const char *text, const char *file, int line)
{
	int handled;

	if ( (Host_fatal_fn == NULL) || Host_fatal_busy ) {
		return 0;
	}
	Host_fatal_busy = 1;
	handled = Host_fatal_fn(kind, text, file, line);
	Host_fatal_busy = 0;
	return handled;
}
