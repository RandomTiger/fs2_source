// HostFatal.h
// Claude: fatal errors to the host (P1, D-20261022-143927). Error(), Warning() and asserts tell the host before they
// do anything else (after an autotest has logged them). With no callback set, which is the exe's case, nothing here
// changes: the dialog, exit(1), TerminateProcess and the autotest exit codes are as they were. No Windows or engine types.
//
// The callback never returns into game code for an Error: if it returns for one, Error() carries on to the process
// exit, so a host must leave through its own means (a fiber switch after G3, or a jump out of the call into the host
// for a test). For an Assert or a Warning, returning nonzero means handled: the game carries on, with no dialog.
// Returning 0 means not handled: the built-in behaviour follows.

#ifndef _HOST_FATAL_H
#define _HOST_FATAL_H

enum {
	HOST_FATAL_ASSERT = 0,
	HOST_FATAL_WARNING = 1,
	HOST_FATAL_ERROR = 2
};

typedef int (*host_fatal_fn)(int kind, const char *text, const char *file, int line);

// Set the callback, or NULL for none. Returns the previous one.
host_fatal_fn host_fatal_set(host_fatal_fn fn);

// Called by Error(), Warning() and WinAssert(); 0 when no callback is set or it did not handle it.
int host_fatal_notify(int kind, const char *text, const char *file, int line);

#endif
