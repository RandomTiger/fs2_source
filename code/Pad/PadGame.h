// PadGame.h
// Control pad glue for FS2: feeds the pad core's output into the game's own control paths
// (button_info, check_control, control_info), so the rest of the game sees a pad the same
// way it sees keys and the joystick.

#ifndef __PAD_GAME_H__
#define __PAD_GAME_H__

struct control_info;

// Once per game frame, from game_process_keys() before button_info_do().
// Polls the pad, sets button_info bits for one-shot actions and handles pause.
void	pad_frame();

// From check_control(): 1 if the pad holds continuous control id (FIRE_PRIMARY, AFTERBURNER, ...).
int	pad_control_held(int id);

// From read_keyboard_controls(): adds the pad's yaw, pitch, roll and throttle.
void	pad_read_flight(struct control_info *ci, float frame_time);

// From the pause screen: polls the pad, returns 1 if the pause button was pressed.
int	pad_pause_pressed();

// From joy_init(): 1 if an XInput pad is connected, so the legacy joystick code leaves it alone
// (it would otherwise read the same pad, with the D-pad as view controls).
int	pad_owns_joystick();

// From the HUD render (PadHud.cpp): draws the open pad tree options.
void	pad_hud_render();

// For PadHud.cpp: the pad's output from this frame.
struct pad_output;
const struct pad_output	*pad_game_output();

#endif
