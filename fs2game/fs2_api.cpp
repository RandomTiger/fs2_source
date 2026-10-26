// fs2_api.cpp
// The plain C API of fs2game.dll over game_host_* (D-20261022-143922). The start-up checks that the exe's
// WinMainSub does before game_host_init are done here, once per process.

#include <windows.h>
#include <direct.h>
#include <string.h>

#include "pstypes.h"
#include "SystemVars.h"
#include "HostHooks.h"
#include "FreeSpace.h"
#include "Replay.h"
#include "fs2_api.h"

static host_hooks Api_hooks;
static int Api_hooks_set = 0;
static int Api_vm_ready = 0;
static int Api_running = 0;

// the -headless option as a whole token, as WinMainSub looks for it
static int api_cmdline_has(const char *cmdline, const char *option)
{
	size_t n = strlen(option);
	const char *p = cmdline;
	while ( (p = strstr(p, option)) != NULL ) {
		int before = (p == cmdline) || (p[-1] == ' ') || (p[-1] == '\t');
		int after = (p[n] == 0) || (p[n] == ' ') || (p[n] == '\t');
		if ( before && after ) {
			return 1;
		}
		p += n;
	}
	return 0;
}

extern "C" {

int fs2_api_version(void)
{
	return FS2_API_VERSION;
}

int fs2_set_hooks(const fs2_hooks *h)
{
	if ( (h == NULL) || (h->time_counter == NULL) || (h->time_frequency == NULL) || (h->poll_events == NULL)
		|| (h->is_foreground == NULL) || (h->mouse_get_pos == NULL) || (h->mouse_set_pos == NULL)
		|| (h->joy_count == NULL) || (h->joy_caps == NULL) || (h->joy_read == NULL) ) {
		return 0;
	}
	// the two structs have the same layout; the function pointers differ only in their types' names
	Api_hooks.time_counter = (longlong (*)())h->time_counter;
	Api_hooks.time_frequency = (longlong (*)())h->time_frequency;
	Api_hooks.poll_events = h->poll_events;
	Api_hooks.is_foreground = h->is_foreground;
	Api_hooks.mouse_get_pos = h->mouse_get_pos;
	Api_hooks.mouse_set_pos = h->mouse_set_pos;
	Api_hooks.joy_count = h->joy_count;
	Api_hooks.joy_caps = (int (*)(int, host_joy_caps *))h->joy_caps;
	Api_hooks.joy_read = (int (*)(int, host_joy_state *))h->joy_read;
	Api_hooks_set = 1;
	return 1;
}

int fs2_init(const char *cmdline, const char *root_dir)
{
	char cmd[4096], root[1024];

	if ( Api_running || (cmdline == NULL) || (root_dir == NULL) ) {
		return 0;
	}
	strncpy(cmd, cmdline, sizeof(cmd) - 1);
	cmd[sizeof(cmd) - 1] = 0;
	strncpy(root, root_dir, sizeof(root) - 1);
	root[sizeof(root) - 1] = 0;

	if ( Api_hooks_set ) {
		host_hooks_set(&Api_hooks);
	} else {
		host_hooks_set(api_cmdline_has(cmd, "-headless") ? &host_hooks_headless : &host_hooks_windows);
	}

	replay_wait_for_restart();

	if ( !Api_vm_ready ) {
		MEMORYSTATUS ms;
		ms.dwLength = sizeof(MEMORYSTATUS);
		GlobalMemoryStatus(&ms);
		Freespace_total_ram = (uint)ms.dwTotalPhys;
		if ( !vm_init(24*1024*1024) ) {
			return 0;
		}
		Api_vm_ready = 1;
	}

	if ( _chdir(root) != 0 ) {
		return 0;
	}

	if ( !game_host_init(cmd, root) ) {
		return 0;
	}
	Api_running = 1;
	return 1;
}

int fs2_frame(void)
{
	if ( !Api_running ) {
		return 0;
	}
	return game_host_frame();
}

int fs2_shutdown(void)
{
	if ( !Api_running ) {
		return 1;
	}
	Api_running = 0;
	return game_host_shutdown();
}

}	// extern "C"
