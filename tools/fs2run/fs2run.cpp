// fs2run.exe: the native runner for fs2game.dll (P5). Loads the DLL, runs the game in it the way the exe's WinMain
// does, and exits with the game's exit code. No Godot.
//
//   fs2run.exe <fs2game dll> <game root folder> <command line> [<command line for a second run>]
//
// The command line is one argument, as the game's own (for example "-window -autotest -headless -replay r.fs2rec
// -trace t.trace"). A second command line runs after the first in the same process (init, shutdown, init), the
// restart check of D-055. The second run is skipped when the first failed.

#include <stdio.h>
#include "fs2_loader.h"

static int run_once(Fs2Api &api, const char *cmdline, const char *root)
{
	if ( !api.init(cmdline, root) ) {
		fprintf(stderr, "fs2run: fs2_init failed\n");
		return 1;
	}
	while ( api.frame() ) {
	}
	return api.shutdown();
}

int main(int argc, char **argv)
{
	if ( argc < 4 ) {
		fprintf(stderr, "usage: fs2run <fs2game dll> <game root> <command line> [<second command line>]\n");
		return 2;
	}
	Fs2Api api;
	if ( !api.load(argv[1]) ) {
		fprintf(stderr, "fs2run: cannot load %s (error %lu)\n", argv[1], GetLastError());
		return 2;
	}
	int code = run_once(api, argv[3], argv[2]);
	if ( (code == 0) && (argc > 4) ) {
		code = run_once(api, argv[4], argv[2]);
	}
	return code;
}
