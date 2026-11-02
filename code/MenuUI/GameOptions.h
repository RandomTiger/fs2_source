/*
 * Claude: the Game Options screen and its two fs2.ini keys (D-20261102-200101..107).
 *
 * The main hall's OPTIONS label (bottom left) opens it.  Two choices:
 *   FlightControl=Pad|Joystick|Mouse  the in-game flight reader; applied at pilot load and when chosen here.
 *                                     No key: the automatic rule (a pad takes the joystick's place, joy_init).
 *   GraphicsMode=GR_1024|GR_640       the art mode gr_init picks at the next start; FS2 retail only
 *                                     (data set profile gr_mode_choice).  No key: as before.
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

// where the main hall draws its OPTIONS label; 1 if (x, y) is on it
int game_options_hall_label_hit(int x, int y);

// draws the main hall's OPTIONS label, lit when 'hover'
void game_options_hall_label_draw(int hover);

#endif
