// fs2_loader.h
// Loads fs2game.dll at run time and looks up the C API of fs2_api.h. Used by tools/fs2run and the Godot wrapper,
// so both load the same DLL the same way (D-20261022-143922 iii). Header only; includes windows.h, so it is not
// part of the DLL's own API.

#ifndef FS2_LOADER_H
#define FS2_LOADER_H

#include <windows.h>
#include "fs2_api.h"

struct Fs2Api {
	HMODULE	module;
	int	(*api_version)(void);
	int	(*set_hooks)(const fs2_hooks *);
	int	(*set_render_sink)(const fs2_render_sink *);
	int	(*set_input_script_sink)(void (*)(const fs2_input_script *));	// optional
	int	(*init)(const char *, const char *);
	int	(*frame)(void);
	int	(*shutdown)(void);
	void	(*set_fatal_return)(int);
	void	(*set_fiber)(int);
	void	(*set_host_pacing)(int);	// optional
	void	(*set_window_handle)(unsigned long long);	// v7, optional
	const char *(*last_error)(void);
	void	(*input_key)(unsigned int, int, unsigned int);
	void	(*input_mouse_button)(unsigned int, int);
	void	(*input_focus)(int);
	void	(*input_close)(void);
	void	(*screen_size)(int *, int *);

	Fs2Api() : module(NULL), api_version(NULL), set_hooks(NULL), set_render_sink(NULL), set_input_script_sink(NULL), init(NULL), frame(NULL), shutdown(NULL), set_fatal_return(NULL), set_fiber(NULL), set_host_pacing(NULL), set_window_handle(NULL), last_error(NULL), input_key(NULL), input_mouse_button(NULL), input_focus(NULL), input_close(NULL), screen_size(NULL) {}

	// 1 on success. The DLL's own folder is searched for anything it depends on.
	int load(const char *dll_path)
	{
		module = LoadLibraryExA(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if ( module == NULL ) {
			return 0;
		}
		api_version = (int (*)(void))GetProcAddress(module, "fs2_api_version");
		set_hooks = (int (*)(const fs2_hooks *))GetProcAddress(module, "fs2_set_hooks");
		set_render_sink = (int (*)(const fs2_render_sink *))GetProcAddress(module, "fs2_set_render_sink");
		set_input_script_sink = (int (*)(void (*)(const fs2_input_script *)))GetProcAddress(module, "fs2_set_input_script_sink");
		init = (int (*)(const char *, const char *))GetProcAddress(module, "fs2_init");
		frame = (int (*)(void))GetProcAddress(module, "fs2_frame");
		shutdown = (int (*)(void))GetProcAddress(module, "fs2_shutdown");
		set_fatal_return = (void (*)(int))GetProcAddress(module, "fs2_set_fatal_return");
		set_fiber = (void (*)(int))GetProcAddress(module, "fs2_set_fiber");
		set_host_pacing = (void (*)(int))GetProcAddress(module, "fs2_set_host_pacing");
		set_window_handle = (void (*)(unsigned long long))GetProcAddress(module, "fs2_set_window_handle");
		last_error = (const char *(*)(void))GetProcAddress(module, "fs2_last_error");
		input_key = (void (*)(unsigned int, int, unsigned int))GetProcAddress(module, "fs2_input_key");
		input_mouse_button = (void (*)(unsigned int, int))GetProcAddress(module, "fs2_input_mouse_button");
		input_focus = (void (*)(int))GetProcAddress(module, "fs2_input_focus");
		input_close = (void (*)(void))GetProcAddress(module, "fs2_input_close");
		screen_size = (void (*)(int *, int *))GetProcAddress(module, "fs2_screen_size");
		if ( !input_key || !input_mouse_button || !input_focus || !input_close || !screen_size ) {
			FreeLibrary(module);
			module = NULL;
			return 0;
		}
		if ( !api_version || !set_hooks || !set_render_sink || !init || !frame || !shutdown || !set_fatal_return || !set_fiber || !last_error || (api_version() != FS2_API_VERSION) ) {
			FreeLibrary(module);
			module = NULL;
			return 0;
		}
		return 1;
	}
};

#endif
