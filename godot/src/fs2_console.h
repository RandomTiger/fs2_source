// fs2_console.h
// GodotOptions: the drop-down command console (Tom), also in release builds: the key left of 1 slides it down from the top of the
// window and back up. Live play only: a scripted or headless run has none, so compared runs never see it. The host answers help and
// fps itself and hands other lines to the game (fs2_console_command, v10). Everything here runs on Godot's main thread.
#ifndef FS2_CONSOLE_H
#define FS2_CONSOLE_H

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/viewport.hpp>

#include "fs2_loader.h"

namespace fs2console {

// Before fs2_init. parent draws it (the Fs2Screen); enabled false leaves no console.
void attach(godot::Node *parent, Fs2Api *api, bool enabled);
// True if the game must not see this event: the console's key, and while open key presses and mouse presses. Releases and
// mouse motion still reach the game, so a key held when it opened does not stick.
bool input(const godot::Ref<godot::InputEvent> &event, godot::Viewport *viewport);
// Every Godot frame: the slide, the layout, the FPS display.
void tick(double delta);
// Between game frames: runs the lines waiting for the game.
void run_pending(void);

} // namespace fs2console

#endif
