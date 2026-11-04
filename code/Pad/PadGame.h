// PadGame.h
// Control pad glue for FS2: feeds the pad core's output into the game's own control paths
// (button_info, check_control, control_info), so the rest of the game sees a pad the same
// way it sees keys and the joystick.

#ifndef __PAD_GAME_H__
#define __PAD_GAME_H__

struct control_info;

// pad.cfg read and the core set up, if not yet. For the menu layer (PadMenuGame.cpp), which runs first.
void	pad_game_settings_read();

// Once per game frame, from game_process_keys() before button_info_do().
// Polls the pad, sets button_info bits for one-shot actions and handles pause.
void	pad_frame();

// From check_control(): 1 if the pad holds continuous control id (FIRE_PRIMARY, AFTERBURNER, ...).
int	pad_control_held(int id);

// From voice_frame(): 1 while the pad holds the voice push-to-talk button (West, 360: X).
int	pad_voice_talk_held();

// For the pad test (PadMenuTest.cpp): how many times South has targeted in flight.
int	pad_game_south_count();

// From read_keyboard_controls(): adds the pad's yaw, pitch, roll and throttle.
void	pad_read_flight(struct control_info *ci, float frame_time);

// From the pause screen: polls the pad, returns 1 if the pause button was pressed.
int	pad_pause_pressed();

// From joy_init(): 1 if an XInput pad is connected, so the legacy joystick code leaves it alone
// (it would otherwise read the same pad, with the D-pad as view controls).
int	pad_owns_joystick();

// From the HUD render (PadHud.cpp): draws the open pad tree options.
void	pad_hud_render();

// From hud_show_ets(): 1 while North is held and this gauge (0 weapons, 1 shields, 2 engines) is the system
// North + D-pad changes. pad_ets_recolor (PadHud.cpp) sets its colour; a gauge not highlighted is left alone.
int	pad_ets_highlight(int gauge);
void	pad_ets_recolor(int gauge, int bright);

// For PadHud.cpp: the pad's output from this frame.
struct pad_output;
const struct pad_output	*pad_game_output();

#endif
