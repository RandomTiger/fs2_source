/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/MissionUI/MissionPause.cpp $
 * $Revision: 7 $
 * $Date: 7/29/99 10:48p $
 * $Author: Dave $
 * 
 *
 * $Log: /Freespace2/code/MissionUI/MissionPause.cpp $
 * 
 * 7     7/29/99 10:48p Dave
 * Multiplayer pause screen.
 * 
 * 6     6/29/99 7:39p Dave
 * Lots of small bug fixes.
 * 
 * 5     6/09/99 2:17p Dave
 * Fixed up pleasewait bitmap rendering.
 * 
 *
 * $NoKeywords: $
 */

#include "MissionPause.h"
#include "ui.h"
#include "multi_pause.h"
#include "popup.h"
#include "2d.h"
#include "bmpman.h"
#include "key.h"
#include "audiostr.h"
#include "gamesequence.h"
#include "freespace.h"
#include "hud.h"
#include "object.h"
#include "font.h"
#include "Alphacolors.h"
#include "beam.h"
#include "cfile.h"	// Claude: Cfile_fs1_data
#include "../Pad/PadGame.h"	// Claude: control pad
#include "3d.h"		// Claude: debug camera (#125)
#include "timer.h"
#include "mouse.h"

// ----------------------------------------------------------------------------------------------------------------
// PAUSE DEFINES/VARS
//

// pause bitmap name
char *Pause_bmp_name[GR_NUM_RESOLUTIONS] = {
	"PleaseWait",
	"2_PleaseWait"
};

// pause bitmap display stuff
int Please_wait_coords[GR_NUM_RESOLUTIONS][4] = {
	{ // GR_640
		152, 217, 316, 26
	},
	{ // GR_1024
		247, 346, 510, 36
	}	
};

char *Pause_multi_fname[GR_NUM_RESOLUTIONS] = {
	"MPPause",
	"2_MPPause"
};
char *Pause_multi_mask[GR_NUM_RESOLUTIONS] = {
	"MPPause-m",
	"2_MPPause-m"
};

// pause window objects
UI_WINDOW Pause_win;
UI_CHECKBOX Pause_single_step;
UI_CHECKBOX Pause_physics;
UI_CHECKBOX Pause_ai;
UI_CHECKBOX Pause_ai_render;
UI_CHECKBOX Pause_firing;
UI_CHECKBOX Pause_external_view_mode_check;
UI_BUTTON Pause_continue;

// if we're already paused
int Paused = 0;

// background screen (for the chatbox)
int Pause_background_bitmap = -1;

// saved background screen
int Pause_saved_screen = -1;

// if we're in external vie wmode
int Pause_external_view_mode = 0;

// externs
extern int Player_attacking_enabled;
extern int Ai_render_debug_flag;
extern int Ai_firing_enabled;
extern int physics_paused;
extern int ai_paused;
extern int last_single_step;
extern int game_single_step;

// Claude: debug camera (#125).  Shift+Pause in a single-player mission pauses the game as the Pause key does
// and shows the frozen scene from a free camera: W/S forward and back, A/D strafe, the mouse looks, Shift
// moves faster, Shift+Pause or Esc leaves.  Only the view moves: no mission time passes and nothing is
// simulated.  The 3D view and each object's OF_WAS_RENDERED (which ship-ship collision reads) are put back
// on leaving, so the next game frame sees what it would have after a plain pause.  Nothing is saved to the
// pilot file.
int Pause_debug_camera = 0;		// set by the Shift+Pause key before GS_EVENT_PAUSE_GAME; cleared on leaving

#define DEBUG_CAMERA_SPEED			150.0f		// metres a second
#define DEBUG_CAMERA_FAST				6.0f			// with Shift held
#define DEBUG_CAMERA_LOOK				0.003f		// radians per mouse step

extern int Keep_mouse_centered;
extern matrix View_matrix, Unscaled_matrix, Light_matrix, Eye_matrix;
extern vector View_position, Light_base, Eye_position, Matrix_scale;
extern float View_zoom;
extern void obj_render_all(void (*render_function)(object *objp));
extern void obj_render(object *obj);
extern void beam_render_all();
extern void particle_render_all();
extern void trail_render_all();
extern void mflash_render_all();
extern void render_shields();
extern void nebl_render_all();
extern void neb2_render_setup(vector *eye_pos, matrix *eye_orient);
extern void neb2_render_player();
extern void stars_draw(int show_stars, int show_suns, int show_nebulas, int show_subspace);

static vector Dc_pos;
static angles Dc_angles;
static int Dc_last_ms;
static matrix Dc_save_view_matrix, Dc_save_unscaled_matrix, Dc_save_light_matrix, Dc_save_eye_matrix;
static vector Dc_save_view_position, Dc_save_light_base, Dc_save_eye_position, Dc_save_matrix_scale;
static float Dc_save_view_zoom;
static ubyte Dc_save_rendered[MAX_OBJECTS];

static void debug_camera_init()
{
	int i;

	Dc_save_view_matrix = View_matrix;
	Dc_save_unscaled_matrix = Unscaled_matrix;
	Dc_save_light_matrix = Light_matrix;
	Dc_save_eye_matrix = Eye_matrix;
	Dc_save_view_position = View_position;
	Dc_save_light_base = Light_base;
	Dc_save_eye_position = Eye_position;
	Dc_save_matrix_scale = Matrix_scale;
	Dc_save_view_zoom = View_zoom;
	for (i=0; i<MAX_OBJECTS; i++) {
		Dc_save_rendered[i] = (Objects[i].flags & OF_WAS_RENDERED) ? 1 : 0;
	}

	// start where the player's view was
	Dc_pos = Eye_position;
	vm_extract_angles_matrix(&Dc_angles, &Eye_matrix);
	Dc_last_ms = timer_get_milliseconds();
}

static void debug_camera_close()
{
	int i;

	View_matrix = Dc_save_view_matrix;
	Unscaled_matrix = Dc_save_unscaled_matrix;
	Light_matrix = Dc_save_light_matrix;
	Eye_matrix = Dc_save_eye_matrix;
	View_position = Dc_save_view_position;
	Light_base = Dc_save_light_base;
	Eye_position = Dc_save_eye_position;
	Matrix_scale = Dc_save_matrix_scale;
	View_zoom = Dc_save_view_zoom;
	for (i=0; i<MAX_OBJECTS; i++) {
		if (Dc_save_rendered[i]) {
			Objects[i].flags |= OF_WAS_RENDERED;
		} else {
			Objects[i].flags &= ~OF_WAS_RENDERED;
		}
	}
	Keep_mouse_centered = 0;
}

static void debug_camera_do()
{
	int k, now, dx, dy;
	float dt, speed, fwd, side;
	matrix orient;

	k = Pause_win.process() & ~KEY_DEBUGGED;
	if ( (k == KEY_ESC) || (k == (KEY_SHIFTED | KEY_PAUSE)) ) {
		gameseq_post_event(GS_EVENT_PREVIOUS_STATE);
	}

	now = timer_get_milliseconds();
	dt = (now - Dc_last_ms) / 1000.0f;
	Dc_last_ms = now;
	if (dt < 0.0f) {
		dt = 0.0f;
	} else if (dt > 0.1f) {
		dt = 0.1f;
	}

	// mouse look: right turns right, down looks down
	mouse_get_delta(&dx, &dy, NULL);
	Dc_angles.h += dx * DEBUG_CAMERA_LOOK;
	Dc_angles.p += dy * DEBUG_CAMERA_LOOK;
	if (Dc_angles.p > PI/2 - 0.01f) {
		Dc_angles.p = PI/2 - 0.01f;
	} else if (Dc_angles.p < -PI/2 + 0.01f) {
		Dc_angles.p = -PI/2 + 0.01f;
	}
	vm_angles_2_matrix(&orient, &Dc_angles);

	speed = DEBUG_CAMERA_SPEED * dt;
	if (keyd_pressed[KEY_LSHIFT] || keyd_pressed[KEY_RSHIFT]) {
		speed *= DEBUG_CAMERA_FAST;
	}
	fwd = (float)(keyd_pressed[KEY_W] - keyd_pressed[KEY_S]);
	side = (float)(keyd_pressed[KEY_D] - keyd_pressed[KEY_A]);
	vm_vec_scale_add2(&Dc_pos, &orient.fvec, fwd * speed);
	vm_vec_scale_add2(&Dc_pos, &orient.rvec, side * speed);

	// the scene as game_render_frame draws it, without the HUD
	gr_reset_clip();
	gr_clear();
	neb2_render_setup(&Dc_pos, &orient);
	g3_start_frame(1);			// the game's own z-buffer switch (game_zbuffer) is in FreeSpace.cpp, which the tools linking code.lib lack
	g3_set_view_matrix(&Dc_pos, &orient, Dc_save_view_zoom);
	if ( Game_subspace_effect ) {
		stars_draw(0,0,0,1);
	} else {
		stars_draw(1,1,1,0);
	}
	obj_render_all(obj_render);
	beam_render_all();
	particle_render_all();
	trail_render_all();
	mflash_render_all();
	render_shields();
	nebl_render_all();
	neb2_render_player();
	g3_end_frame();

	gr_set_color_fast(&Color_normal);
	gr_set_font(FONT1);
	gr_string(10, 10, NOX("Debug camera: W/S/A/D move, mouse look, Shift faster, Shift+Pause or Esc leaves"));

	Pause_win.draw();
	Keep_mouse_centered = 1;		// the flip reads the mouse; keep it in the window so the look never stops
	gr_flip();
	Keep_mouse_centered = 0;
}

// ----------------------------------------------------------------------------------------------------------------
// PAUSE FUNCTIONS
//

// initialize the pause screen
void pause_init(int multi)
{
	// if we're already paused. do nothing
	if ( Paused ) {
		return;
	}	

	// Claude: debug camera (#125), single player only
	if (multi || (Game_mode & GM_STANDALONE_SERVER)) {
		Pause_debug_camera = 0;
	}
	if (Pause_debug_camera) {
		debug_camera_init();
	}

	// pause all beam weapon sounds
	beam_pause_sounds();

	if(!(Game_mode & GM_STANDALONE_SERVER)){
		Pause_saved_screen = gr_save_screen();

		// pause all game music
		audiostream_pause_all();

		//JAS: REMOVED CALL TO SET INTERFACE PALETTE TO GET RID OF SCREEN CLEAR WHEN PAUSING
		//common_set_interface_palette();  // set the interface palette
		Pause_win.create(0, 0, gr_screen.max_w, gr_screen.max_h, 0);	

		if (multi) {
			Pause_win.set_mask_bmap(Pause_multi_mask[gr_screen.res]);
			Pause_background_bitmap = bm_load(Pause_multi_fname[gr_screen.res]);

			multi_pause_init(&Pause_win);		
		} else {
			if ( Cfile_fs1_data ) {	// Claude: FS1's single player pause is its Pause box, "Mission Paused" in the art, where retail draws it
				Pause_bmp_name[GR_640] = NOX("Pause");
				Please_wait_coords[GR_640][0] = 80;
				Please_wait_coords[GR_640][1] = 127;
			}
			Pause_background_bitmap = bm_load(Pause_bmp_name[gr_screen.res]);
		}
	} else {
		multi_pause_init(NULL);
	}

	Paused = 1;
}

// pause do frame - will handle running multiplayer operations if necessary
void pause_do(int multi)
{
	int k;
	char *pause_str = XSTR("Paused", 767);
	int str_w, str_h;

	if(Game_mode & GM_STANDALONE_SERVER){
		multi_pause_do();
	} else if (Pause_debug_camera) {	// Claude: debug camera (#125)
		debug_camera_do();
	} else {		
		//	RENDER A GAME FRAME HERE AS THE BACKGROUND
		gr_restore_screen(Pause_saved_screen);
		if (Pause_background_bitmap >= 0) {
			gr_set_bitmap(Pause_background_bitmap);
			if(multi){
				gr_bitmap(0,0);
			} else {
				// draw the bitmap
				gr_bitmap(Please_wait_coords[gr_screen.res][0], Please_wait_coords[gr_screen.res][1]);

				// draw "Paused" on it
				if ( !Cfile_fs1_data ) {	// Claude: FS1's has it in the art
				gr_set_color_fast(&Color_normal);
				gr_set_font(FONT2);
				gr_get_string_size(&str_w, &str_h, pause_str);
				gr_string((gr_screen.max_w - str_w) / 2, (gr_screen.max_h - str_h) / 2, pause_str);
				gr_set_font(FONT1);
				}	// Claude
			}
		}
	
		// the multi paused screen will do its own window processing
		if (multi) {
			multi_pause_do();
		}
		// otherwise process the ui window here
		else {
			k = Pause_win.process() & ~KEY_DEBUGGED;
			if (pad_pause_pressed()) {	// Claude: control pad
				k = KEY_PAUSE;
			}
			switch (k) {			
			case KEY_ESC:
			case KEY_PAUSE:
				gameseq_post_event(GS_EVENT_PREVIOUS_STATE);		
				break;
			}	// end switch
		}
	
		// draw the background window
		Pause_win.draw();		

		// a very unique case where we shouldn't be doing the page flip because we're inside of popup code
		if(!popup_active()){
			gr_flip();
		} else {
			// this should only be happening in a very unique multiplayer case
			Assert(Game_mode & GM_MULTIPLAYER);
		}
	}
}

// close the pause screen
void pause_close(int multi)
{
	// if we're not paused - do nothing
	if ( !Paused ) {
		return;
	}

	// Claude: debug camera (#125): the view and render flags put back
	if (Pause_debug_camera) {
		debug_camera_close();
		Pause_debug_camera = 0;
	}

	// unpause all beam weapon sounds
	beam_unpause_sounds();

	// deinit stuff
	if(Game_mode & GM_STANDALONE_SERVER){
		multi_pause_close();
	} else {
		gr_free_screen(Pause_saved_screen);	

		if (Pause_background_bitmap){
			bm_unload(Pause_background_bitmap);
		}

		Pause_win.destroy();		
		game_flush();
		if (multi) {
			multi_pause_close();
		}

		// unpause all the music
		audiostream_unpause_all();		
	}

	Paused = 0;
}

// debug pause init
void pause_debug_init()
{
	Pause_win.create( 100,100,400,300, WIN_DIALOG );

	Pause_physics.create( &Pause_win, NOX("Physics Pause <P>"), 200, 150, physics_paused );
	Pause_ai.create( &Pause_win, NOX("AI Pause <A>"), 200, 175, ai_paused );
	#ifndef NDEBUG
	Pause_ai_render.create( &Pause_win, NOX("AI Render Stuff <R>"), 200, 200, Ai_render_debug_flag);
	#endif
	Pause_firing.create( &Pause_win, NOX("AI firing <F>"), 200, 225, Ai_firing_enabled);
	Pause_external_view_mode_check.create( &Pause_win, NOX("External View <E>"), 200, 250, Pause_external_view_mode);
	Pause_single_step.create( &Pause_win, NOX("Single Step <S>"), 200, 290, game_single_step );
	Pause_continue.create( &Pause_win, NOX("Leave Pause"), 200, 350, 200, 40 );

	Pause_single_step.set_hotkey( KEY_S );
	Pause_physics.set_hotkey( KEY_P );
	Pause_ai.set_hotkey( KEY_A );
	Pause_ai_render.set_hotkey( KEY_R );
	Pause_firing.set_hotkey( KEY_F );
	Pause_external_view_mode_check.set_hotkey( KEY_E );
	Pause_continue.set_hotkey( KEY_ESC );

	Pause_continue.set_focus();
}

// debug pause do frame
void pause_debug_do()
{
	int key;

	key = Pause_win.process();
	if ( Pause_single_step.changed())	{
		game_single_step = Pause_single_step.checked();
	}

	if ( Pause_physics.changed())	{
		physics_paused = Pause_physics.checked();
	}

	if ( Pause_ai.changed())	{
		ai_paused = Pause_ai.checked();
		if (ai_paused){
			obj_init_all_ships_physics();
		}
	}

	if ( Pause_ai_render.changed())	{
		Ai_render_debug_flag = Pause_ai_render.checked();
	}

	if ( Pause_firing.changed())	{
		Ai_firing_enabled = Pause_firing.checked();
	}

	if ( Pause_external_view_mode_check.changed())	{
		Pause_external_view_mode = Pause_external_view_mode_check.checked();
		if (Pause_external_view_mode){
			HUD_sourced_printf(HUD_SOURCE_HIDDEN, XSTR( "External view of player ship.", 182));
		} else {
			HUD_sourced_printf(HUD_SOURCE_HIDDEN, XSTR( "View from inside player ship.", 183));
		}
	}

	if ( Pause_continue.pressed() || (key == KEY_PAUSE) )	{	//	Changed, MK, 11/9/97, only Pause break pause.
		gameseq_post_event(GS_EVENT_PREVIOUS_STATE);
	}

	gr_clear();
	Pause_win.draw();

	gr_flip();
}

// debug pause close
void pause_debug_close()
{
	last_single_step = 0;	// Make so single step waits a frame before stepping
	Pause_win.destroy();
	game_flush();
}
