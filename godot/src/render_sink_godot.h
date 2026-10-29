// render_sink_godot.h  (R1-1)
// The Godot implementation of the render sink (render_sink.h): draws into the canvas of a SubViewport with
// RenderingServer canvas items. godot-cpp lives here and in fs2_screen.*, never in the game DLL (D-20261022-143922).
//
// A game frame builds a fresh set of canvas items under a hidden root; frame_end() makes that root the visible one and
// frees the previous one ("latest frame wins": a frame that was never rendered is simply replaced).
//
// Depth runs (R1-8, D2 hybrid, sink v2): the draws of a run are collected as depth batches. At depth_end the draws before
// the first depth-tested one and after the last go on the canvas as usual; the ones in between go to a world layer: a
// RenderingServer viewport with its own scenario and an orthographic camera, one mesh instance per batch, depth tested
// on the GPU, and the layer's texture is composited into the canvas at that point of the order (premultiplied alpha).
// Layers are pooled and owned by the frame like the canvas items.
#ifndef FS2_RENDER_SINK_GODOT_H
#define FS2_RENDER_SINK_GODOT_H

#include <map>
#include <utility>
#include <vector>

#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "batcher.h"
#include "render_sink.h"

namespace fs2sink {

// Texture pool on/off (perf group 2 fix 2); false is the old create/free-every-time behaviour.
constexpr bool kTexturePool = true;
constexpr size_t kTexPoolMax = 256;
// Shader/pipeline pre-warm on/off (perf group 2 fix 4): at the first accepted frame the sink draws every canvas material and the common
// depth-layer shaders once into private 16x16 viewports that nothing composites, so Godot builds the variants then, not mid-mission.
constexpr bool kPrewarm = true;
constexpr int kPrewarmFrames = 3;
// Instance pool on/off (perf group 2 fix 3); false frees and creates the mesh instances every frame as before.
constexpr bool kInstancePool = true;

// Switch point for the nebula fog-colour read (perf pass group 2, D-20261028-233135). neb2_pre_render reads a 32x32 corner of the
// screen (Neb.cpp ESIZE) every frame; the host does not use it (fog is not drawn until R2; neb2_get_pixel is called only by the
// D3D backend), and the read is a synchronous GPU round trip (6.5-10 ms per drawn frame in nebula missions). While true, a
// read of at most kNebFogReadMaxPixels pixels at (0,0) is answered "no data" without touching the GPU, which leaves the game's
// buffer untouched, as the dummy renderer does. Larger reads (Autotest screenshots) are served as before.
// R2 (fog) MUST set this false, or replace the read with an asynchronous one, and cost it again.
constexpr bool kSkipNebFogRead = true;
constexpr int kNebFogReadMaxPixels = 32 * 32;

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
	// Measurement only (R1-feel): microseconds spent in the sink (frame_end and draw_tris)
	unsigned long long stat_sink_usec = 0;

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
		int w = 0, h = 0, format = 0;
	};
	// Pool of dead textures (perf group 2 fix 2): a texture that has aged out is kept, not freed, and the next create of the
	// same size and format re-uploads into it (texture_2d_update) instead of making a new GPU texture. Nebula missions free
	// and re-create 48-80 bitmaps every drawn frame. kTexPoolMax bounds it; the oldest entry is freed beyond that.
	struct Pooled {
		godot::RID rid;
		int w = 0, h = 0, format = 0;
	};
	std::vector<Pooled> tex_pool;
	std::map<int32_t, Tex> textures;
	std::vector<Dying> dying;
	godot::RID shaders[12], materials[12];	// by blend + 3 * (a8 + 2 * perspective)
	std::vector<godot::RID> pending_meshes, shown_meshes, spare_meshes;
	TriBatcher batcher;

	godot::RID material_for(int blend, bool a8, bool persp);
	godot::RID white_texture();
	godot::RID spatial_shader(int blend, int zmode, bool a8, bool lin, bool rpt);
	void prewarm();
	void prewarm_release();
	bool warmed = false;
	int warm_age = 0;
	std::vector<godot::RID> warm_viewports, warm_instances, warm_items, warm_misc;
	void emit_canvas(const Batch &b);

	// depth runs
	struct Layer {
		godot::RID viewport, scenario, camera, texture;
		int w = 0, h = 0;
		std::vector<godot::RID> instances;
	};
	std::vector<Layer> pending_layers, shown_layers, spare_layers;
	std::vector<godot::RID> spare_instances;	// recycled mesh instances (perf group 2 fix 3): out of any scenario until reused
	bool in_run = false;
	int run_epoch = 0;
	std::vector<Batch> run_batches;
	godot::RID composite_shader, composite_material, white_tex;
	std::map<uint32_t, godot::RID> spatial_shaders;
	std::map<std::pair<uint32_t, int64_t>, godot::RID> spatial_materials;	// (shader key, texture rid id)
	Layer acquire_layer();
	void release_layers(std::vector<Layer> &layers);
	void free_layer(Layer &l);
	void end_run();
	void sync_batcher_clip();
	godot::RID spatial_material(int blend, int zmode, bool a8, uint32_t texflags, const godot::RID &tex);
	void add_to_layer(Layer &l, const Batch &b, int order);
	void drop_materials_of(const godot::RID &tex);
	godot::RID mesh_for_batch(const Batch &b);
	void release_meshes(std::vector<godot::RID> &meshes);
	void age_dying(bool force_all);
	void retire(const Tex &t);

	godot::RID cur_item, cur_parent;	// the open item that further batches of the same signature join
	uint32_t cur_sig = 0;
	int next_index = 0;
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
	static void s_depth_begin(void *u);
	static void s_depth_end(void *u);
	static void s_depth_clear(void *u, int x, int y, int w, int h);
};

} // namespace fs2sink

#endif
