// fs2_api.cpp
// The plain C API of fs2game.dll over game_host_* (D-20261022-143922). The start-up checks that the exe's
// WinMainSub does before game_host_init are done here, once per process.

#include <windows.h>
#include <direct.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>

#include "pstypes.h"
#include "SystemVars.h"
#include "HostHooks.h"
#include "HostFatal.h"
#include "FreeSpace.h"
#include "Replay.h"
#include "GameSequence.h"
#include "GameCtx.h"
#include "Key.h"
#include "Mouse.h"
#include "2d.h"
#include "fs2_api.h"

extern int gr_host_attach_sink(const fs2_render_sink *sink);	// gr_host.cpp

static_assert(FS2_MOUSE_LEFT == MOUSE_LEFT_BUTTON, "FS2_MOUSE_* must match Mouse.h");
static_assert(FS2_MOUSE_RIGHT == MOUSE_RIGHT_BUTTON, "FS2_MOUSE_* must match Mouse.h");
static_assert(FS2_MOUSE_MIDDLE == MOUSE_MIDDLE_BUTTON, "FS2_MOUSE_* must match Mouse.h");

static host_hooks Api_hooks;
static int Api_hooks_set = 0;
static int Api_vm_ready = 0;
static int Api_running = 0;
static int Api_fatal_return = 0;
static int Api_fatal_armed = 0;
static int Api_fatal_hit = 0;	// an Error() was handed back: the game may be mid-mission
static jmp_buf Api_fatal_jmp;
static char Api_error_text[1024] = "";

// G3: the game as a coroutine (GameCtx, P6). Init, the frame loop and the shutdown run on the fiber.
static int Api_fiber_mode = 0;		// fs2_set_fiber: 0 off, 1 on, 2 stress
static int Api_fiber_on = 0;		// a fiber exists for the current run
static int Api_fiber_init_ok = 0;
static int Api_fiber_ended = 0;		// game_ctx_resume reported the fiber's procedure has returned
static int Api_fiber_code = 1;		// game_host_shutdown's result, when the fiber ran it
static char Api_fiber_cmd[4096], Api_fiber_root[1024];

static void api_fiber_proc(void *)
{
	// an Error() handed back by api_fatal longjmps here, on the fiber's own stack; the fiber then parks for good
	if ( Api_fatal_return && setjmp(Api_fatal_jmp) ) {
		for (;;) {
			game_ctx_yield();
		}
	}
	Api_fiber_init_ok = game_host_init(Api_fiber_cmd, Api_fiber_root);
	if ( !Api_fiber_init_ok ) {
		return;
	}
	while ( game_host_frame() ) {
	}
	Api_fiber_code = game_host_shutdown();
}

// the host_fatal callback (P1): an Error() while a call into the game is armed leaves for the caller, never back into the
// game. Anything else, or an Error() outside such a call, returns 0 and the built-in behaviour (process exit) follows.
static int api_fatal(int kind, const char *text, const char *file, int line)
{
	if ( (kind != HOST_FATAL_ERROR) || !Api_fatal_armed ) {
		return 0;
	}
	_snprintf(Api_error_text, sizeof(Api_error_text) - 1, "%s (%s line %d)", text, file ? file : "", line);
	Api_error_text[sizeof(Api_error_text) - 1] = 0;
	Api_fatal_armed = 0;
	Api_fatal_hit = 1;
	longjmp(Api_fatal_jmp, 1);
}

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

int fs2_set_render_sink(const fs2_render_sink *sink)
{
	return gr_host_attach_sink(sink);
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

	if ( Api_fiber_mode ) {
		// G3: the game runs on a fiber from the first fs2_frame on; init happens there
		memcpy(Api_fiber_cmd, cmd, sizeof(cmd) < sizeof(Api_fiber_cmd) ? sizeof(cmd) : sizeof(Api_fiber_cmd));
		memcpy(Api_fiber_root, root, sizeof(root));
		Api_fiber_init_ok = 0;
		Api_fiber_ended = 0;
		Api_fiber_code = 1;
		Api_fatal_hit = 0;
		host_fatal_set(Api_fatal_return ? api_fatal : NULL);
		Api_fatal_armed = 0;
		if ( !game_ctx_start(api_fiber_proc, NULL, 1024 * 1024, (Api_fiber_mode == 2)) ) {
			return 0;
		}
		Api_fiber_on = 1;
		Api_running = 1;
		return 1;
	}

	// P1: with fs2_set_fatal_return on, an Error() during init leaves here (the setjmp), the game half started; the
	// caller's fs2_shutdown closes it
	int started;
	host_fatal_set(Api_fatal_return ? api_fatal : NULL);
	Api_fatal_armed = 0;
	Api_running = 1;
	if ( Api_fatal_return && setjmp(Api_fatal_jmp) ) {
		return 0;
	}
	Api_fatal_armed = Api_fatal_return;
	started = game_host_init(cmd, root);
	Api_fatal_armed = 0;
	if ( !started ) {
		Api_running = 0;
		return 0;
	}
	return 1;
}

int fs2_frame(void)
{
	if ( !Api_running ) {
		return 0;
	}
	if ( Api_fiber_on ) {
		Api_fatal_armed = Api_fatal_return;
		int alive = game_ctx_resume();
		Api_fatal_armed = 0;
		if ( !alive ) {
			Api_fiber_ended = 1;
		}
		if ( Api_fatal_hit ) {
			return -1;
		}
		if ( !alive && !Api_fiber_init_ok ) {
			strcpy(Api_error_text, "game_host_init failed");
			return -1;
		}
		return alive ? 1 : 0;
	}
	int result;
	if ( Api_fatal_return && setjmp(Api_fatal_jmp) ) {
		return -1;
	}
	Api_fatal_armed = Api_fatal_return;
	result = game_host_frame();
	Api_fatal_armed = 0;
	return result;
}

// G4: the host's event sinks (fs2_api.h), the calls win32_message_handler makes for the same events
void fs2_input_key(unsigned int code, int state, unsigned int latency_ms)
{
	key_mark(code, state, latency_ms);
}

void fs2_input_mouse_button(unsigned int button, int set)
{
	mouse_mark_button(button, set);	// FS2_MOUSE_* are the game's MOUSE_*_BUTTON bits
}

void fs2_input_focus(int got)
{
	if ( got ) {
		key_got_focus();
	} else {
		key_lost_focus();
	}
}

void fs2_input_close(void)
{
	gameseq_post_event(GS_EVENT_QUIT_GAME);
}

void fs2_screen_size(int *w, int *h)
{
	*w = gr_screen.max_w;
	*h = gr_screen.max_h;
}

void fs2_set_fiber(int mode)
{
	Api_fiber_mode = (mode < 0) ? 0 : (mode > 2) ? 2 : mode;
}

void fs2_set_fatal_return(int on)
{
	Api_fatal_return = on ? 1 : 0;
}

const char *fs2_last_error(void)
{
	return Api_error_text;
}

int fs2_shutdown(void)
{
	if ( !Api_running ) {
		return 1;
	}
	Api_running = 0;
	Api_fatal_armed = 0;
	if ( Api_fiber_on && !Api_fatal_hit ) {
		int ended = Api_fiber_ended;
		int code;
		if ( ended && !Api_fiber_init_ok ) {
			code = 1;	// init failed: nothing to shut down, as without the fiber
		} else if ( ended ) {
			code = Api_fiber_code;	// the fiber ran game_host_shutdown
		} else {
			code = game_host_shutdown();	// abandoned mid-run: close it from the host's stack
		}
		fprintf(stderr, "fiber: %d flip yields, %d os_poll yields, game %s\n", game_ctx_flip_yields(), game_ctx_poll_yields(), ended ? "ended" : "abandoned");
		game_ctx_end();
		Api_fiber_on = 0;
		host_fatal_set(NULL);
		return code;
	}
	if ( Api_fatal_hit ) {
		// after a handed-back Error() the game is wherever it stopped, often mid-mission: walk it out through the game
		// sequence as a quit does, so the mission is closed before game_host_shutdown. A second Error() ends the walk.
		// A running recording or replay cannot follow that (its stream wants the next recorded input): end it first.
		Api_fatal_hit = 0;
		replay_abandon();
		if ( setjmp(Api_fatal_jmp) == 0 ) {
			Api_fatal_armed = 1;
			gameseq_post_event(GS_EVENT_QUIT_GAME);
			for ( int i = 0; (i < 16) && game_host_frame(); i++ ) {
			}
		}
		Api_fatal_armed = 0;
	}
	host_fatal_set(NULL);
	int code = game_host_shutdown();
	if ( Api_fiber_on ) {	// after a handed-back Error(): the fiber is parked mid-game, drop it
		game_ctx_end();
		Api_fiber_on = 0;
	}
	return code;
}

}	// extern "C"
