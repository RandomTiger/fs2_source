// GameCtx.cpp
// Claude: P6, see GameCtx.h.  Windows fiber backend.

#include <windows.h>
#include <float.h>
#include "GameCtx.h"

#define GAME_CTX_POLL_LIMIT	16		// os_poll calls with no flip before the game yields

static void *Ctx_host = NULL;		// the host's fiber (the thread converted)
static void *Ctx_game = NULL;		// the game's fiber
static int Ctx_converted = 0;		// the host thread was converted by us
static int Ctx_done = 0;
static int Ctx_stress = 0;
static int Ctx_polls = 0;			// os_poll calls since the last yield
static int Ctx_flip_yields = 0;
static int Ctx_poll_yields = 0;
static unsigned int Ctx_fpu = 0;	// the host's floating point control word, set again at fiber entry
static void (*Ctx_proc)(void *) = NULL;
static void *Ctx_arg = NULL;

static VOID CALLBACK game_ctx_entry(PVOID)
{
#ifdef _WIN64
	_control87(Ctx_fpu, _MCW_DN | _MCW_EM | _MCW_RC);	// x64: the infinity and precision controls do not exist
#else
	_control87(Ctx_fpu, _MCW_DN | _MCW_EM | _MCW_IC | _MCW_RC | _MCW_PC);
#endif
	Ctx_proc(Ctx_arg);
	// a fiber procedure that returns ends the thread, so switch back for good
	Ctx_done = 1;
	for (;;) {
		SwitchToFiber(Ctx_host);
	}
}

int game_ctx_start(void (*proc)(void *), void *arg, int stack_bytes, int stress)
{
	Ctx_host = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
	if (Ctx_host == NULL) {
		// already a fiber (a second start in one process)
		Ctx_host = GetCurrentFiber();
		Ctx_converted = 0;
	} else {
		Ctx_converted = 1;
	}
	Ctx_proc = proc;
	Ctx_arg = arg;
	Ctx_done = 0;
	Ctx_stress = stress;
	Ctx_polls = 0;
	Ctx_flip_yields = 0;
	Ctx_poll_yields = 0;
	Ctx_fpu = _control87(0, 0);
	Ctx_game = CreateFiberEx(0, (SIZE_T)stack_bytes, FIBER_FLAG_FLOAT_SWITCH, game_ctx_entry, NULL);
	return (Ctx_game != NULL);
}

int game_ctx_resume()
{
	if (Ctx_done) {
		return 0;
	}
	SwitchToFiber(Ctx_game);
	return !Ctx_done;
}

void game_ctx_end()
{
	if (Ctx_game) {
		DeleteFiber(Ctx_game);
		Ctx_game = NULL;
	}
	if (Ctx_converted) {
		ConvertFiberToThread();
		Ctx_converted = 0;
	}
	Ctx_host = NULL;
}

int game_ctx_active()
{
	return (Ctx_game != NULL) && (GetCurrentFiber() == Ctx_game);
}

void game_ctx_yield()
{
	if (game_ctx_active()) {
		Ctx_polls = 0;
		SwitchToFiber(Ctx_host);
	}
}

void game_ctx_flip_yield()
{
	if (game_ctx_active()) {
		Ctx_flip_yields++;
		game_ctx_yield();
	}
}

void game_ctx_poll_yield()
{
	if (game_ctx_active()) {
		Ctx_polls++;
		if (Ctx_stress || (Ctx_polls >= GAME_CTX_POLL_LIMIT)) {
			Ctx_poll_yields++;
			game_ctx_yield();
		}
	}
}

int game_ctx_flip_yields()
{
	return Ctx_flip_yields;
}

int game_ctx_poll_yields()
{
	return Ctx_poll_yields;
}
