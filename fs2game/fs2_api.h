// fs2_api.h
// The plain C API of fs2game.dll (D-20261022-143922): a thin layer over game_host_init / game_host_frame /
// game_host_shutdown and the host_hooks table. No windows.h, no engine type, no C++ here. Nothing allocated on one
// side of the DLL boundary is freed on the other: the caller's strings are copied, the hook table is plain function
// pointers and PODs.

#ifndef FS2_API_H
#define FS2_API_H

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
} fs2_hooks;

// Version of this API; bumped when a function or struct changes.
#define FS2_API_VERSION 3

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

// Shut the game down. Returns the game's exit code (0 is a pass for -autotest).
FS2_API int fs2_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
