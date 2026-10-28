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
// The nine hooks of fs2_api.h, ready for fs2_set_hooks.
void fill(fs2_hooks *hooks);
// Forget queued events and key states (a new run starts clean).
void reset();

// Queue one of Godot's input events (keys and mouse buttons and motion are used; the rest is ignored). The viewport size is
// the size the event's position is relative to.
void push_input(const godot::Ref<godot::InputEvent> &event, float viewport_w, float viewport_h);
void push_focus(bool got);
void push_close();

// Measurement only (R1-feel): how many events the game has taken from the queue, and when it took the last (ticks_usec).
unsigned long long consumed_count(void);
long long consumed_usec(void);

// The Godot key to the game's key code (Key.h), 0 for a key the game has no code for. location is godot::KeyLocation.
unsigned int key_code(int physical_keycode, int location);

} // namespace fs2host

#endif
