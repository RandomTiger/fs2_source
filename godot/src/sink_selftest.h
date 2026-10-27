// sink_selftest.h  (R1-1)
// Self-test of the Godot render sink: runs the fixed scenes of selftest_scenes.h through the real sink in a windowed Godot,
// reads the viewport back, checks the probe pixels and saves one PNG per scene. Not the game. Run:
//   Godot --path godot res://selftest.tscn      (env FS2_SELFTEST_OUT = folder for the PNGs, default the current folder)
// Needs a real RenderingServer; under --headless the image is empty and the test says so.
#ifndef FS2_SINK_SELFTEST_H
#define FS2_SINK_SELFTEST_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

#include "fs2_screen.h"

namespace godot {

class Fs2SinkTest : public Node {
	GDCLASS(Fs2SinkTest, Node)

	Fs2Screen *screen = nullptr;
	int scene_index = -1;
	int wait = 0;
	int failures = 0;
	String out_dir;

	void start_scene();
	void finish_scene();

protected:
	static void _bind_methods() {}

public:
	void _ready() override;
	void _process(double delta) override;
};

} // namespace godot

#endif
