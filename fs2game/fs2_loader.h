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
	int	(*init)(const char *, const char *);
	int	(*frame)(void);
	int	(*shutdown)(void);
	void	(*set_fatal_return)(int);
	const char *(*last_error)(void);

	Fs2Api() : module(NULL), api_version(NULL), set_hooks(NULL), init(NULL), frame(NULL), shutdown(NULL), set_fatal_return(NULL), last_error(NULL) {}

	// 1 on success. The DLL's own folder is searched for anything it depends on.
	int load(const char *dll_path)
	{
		module = LoadLibraryExA(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if ( module == NULL ) {
			return 0;
		}
		api_version = (int (*)(void))GetProcAddress(module, "fs2_api_version");
		set_hooks = (int (*)(const fs2_hooks *))GetProcAddress(module, "fs2_set_hooks");
		init = (int (*)(const char *, const char *))GetProcAddress(module, "fs2_init");
		frame = (int (*)(void))GetProcAddress(module, "fs2_frame");
		shutdown = (int (*)(void))GetProcAddress(module, "fs2_shutdown");
		set_fatal_return = (void (*)(int))GetProcAddress(module, "fs2_set_fatal_return");
		last_error = (const char *(*)(void))GetProcAddress(module, "fs2_last_error");
		if ( !api_version || !set_hooks || !init || !frame || !shutdown || !set_fatal_return || !last_error || (api_version() != FS2_API_VERSION) ) {
			FreeLibrary(module);
			module = NULL;
			return 0;
		}
		return 1;
	}
};

#endif
