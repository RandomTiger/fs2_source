// fs2_screen.h  (R1-1)
// The Godot node that shows the game screen: a SubViewport of the game screen size that the render sink draws into, and
// a TextureRect that scales it into the window keeping the aspect (the window is letterboxed; mouse mapping is R1-6).
#ifndef FS2_SCREEN_H
#define FS2_SCREEN_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture_rect.hpp>

#include "render_sink_godot.h"

namespace godot {

class Fs2Screen : public Node {
	GDCLASS(Fs2Screen, Node)

	SubViewport *viewport = nullptr;
	TextureRect *view = nullptr;
	fs2sink::GodotSink sink;

protected:
	static void _bind_methods();

public:
	void _ready() override;
	void _exit_tree() override;

	// The sink table for the game (fs2_set_render_sink, R1-5). Valid while this node lives.
	fs2_render_sink *get_sink() { return sink.table(); }
	fs2sink::GodotSink &sink_core() { return sink; }
	// Game frames left in this Godot frame; only the last is drawn (see GodotSink::set_frames_left).
	void set_frames_left(int n) { sink.set_frames_left(n); }
	int get_frames_committed() const { return const_cast<fs2sink::GodotSink &>(sink).frames_committed(); }
	// The frame on show, as an image (needs a rendered window; empty under --headless).
	Ref<Image> get_image();
	RID get_viewport_rid() const { return viewport ? viewport->get_viewport_rid() : RID(); }
	bool save_png(const String &path);
};

} // namespace godot

#endif
