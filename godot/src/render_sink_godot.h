// render_sink_godot.h  (R1-1)
// The Godot implementation of the render sink (render_sink.h): draws into the canvas of a SubViewport with
// RenderingServer canvas items. godot-cpp lives here and in fs2_screen.*, never in the game DLL (D-20261022-143922).
//
// A game frame builds a fresh set of canvas items under a hidden root; frame_end() makes that root the visible one and
// frees the previous one ("latest frame wins": a frame that was never rendered is simply replaced).
#ifndef FS2_RENDER_SINK_GODOT_H
#define FS2_RENDER_SINK_GODOT_H

#include <map>
#include <vector>

#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "batcher.h"
#include "render_sink.h"

namespace fs2sink {

class GodotSink : public BatchEmitter {
public:
	GodotSink() {}
	~GodotSink() { shutdown(); }

	// The sink draws into the canvas of this SubViewport and resizes it when frame_begin() gets another game screen size.
	bool init(godot::SubViewport *viewport);
	void shutdown();
	fs2_render_sink *table() { return &sink; }

	// How many game frames are left in this Godot frame (the runner steps several per _process). frame_begin() answers
	// "draw it" only for the last, so a replay does not rebuild frames nobody sees. Default 1: every frame is drawn.
	void set_frames_left(int n) { frames_left = n < 1 ? 1 : n; }

	int frames_committed() const { return committed; }

	// Perspective-correct UVs (S-2, D-20261027-190102): custom shader (default) or CPU subdivision (fallback).
	void set_perspective_mode(PerspectiveMode m) { batcher.set_perspective_mode(m); }
	const BatcherStats &batch_stats() const { return batcher.stats(); }

	void emit(const Batch &b) override;

private:
	fs2_render_sink sink = {};
	godot::SubViewport *vp = nullptr;
	godot::RID canvas;
	int width = 0, height = 0;
	int frames_left = 1;
	int committed = 0;
	bool in_frame = false;

	// the frame being built (hidden) and the one on show
	godot::RID pending_root;
	std::vector<godot::RID> pending_items;
	godot::RID shown_root;
	std::vector<godot::RID> shown_items;
	std::vector<godot::RID> spare_items;	// recycled canvas items

	// textures: the host's ids to RenderingServer textures. A freed texture lives two more frame_ends, because items
	// of the frame on show may still draw it.
	struct Tex {
		godot::RID rid;
		int w = 0, h = 0, format = 0;
		uint32_t flags = 0;
	};
	struct Dying {
		godot::RID rid;
		int age = 0;
	};
	std::map<int32_t, Tex> textures;
	std::vector<Dying> dying;
	godot::RID shaders[12], materials[12];	// by blend + 3 * (a8 + 2 * perspective)
	std::vector<godot::RID> pending_meshes, shown_meshes, spare_meshes;
	TriBatcher batcher;

	godot::RID material_for(int blend, bool a8, bool persp);
	godot::RID mesh_for_batch(const Batch &b);
	void release_meshes(std::vector<godot::RID> &meshes);
	void age_dying(bool force_all);
	void retire(const godot::RID &r);

	godot::RID parent;			// where new items go: pending_root, or the clip item
	int clip[4] = { 0, 0, 0, 0 };
	bool clipped = false;
	bool clip_empty = false;

	godot::RID new_item(const godot::RID &under);
	void release_items(std::vector<godot::RID> &items, godot::RID &root);
	void set_clip_state(int x, int y, int w, int h);
	void open_clip_item();

	static int s_frame_begin(void *u, int w, int h);
	static void s_frame_end(void *u);
	static void s_clear(void *u, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
	static void s_set_clip(void *u, int x, int y, int w, int h);
	static void s_texture_create(void *u, int32_t id, int w, int h, int format, uint32_t flags, const uint8_t *pixels);
	static void s_texture_free(void *u, int32_t id);
	static void s_texture_free_all(void *u);
	static void s_draw_tris(void *u, const fs2_sink_draw *d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni);
	static int s_capture(void *u, int32_t id);
	static int s_read_region(void *u, int x, int y, int w, int h, uint8_t *out);
	static void s_set_gamma(void *u, float g);
};

} // namespace fs2sink

#endif
