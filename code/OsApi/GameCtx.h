// GameCtx.h
// Claude: P6, D-20261022-065224. The game as a coroutine: a host resumes it once per host frame and the game
// gives control back at gr_flip (or at os_poll, for a wait loop that never flips).  The host calls game code only
// by resuming it.  Two calls make the seam (game_ctx_resume, game_ctx_yield); the Windows backend is an OS fiber
// with FIBER_FLAG_FLOAT_SWITCH, another backend can sit behind the same calls.  No Windows types here.

#ifndef _GAME_CTX_H
#define _GAME_CTX_H

// Starts the game context: proc(arg) runs on its own stack the first time game_ctx_resume is called.
// stress: 1 to yield at every os_poll as well, 0 to yield at gr_flip and after GAME_CTX_POLL_LIMIT polls without a flip.
// Turns the calling thread into the host's context.  Returns 1 on success.
int game_ctx_start(void (*proc)(void *), void *arg, int stack_bytes, int stress);

// Host side: runs the game until its next yield.  Returns 1 if the game is still running, 0 once proc has returned.
int game_ctx_resume();

// Host side, after resume returned 0 (or to abandon the game): frees the context.
void game_ctx_end();

// Game side: gives control back to the host.  Does nothing when the game is not running as a coroutine.
void game_ctx_yield();

// Game side: called at gr_flip, and at os_poll.  They count, then yield as the rules above say; both do
// nothing when the game is not running as a coroutine.
void game_ctx_flip_yield();
void game_ctx_poll_yield();

// 1 while the game runs as a coroutine
int game_ctx_active();

// yields so far, for the host to log
int game_ctx_flip_yields();
int game_ctx_poll_yields();

#endif
