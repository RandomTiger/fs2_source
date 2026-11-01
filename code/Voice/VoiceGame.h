/*
 * Voice control for squad orders: the game side.  Push-to-talk, say a target ("Alpha 2",
 * "Beta wing", "all fighters") and the squad menu opens at it; then say an order the menu
 * shows.  The recognised choice is recorded and replayed, so a replay needs no microphone and
 * no speech DLL.  Single player only.  See docs/VOICE_CONTROL.md.
 */

#ifndef _FS2_VOICEGAME_H
#define _FS2_VOICEGAME_H

void voice_game_init();		// game_init: reads [Voice] in fs2.ini, loads the engine if enabled
void voice_game_close();	// game_shutdown
void voice_frame();			// each mission frame, from game_process_keys before button_info_do

#endif
