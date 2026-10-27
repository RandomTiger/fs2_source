// fs2run.exe: the native runner for fs2game.dll (P5). Loads the DLL, runs the game in it the way the exe's WinMain
// does, and exits with the game's exit code. No Godot.
//
//   fs2run.exe <fs2game dll> <game root folder> <command line> [<command line for a second run>]
//
// The command line is one argument, as the game's own (for example "-window -autotest -headless -replay r.fs2rec
// -trace t.trace"). A second command line runs after the first in the same process (init, shutdown, init), the
// restart check of D-055. The second run is skipped when the first failed.

#include <stdio.h>
#include <stdlib.h>
#include "fs2_loader.h"

#define RUN_FATAL 100	// run_once: the game handed a fatal Error() back (P1)

static int run_once(Fs2Api &api, const char *cmdline, const char *root)
{
	if ( !api.init(cmdline, root) ) {
		fprintf(stderr, "fs2run: fs2_init failed\n");
		return 1;
	}
	int r;
	while ( (r = api.frame()) > 0 ) {
	}
	if ( r < 0 ) {
		fprintf(stderr, "fs2run: fatal error returned to the host: %s\n", api.last_error());
		api.shutdown();
		return RUN_FATAL;
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
	// P1 test: FS2_FATAL_RETURN=1 hands a fatal Error() of the first run back to this program (force one with
	// FS2_FORCE_ERROR_FRAME=<n>); the process lives on and the second command line runs. Exit 0 only if the first run ended
	// that way and the second passed.
	const char *fr = getenv("FS2_FATAL_RETURN");
	if ( fr && (fr[0] == '1') ) {
		api.set_fatal_return(1);
		if ( argc < 5 ) {
			fprintf(stderr, "fs2run: FS2_FATAL_RETURN needs a second command line\n");
			return 2;
		}
		int first = run_once(api, argv[3], argv[2]);
		if ( first != RUN_FATAL ) {
			fprintf(stderr, "fs2run: expected a fatal error in the first run, got exit %d\n", first);
			return 1;
		}
		fprintf(stderr, "fs2run: process still alive after the fatal error, starting the second run\n");
		return run_once(api, argv[4], argv[2]);
	}
	int code = run_once(api, argv[3], argv[2]);
	if ( (code == 0) && (argc > 4) ) {
		code = run_once(api, argv[4], argv[2]);
	}
	return code;
}
