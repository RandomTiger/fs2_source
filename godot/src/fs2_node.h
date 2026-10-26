// fs2_node.h
// The Godot side of the headless runner (G2): a node that loads fs2game.dll through the plain C API and steps the
// game from _process. godot-cpp lives only here (D-20261022-143922); the game DLL has no Godot type.
#ifndef FS2_NODE_H
#define FS2_NODE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

#include "fs2_loader.h"

namespace godot {

class Fs2Game : public Node {
	GDCLASS(Fs2Game, Node)

	Fs2Api api;
	bool running = false;
	int frames_per_process = 8;
	int frame_count = 0;

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

	void _process(double delta) override;
};

} // namespace godot

#endif
