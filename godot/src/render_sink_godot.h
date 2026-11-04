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
#include <set>
#include <utility>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "batcher.h"
#include "depth_contract.h"
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

// The nebula fog-colour read (R2). neb2_pre_render renders a 32x32 background at the top left of the frame and reads it back
// (gr_get_region, Neb.cpp ESIZE); neb2_get_pixel feeds it to the fogged polys (TMAP_FLAG_PIXEL_FOG), so the read now matters.
// The old read (the shown frame, full size, a GPU round trip: 6.5-10 ms per drawn frame in nebula missions) is gone. The probe
// below renders the frame built so far (pending_root and its layers) into a w x h viewport and reads that.
//   kNebFogOff:  the read answers "no data" (the buffer stays as the dummy renderer leaves it; fog colour black)
//   kNebFogSame: the probe is rendered and read in the same frame (exact; costs a render + a small readback)
//   kNebFogLag:  the colour comes from the pre-render of drawn frame k - kNebFogLagFrames (fixed, counted in drawn frames, never
//                depending on GPU timing; the first drawn frame and the one after a size change use the same-frame probe)
//   kNebFogGpu:  (option 5, prototype) the pre-render is moved into a 32x32 slot viewport that Godot renders before the layers of the same
//                frame, and the fog shader looks the colour up in that texture: neb2_get_pixel's 3x3 average and the per polygon average
//                are done in the vertex shader from a per frame table of the polys' cells. No readback; read_region answers "no data".
constexpr int kNebFogOff = 0, kNebFogSame = 1, kNebFogLag = 2, kNebFogGpu = 3;
// D-20261030-063743: the state is chosen at run time, Godot-side (Fs2Game::start, fs2_node.cpp): kNebFogGpu for live shown play (a window, no
// -replay/-autotest/-record/-at_ run), kNebFogOff for every scripted, trace-compared or gate run. FS2_NEBFOG=off|same|lag|gpu overrides it.
inline int g_nebfog = kNebFogOff;
// P3a (GodotRender3D, D-20261031-120702): opaque depth-written batches of one epoch are regrouped by texture. FS2_OPAQUE_REORDER=0 turns it off.
inline bool g_opaque_reorder = true;
// P3b: winding of front faces on the layer (y up) for mesh draws with the cull flag; settled by the depth harness (design note)
constexpr bool kMeshFrontCCW = true;
constexpr int kNebFogLagFrames = 1;
constexpr int kNebFogReadMaxPixels = 64 * 64;	// reads at (0,0) up to this size are the fog read; larger ones are screenshots

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
	unsigned long long stat_mesh_draws = 0;	// mesh draws that reached a 3D layer, all frames (counted-shot proof)
	unsigned long long stat_bb_draws = 0;	// billboard draws that reached a 3D layer, all frames (counted-shot proof)
	unsigned long long stat_mesh_models = 0;	// distinct (model, state) per run summed: batching potential = draws vs this
	unsigned long long stat_reorder_saved = 0;	// layer batches removed by the opaque regrouping (P3a)

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
		int arr = -1, layer = 0;	// a layer of texture array chunk `arr` (billboards), or -1
	};
	struct Dying {
		godot::RID rid;
		int age = 0;
		int w = 0, h = 0, format = 0;
		int arr = -1, layer = 0;
	};
	// Texture arrays for billboards: textures up to kArrMaxDim a side also live as a layer of a Texture2DArray chunk of their (w, h, format, linear)
	// group, built at texture_create time (never at draw time), so one billboard run can span every texture of a chunk. A freed texture's layer is
	// reusable after the same two frame_ends as its RID.
	struct ArrChunk {
		godot::RID rid;
		int w = 0, h = 0, format = 0;
		bool lin = false;
		std::vector<int> free_layers;
	};
	static constexpr int kArrCap = 64;
	static constexpr int kArrMaxDim = 512;
	std::vector<ArrChunk> arr_chunks;
	// Animations (texture_array_create): one exact-size chunk per ANI, its layers claimed by texture_layer. The images wait here until every layer
	// has its standalone texture (the draws that are not billboards sample those).
	struct AnimArr {
		int chunk = -1;	// -1: not an array (too big, or arrays off)
		int claimed = 0;
		int w = 0, h = 0, format = 0;
		uint32_t flags = 0;
		std::vector<godot::Ref<godot::Image>> imgs;
	};
	std::map<int32_t, AnimArr> anim_arrays;
	bool arr_off = false;	// FS2_BB_ARR_OFF=1: billboards keep one texture per run (proof baseline)
	bool arr_alloc(int w, int h, int format, bool lin, int &chunk, int &layer);
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
	godot::RID spatial_shader(int blend, int zmode, bool a8, bool lin, bool rpt, bool fog);
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
		bool probe_parent = false;	// its parent viewport is a fog probe slot (fog lag), not the game viewport
		std::vector<godot::RID> instances;
		std::vector<godot::RID> mesh_materials;	// v4 mesh draws: one material and one colour table per draw, back to the pools when the layer goes
		std::vector<std::pair<int, godot::RID>> mesh_coltex;	// (rows, texture)
		std::vector<godot::RID> bb_instances;	// billboard runs: one instance each, kept apart from `instances` (they carry a sorting offset)
		std::vector<std::pair<int, godot::RID>> bb_multimeshes;	// (bucket, multimesh) of those runs, back to bb_mm_pool when the layer goes
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
	godot::RID spatial_material(int blend, int zmode, bool a8, uint32_t texflags, const godot::RID &tex, bool fog);

	// kNebFogGpu: per frame table of the fogged polys' lookup cells (RGBA8 texels, 128 wide, one table and one fog slot per frame parity,
	// because the frame on show still reads its own while the next one is built), and the render order of the slot before the layers
	godot::RID gpu_tab[2];
	int gpu_tab_rows[2] = { 0, 0 };
	godot::RID gpu_bg_texture(int parity);
	godot::RID gpu_tab_texture(int parity);
	void gpu_frame_end();
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
	static void s_texture_array_create(void *u, int32_t array_id, int w, int h, int nlayers, int format, uint32_t flags, const uint8_t *pixels);
	static void s_texture_layer(void *u, int32_t id, int32_t array_id, int layer);
	static void s_texture_array_free(void *u, int32_t array_id);
	static void s_draw_tris(void *u, const fs2_sink_draw *d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni);
	static int s_capture(void *u, int32_t id);
	static int s_read_region(void *u, int x, int y, int w, int h, uint8_t *out);
	int probe_read(int w, int h, uint8_t *out);
	godot::RID probe_vp;
	int probe_w = 0, probe_h = 0;

	// kNebFogLag: the pre-render (what the frame has drawn when the 32x32 read comes) is moved out of the frame into a slot with its
	// own viewport, which Godot renders with the frame that follows; the colour of drawn frame k is read from the slot of drawn frame
	// k - kNebFogLagFrames. A ring of kNebFogLagFrames + 1 slots. `stamp` is the value of `committed` when the slot was filled.
	struct FogSlot {
		godot::RID canvas, vp, root;
		std::vector<godot::RID> items, meshes;
		std::vector<Layer> layers;
		int w = 0, h = 0;
		int stamp = -1;
		uint64_t drawn = 0;	// Godot frames drawn when it was filled: it is rendered once frames_drawn is higher
		bool live = false;
	};
	FogSlot fog_slots[kNebFogLagFrames + 1];
	int fog_read(int w, int h, uint8_t *out);
	void fog_capture(int w, int h);
	void fog_capture_gpu(int w, int h);
	int gpu_pre_layers = 0;
	void fog_release_slot(FogSlot &s);
	void fog_slot_init(FogSlot &sl);
	int fog_slot_pixels(FogSlot &s, int w, int h, uint8_t *out);
	static void s_set_gamma(void *u, float g);
	static void s_depth_begin(void *u);
	static void s_depth_end(void *u);
	static void s_depth_clear(void *u, int x, int y, int w, int h);
	static void s_set_view(void *u, const fs2_sink_view *v);
	fs2_sink_view view = {};	// the last camera the game sent (v3)

	// v4 meshes (P3b): model-space geometry kept by id; a draw inside a run leaves a placeholder batch (key.texture = -(draw index + 1))
	// so it keeps its place among the run's batches, and add_to_layer turns it into mesh instances with the mesh shader.
	struct MeshGroup {
		godot::RID mesh_rid;		// a mesh with one surface, made once with the geometry and kept across frames
	};
	struct MeshGroupState {			// how a group is drawn: set per draw (mesh_state), not part of the geometry
		fs2_sink_mesh_group g;
		int zmode = 2;
	};
	struct MeshData {
		std::vector<MeshGroup> groups;
		std::vector<std::vector<MeshGroupState>> states;	// every distinct state the game has set; a draw keeps the index it was made with
		int cur = 0;
	};
	struct MeshDraw {
		int32_t id = 0;
		int state = 0;	// index into the mesh's states
		float xf[12] = {};
		int epoch = 0;
		int cw = 0, ch = 0, ox = 0, oy = 0;	// the canvas and its screen origin when drawn
		std::vector<uint8_t> col;	// RGBA8 per mesh vertex
		bool lit = false;			// lit on the GPU from `light` (mesh_draw_lit); col is empty
		fs2_sink_mesh_light light = {};
		bool clip = false;			// the user clip plane (mesh_clip) in force when drawn
		float plane[4] = {};
		bool crect = false;			// the screen clip rect (set_clip) in force when drawn: x, y, w, h
		int cr[4] = {};
		bool fog = false;			// the fog (mesh_fog) in force when drawn
		fs2_sink_mesh_fog fogv = {};
		bool thr = false;			// a thruster flame (mesh_thrust) in force when drawn: z scaled by thrv[0] below thrv[1]
		float thrv[2] = {};
	};
	bool mesh_thr_on = false;
	float mesh_thr[2] = {};
	static void s_mesh_thrust(void *u, const float t[2]);
	bool mesh_clip_on = false;
	float mesh_clip_plane[4] = {};
	static void s_mesh_clip(void *u, const float plane[4]);
	fs2_sink_mesh_fog mesh_fog_state = {};
	static int s_mesh_fog(void *u, const fs2_sink_mesh_fog *fog);
	std::vector<godot::RID> mesh_mat_pool;
	std::map<int, std::vector<godot::RID>> mesh_col_pool;	// by rows
	void release_mesh_res(Layer &l, bool free_them);
	std::map<int32_t, MeshData> mesh_data;
	std::vector<MeshDraw> mesh_draws;
	std::map<uint32_t, godot::RID> mesh_shaders;
	// v5 billboards (R3): one shared unit quad; the vertex shader places the corners (docs/R3_billboards_design.md)
	struct BbDraw {
		fs2_sink_billboard b = {};
		int epoch = 0;
		int cw = 0, ch = 0, ox = 0, oy = 0;
		float scale[2] = {1.0f, 1.0f};	// Matrix_scale x, y
		bool crect = false;
		int cr[4] = {};
		int arr = -1, layer = 0;	// texture array chunk and layer, resolved at draw time
	};
	static constexpr int32_t kBbBase = 1 << 24;	// a batch whose key.texture is <= -kBbBase is billboard draw (-key.texture - kBbBase)
	std::vector<BbDraw> bb_draws;
	std::map<uint32_t, godot::RID> bb_shaders;
	godot::RID bb_quad;
	godot::RID billboard_shader(int kind, int blend, int zmode, bool a8, bool lin, bool crect, bool arr);
	// batched instancing: adjacent billboard draws of one state are one multimesh, one material, one instance (per-draw data in the transform + custom data)
	static constexpr int kBbBuckets = 5;
	static constexpr int kBbBucketCap[kBbBuckets] = {8, 32, 128, 512, 2048};
	std::vector<godot::RID> bb_mm_pool[kBbBuckets];
	std::vector<godot::RID> spare_bb_instances;
	std::vector<size_t> bb_run_idx;
	bool bb_same_run(const BbDraw &a, const BbDraw &b) const;
	void add_bb_run(Layer &l, const std::vector<size_t> &idx, int order);
	static int s_billboard_draw(void *u, const fs2_sink_billboard *b);
	godot::RID mesh_shader(int blend, int zmode, bool a8, bool lin, bool rpt, bool cull, bool lit, bool uclip, bool fog, bool ovl, bool thr, bool crect);
	godot::RID mesh_group_material(const MeshGroupState &st, const godot::RID &cols, bool lit, bool uclip, bool fog, bool thr, bool crect);
	static MeshGroupState mesh_group_state(const fs2_sink_mesh_group &g);
	static void s_mesh_state(void *u, int32_t id, const fs2_sink_mesh_group *g, int ng);
	void free_mesh_data(MeshData &md);
	void add_mesh_to_layer(Layer &l, const MeshDraw &d, int order);
	static void s_mesh_create(void *u, int32_t id, const fs2_sink_mesh_vertex *v, int nv, const fs2_sink_mesh_group *g, int ng,
		const uint32_t *idx, int ni);
	static void s_mesh_free(void *u, int32_t id);
	static void s_mesh_draw(void *u, int32_t id, const float *xform, const uint8_t *colours, int ncolours);
	static void s_mesh_draw_lit(void *u, int32_t id, const float *xform, const fs2_sink_mesh_light *light);
	bool have_view = false;
};

} // namespace fs2sink

#endif
