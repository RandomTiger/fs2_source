// fs2_api.h
// The plain C API of fs2game.dll (D-20261022-143922): a thin layer over game_host_init / game_host_frame /
// game_host_shutdown and the host_hooks table. No windows.h, no engine type, no C++ here. Nothing allocated on one
// side of the DLL boundary is freed on the other: the caller's strings are copied, the hook table is plain function
// pointers and PODs.

#ifndef FS2_API_H
#define FS2_API_H

#include "render_sink.h"

#ifdef __cplusplus
extern "C" {
#endif

// a joystick read, as host_joy_state in code/OsApi/HostHooks.h
typedef struct fs2_joy_state {
	unsigned int	axes[6];
	unsigned int	buttons;
	unsigned int	pov;
} fs2_joy_state;

typedef struct fs2_joy_caps {
	char	name[32];
	int	has_z, has_r, has_u, has_v;
} fs2_joy_caps;

// a control pad's state, as pad_state in code/Pad/PadState.h: 15 buttons and 6 axes in the standard gamepad order (Godot's
// JoyButton and JoyAxis order), sticks -1..1 with +y down, triggers 0..1
#define FS2_PAD_BUTTONS 15
#define FS2_PAD_AXES 6
typedef struct fs2_pad_state {
	int	connected;
	float	axis[FS2_PAD_AXES];
	int	button[FS2_PAD_BUTTONS];
} fs2_pad_state;

// as host_hooks in code/OsApi/HostHooks.h. Every entry must be filled.
typedef struct fs2_hooks {
	long long	(*time_counter)(void);
	long long	(*time_frequency)(void);
	void		(*poll_events)(void);
	int		(*is_foreground)(void);
	void		(*mouse_get_pos)(int *x, int *y);
	void		(*mouse_set_pos)(int x, int y);
	int		(*joy_count)(void);
	int		(*joy_caps)(int stick, fs2_joy_caps *caps);
	int		(*joy_read)(int stick, fs2_joy_state *state);
	int		(*pad_poll)(fs2_pad_state *out);
} fs2_hooks;

// Version of this API; bumped when a function or struct changes.
#define FS2_API_VERSION 7

#ifdef FS2GAME_EXPORTS
#define FS2_API __declspec(dllexport)
#else
#define FS2_API
#endif

FS2_API int fs2_api_version(void);

// Use these hooks instead of the built-in ones. Call before fs2_init; the table is copied. NULL, or any empty
// entry, returns 0 and changes nothing. Without a call the built-in host is used: the headless one when the
// command line has -headless, else the Windows one.
FS2_API int fs2_set_hooks(const fs2_hooks *hooks);

// R1-5: the render sink (render_sink.h) that shows the game's pixels. Call before fs2_init; the table is copied. With a valid
// table the game draws through the host renderer (GR_HOST); with NULL, or a table of another version or with an empty entry,
// it returns 0 and the game keeps the null renderer (GR_DUMMY) as before. Under -headless the host renderer still reports "no
// device" like the null renderer, so a compared run takes the same branches.
FS2_API int fs2_set_render_sink(const fs2_render_sink *sink);

// Input injection tests (D-20261028-103747): with -autotest -at_input joyhost or padhost the scripted stick or pad is not put in the
// game's host table; the game hands each frame's scripted state to this sink instead, and the host feeds it into its own input, so the
// game reads it back through the host's real joystick or pad hooks. kind is 1 for a stick (joy is valid), 2 for a pad (pad is valid).
// Called on the game's thread, once at start-up with the idle state and then after each gameplay frame. Call before fs2_init; NULL clears.
typedef struct fs2_input_script {
	int		kind;
	fs2_joy_state	joy;
	fs2_pad_state	pad;
} fs2_input_script;
FS2_API int fs2_set_input_script_sink(void (*sink)(const fs2_input_script *script));

// Start the game: cmdline is the game's command line (copied), root_dir the game's install folder (copied, no
// trailing slash; the current directory is set to it). Returns 1 when the game is running, 0 when it failed.
FS2_API int fs2_init(const char *cmdline, const char *root_dir);

// Run one frame. Returns 1 to go on, 0 when the game has ended (then call fs2_shutdown), or -1 when a fatal Error() was
// handed to the host (see fs2_set_fatal_return; then read fs2_last_error and call fs2_shutdown).
FS2_API int fs2_frame(void);

// Fatal errors (P1, D-20261022-143927). Off by default: an Error() ends the process with the game's exit code, as the
// exe does. With on = 1 an Error() in fs2_init or fs2_frame leaves the game at once and returns control to the caller:
// fs2_init returns 0, fs2_frame returns -1, and fs2_last_error has the message. The game never continues after it. The
// caller then calls fs2_shutdown, which closes what it can, and may call fs2_init again. Asserts and warnings are
// unchanged. Provisional shape, see godot_questions.md.
FS2_API void fs2_set_fatal_return(int on);

// The text of the last Error() that fs2_set_fatal_return handed back ("" if none). Valid until the next one.
FS2_API const char *fs2_last_error(void);

// G3: run the game as a coroutine on a fiber (P6, GameCtx). mode 0 = off (the default), 1 = on, 2 = stress (also yields at
// every os_poll). Call before fs2_init. With it on, fs2_init only prepares the game; init, every frame and the shutdown
// run on the fiber, and each fs2_frame resumes the game until its next yield (gr_flip, or after several os_polls with
// no flip), so one call is one host frame, not one game frame. fs2_frame returns 1 while the game runs, 0 when it has
// ended, -1 on a fatal Error() handed back or when game init failed (fs2_last_error has the text). fs2_shutdown then
// frees the fiber and returns the game's exit code.
FS2_API void fs2_set_fiber(int mode);

// R1-feel: optional (the API version stays 5). With on = 1 the host paces the frames by the wall clock, so the game no longer sleeps to its
// 120 fps cap; the cap still sets Frametime (1/120 s per frame), so game time and recorded behaviour are unchanged. Call before fs2_init.
FS2_API void fs2_set_host_pacing(int on);

// v7 (D-20261029-060953): the native window handle (an HWND, as an integer) that DirectSound takes its cooperative level on, in place of the
// game's own window. Call before fs2_init; 0 clears it, and unset (the default) leaves the game's behaviour unchanged. The host owns the window:
// the game only hands the value to DirectSound and never subclasses, pumps, resizes or destroys it, and fs2_shutdown closes the sound before
// returning, so the host may release the window after it. For interactive shown play only: a run with -headless or -nosounddevice has no use for it.
// When set, the game logs one "fs2api: sound" line to stderr once it is up, with the handle and what DirectSound made of it.
FS2_API void fs2_set_window_handle(unsigned long long handle);

// G4: the host's events go into the game through these sinks, the ones the Windows window procedure calls. Call them only from
// inside the poll_events hook (that is where the game takes the host's events, and the recorder puts them at the same point). A
// recording being replayed ignores them; a recording being made stores them.
// Keys: code is the game's key code (the DirectInput scan code, +0x80 for an extended key, as KEY_* in Key.h), state 1 down and 0
// up, latency_ms the time since the key was pressed. Right shift and right alt are the caller's to fold into the left ones.
#define FS2_MOUSE_LEFT		1
#define FS2_MOUSE_RIGHT		2
#define FS2_MOUSE_MIDDLE	4
FS2_API void fs2_input_key(unsigned int code, int state, unsigned int latency_ms);
// A mouse button (FS2_MOUSE_*) went down (set = 1) or up (set = 0).
FS2_API void fs2_input_mouse_button(unsigned int button, int set);
// The game window got (1) or lost (0) the keyboard focus.
FS2_API void fs2_input_focus(int got);
// The player closed the game window: the game quits as it does for WM_CLOSE.
FS2_API void fs2_input_close(void);
// The size of the game's screen in pixels, for mapping the host window's cursor onto it. 0 and 0 before fs2_init.
FS2_API void fs2_screen_size(int *w, int *h);

// Shut the game down. Returns the game's exit code (0 is a pass for -autotest).
FS2_API int fs2_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
