// fs2_node.h
// The Godot side of the headless runner (G2): a node that loads fs2game.dll through the plain C API and steps the
// game from _process. godot-cpp lives only here (D-20261022-143922); the game DLL has no Godot type.
#ifndef FS2_NODE_H
#define FS2_NODE_H

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

#include "fs2_loader.h"
#include "host_hooks_godot.h"

namespace godot {

class Fs2Game : public Node {
	GDCLASS(Fs2Game, Node)

	Fs2Api api;
	bool running = false;
	int frames_per_process = 8;
	int frame_count = 0;
	bool fatal_return = false;
	int fiber_mode = 0;
	bool forward_input = true;
	Node *screen = nullptr;	// an Fs2Screen: when set, the game draws through it (fs2_set_render_sink)

protected:
	static void _bind_methods();

public:
	// Load the DLL and start the game. Returns false (and prints why) when it could not start.
	bool start(const String &dll_path, const String &root_dir, const String &cmdline);
	// Step up to frames_per_process game frames. Returns true while the game is still running; when the game ends
	// it shuts down, emits "finished" with the exit code and returns false.
	bool step();
	bool is_running() const { return running; }
	int get_frame_count() const { return frame_count; }
	void set_frames_per_process(int n) { frames_per_process = n < 1 ? 1 : n; }
	int get_frames_per_process() const { return frames_per_process; }
	// P1: hand a fatal Error() back as the "fatal" signal instead of ending the process (fs2_set_fatal_return)
	void set_fatal_return(bool on) { fatal_return = on; api.module ? api.set_fatal_return(on ? 1 : 0) : (void)0; }
	bool get_fatal_return() const { return fatal_return; }

	// G3: 0 = the game runs on this node's stack (as in G2), 1 = on a fiber resumed once per step() call frame, 2 = fiber
	// that also yields at every os_poll (stress). Set before start().
	void set_fiber_mode(int m) { fiber_mode = m < 0 ? 0 : (m > 2 ? 2 : m); }
	int get_fiber_mode() const { return fiber_mode; }

	// R1-5: draw the game through this Fs2Screen (GR_HOST); null keeps the null renderer. Set before start().
	void set_screen(Node *n);
	Node *get_screen() const { return screen; }

	// G4: Godot's keyboard, mouse and window events go to the game through the host queue (host_hooks_godot). Off: the game gets none.
	void set_forward_input(bool on) { forward_input = on; }
	bool get_forward_input() const { return forward_input; }
	void on_focus_in() { if (forward_input) fs2host::push_focus(true); }
	void on_focus_out() { if (forward_input) fs2host::push_focus(false); }
	void on_close_requested() { if (forward_input) fs2host::push_close(); }

	void _ready() override;
	void _input(const Ref<InputEvent> &event) override;
	void _process(double delta) override;
};

} // namespace godot

#endif
