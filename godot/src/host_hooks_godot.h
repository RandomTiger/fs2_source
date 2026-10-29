// host_hooks_godot.h
// G4: Godot fills the game's host hooks (code/OsApi/HostHooks.h): time, events, mouse, joystick. Godot's events are queued here
// as they arrive (_input, window signals) and only the poll_events hook, which the game calls from os_poll, hands them to the
// game, so the game sees them at the same point in its frame as it saw Windows messages. The game's own replay code decides
// whether it takes them: a recording being replayed ignores live events and reads its own values, so these hooks only matter in
// live play. Everything here runs on Godot's main thread (the game is a fiber on that thread, G3).
#ifndef HOST_HOOKS_GODOT_H
#define HOST_HOOKS_GODOT_H

#include <godot_cpp/classes/input_event.hpp>

#include "fs2_loader.h"

namespace fs2host {

// Use this DLL's sinks and read the host's state afresh. Call after the DLL is loaded, before fs2_set_hooks / fs2_init.
void attach(Fs2Api *api);
// The ten hooks of fs2_api.h, ready for fs2_set_hooks.
void fill(fs2_hooks *hooks);
// The sink for the game's input test (fs2_set_input_script_sink): turns its scripted joystick or pad into synthetic joypad
// events on device 0, which the joystick and pad hooks then read back. Used only with -at_input joyhost / padhost.
void (*input_script_sink())(const fs2_input_script *);
// Forget queued events and key states (a new run starts clean).
void reset();

// Queue one of Godot's input events (keys and mouse buttons and motion are used; the rest is ignored). The viewport size is
// the size the event's position is relative to.
// A key event with a physical key, or a keycode alone, goes as that key. A text event (a character and no physical key: Steam's
// keyboard, an IME) goes as the game key that types the character on the game's US table, with shift as the character needs,
// one character per game frame so the game's key buffer cannot overflow; a character no game key types is dropped.
void push_input(const godot::Ref<godot::InputEvent> &event, float viewport_w, float viewport_h);
void push_focus(bool got);
void push_close();
// Call once before each game frame: paces typed text.
void frame_tick();
// The game key that types character c on the game's US table, and whether it needs shift; 0 for none.
unsigned int text_key(unsigned int c, bool *shift);

// Measurement only (R1-feel): how many events the game has taken from the queue, and when it took the last (ticks_usec).
unsigned long long consumed_count(void);
long long consumed_usec(void);

// The Godot key to the game's key code (Key.h), 0 for a key the game has no code for. location is godot::KeyLocation.
unsigned int key_code(int physical_keycode, int location);

} // namespace fs2host

#endif
