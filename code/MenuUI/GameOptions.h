/*
 * Claude: the Game Options screen and its two fs2.ini keys (D-20261102-200101..107).
 *
 * The Options button on the pilot screen (bottom right, live play only) opens it.  Two choices:
 *   FlightControl=Pad|Joystick|Mouse  the in-game flight reader; applied at pilot load and when chosen here.
 *                                     No key: the automatic rule (a pad takes the joystick's place, joy_init).
 *   GraphicsMode=GR_1024|GR_640       the art mode gr_init picks at the next start; FS2 retail only
 *                                     (data set profile gr_mode_choice).  No key: as before.
 *   DisplayResolution=classic|desktop|WxH  the Resolution row (GrVirtual.h, GodotResolution); every data set.
 *   Where the backend changes mode live (gr_live_mode_change: the Godot host) a choice applies at once; otherwise at the next start.
 * Both live in fs2.ini's [Settings] (os_config_*).  A replay, a recording and autotest ignore both keys: they
 * neither read nor write them.  Tests set the flight choice in-process (game_options_set_flight_device).
 */

#ifndef _GAME_OPTIONS_H
#define _GAME_OPTIONS_H

#define GAME_OPTIONS_FLIGHT_AUTO			0	// no key: pad if one is connected at start, else the joystick; mouse as the pilot has it
#define GAME_OPTIONS_FLIGHT_PAD			1
#define GAME_OPTIONS_FLIGHT_JOYSTICK		2
#define GAME_OPTIONS_FLIGHT_MOUSE			3

// the flight choice in force
int game_options_flight_device();

// sets the flight choice in-process (no ini write) and applies it to Use_mouse_to_fly; for tests
void game_options_set_flight_device(int device);

// 1 if the pad's flight reads (pad_read_flight, pad_control_held) count
int game_options_pad_flies();

// 1 if the legacy joystick's flight reads (axes and buttons as controls) count
int game_options_joystick_flies();

// 1 if joy_init leaves the stick to a connected pad (every choice but Joystick)
int game_options_joystick_yields_to_pad();

// after a pilot loads: a Pad/Joystick/Mouse key sets Use_mouse_to_fly
void game_options_apply_flight();

// the Options screen's mouse toggle changed Use_mouse_to_fly: keep the key in step
void game_options_mouse_changed(int on);

// gr_init's art mode: 'hi' is 1 if the game would pick GR_1024 (Glide/D3D chain); returns 1 for GR_1024
int game_options_gr_hi(int hi);

// gr_init's art mode for GR_HOST and GR_DUMMY: 'mode' is the default
int game_options_gr_mode(int mode);

// the screen (GS_STATE_GAME_OPTIONS)
void game_options_screen_init();
void game_options_screen_do_frame(float frametime);
void game_options_screen_close();

// the screen's Back button, as a click on it; for the autotest menu tour
void game_options_screen_back();

// the menu tour's live steps on the screen (D-20261102-204807), as clicks on its rows; a compared run writes no key.
// Returns 0 when the step is not live here (no GR row, or a backend without live mode change).
#define GAME_OPTIONS_TOUR_GR_640		0	// the GR row: 640 x 480, live
#define GAME_OPTIONS_TOUR_GR_1024		1	// the GR row: 1024 x 768, live
#define GAME_OPTIONS_TOUR_DEVICE		2	// the flight row: the next device, ownership re-run
#define GAME_OPTIONS_TOUR_DEVICE_BACK	3	// the flight choice before GAME_OPTIONS_TOUR_DEVICE back
#define GAME_OPTIONS_TOUR_RES			4	// the Resolution row: the next entry
#define GAME_OPTIONS_TOUR_RES_BACK		5	// the entry before GAME_OPTIONS_TOUR_RES back
#define GAME_OPTIONS_TOUR_STEPS			6
int game_options_tour_step(int step);

// the pilot screen's Options button (live play only: a compared run neither loads nor draws it)
void game_options_pilot_button_init();
// draws it and takes a click on it; call once a frame, before the flip
void game_options_pilot_button_do();
// 1 if the screen is closing because the button was clicked: Back returns to the pilot screen
int game_options_pilot_button_leaving();

// the Godot host's console (fs2_console_command): "resolution ..." and "unlock missions".  Returns 0 for any other line.
int game_options_console_command(const char *line, char *out, int out_size);

// GodotOptions: an option from one line of GameOptionList.h has its command line, its Options screen row and its console
// command from this one table. The key is read at the start; the command line, if given, wins for that run and is not saved.
// A compared run (autotest, record, replay) ignores the key, so only an explicit command line changes it.
#define GO_OPTION(id, name, key, label, dflt, host_only) id,
enum {
#include "GameOptionList.h"
	GO_OPT_COUNT
};
#undef GO_OPTION

void game_options_settings_init();
int game_option(int id);
void game_option_set(int id, int value);
int game_option_hud_lift();	// pixels the head box and directives move up with subtitles off
int game_option_hud_lines_x();	// pixels the top message lines start further right with subtitles off
int game_option_voiced_text_shown();	// the Subtitles option: a compared run always shows the line

#endif
