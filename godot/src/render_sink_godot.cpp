#include "render_sink_godot.h"
#include "opaque_reorder.h"
#include "perf_prof.h"

#include <algorithm>
#include <cstring>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector4.hpp>

using namespace godot;

static_assert(sizeof(Vector2) == 2 * sizeof(float), "godot-cpp must be built with single precision");
static_assert(sizeof(Color) == 4 * sizeof(float), "Color is four floats");

namespace fs2sink {

bool GodotSink::init(SubViewport *viewport)
{
	shutdown();
	if (viewport == nullptr) {
		return false;
	}
	Ref<World2D> world = viewport->find_world_2d();
	if (world.is_null()) {
		return false;
	}
	vp = viewport;
	arr_off = OS::get_singleton()->get_environment("FS2_BB_ARR_OFF") == String("1");
	canvas = world->get_canvas();
	sink.size = sizeof(sink);
	sink.version = FS2_RENDER_SINK_VERSION;
	sink.user = this;
	sink.frame_begin = &GodotSink::s_frame_begin;
	sink.frame_end = &GodotSink::s_frame_end;
	sink.clear = &GodotSink::s_clear;
	sink.set_clip = &GodotSink::s_set_clip;
	sink.texture_create = &GodotSink::s_texture_create;
	sink.texture_free = &GodotSink::s_texture_free;
	sink.texture_free_all = &GodotSink::s_texture_free_all;
	sink.texture_array_create = &GodotSink::s_texture_array_create;
	sink.texture_layer = &GodotSink::s_texture_layer;
	sink.texture_array_free = &GodotSink::s_texture_array_free;
	sink.draw_tris = &GodotSink::s_draw_tris;
	sink.capture_to_texture = &GodotSink::s_capture;
	sink.read_region = &GodotSink::s_read_region;
	sink.set_gamma = &GodotSink::s_set_gamma;
	sink.depth_begin = &GodotSink::s_depth_begin;
	sink.depth_end = &GodotSink::s_depth_end;
	sink.depth_clear = &GodotSink::s_depth_clear;
	sink.set_view = &GodotSink::s_set_view;
	sink.mesh_create = &GodotSink::s_mesh_create;
	sink.mesh_free = &GodotSink::s_mesh_free;
	sink.mesh_draw = &GodotSink::s_mesh_draw;
	sink.mesh_state = &GodotSink::s_mesh_state;
	sink.mesh_draw_lit = &GodotSink::s_mesh_draw_lit;
	sink.mesh_clip = &GodotSink::s_mesh_clip;
	sink.mesh_thrust = &GodotSink::s_mesh_thrust;
	sink.mesh_fog = &GodotSink::s_mesh_fog;
	sink.billboard_draw = &GodotSink::s_billboard_draw;
	sink.mesh_fog_mark = &GodotSink::s_mesh_fog_mark;
	{
		// the shared unit quad of the billboard draws, made here (no lazy creation): the vertex shader moves its four corners
		RenderingServer *rs = RenderingServer::get_singleton();
		PackedVector3Array qp; PackedVector2Array qu; PackedInt32Array qi;
		qp.resize(4); qu.resize(4); qi.resize(6);
		const float cx[4] = {0.0f, 1.0f, 1.0f, 0.0f}, cy[4] = {0.0f, 0.0f, 1.0f, 1.0f};
		for (int i = 0; i < 4; i++) { qp.ptrw()[i] = Vector3(cx[i], cy[i], 0.0f); qu.ptrw()[i] = Vector2(cx[i], cy[i]); }
		const int ids[6] = {3, 2, 1, 3, 1, 0};
		for (int i = 0; i < 6; i++) { qi.ptrw()[i] = ids[i]; }
		Array qa; qa.resize(RenderingServer::ARRAY_MAX);
		qa[RenderingServer::ARRAY_VERTEX] = qp; qa[RenderingServer::ARRAY_TEX_UV] = qu; qa[RenderingServer::ARRAY_INDEX] = qi;
		bb_quad = rs->mesh_create();
		rs->mesh_add_surface_from_arrays(bb_quad, RenderingServer::PRIMITIVE_TRIANGLES, qa);
		rs->mesh_set_custom_aabb(bb_quad, AABB(Vector3(-1.0e6f, -1.0e6f, -1.0e6f), Vector3(2.0e6f, 2.0e6f, 2.0e6f)));
		{
			PackedVector3Array fp; PackedInt32Array fi;
			fp.resize(5); fi.resize(9);
			for (int i = 0; i < 5; i++) { fp.ptrw()[i] = Vector3((float)i, 0.0f, 0.0f); }
			const int fids[9] = {0, 1, 2, 0, 2, 3, 0, 3, 4};
			for (int i = 0; i < 9; i++) { fi.ptrw()[i] = fids[i]; }
			Array fa; fa.resize(RenderingServer::ARRAY_MAX);
			fa[RenderingServer::ARRAY_VERTEX] = fp; fa[RenderingServer::ARRAY_INDEX] = fi;
			bb_fan5 = rs->mesh_create();
			rs->mesh_add_surface_from_arrays(bb_fan5, RenderingServer::PRIMITIVE_TRIANGLES, fa);
			rs->mesh_set_custom_aabb(bb_fan5, AABB(Vector3(-1.0e6f, -1.0e6f, -1.0e6f), Vector3(2.0e6f, 2.0e6f, 2.0e6f)));
		}
	}
	batcher.set_emitter(this);
	return true;
}

void GodotSink::shutdown()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr) {
		prewarm_release();
		for (FogSlot &fs : fog_slots) {
			fog_release_slot(fs);
			if (fs.vp.is_valid()) { rs->free_rid(fs.vp); }
			if (fs.canvas.is_valid()) { rs->free_rid(fs.canvas); }
			fs = FogSlot();
		}
		release_layers(pending_layers);
		release_layers(shown_layers);
		for (Layer &l : spare_layers) {
			free_layer(l);
		}
		for (auto &m : spatial_materials) {
			rs->free_rid(m.second);
		}
		for (auto &m : mesh_data) { free_mesh_data(m.second); }
		mesh_data.clear();
		for (auto &sh : mesh_shaders) { rs->free_rid(sh.second); }
		mesh_shaders.clear();
		for (RID &r : gpu_tab) {
			if (r.is_valid()) { rs->free_rid(r); }
			r = RID();
		}
		for (auto &sh : spatial_shaders) {
			rs->free_rid(sh.second);
		}
		if (composite_material.is_valid()) {
			rs->free_rid(composite_material);
		}
		if (composite_shader.is_valid()) {
			rs->free_rid(composite_shader);
		}
		if (white_tex.is_valid()) {
			rs->free_rid(white_tex);
		}
		if (probe_vp.is_valid()) {
			rs->free_rid(probe_vp);
		}
		release_items(pending_items, pending_root);
		release_items(shown_items, shown_root);
		for (RID &r : spare_items) {
			rs->free_rid(r);
		}
		release_meshes(pending_meshes);
		release_meshes(shown_meshes);
		for (RID &r : spare_meshes) {
			rs->free_rid(r);
		}
		for (auto &t : textures) {
			rs->free_rid(t.second.rid);
		}
		for (Dying &d : dying) {
			rs->free_rid(d.rid);
		}
		for (Pooled &p : tex_pool) {
			rs->free_rid(p.rid);
		}
		for (ArrChunk &c : arr_chunks) { rs->free_rid(c.rid); }
		for (RID &r : spare_instances) {
			rs->free_rid(r);
		}
		for (RID &r : spare_bb_instances) { rs->free_rid(r); }
		for (auto &pool : bb_mm_pool) { for (RID &r : pool) { rs->free_rid(r); } pool.clear(); }
		for (RID &r : mesh_mat_pool) { rs->free_rid(r); }
		mesh_mat_pool.clear();
		for (auto &m : bb_mats) { rs->free_rid(m.second); }
		if (bb_quad.is_valid()) { rs->free_rid(bb_quad); bb_quad = RID(); }
		if (bb_fan5.is_valid()) { rs->free_rid(bb_fan5); bb_fan5 = RID(); }
		for (auto &pool : mesh_col_pool) { for (RID &r : pool.second) { rs->free_rid(r); } }
		mesh_col_pool.clear();
		for (auto &sh : bb_shaders) { rs->free_rid(sh.second); }	// #225: the billboard shaders, after the materials that used them
		bb_shaders.clear();
		for (int i = 0; i < 12; i++) {
			if (materials[i].is_valid()) {
				rs->free_rid(materials[i]);
			}
			if (shaders[i].is_valid()) {
				rs->free_rid(shaders[i]);
			}
			materials[i] = RID();
			shaders[i] = RID();
		}
	}
	batcher.discard();
	batcher.set_depth_run(false, 0, 0, 0, 0, 0);
	spare_layers.clear();
	spatial_materials.clear();
	bb_mats.clear();
	spatial_shaders.clear();
	composite_material = RID();
	composite_shader = RID();
	white_tex = RID();
	probe_vp = RID();
	probe_w = probe_h = 0;
	run_batches.clear();
	in_run = false;
	spare_items.clear();
	spare_meshes.clear();
	pending_meshes.clear();
	shown_meshes.clear();
	textures.clear();
	dying.clear();
	tex_pool.clear();
	arr_chunks.clear();
	spare_instances.clear();
	spare_bb_instances.clear();
	vp = nullptr;
	in_frame = false;
	warmed = false;
	warm_age = 0;
}

RID GodotSink::new_item(const RID &under)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	fs2perf::count(fs2perf::C_CANVAS_ITEMS);
	RID item;
	if (!spare_items.empty()) {
		item = spare_items.back();
		spare_items.pop_back();
	} else {
		item = rs->canvas_item_create();
	}
	rs->canvas_item_set_parent(item, under);
	// Godot sorts sibling items by draw index, and its sort of more than 16 is not stable: give every item its own index
	rs->canvas_item_set_draw_index(item, next_index++);
	return item;
}

// Items go back to the pool cleared and unparented; the root is freed.
void GodotSink::release_items(std::vector<RID> &items, RID &root)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (RID &r : items) {
		rs->canvas_item_clear(r);
		rs->canvas_item_set_clip(r, false);
		rs->canvas_item_set_custom_rect(r, false);
		rs->canvas_item_set_material(r, RID());
		rs->canvas_item_set_parent(r, RID());
		if (g_nebfog == kNebFogGpu) { rs->canvas_item_set_visibility_layer(r, 1); }
		spare_items.push_back(r);
	}
	items.clear();
	if (root.is_valid()) {
		rs->free_rid(root);
		root = RID();
	}
}

void GodotSink::release_meshes(std::vector<RID> &meshes)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (RID &m : meshes) {
		rs->mesh_clear(m);
		spare_meshes.push_back(m);
	}
	meshes.clear();
}

void GodotSink::open_clip_item()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	RID item = new_item(pending_root);
	pending_items.push_back(item);
	rs->canvas_item_set_custom_rect(item, true, Rect2((float)clip[0], (float)clip[1], (float)clip[2], (float)clip[3]));
	rs->canvas_item_set_clip(item, true);
	parent = item;
}

void GodotSink::set_clip_state(int x, int y, int w, int h)
{
	const bool n_empty = (w <= 0 || h <= 0);
	const bool n_full = !n_empty && x <= 0 && y <= 0 && x + w >= width && y + h >= height;
	const bool n_clipped = !n_empty && !n_full;
	if (n_empty == clip_empty && n_clipped == clipped && (!n_clipped || (clip[0] == x && clip[1] == y && clip[2] == w && clip[3] == h))) {
		return; // nothing changes: the batch goes on
	}
	batcher.flush();
	clip_empty = n_empty;
	if (!n_clipped) {
		clipped = false;
		if (in_run) {
			sync_batcher_clip();
			return;
		}
		parent = pending_root;
		return;
	}
	clip[0] = x; clip[1] = y; clip[2] = w; clip[3] = h;
	clipped = true;
	if (in_run) {
		sync_batcher_clip(); // a depth run clips on the CPU; the canvas clip item is opened when the run ends
		return;
	}
	open_clip_item();
}

void GodotSink::sync_batcher_clip()
{
	if (clipped) {
		batcher.set_depth_run(in_run, run_epoch, clip[0], clip[1], clip[2], clip[3]);
	} else {
		batcher.set_depth_run(in_run, run_epoch, 0, 0, width, height);
	}
}

int GodotSink::s_frame_begin(void *u, int w, int h)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	fs2perf::count(fs2perf::C_SINK_FRAME_BEGIN);
	if (s->vp == nullptr || s->frames_left > 1) {
		return 0;
	}
	fs2perf::Zone _pz(fs2perf::Z_FRAME_BEGIN);
	fs2perf::count(fs2perf::C_SINK_FRAME_ACCEPTED);
	RenderingServer *rs = RenderingServer::get_singleton();
	if constexpr (kPrewarm) {
		if (!s->warmed) {
			s->warmed = true;
			s->prewarm();
		}
	}
	if (w != s->width || h != s->height) {
		s->width = w;
		s->height = h;
		s->vp->set_size(Vector2i(w, h));
	}
	if (g_nebfog == kNebFogGpu) {
		s->batcher.set_fog_gpu(true, w, h);
		s->batcher.fog_table_clear(s->committed);
	}
	// a frame that was started and never ended is dropped
	s->next_index = 0;
	s->batcher.discard();
	s->in_run = false;
	s->run_batches.clear();
	s->batcher.set_depth_run(false, 0, 0, 0, 0, 0);
	s->release_layers(s->pending_layers);
	s->release_items(s->pending_items, s->pending_root);
	s->release_meshes(s->pending_meshes);
	s->pending_root = rs->canvas_item_create();
	rs->canvas_item_set_parent(s->pending_root, s->canvas);
	rs->canvas_item_set_visible(s->pending_root, false);
	s->parent = s->pending_root;
	s->clipped = false;
	s->clip_empty = false;
	s->in_frame = true;
	return 1;
}

void GodotSink::s_frame_end(void *u)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame) {
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_FRAME_END);
	if (!s->warm_viewports.empty() && ++s->warm_age >= kPrewarmFrames) {
		s->prewarm_release();
	}
	const uint64_t t0 = godot::Time::get_singleton()->get_ticks_usec();
	if (s->in_run) {
		s->end_run();
	}
	s->batcher.flush();
	if (g_nebfog == kNebFogGpu) {
		s->gpu_frame_end();
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->canvas_item_set_visible(s->pending_root, true);
	s->release_layers(s->shown_layers);
	s->shown_layers.swap(s->pending_layers);
	s->release_items(s->shown_items, s->shown_root);
	s->release_meshes(s->shown_meshes);
	s->shown_root = s->pending_root;
	s->shown_items.swap(s->pending_items);
	s->shown_meshes.swap(s->pending_meshes);
	s->pending_root = RID();
	s->in_frame = false;
	s->committed++;
	s->age_dying(false);
	s->stat_sink_usec += godot::Time::get_singleton()->get_ticks_usec() - t0;
}

void GodotSink::s_clear(void *u, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame) {
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_CLEAR);
	fs2perf::count(fs2perf::C_SINK_CLEAR);
	s->batcher.flush();
	if (s->in_run) {
		// inside a depth run the clear is a full-screen opaque quad of the run, no depth test
		s->batcher.set_depth_run(true, s->run_epoch, 0, 0, s->width, s->height);
		const float W = (float)s->width, Hh = (float)s->height;
		fs2_sink_vertex v[4] = {};
		v[1].x = W; v[2].x = W; v[2].y = Hh; v[3].y = Hh;
		for (fs2_sink_vertex &q : v) { q.r = r; q.g = g; q.b = b; q.a = a; q.sw = 1.0f; }
		static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
		fs2_sink_draw d = {};
		d.blend = FS2_SINK_BLEND_OPAQUE;
		d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 255;
		s->batcher.add(d, v, 4, idx, 6, 0, 0);
		s->batcher.flush();
		s->sync_batcher_clip();
		return;
	}
	// a clear is a full-screen rectangle at this point of the order, ignoring the clip
	RenderingServer *rs = RenderingServer::get_singleton();
	RID item = s->new_item(s->pending_root);
	s->pending_items.push_back(item);
	rs->canvas_item_add_rect(item, Rect2(0.0f, 0.0f, (float)s->width, (float)s->height), Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f));
	if (s->clipped) {
		s->open_clip_item(); // later items must come after the clear in the order
	}
}

void GodotSink::s_set_clip(void *u, int x, int y, int w, int h)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (s->in_frame) {
		fs2perf::Zone _pz(fs2perf::Z_SET_CLIP);
		fs2perf::count(fs2perf::C_SINK_SET_CLIP);
		s->set_clip_state(x, y, w, h);
	}
}

void GodotSink::retire(const Tex &t)
{
	Dying d;
	d.rid = t.rid;
	d.w = t.w; d.h = t.h; d.format = t.format;
	d.arr = t.arr; d.layer = t.layer;
	dying.push_back(d);
}

// A retired texture is freed after two frame_ends: the frame on show may still draw it.
void GodotSink::age_dying(bool force_all)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	size_t keep = 0;
	for (size_t i = 0; i < dying.size(); i++) {
		dying[i].age++;
		if (force_all || dying[i].age >= 2) {
			drop_materials_of(dying[i].rid);
			if (dying[i].arr >= 0 && dying[i].arr < (int)arr_chunks.size() && !force_all) { arr_chunks[(size_t)dying[i].arr].free_layers.push_back(dying[i].layer); }
			if (kTexturePool && !force_all) {
				Pooled p;
				p.rid = dying[i].rid; p.w = dying[i].w; p.h = dying[i].h; p.format = dying[i].format;
				tex_pool.push_back(p);
			} else {
				rs->free_rid(dying[i].rid);
			}
		} else {
			dying[keep++] = dying[i];
		}
	}
	dying.resize(keep);
	while (tex_pool.size() > kTexPoolMax) {
		rs->free_rid(tex_pool.front().rid);
		tex_pool.erase(tex_pool.begin());
	}
}

void GodotSink::s_texture_create(void *u, int32_t id, int w, int h, int format, uint32_t flags, const uint8_t *pixels)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (id == 0 || w <= 0 || h <= 0 || pixels == nullptr) {	// legal outside a frame too: level load uploads before the first frame
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_TEX_CREATE);
	if (s->in_frame) { fs2perf::count(fs2perf::C_TEX_LATE); }
	fs2perf::count(fs2perf::C_TEX_CREATE);
	fs2perf::count(fs2perf::C_TEX_BYTES, (uint64_t)w * h * (format == FS2_SINK_TEX_A8 ? 1 : 4));
	s->batcher.flush(); // pending draws resolve their texture id at emit: they must see the old one
	const bool a8 = (format == FS2_SINK_TEX_A8);
	PackedByteArray bytes;
	const int64_t n = (int64_t)w * h * (a8 ? 1 : 4);
	bytes.resize(n);
	std::memcpy(bytes.ptrw(), pixels, (size_t)n);
	Ref<Image> img = Image::create_from_data(w, h, false, a8 ? Image::FORMAT_R8 : Image::FORMAT_RGBA8, bytes);
	auto it = s->textures.find(id);
	if (it != s->textures.end()) {
		s->retire(it->second);
	}
	Tex t;
	RenderingServer *rs = RenderingServer::get_singleton();
	bool reused = false;
	if (kTexturePool) {
		for (size_t i = s->tex_pool.size(); i-- > 0;) {
			if (s->tex_pool[i].w == w && s->tex_pool[i].h == h && s->tex_pool[i].format == format) {
				t.rid = s->tex_pool[i].rid;
				s->tex_pool.erase(s->tex_pool.begin() + (std::ptrdiff_t)i);
				rs->texture_2d_update(t.rid, img, 0);
				reused = true;
				break;
			}
		}
	}
	if (!reused) {
		t.rid = rs->texture_2d_create(img);
	}
	t.w = w; t.h = h; t.format = format; t.flags = flags;
	if (w <= kArrMaxDim && h <= kArrMaxDim && s->arr_alloc(w, h, format, (flags & FS2_SINK_TEXF_LINEAR) != 0, t.arr, t.layer)) {
		rs->texture_2d_update(s->arr_chunks[(size_t)t.arr].rid, img, t.layer);
	}
	s->textures[id] = t;
}

// A free layer of a chunk of this group, or a new chunk (all layers blank until a texture is uploaded into one). False with arrays off.
bool GodotSink::arr_alloc(int w, int h, int format, bool lin, int &chunk, int &layer)
{
	if (arr_off) { return false; }
	for (size_t i = 0; i < arr_chunks.size(); i++) {
		ArrChunk &c = arr_chunks[i];
		if (c.w == w && c.h == h && c.format == format && c.lin == lin && !c.free_layers.empty()) {
			chunk = (int)i; layer = c.free_layers.back(); c.free_layers.pop_back();
			return true;
		}
	}
	int nsame = 0;	// chunks of this group so far: each new one is twice the last (8, 16, 32, 64), so a small group wastes little
	for (const ArrChunk &o : arr_chunks) { if (o.w == w && o.h == h && o.format == format && o.lin == lin) { nsame++; } }
	const int cap = std::min(kArrCap, 8 << std::min(nsame, 3));
	TypedArray<Ref<Image>> layers;
	for (int i = 0; i < cap; i++) {
		layers.push_back(Image::create(w, h, false, format == FS2_SINK_TEX_A8 ? Image::FORMAT_R8 : Image::FORMAT_RGBA8));
	}
	ArrChunk c;
	c.rid = RenderingServer::get_singleton()->texture_2d_layered_create(layers, RenderingServer::TEXTURE_LAYERED_2D_ARRAY);
	c.w = w; c.h = h; c.format = format; c.lin = lin;
	for (int i = cap - 1; i >= 1; i--) { c.free_layers.push_back(i); }
	fs2perf::count(fs2perf::C_ARR_BYTES, (uint64_t)w * h * cap * (format == FS2_SINK_TEX_A8 ? 1 : 4));
	arr_chunks.push_back(c);
	chunk = (int)arr_chunks.size() - 1;
	layer = 0;
	return true;
}

void GodotSink::s_texture_free(void *u, int32_t id)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	fs2perf::Zone _pz(fs2perf::Z_TEX_FREE);
	fs2perf::count(fs2perf::C_TEX_FREE);
	s->batcher.flush();
	auto it = s->textures.find(id);
	if (it != s->textures.end()) {
		s->retire(it->second);
		s->textures.erase(it);
	}
}

// An animation as one array of exactly nlayers (the host knows the frame count): no blank layers, uploaded at level load.
void GodotSink::s_texture_array_create(void *u, int32_t array_id, int w, int h, int nlayers, int format, uint32_t flags, const uint8_t *pixels)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (array_id == 0 || w <= 0 || h <= 0 || nlayers <= 0 || pixels == nullptr || s->anim_arrays.count(array_id) != 0) { return; }
	const bool as_array = !s->arr_off && w <= kArrMaxDim && h <= kArrMaxDim;	// otherwise the frames become single textures, as texture_create would make them
	fs2perf::Zone _pz(fs2perf::Z_TEX_CREATE);
	if (s->in_frame) { fs2perf::count(fs2perf::C_TEX_LATE); }
	s->batcher.flush();
	const bool a8 = (format == FS2_SINK_TEX_A8);
	const int64_t fb = (int64_t)w * h * (a8 ? 1 : 4);
	AnimArr a;
	TypedArray<Ref<Image>> layers;
	for (int i = 0; i < nlayers; i++) {
		PackedByteArray bytes;
		bytes.resize(fb);
		std::memcpy(bytes.ptrw(), pixels + (size_t)fb * (size_t)i, (size_t)fb);
		Ref<Image> img = Image::create_from_data(w, h, false, a8 ? Image::FORMAT_R8 : Image::FORMAT_RGBA8, bytes);
		layers.push_back(img);
		a.imgs.push_back(img);
	}
	a.w = w; a.h = h; a.format = format; a.flags = flags;
	if (as_array) {
		ArrChunk c;
		c.rid = RenderingServer::get_singleton()->texture_2d_layered_create(layers, RenderingServer::TEXTURE_LAYERED_2D_ARRAY);
		c.w = w; c.h = h; c.format = format; c.lin = (flags & FS2_SINK_TEXF_LINEAR) != 0;
		fs2perf::count(fs2perf::C_ARR_BYTES, (uint64_t)fb * (uint64_t)nlayers);
		s->arr_chunks.push_back(c);
		a.chunk = (int)s->arr_chunks.size() - 1;
	}
	s->anim_arrays[array_id] = a;
}

void GodotSink::s_texture_layer(void *u, int32_t id, int32_t array_id, int layer)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	auto it = s->anim_arrays.find(array_id);
	if (id == 0 || it == s->anim_arrays.end() || layer < 0 || layer >= (int)it->second.imgs.size() || s->textures.count(id) != 0) { return; }
	fs2perf::Zone _pz(fs2perf::Z_TEX_CREATE);
	if (s->in_frame) { fs2perf::count(fs2perf::C_TEX_LATE); }
	fs2perf::count(fs2perf::C_TEX_CREATE);
	AnimArr &a = it->second;
	Tex t;
	t.rid = RenderingServer::get_singleton()->texture_2d_create(a.imgs[(size_t)layer]);
	t.w = a.w; t.h = a.h; t.format = a.format; t.flags = a.flags;
	t.arr = a.chunk; t.layer = layer;
	s->textures[id] = t;
	if (++a.claimed >= (int)a.imgs.size()) { s->anim_arrays.erase(it); }
}

// The frames are freed one by one by texture_free (their layers become reusable); nothing else to drop.
void GodotSink::s_texture_array_free(void *u, int32_t array_id)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	s->anim_arrays.erase(array_id);
}

void GodotSink::s_texture_free_all(void *u)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	s->batcher.flush();
	for (auto &t : s->textures) {
		s->retire(t.second);
	}
	s->textures.clear();
}

// One shader per (blend, A8, perspective). Plain alpha blending of an RGBA texture needs none: the canvas default does it.
// Vertex colour goes through a varying so the result does not depend on what COLOR holds when fragment() starts.
RID GodotSink::material_for(int blend, bool a8, bool persp)
{
	if (blend == FS2_SINK_BLEND_ALPHA && !a8 && !persp) {
		return RID();
	}
	const int i = blend + 3 * ((a8 ? 1 : 0) + 2 * (persp ? 1 : 0));
	if (blend < 0 || blend > 2 || i >= 12) {
		return RID();
	}
	if (!materials[i].is_valid()) {
		RenderingServer *rs = RenderingServer::get_singleton();
		// ADD is D3D's ONE:ONE: premultiplied blending with alpha 0 adds the colour and leaves the alpha alone
		const char *mode = blend == FS2_SINK_BLEND_ADD ? "blend_premul_alpha" : (blend == FS2_SINK_BLEND_OPAQUE ? "blend_disabled" : "blend_mix");
		String code = String("shader_type canvas_item;\nrender_mode ") + mode + ";\nvarying vec4 vcol;\n";
		if (persp) {
			code += "varying vec3 pc;\n";
		}
		code += "void vertex() {\n\tvcol = COLOR;\n";
		if (persp) {
			code += "\tpc = CUSTOM0.xyz;\n";
		}
		code += "}\nvoid fragment() {\n";
		code += persp ? "\tvec4 t = texture(TEXTURE, pc.xy / pc.z);\n" : "\tvec4 t = texture(TEXTURE, UV);\n";
		if (a8) {
			code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n";
		}
		code += "\tCOLOR = vcol * t;\n";
		if (blend == FS2_SINK_BLEND_OPAQUE) {
			code += "\tCOLOR.a = 1.0;\n";
		} else if (blend == FS2_SINK_BLEND_ADD) {
			code += "\tCOLOR.a = 0.0;\n";
		}
		code += "}\n";
		shaders[i] = rs->shader_create();
		rs->shader_set_code(shaders[i], code);
		materials[i] = rs->material_create();
		rs->material_set_shader(materials[i], shaders[i]);
	}
	return materials[i];
}

void GodotSink::emit(const Batch &b)
{
	if (!in_frame) {
		return;
	}
	fs2perf::count(fs2perf::C_BATCHES);
	fs2perf::count(fs2perf::C_BATCH_VERTS, (uint64_t)b.nverts());
	if (b.key.depth != 0) {
		run_batches.push_back(b); // placed at depth_end
		return;
	}
	emit_canvas(b);
}

// A batch becomes one canvas item with one array draw (affine) or one mesh draw (perspective: the mesh carries CUSTOM0).
void GodotSink::emit_canvas(const Batch &b)
{
	fs2perf::Zone _pz(fs2perf::Z_EMIT_CANVAS);
	RenderingServer *rs = RenderingServer::get_singleton();
	const int nv = b.nverts();
	const int ni = (int)b.idx.size();
	RID tex;
	bool a8 = false;
	uint32_t tflags = 0;
	if (b.key.texture != 0) {
		auto it = textures.find(b.key.texture);
		if (it == textures.end()) {
			return;
		}
		tex = it->second.rid;
		a8 = (it->second.format == FS2_SINK_TEX_A8);
		tflags = it->second.flags;
	}
	// Consecutive batches with the same material and sampling share one canvas item (one draw command each): thousands of items per frame lose draws in Godot's canvas renderer.
	const uint32_t sig = ((uint32_t)b.key.blend << 8) | (a8 ? 4u : 0u) | (b.key.perspective != 0 ? 2u : 0u) | 0x10000u | ((tflags & FS2_SINK_TEXF_LINEAR) ? 0x20000u : 0u) | ((tflags & FS2_SINK_TEXF_REPEAT) ? 0x40000u : 0u);
	RID item;
	if (cur_item.is_valid() && cur_sig == sig && cur_parent == parent && !pending_items.empty() && pending_items.back() == cur_item) {
		item = cur_item;
	} else {
		item = new_item(parent);
		pending_items.push_back(item);
		rs->canvas_item_set_default_texture_filter(item, (tflags & FS2_SINK_TEXF_LINEAR) ? RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR : RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
		rs->canvas_item_set_default_texture_repeat(item, (tflags & FS2_SINK_TEXF_REPEAT) ? RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED : RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		rs->canvas_item_set_material(item, material_for(b.key.blend, a8, b.key.perspective != 0));
		cur_item = item;
		cur_sig = sig;
		cur_parent = parent;
	}

	PackedInt32Array indices;
	PackedVector2Array points, uvs;
	PackedColorArray colors;
	indices.resize(ni);
	points.resize(nv);
	uvs.resize(nv);
	colors.resize(nv);
	std::memcpy(indices.ptrw(), b.idx.data(), sizeof(int32_t) * (size_t)ni);
	std::memcpy(points.ptrw(), b.pos.data(), sizeof(float) * 2 * (size_t)nv);
	std::memcpy(uvs.ptrw(), b.uv.data(), sizeof(float) * 2 * (size_t)nv);
	std::memcpy(colors.ptrw(), b.col.data(), sizeof(float) * 4 * (size_t)nv);

	if (b.key.perspective == 0) {
		rs->canvas_item_add_triangle_array(item, indices, points, colors, uvs, PackedInt32Array(), PackedFloat32Array(), tex);
		return;
	}
	PackedFloat32Array custom;
	custom.resize((int64_t)nv * 4);
	std::memcpy(custom.ptrw(), b.custom.data(), sizeof(float) * 4 * (size_t)nv);
	Array arrays;
	arrays.resize(RenderingServer::ARRAY_MAX);
	arrays[RenderingServer::ARRAY_VERTEX] = points;
	arrays[RenderingServer::ARRAY_COLOR] = colors;
	arrays[RenderingServer::ARRAY_TEX_UV] = uvs;
	arrays[RenderingServer::ARRAY_CUSTOM0] = custom;
	arrays[RenderingServer::ARRAY_INDEX] = indices;
	fs2perf::Zone _pm(fs2perf::Z_MESH_BUILD);
	fs2perf::count(fs2perf::C_MESHES);
	RID mesh;
	if (!spare_meshes.empty()) {
		mesh = spare_meshes.back();
		spare_meshes.pop_back();
	} else {
		mesh = rs->mesh_create();
	}
	const int64_t fmt = (int64_t)RenderingServer::ARRAY_FLAG_USE_2D_VERTICES | ((int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT);
	rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)fmt);
	pending_meshes.push_back(mesh);
	rs->canvas_item_add_mesh(item, mesh, Transform2D(), Color(1, 1, 1, 1), tex);
}

void GodotSink::s_draw_tris(void *u, const fs2_sink_draw *d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame || s->clip_empty || nv <= 0 || ni < 3) {
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_DRAW_TRIS);
	fs2perf::count(fs2perf::C_SINK_DRAW_CALLS);
	fs2perf::count(fs2perf::C_SINK_VERTS, (uint64_t)nv);
	fs2perf::count(fs2perf::C_SINK_INDICES, (uint64_t)ni);
	int tw = 0, th = 0;
	if (d->texture != 0) {
		auto it = s->textures.find(d->texture);
		if (it == s->textures.end()) {
			return; // the host drew with a texture it never made: nothing to show
		}
		tw = it->second.w;
		th = it->second.h;
	}
	if (g_nebfog != kNebFogOff) {
		// the fog read ends the sink's run (s_read_region) but the host believes it is still open and never begins another: a depth draw outside a run opens one
		if (!s->in_run && (d->flags & (FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_ZWRITE)) != 0) {
			s_depth_begin(s);
		}
	}
	const uint64_t t0 = godot::Time::get_singleton()->get_ticks_usec();
	if (g_nebfog == kNebFogOff) {
		// kNebFog off: the draw is what it was before R2, fog fields ignored (the host still sets them for fogged polys)
		fs2_sink_draw nofog = *d;
		nofog.fog_mode = FS2_SINK_FOG_NONE;
		s->batcher.add(nofog, v, nv, idx, ni, tw, th);
	} else {
		s->batcher.add(*d, v, nv, idx, ni, tw, th);
	}
	s->stat_sink_usec += godot::Time::get_singleton()->get_ticks_usec() - t0;
}

// The capture is the last frame the viewport has rendered (the frame on show), as read_region does.
int GodotSink::s_capture(void *u, int32_t id)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	fs2perf::Zone _pz(fs2perf::Z_CAPTURE);
	fs2perf::count(fs2perf::C_CAPTURE);
	s->batcher.flush();
	if (s->vp == nullptr || id == 0) {
		return 0;
	}
	RenderingServer::get_singleton()->force_draw(false); // render what frame_end has shown: the viewport has not drawn it yet
	Ref<Image> img = s->vp->get_texture()->get_image();
	if (img.is_null() || img->is_empty()) {
		return 0;
	}
	img->convert(Image::FORMAT_RGBA8);
	auto it = s->textures.find(id);
	if (it != s->textures.end()) {
		s->retire(it->second);
	}
	Tex t;
	t.rid = RenderingServer::get_singleton()->texture_2d_create(img);
	t.w = img->get_width(); t.h = img->get_height(); t.format = FS2_SINK_TEX_RGBA8; t.flags = 0;
	s->textures[id] = t;
	return 1;
}

// The w x h top left corner of a probe image as RGBA8 (missing pixels black)
static void copy_probe_pixels(Ref<Image> img, int w, int h, uint8_t *out)
{
	img->convert(Image::FORMAT_RGBA8);
	const int iw = img->get_width(), ih = img->get_height();
	const PackedByteArray data = img->get_data();
	const uint8_t *src = data.ptr();
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			uint8_t *d = out + ((size_t)row * (size_t)w + (size_t)col) * 4;
			if (col >= iw || row >= ih) {
				d[0] = d[1] = d[2] = 0; d[3] = 255;
			} else {
				memcpy(d, src + ((size_t)row * (size_t)iw + (size_t)col) * 4, 4);
			}
		}
	}
}

// The fog read (R2): render what the frame has drawn so far into a w x h viewport and read it. The frame is under pending_root
// (hidden until frame_end) and its layers; the probe viewport shows the same canvas, so pending_root is made visible for one
// force_draw with the shown frame hidden and the game viewport switched off (it would draw the old frame for nothing). The layers of
// the frame take the probe as their parent for that draw, so Godot renders them before it. Same-frame, exact; costs one
// force_draw of the layers plus a w x h readback (kNebFog, perf_profile).
int GodotSink::probe_read(int w, int h, uint8_t *out)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (vp == nullptr || !pending_root.is_valid() || out == nullptr) {
		return 0;
	}
	if (!probe_vp.is_valid()) {
		probe_vp = rs->viewport_create();
		rs->viewport_attach_canvas(probe_vp, canvas);
		rs->viewport_set_transparent_background(probe_vp, false);
		rs->viewport_set_active(probe_vp, true);
		rs->viewport_set_update_mode(probe_vp, RenderingServer::VIEWPORT_UPDATE_DISABLED);
	}
	if (probe_w != w || probe_h != h) {
		probe_w = w;
		probe_h = h;
		rs->viewport_set_size(probe_vp, w, h);
	}
	const RID game = vp->get_viewport_rid();
	rs->viewport_set_active(game, false);
	if (shown_root.is_valid()) { rs->canvas_item_set_visible(shown_root, false); }
	rs->canvas_item_set_visible(pending_root, true);
	for (Layer &l : shown_layers) { rs->viewport_set_update_mode(l.viewport, RenderingServer::VIEWPORT_UPDATE_DISABLED); }
	for (Layer &l : pending_layers) { rs->viewport_set_parent_viewport(l.viewport, probe_vp); }
	rs->viewport_set_update_mode(probe_vp, RenderingServer::VIEWPORT_UPDATE_ONCE);
	rs->force_draw(false);
	Ref<Image> img = rs->texture_2d_get(rs->viewport_get_texture(probe_vp));
	for (Layer &l : pending_layers) { rs->viewport_set_parent_viewport(l.viewport, game); }
	for (Layer &l : shown_layers) { rs->viewport_set_update_mode(l.viewport, RenderingServer::VIEWPORT_UPDATE_ALWAYS); }
	rs->canvas_item_set_visible(pending_root, false);
	if (shown_root.is_valid()) { rs->canvas_item_set_visible(shown_root, true); }
	rs->viewport_set_active(game, true);
	if (img.is_null() || img->is_empty()) {
		return 0;
	}
	copy_probe_pixels(img, w, h, out);
	return 1;
}

void GodotSink::fog_release_slot(FogSlot &sl)
{
	if (!sl.live) {
		return;
	}
	release_layers(sl.layers);
	release_meshes(sl.meshes);
	release_items(sl.items, sl.root);
	sl.live = false;
}

void GodotSink::fog_slot_init(FogSlot &sl)
{
	if (sl.vp.is_valid()) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	sl.vp = rs->viewport_create();
	if (g_nebfog == kNebFogGpu) {
		// the slot renders the frame's own canvas, only the items with visibility bit 2 (the pre-render, marked by fog_capture_gpu)
		rs->viewport_attach_canvas(sl.vp, canvas);
		rs->viewport_set_canvas_cull_mask(sl.vp, 2);
	} else {
		sl.canvas = rs->canvas_create();
		rs->viewport_attach_canvas(sl.vp, sl.canvas);
	}
	rs->viewport_set_transparent_background(sl.vp, false);
	rs->viewport_set_active(sl.vp, true);
	rs->viewport_set_update_mode(sl.vp, RenderingServer::VIEWPORT_UPDATE_DISABLED);
}

// Lag mode, step 1: the frame built so far (the pre-render; the draws that follow are covered by the clear after the read) leaves the
// frame for a slot of the ring. Its viewport is rendered once by the next Godot draw, the layers before it (their parent).
void GodotSink::fog_capture(int w, int h)
{
	fs2perf::Zone _cz(fs2perf::Z_FOG_CAPTURE);
	RenderingServer *rs = RenderingServer::get_singleton();
	if (in_run) {
		end_run();
	}
	batcher.flush();
	FogSlot &sl = fog_slots[committed % (kNebFogLagFrames + 1)];
	fog_release_slot(sl); // the slot of drawn frame k - L - 1: read already
	fog_slot_init(sl);
	if (sl.w != w || sl.h != h) {
		sl.w = w;
		sl.h = h;
		rs->viewport_set_size(sl.vp, w, h);
	}
	rs->canvas_item_set_parent(pending_root, sl.canvas);
	rs->canvas_item_set_visible(pending_root, true);
	sl.root = pending_root;
	sl.items.swap(pending_items);
	sl.meshes.swap(pending_meshes);
	sl.layers.swap(pending_layers);
	for (Layer &l : sl.layers) {
		rs->viewport_set_parent_viewport(l.viewport, sl.vp);
		rs->viewport_set_update_mode(l.viewport, RenderingServer::VIEWPORT_UPDATE_ONCE);
		l.probe_parent = true;
	}
	rs->viewport_set_update_mode(sl.vp, RenderingServer::VIEWPORT_UPDATE_ONCE);
	sl.stamp = committed;
	sl.drawn = Engine::get_singleton()->get_frames_drawn();
	sl.live = true;
	// the rest of the frame goes on in a fresh root
	pending_root = rs->canvas_item_create();
	rs->canvas_item_set_parent(pending_root, canvas);
	rs->canvas_item_set_visible(pending_root, false);
	cur_item = RID();
	parent = pending_root;
	if (clipped) {
		open_clip_item();
	}
}

// kNebFogGpu: the pre-render is not moved: what the frame has drawn so far is marked (visibility bits 1|2) and the slot viewport, which
// renders the same canvas with cull mask 2, draws just those; the frame itself keeps them, as with the same-frame probe, so what it shows
// is what it showed before (the frame start clear is under them). Items made after this have bit 1 only (and hide from the slot even
// when their parent is marked).
void GodotSink::fog_capture_gpu(int w, int h)
{
	fs2perf::Zone _cz(fs2perf::Z_FOG_CAPTURE);
	RenderingServer *rs = RenderingServer::get_singleton();
	if (in_run) {
		end_run();
	}
	batcher.flush();
	FogSlot &sl = fog_slots[committed & 1];
	fog_slot_init(sl);
	if (sl.w != w || sl.h != h) {
		sl.w = w;
		sl.h = h;
		rs->viewport_set_size(sl.vp, w, h);
	}
	rs->canvas_item_set_visibility_layer(pending_root, 3);
	for (const RID &r : pending_items) {
		rs->canvas_item_set_visibility_layer(r, 3);
	}
	gpu_pre_layers = (int)pending_layers.size();
	rs->viewport_set_update_mode(sl.vp, RenderingServer::VIEWPORT_UPDATE_ONCE);
	sl.stamp = committed;
	sl.drawn = Engine::get_singleton()->get_frames_drawn();
	sl.live = true;
	cur_item = RID();	// later batches must not join a marked item
	parent = pending_root;
	if (clipped) {
		open_clip_item();
	}
}

// Lag mode: the colour of drawn frame k is the pre-render of drawn frame k - L (fixed, counted in drawn frames: `committed` counts
// frame_ends of frames the sink drew). Without that frame (first drawn frame of a mission, the frame after a size change, a
// frame between that drew no fog read) the read is the same-frame probe. Either way this frame's pre-render is kept for later.
int GodotSink::fog_read(int w, int h, uint8_t *out)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	bool twice = false;
	FogSlot *src = nullptr;
	for (FogSlot &sl : fog_slots) {
		if (!sl.live) { continue; }
		if (sl.stamp == committed) { twice = true; }
		if (sl.stamp == committed - kNebFogLagFrames && sl.w == w && sl.h == h) { src = &sl; }
	}
	if (twice) {
		fs2perf::count(fs2perf::C_FOG_SAME);
		return probe_read(w, h, out); // a second read in one drawn frame: nothing left to move
	}
	int ok = 0;
	if (src == nullptr) {
		fs2perf::count(fs2perf::C_FOG_SAME);
		ok = probe_read(w, h, out);
	} else {
		fs2perf::count(fs2perf::C_FOG_LAG);
		if (Engine::get_singleton()->get_frames_drawn() <= src->drawn) {
			fs2perf::count(fs2perf::C_FOG_WAIT); // Godot has not drawn since: draw now, so the colour never depends on pacing
			rs->force_draw(false);
		}
		fs2perf::Zone _gz(fs2perf::Z_FOG_GET);
		Ref<Image> img = rs->texture_2d_get(rs->viewport_get_texture(src->vp));
		if (img.is_valid() && !img->is_empty()) {
			copy_probe_pixels(img, w, h, out);
			ok = 1;
		}
	}
	fog_capture(w, h);
	return ok;
}

int GodotSink::s_read_region(void *u, int x, int y, int w, int h, uint8_t *out)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	fs2perf::Zone _pz(fs2perf::Z_READ_REGION);
	fs2perf::count(fs2perf::C_READ_REGION);
	fs2perf::count(fs2perf::C_READ_BYTES, (uint64_t)(w > 0 ? w : 0) * (uint64_t)(h > 0 ? h : 0) * 4);
	s->batcher.flush();
	if (x == 0 && y == 0 && w > 0 && h > 0 && (long long)w * h <= kNebFogReadMaxPixels) {
		// the neb2 fog-colour read: see kNebFog
		if (g_nebfog == kNebFogOff) {
			return 0;
		} else {
			if (!s->in_frame) {
				return 0;
			}
			if (s->in_run) {
				s->end_run();
			}
			if (g_nebfog == kNebFogSame) {
				fs2perf::count(fs2perf::C_FOG_SAME);
				return s->probe_read(w, h, out);
			} else if (g_nebfog == kNebFogLag) {
				return s->fog_read(w, h, out);
			} else {
				// kNebFogGpu: the pre-render stays on the GPU; the host gets "no data" and its buffer stays as the dummy leaves it
				FogSlot &sl = s->fog_slots[s->committed & 1];
				fs2perf::count(sl.live && sl.stamp == s->committed ? fs2perf::C_FOG_WAIT : fs2perf::C_FOG_LAG);
				s->fog_capture_gpu(w, h);
				return 0;
			}
		}
	}
	// the last frame the viewport has rendered (D3D reads the back buffer, which at the start of a frame still holds the last frame)
	if (s->vp == nullptr || out == nullptr || w <= 0 || h <= 0) {
		return 0;
	}
	RenderingServer::get_singleton()->force_draw(false); // render what frame_end has shown: the viewport has not drawn it yet
	Ref<Image> img = s->vp->get_texture()->get_image();
	if (img.is_null() || img->is_empty()) {
		return 0;
	}
	img->convert(Image::FORMAT_RGBA8);
	const int iw = img->get_width(), ih = img->get_height();
	const PackedByteArray data = img->get_data();
	const uint8_t *src = data.ptr();
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			uint8_t *d = out + ((size_t)row * (size_t)w + (size_t)col) * 4;
			const int sx = x + col, sy = y + row;
			if (sx < 0 || sy < 0 || sx >= iw || sy >= ih) {
				d[0] = d[1] = d[2] = 0; d[3] = 255;
			} else {
				memcpy(d, src + ((size_t)sy * (size_t)iw + (size_t)sx) * 4, 4);
			}
		}
	}
	return 1;
}

void GodotSink::s_set_gamma(void *, float) {}

// ---- depth runs (R1-8) ------------------------------------------------------------------------------------------

static const int MAX_EPOCH = 24;	// depth clears per run told apart; later ones share the nearest band

void GodotSink::s_depth_begin(void *u)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame || s->in_run) {
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_DEPTH);
	s->batcher.flush();
	s->in_run = true;
	s->run_epoch = 0;
	s->run_batches.clear();
	s->sync_batcher_clip();
}

void GodotSink::s_depth_end(void *u)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (s->in_frame && s->in_run) {
		s->end_run();
	}
}

// v3: the camera is kept for the mesh path (P1); no draw uses it yet
void GodotSink::s_set_view(void *u, const fs2_sink_view *v)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (s->in_frame && v != nullptr) {
		s->view = *v;
		s->have_view = true;
	}
}

// Depth cleared in a rectangle: everything drawn after it is in front of everything before it. Approximation: the
// whole layer, not only the rectangle. The same as D3D while the depth draws after the clear do not overlap earlier
// ones outside the rectangle; in the missions checked (R1-9) they are the target box model, whose z is near 0 and
// wins either way.
void GodotSink::s_depth_clear(void *u, int, int, int, int)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame || !s->in_run) {
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_DEPTH);
	s->batcher.flush();
	bool any_depth = false;
	for (const Batch &b : s->run_batches) {
		if (b.key.zmode != 0) { any_depth = true; break; }
	}
	if (any_depth && s->run_epoch < MAX_EPOCH) {
		s->run_epoch++;
	}
	s->sync_batcher_clip();
}

// A depth batch drawn on the canvas: its geometry is already clipped and carries custom, so it is a perspective batch.
static Batch canvas_form(const Batch &b)
{
	Batch c = b;
	c.key.depth = 0; c.key.zmode = 0; c.key.epoch = 0; c.key.zbias = 0; c.key.perspective = 1;
	c.z.clear(); c.fogv.clear(); c.key.fog = 0;
	return c;
}

void GodotSink::end_run()
{
	fs2perf::Zone _pz(fs2perf::Z_RUN_END);
	batcher.flush();
	in_run = false;
	batcher.set_depth_run(false, 0, 0, 0, 0, 0);
	RenderingServer *rs = RenderingServer::get_singleton();
	if (g_opaque_reorder) {
		const size_t before = run_batches.size();
		reorder_opaque_batches(run_batches);
		stat_reorder_saved += before - run_batches.size();
	}
	int first = -1, last = -1;
	for (int i = 0; i < (int)run_batches.size(); i++) {
		if (run_batches[i].key.zmode != 0) {
			if (first < 0) { first = i; }
			last = i;
		}
	}
	parent = pending_root;
	cur_item = RID();
	const int n = (int)run_batches.size();
	for (int i = 0; i < (first < 0 ? n : first); i++) {
		emit_canvas(canvas_form(run_batches[i]));
	}
	if (first >= 0) {
		Layer l = acquire_layer();
		fs2perf::count(fs2perf::C_LAYERS);
		for (int i = first; i <= last; i++) {
			if (run_batches[i].key.texture <= -kBbBase) {
				// a span of additive, depth-read-only billboard draws commutes: one run per state (bucket), whatever the draw order in the span
				const size_t d0 = (size_t)(-(int64_t)run_batches[i].key.texture - kBbBase - 1);
				if (d0 < bb_draws.size() && bb_additive(bb_draws[d0])) {
					int j = i, nb = 0, lastb = -1;
					for (; j <= last && run_batches[j].key.texture <= -kBbBase; j++) {
						const size_t di = (size_t)(-(int64_t)run_batches[j].key.texture - kBbBase - 1);
						if (di >= bb_draws.size() || !bb_additive(bb_draws[di])) { break; }
						int bi = -1;
						if (lastb >= 0 && (int)bb_buckets[lastb].size() < kBbBucketCap[kBbBuckets - 1] && bb_same_run(bb_draws[bb_buckets[lastb][0]], bb_draws[di])) { bi = lastb; }
						for (int k = 0; bi < 0 && k < nb; k++) {
							if ((int)bb_buckets[k].size() < kBbBucketCap[kBbBuckets - 1] && bb_same_run(bb_draws[bb_buckets[k][0]], bb_draws[di])) { bi = k; }
						}
						if (bi < 0) {
							if ((int)bb_buckets.size() <= nb) { bb_buckets.emplace_back(); }
							bb_buckets[nb].clear();
							bi = nb++;
						}
						bb_buckets[bi].push_back(di);
						lastb = bi;
					}
					for (int k = 0; k < nb; k++) { add_bb_run(l, bb_buckets[k], i - first); }
					i = j - 1;
					continue;
				}
				// a run of adjacent billboard draws of one state is one instance (kept in game order inside it)
				bb_run_idx.clear();
				int j = i;
				while (j <= last && run_batches[j].key.texture <= -kBbBase && (int)bb_run_idx.size() < kBbBucketCap[kBbBuckets - 1]) {
					const size_t di = (size_t)(-(int64_t)run_batches[j].key.texture - kBbBase - 1);
					if (di >= bb_draws.size()) { break; }
					if (!bb_run_idx.empty() && !bb_same_run(bb_draws[bb_run_idx[0]], bb_draws[di])) { break; }
					bb_run_idx.push_back(di);
					j++;
				}
				if (j == i) { continue; }
				add_bb_run(l, bb_run_idx, i - first);
				i = j - 1;
				continue;
			}
			add_to_layer(l, run_batches[i], i - first);
		}
		if (!composite_material.is_valid()) {
			composite_shader = rs->shader_create();
			rs->shader_set_code(composite_shader, "shader_type canvas_item;\nrender_mode blend_premul_alpha;\nvoid fragment() { COLOR = texture(TEXTURE, UV); }\n");
			composite_material = rs->material_create();
			rs->material_set_shader(composite_material, composite_shader);
		}
		RID item = new_item(pending_root);
		pending_items.push_back(item);
		rs->canvas_item_set_material(item, composite_material);
		rs->canvas_item_add_texture_rect(item, Rect2(0.0f, 0.0f, (float)width, (float)height), l.texture);
		pending_layers.push_back(l);
		cur_item = RID();
		for (int i = last + 1; i < n; i++) {
			emit_canvas(canvas_form(run_batches[i]));
		}
	}
	run_batches.clear();
	{ std::set<std::pair<int32_t, int>> seen; for (const MeshDraw &md : mesh_draws) { seen.insert(std::make_pair(md.id, md.state)); } stat_mesh_models += seen.size(); }
	mesh_draws.clear();
	bb_draws.clear();
	cur_item = RID();
	if (clipped) {
		open_clip_item();
	} else {
		parent = pending_root;
	}
}

GodotSink::Layer GodotSink::acquire_layer()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	Layer l;
	if (!spare_layers.empty()) {
		l = spare_layers.back();
		spare_layers.pop_back();
		if (l.probe_parent && vp != nullptr) {
			rs->viewport_set_parent_viewport(l.viewport, vp->get_viewport_rid());
			l.probe_parent = false;
		}
	} else {
		l.scenario = rs->scenario_create();
		l.viewport = rs->viewport_create();
		l.camera = rs->camera_create();
		rs->camera_set_transform(l.camera, Transform3D());
		rs->camera_set_use_vertical_aspect(l.camera, false);
		rs->viewport_set_scenario(l.viewport, l.scenario);
		rs->viewport_attach_camera(l.viewport, l.camera);
		rs->viewport_set_transparent_background(l.viewport, true);
		rs->viewport_set_disable_2d(l.viewport, true);
		// HDR 2D: the 3D pass writes its float buffer out unconverted, so the game's sRGB values blend as D3D blends them
		rs->viewport_set_use_hdr_2d(l.viewport, true);
		if (vp != nullptr) {
			rs->viewport_set_parent_viewport(l.viewport, vp->get_viewport_rid());
		}
		rs->viewport_set_active(l.viewport, true);
		l.texture = rs->viewport_get_texture(l.viewport);
	}
	if (l.w != width || l.h != height) {
		l.w = width;
		l.h = height;
		rs->viewport_set_size(l.viewport, width, height);
		// keep height: y spans [-h/2, h/2], x follows the aspect, so a pixel is one unit
		rs->camera_set_orthogonal(l.camera, (float)height, 0.5f, 30000.0f);
	}
	rs->viewport_set_update_mode(l.viewport, RenderingServer::VIEWPORT_UPDATE_ALWAYS);
	return l;
}

void GodotSink::release_layers(std::vector<Layer> &layers)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (Layer &l : layers) {
		for (RID &i : l.instances) {
			if (kInstancePool) {
				rs->instance_set_scenario(i, RID());
				spare_instances.push_back(i);
			} else {
				rs->free_rid(i);
			}
		}
		l.instances.clear();
		for (RID &i : l.bb_instances) {
			rs->instance_set_scenario(i, RID());
			spare_bb_instances.push_back(i);
		}
		l.bb_instances.clear();
		release_mesh_res(l, false);
		rs->viewport_set_update_mode(l.viewport, RenderingServer::VIEWPORT_UPDATE_DISABLED);
		spare_layers.push_back(l);
	}
	layers.clear();
}

void GodotSink::free_layer(Layer &l)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (RID &i : l.instances) {
		rs->free_rid(i);
	}
	l.instances.clear();
	for (RID &i : l.bb_instances) { rs->free_rid(i); }
	l.bb_instances.clear();
	release_mesh_res(l, true);
	rs->free_rid(l.viewport);
	rs->free_rid(l.camera);
	rs->free_rid(l.scenario);
}

void GodotSink::drop_materials_of(const RID &tex)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (auto it = bb_mats.begin(); it != bb_mats.end();) {
		if (it->first[10] == (int64_t)tex.get_id()) {
			rs->free_rid(it->second);
			it = bb_mats.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = spatial_materials.begin(); it != spatial_materials.end();) {
		if (it->first.second == (int64_t)tex.get_id()) {
			rs->free_rid(it->second);
			it = spatial_materials.erase(it);
		} else {
			++it;
		}
	}
}

// One spatial shader per (blend, depth mode, A8, filter, repeat); one material per shader and texture. Unshaded; the
// texture and vertex colour are the game's sRGB values, kept as they are: the layer is HDR 2D, so blending happens on
// those values, as in D3D (R1-9).
RID GodotSink::spatial_material(int blend, int zmode, bool a8, uint32_t texflags, const RID &tex, bool fog)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const bool lin = (texflags & FS2_SINK_TEXF_LINEAR) != 0, rpt = (texflags & FS2_SINK_TEXF_REPEAT) != 0;
	uint32_t skey = (uint32_t)(blend & 3) | ((uint32_t)(zmode & 3) << 2) | (a8 ? 16u : 0u) | (lin ? 32u : 0u) | (rpt ? 64u : 0u) | (fog ? 128u : 0u);
	const int parity = (g_nebfog == kNebFogGpu && fog) ? (committed & 1) : 0;	// fog gpu: the material reads the fog slot and table of its frame parity
	skey |= parity ? 256u : 0u;
	const std::pair<uint32_t, int64_t> mkey(skey, (int64_t)tex.get_id());
	auto mit = spatial_materials.find(mkey);
	if (mit != spatial_materials.end()) {
		return mit->second;
	}
	RID sh = spatial_shader(blend, zmode, a8, lin, rpt, fog);
	RID m = rs->material_create();
	rs->material_set_shader(m, sh);
	rs->material_set_param(m, "tex", tex);
	if (g_nebfog == kNebFogGpu) {
		if (fog) {
			rs->material_set_param(m, "bg", gpu_bg_texture(parity));
			rs->material_set_param(m, "bgp", gpu_bg_texture(parity ^ 1));	// the frame before's pre-render (CUSTOM1.w = 1)
			rs->material_set_param(m, "ptab", gpu_tab_texture(parity));
		}
	}
	spatial_materials[mkey] = m;
	return m;
}

RID GodotSink::white_texture()
{
	if (!white_tex.is_valid()) {
		RenderingServer *rs = RenderingServer::get_singleton();
		PackedByteArray px;
		px.resize(4);
		px.fill(255);
		white_tex = rs->texture_2d_create(Image::create_from_data(1, 1, false, Image::FORMAT_RGBA8, px));
	}
	return white_tex;
}

RID GodotSink::spatial_shader(int blend, int zmode, bool a8, bool lin, bool rpt, bool fog)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const uint32_t skey = (uint32_t)(blend & 3) | ((uint32_t)(zmode & 3) << 2) | (a8 ? 16u : 0u) | (lin ? 32u : 0u) | (rpt ? 64u : 0u) | (fog ? 128u : 0u);
	RID sh;
	auto sit = spatial_shaders.find(skey);
	if (sit != spatial_shaders.end()) {
		sh = sit->second;
	} else {
		String code = "shader_type spatial;\nrender_mode unshaded, cull_disabled, shadows_disabled, fog_disabled, ";
		code += blend == FS2_SINK_BLEND_ADD ? "blend_premul_alpha, " : "blend_mix, ";	// ADD: ONE:ONE, see material_for
		code += (zmode == 2 || zmode == 3) ? "depth_draw_always" : "depth_draw_never";
		if (zmode == 0 || zmode == 3) {
			code += ", depth_test_disabled";
		}
		code += ";\nuniform sampler2D tex : ";
		code += lin ? "filter_linear, " : "filter_nearest, ";
		code += rpt ? "repeat_enable;\n" : "repeat_disable;\n";
		code += "varying vec4 vcol;\nvarying vec3 pc;\n";
		if (fog && g_nebfog == kNebFogGpu) {
			// fog gpu: CUSTOM1 = (table offset, nv, factor, 1 = cells of a poly of the frame before: look up on `bgp`, D-20261030-052453).
			// The vertex shader does neb2_get_pixel (Neb.cpp 1229-1343) for each vertex of the
			// poly on the 32x32 pre-render `bg` (the start cells come from `ptab`) and the integer average over the poly (GrD3DRender.cpp 962-980)
			code += "uniform sampler2D bg : filter_nearest, repeat_disable;\nuniform sampler2D bgp : filter_nearest, repeat_disable;\nuniform sampler2D ptab : filter_nearest, repeat_disable;\n";
			code += "varying flat vec3 fgc;\nvarying float fgf;\n";
			code += "ivec3 nbpx(int x, int y, bool p) { return ivec3(round((p ? texelFetch(bgp, ivec2(x, y), 0) : texelFetch(bg, ivec2(x, y), 0)).rgb * 255.0)); }\n";
			code += "ivec3 nbget(int xs, int ys, bool p) {\n\tivec3 s = nbpx(xs, ys, p);\n\tint n = 1;\n";
			code += "\tif (xs > 0) { s += nbpx(xs - 1, ys, p); n++; }\n";
			code += "\tif (xs < 31) { s += nbpx(xs + 1, ys, p); n++; }\n";
			code += "\tif (ys > 0) { s += nbpx(xs, ys - 1, p); n++; }\n";
			code += "\tif (ys < 30) { s += nbpx(xs, ys + 1, p); n++; }\n";
			code += "\tif (xs > 0 && ys > 0) { s += nbpx(xs - 1, ys - 1, p); n++; }\n";
			code += "\tif (xs < 31 && ys < 31) { s += nbpx(xs + 1, ys + 1, p); n++; }\n";
			code += "\tif (ys > 0 && xs < 31) { s += nbpx(xs + 1, ys - 1, p); n++; }\n";
			code += "\tif (ys < 31 && xs > 0) { s += nbpx(xs - 1, ys + 1, p); n++; }\n";
			code += "\treturn ivec3(s.x / n, s.y / n, s.z / n);\n}\n";
			code += "void vertex() {\n\tvcol = COLOR; pc = CUSTOM0.xyz;\n";
			code += "\tint off = int(CUSTOM1.x + 0.5);\n\tint nv = int(CUSTOM1.y + 0.5);\n\tbool p = CUSTOM1.w > 0.5;\n\tivec3 tot = ivec3(0);\n";
			code += "\tfor (int i = 0; i < nv; i++) {\n\t\tint k = off + i;\n";
			code += "\t\tivec2 c = ivec2(round(texelFetch(ptab, ivec2(k & 127, k >> 7), 0).rg * 255.0));\n";
			code += "\t\ttot += nbget(c.x, c.y, p);\n\t}\n";
			code += "\tint d = max(nv, 1);\n\tfgc = vec3(float(tot.x / d), float(tot.y / d), float(tot.z / d)) / 255.0;\n\tfgf = CUSTOM1.z;\n}\n";
		} else {
			if (fog) {
				code += "varying vec4 fg;\n";	// fog r g b and the factor (1 = unfogged), per vertex: CUSTOM1
			}
			code += fog ? "void vertex() { vcol = COLOR; pc = CUSTOM0.xyz; fg = CUSTOM1; }\n" : "void vertex() { vcol = COLOR; pc = CUSTOM0.xyz; }\n";
		}
		code += "void fragment() {\n\tvec4 t = texture(tex, pc.xy / pc.z);\n";
		if (a8) {
			code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n";
		}
		code += "\tvec4 c = vcol * t;\n";
		if (blend == FS2_SINK_BLEND_OPAQUE) {
			code += "\tc.a = 1.0;\n";
		} else if (blend == FS2_SINK_BLEND_ADD) {
			code += "\tc.a = 0.0;\n";
		}
		if (fog) {
			if (g_nebfog == kNebFogGpu) {
				code += "\tc.rgb = mix(fgc, c.rgb, fgf);\n";
			} else {
				code += "\tc.rgb = mix(fg.rgb, c.rgb, fg.a);\n";	// D3D: f * colour + (1 - f) * fog colour, rgb only
			}
		}
		// #133, measured against PrePort32 frame captures after the half-pixel shift (gr_host.cpp tmapper_internal): nebula frames
		// are ~27% exactly equal and ~98% within 1 shade; the rest of the frame-wide speckle is 1 shade either way. Tried and not kept:
		// rounding to whole 8-bit shades here in fogged shaders only (no change); in every shader (nebula ~69% exact but still 98%
		// within 1 shade, and space fell 99.8% -> 99.2%); floor instead of round (1-2 shades too dark); truncating the per-vertex
		// fog factor as D3DRGBA stores it (no change); nearest filtering in both renderers (no change). Cause of what is left,
		// confirmed by skipping the nebula poofs in both renderers (27% -> 96.7% exact): D3D quantises each additive poof blend to
		// the 8-bit frame, Godot's HDR 2D viewport accumulates in float. Line drawn there.
		code += "\tALBEDO = clamp(c.rgb, 0.0, 1.0);\n\tALPHA = c.a;\n}\n";
		sh = rs->shader_create();
		rs->shader_set_code(sh, code);
		spatial_shaders[skey] = sh;
	}
	return sh;
}

// Pre-warm (perf group 2 fix 4). Godot builds a shader, and the pipeline for a render target format, the first time something
// that uses it is drawn; that was 29-77 ms on the first frame that used a material. This draws one tiny triangle with each
// canvas material (both draw paths) into a 16x16 viewport of the SubViewport's format, and one per depth-layer shader the missions use into a 16x16 HDR viewport like the layers. Nothing composites these viewports, so what is drawn
// is unchanged; they live kPrewarmFrames frame_ends and go. The shaders stay (they belong to the same caches the game uses).
void GodotSink::prewarm()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const int S = 16;
	const RID tex = white_texture();
	PackedInt32Array indices;
	indices.resize(3);
	indices.ptrw()[0] = 0; indices.ptrw()[1] = 1; indices.ptrw()[2] = 2;
	PackedVector2Array p2, uv;
	p2.resize(3); uv.resize(3);
	p2.ptrw()[0] = Vector2(1, 1); p2.ptrw()[1] = Vector2(14, 1); p2.ptrw()[2] = Vector2(1, 14);
	uv.ptrw()[0] = Vector2(0, 0); uv.ptrw()[1] = Vector2(1, 0); uv.ptrw()[2] = Vector2(0, 1);
	PackedColorArray col;
	col.resize(3);
	col.fill(Color(1, 1, 1, 1));
	PackedFloat32Array custom;
	custom.resize(12);
	for (int i = 0; i < 3; i++) {
		custom.ptrw()[i * 4 + 0] = uv[i].x; custom.ptrw()[i * 4 + 1] = uv[i].y; custom.ptrw()[i * 4 + 2] = 1.0f; custom.ptrw()[i * 4 + 3] = 0.0f;
	}

	// canvas: same format as the game viewport (opaque background, no HDR)
	RID cvp = rs->viewport_create();
	rs->viewport_set_size(cvp, S, S);
	rs->viewport_set_transparent_background(cvp, false);
	rs->viewport_set_active(cvp, true);
	rs->viewport_set_update_mode(cvp, RenderingServer::VIEWPORT_UPDATE_ALWAYS);
	RID ccanvas = rs->canvas_create();
	rs->viewport_attach_canvas(cvp, ccanvas);
	warm_viewports.push_back(cvp);
	warm_misc.push_back(ccanvas);
	Array carrays;
	carrays.resize(RenderingServer::ARRAY_MAX);
	carrays[RenderingServer::ARRAY_VERTEX] = p2;
	carrays[RenderingServer::ARRAY_COLOR] = col;
	carrays[RenderingServer::ARRAY_TEX_UV] = uv;
	carrays[RenderingServer::ARRAY_CUSTOM0] = custom;
	carrays[RenderingServer::ARRAY_INDEX] = indices;
	RID cmesh = rs->mesh_create();
	rs->mesh_add_surface_from_arrays(cmesh, RenderingServer::PRIMITIVE_TRIANGLES, carrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)((int64_t)RenderingServer::ARRAY_FLAG_USE_2D_VERTICES | ((int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT)));
	warm_misc.push_back(cmesh);
	for (int i = 0; i < 12; i++) {
		const int blend = i % 3, rest = i / 3;
		const bool a8 = (rest & 1) != 0, persp = (rest & 2) != 0;
		RID item = rs->canvas_item_create();
		rs->canvas_item_set_parent(item, ccanvas);
		rs->canvas_item_set_material(item, material_for(blend, a8, persp));
		if (persp) {
			rs->canvas_item_add_mesh(item, cmesh, Transform2D(), Color(1, 1, 1, 1), tex);
		} else {
			rs->canvas_item_add_triangle_array(item, indices, p2, col, uv, PackedInt32Array(), PackedFloat32Array(), tex);
		}
		warm_items.push_back(item);
	}

	// depth layers: same setup as acquire_layer()
	RID scen = rs->scenario_create();
	RID lvp = rs->viewport_create();
	RID cam = rs->camera_create();
	rs->camera_set_transform(cam, Transform3D());
	rs->camera_set_use_vertical_aspect(cam, false);
	rs->viewport_set_scenario(lvp, scen);
	rs->viewport_attach_camera(lvp, cam);
	rs->viewport_set_transparent_background(lvp, true);
	rs->viewport_set_disable_2d(lvp, true);
	rs->viewport_set_use_hdr_2d(lvp, true);
	if (vp != nullptr) {
		rs->viewport_set_parent_viewport(lvp, vp->get_viewport_rid());
	}
	rs->viewport_set_size(lvp, S, S);
	rs->camera_set_orthogonal(cam, (float)S, 0.5f, 30000.0f);
	rs->viewport_set_active(lvp, true);
	rs->viewport_set_update_mode(lvp, RenderingServer::VIEWPORT_UPDATE_ALWAYS);
	warm_viewports.push_back(lvp);
	warm_misc.push_back(cam);
	warm_misc.push_back(scen);
	PackedVector3Array p3;
	p3.resize(3);
	p3.ptrw()[0] = Vector3(-6, -6, -10); p3.ptrw()[1] = Vector3(6, -6, -10); p3.ptrw()[2] = Vector3(-6, 6, -10);
	Array sarrays;
	sarrays.resize(RenderingServer::ARRAY_MAX);
	sarrays[RenderingServer::ARRAY_VERTEX] = p3;
	sarrays[RenderingServer::ARRAY_COLOR] = col;
	sarrays[RenderingServer::ARRAY_TEX_UV] = uv;
	sarrays[RenderingServer::ARRAY_CUSTOM0] = custom;
	sarrays[RenderingServer::ARRAY_INDEX] = indices;
	// the shader keys the 1999 campaign missions use (md-01, sm2-07, sm3-06 logged): skey = blend | zmode << 2 | A8 16 | linear 32 | repeat 64.
	// Warming all 96 costs 0.5 s at load for seven that matter; any other combination just builds on first use as before.
	// plus 128 = the fog variant (R2), warmed for the ones nebula missions draw
	static const int kWarmKeys[] = { 0, 1, 9, 17, 38, 41, 105, 128 | 1, 128 | 9, 128 | 41, 128 | 105 };
	for (int skey : kWarmKeys) {
		const int blend = skey & 3, zmode = (skey >> 2) & 3;
		const bool a8 = (skey & 16) != 0, lin = (skey & 32) != 0, rpt = (skey & 64) != 0, wfog = (skey & 128) != 0;
		RID mesh = rs->mesh_create();
		Array warr = sarrays;
		int64_t wfmt = (int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT;
		if (wfog) {
			warr[RenderingServer::ARRAY_CUSTOM1] = custom;
			wfmt |= (int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM1_SHIFT;
		}
		rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, warr, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)wfmt);
		RID mat = rs->material_create();
		rs->material_set_shader(mat, spatial_shader(blend, zmode, a8, lin, rpt, wfog));
		rs->material_set_param(mat, "tex", tex);
		rs->mesh_surface_set_material(mesh, 0, mat);
		RID inst = rs->instance_create2(mesh, scen);
		rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
		warm_instances.push_back(inst);
		warm_misc.push_back(mesh);
		warm_misc.push_back(mat);
	}
}

void GodotSink::prewarm_release()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		return;
	}
	for (RID &r : warm_viewports) { rs->free_rid(r); }
	for (RID &r : warm_instances) { rs->free_rid(r); }
	for (RID &r : warm_items) { rs->free_rid(r); }
	for (RID &r : warm_misc) { rs->free_rid(r); }
	warm_viewports.clear();
	warm_instances.clear();
	warm_items.clear();
	warm_misc.clear();
}

// kNebFogGpu: the viewport texture of the fog slot of a frame parity (the slot exists from here on), and the table texture
RID GodotSink::gpu_bg_texture(int parity)
{
	FogSlot &sl = fog_slots[parity & 1];
	fog_slot_init(sl);
	return RenderingServer::get_singleton()->viewport_get_texture(sl.vp);
}

static PackedByteArray ptab_pixels(int rows, const uint8_t *cells, size_t entries)
{
	PackedByteArray px;
	px.resize((int64_t)rows * 128 * 4);
	px.fill(0);
	uint8_t *d = px.ptrw();
	for (size_t k = 0; k < entries; k++) {
		d[k * 4 + 0] = cells[k * 2]; d[k * 4 + 1] = cells[k * 2 + 1]; d[k * 4 + 3] = 255;
	}
	return px;
}

RID GodotSink::gpu_tab_texture(int parity)
{
	const int p = parity & 1;
	if (!gpu_tab[p].is_valid()) {
		gpu_tab_rows[p] = 64;
		gpu_tab[p] = RenderingServer::get_singleton()->texture_2d_create(Image::create_from_data(128, 64, false, Image::FORMAT_RGBA8, ptab_pixels(64, nullptr, 0)));
	}
	return gpu_tab[p];
}

// kNebFogGpu, end of the frame: the table of the frame's fogged polys goes to the texture of its parity, and the fog slot is put in
// the render order before the frame's layers (slot -> first layer -> ... -> last layer -> game viewport), so the layers read this
// frame's pre-render, not the one of the frame before.
void GodotSink::gpu_frame_end()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const int par = committed & 1;
	const std::vector<uint8_t> &tab = batcher.fog_table();
	const size_t entries = tab.size() / 2;
	if (entries > 0) {
		const int rows = (int)((entries + 127) / 128);
		const RID cur = gpu_tab_texture(par);
		if (rows > gpu_tab_rows[par]) {
			int cap = gpu_tab_rows[par];
			while (cap < rows) { cap *= 2; }
			const RID nt = rs->texture_2d_create(Image::create_from_data(128, cap, false, Image::FORMAT_RGBA8, ptab_pixels(cap, tab.data(), entries)));
			for (auto &m : spatial_materials) {
				if ((m.first.first & 128u) && (((m.first.first >> 8) & 1u) == (uint32_t)par)) {
					rs->material_set_param(m.second, "ptab", nt);
				}
			}
			rs->free_rid(cur);
			gpu_tab[par] = nt;
			gpu_tab_rows[par] = cap;
		} else {
			rs->texture_2d_update(cur, Image::create_from_data(128, gpu_tab_rows[par], false, Image::FORMAT_RGBA8, ptab_pixels(gpu_tab_rows[par], tab.data(), entries)), 0);
		}
	}
	FogSlot &sl = fog_slots[par];
	if (sl.live && sl.stamp == committed && vp != nullptr) {
		const RID game = vp->get_viewport_rid();
		const size_t n = pending_layers.size();
		const size_t k = (size_t)gpu_pre_layers < n ? (size_t)gpu_pre_layers : n;	// layers of the pre-render: they come before the slot
		for (size_t i = 0; i < n; i++) {
			RID lpar = i + 1 == n ? game : pending_layers[i + 1].viewport;
			if (i + 1 == k) { lpar = sl.vp; }
			rs->viewport_set_parent_viewport(pending_layers[i].viewport, lpar);
			pending_layers[i].probe_parent = lpar != game;
		}
		rs->viewport_set_parent_viewport(sl.vp, k < n ? pending_layers[k].viewport : game);
		// Godot orders the active viewports with a comparator that only knows a viewport before its direct parent; two viewports that are
		// not parent and child compare equal and keep the order they were activated in. The chain slot -> layers -> game has such pairs, so
		// it is activated again in the order it must render in (pre layers, slot, the other layers, game)
		for (size_t i = 0; i < k; i++) { rs->viewport_set_active(pending_layers[i].viewport, false); rs->viewport_set_active(pending_layers[i].viewport, true); }
		rs->viewport_set_active(sl.vp, false); rs->viewport_set_active(sl.vp, true);
		for (size_t i = k; i < n; i++) { rs->viewport_set_active(pending_layers[i].viewport, false); rs->viewport_set_active(pending_layers[i].viewport, true); }
		rs->viewport_set_active(game, false); rs->viewport_set_active(game, true);
	}
}

// One mesh instance per batch. The camera looks down -z from the origin; a vertex at screen (x, y) is at
// (x - w/2, h/2 - y). Its distance: later clear epochs nearer, then the game's z, then the z bias. Godot sorts the
// (all transparent) instances back to front by their origin, so each instance's origin carries the call order.
void GodotSink::add_to_layer(Layer &l, const Batch &b, int order)
{
	if (b.key.texture <= -kBbBase) { return; }	// billboard runs are placed by end_run (add_bb_run)
	if (b.key.texture < 0) {
		const size_t di = (size_t)(-(int64_t)b.key.texture - 1);
		if (di < mesh_draws.size()) { add_mesh_to_layer(l, mesh_draws[di], order); }
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_LAYER_ADD);
	fs2perf::count(fs2perf::C_LAYER_BATCHES);
	fs2perf::count(fs2perf::C_MESHES);
	RenderingServer *rs = RenderingServer::get_singleton();
	bool a8 = false;
	uint32_t tflags = 0;
	RID tex;
	if (b.key.texture != 0) {
		auto it = textures.find(b.key.texture);
		if (it == textures.end()) {
			return;
		}
		tex = it->second.rid;
		a8 = (it->second.format == FS2_SINK_TEX_A8);
		tflags = it->second.flags;
	} else {
		tex = white_texture();
	}
	const int nv = b.nverts();
	const int ni = (int)b.idx.size();
	if (nv <= 0 || ni < 3 || (int)b.z.size() != nv || (int)b.custom.size() != nv * 4) {
		return;
	}
	const bool fogged = b.key.fog != 0 && (int)b.fogv.size() == nv * 4;
	const int epoch = b.key.epoch < MAX_EPOCH ? b.key.epoch : MAX_EPOCH;
	const float origin = 29000.0f - 0.25f * (float)order;
	PackedVector3Array pos;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedFloat32Array custom;
	PackedInt32Array indices;
	pos.resize(nv);
	uvs.resize(nv);
	colors.resize(nv);
	custom.resize((int64_t)nv * 4);
	indices.resize(ni);
	const float hw = 0.5f * (float)width, hh = 0.5f * (float)height;
	Vector3 *pp = pos.ptrw();
	for (int i = 0; i < nv; i++) {
		/* zbias: retail D3D adds ~1 depth-buffer step per unit (decals, subobjects over the hull); 2.0 here was ~60 game m per unit and pulled insignias and turrets through the hull */
		const float d = legacy_depth(b.z[i], epoch, MAX_EPOCH, b.key.zbias);
		pp[i] = Vector3(b.pos[i * 2] - hw, hh - b.pos[i * 2 + 1], origin - d);
	}
	std::memcpy(uvs.ptrw(), b.uv.data(), sizeof(float) * 2 * (size_t)nv);
	std::memcpy(colors.ptrw(), b.col.data(), sizeof(float) * 4 * (size_t)nv);
	std::memcpy(custom.ptrw(), b.custom.data(), sizeof(float) * 4 * (size_t)nv);
	std::memcpy(indices.ptrw(), b.idx.data(), sizeof(int32_t) * (size_t)ni);
	Array arrays;
	arrays.resize(RenderingServer::ARRAY_MAX);
	arrays[RenderingServer::ARRAY_VERTEX] = pos;
	arrays[RenderingServer::ARRAY_COLOR] = colors;
	arrays[RenderingServer::ARRAY_TEX_UV] = uvs;
	arrays[RenderingServer::ARRAY_CUSTOM0] = custom;
	arrays[RenderingServer::ARRAY_INDEX] = indices;
	RID mesh;
	if (!spare_meshes.empty()) {
		mesh = spare_meshes.back();
		spare_meshes.pop_back();
	} else {
		mesh = rs->mesh_create();
	}
	int64_t fmt = (int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT;
	if (fogged) {
		PackedFloat32Array fg;
		fg.resize((int64_t)nv * 4);
		std::memcpy(fg.ptrw(), b.fogv.data(), sizeof(float) * 4 * (size_t)nv);
		arrays[RenderingServer::ARRAY_CUSTOM1] = fg;
		fmt |= (int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM1_SHIFT;
	}
	rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)fmt);
	rs->mesh_surface_set_material(mesh, 0, spatial_material(b.key.blend, b.key.zmode, a8, tflags, tex, fogged));
	pending_meshes.push_back(mesh);
	RID inst;
	if (kInstancePool && !spare_instances.empty()) {
		inst = spare_instances.back();
		spare_instances.pop_back();
		rs->instance_set_base(inst, mesh);
		rs->instance_set_scenario(inst, l.scenario);
		rs->instance_geometry_set_material_override(inst, RID());	// a pooled instance may come from a mesh draw, which overrides its material
		rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(0.0f, 0.0f, -origin)));
	} else {
		inst = rs->instance_create2(mesh, l.scenario);
		rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(0.0f, 0.0f, -origin)));
		rs->instance_set_pivot_data(inst, 0.0f, false);
		rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
	}
	l.instances.push_back(inst);
}

// ---- v4 meshes (P3b) ----------------------------------------------------------------------------------------------------------------
// The mesh shader reproduces the sink's depth contract (depth_contract.h) per vertex: eye = xform * model position; the screen
// position is the game's projection (sx = (W + x*W/z)/2), the depth is legacy_depth with the sw clamp (z >= 1) and the z cap (0.98), and the
// position is output homogeneous (w = eye z), so the GPU clips at w > 0 and the fragment shader discards below the near plane (eye z 0.1) and interpolates the UVs
// perspective-correct; colour and fog stay linear in screen space (the game's Gouraud), carried as value * z and divided by the interpolated z.
RID GodotSink::mesh_shader(int blend, int zmode, bool a8, bool lin, bool rpt, bool cull, bool lit, bool uclip, bool fog, bool ovl, bool thr, bool crect, bool stale)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	stale = stale && fog && g_nebfog == kNebFogGpu;
	const uint32_t skey = (stale ? 16384u : 0u) | (crect ? 8192u : 0u) |(uint32_t)(blend & 3) | ((uint32_t)(zmode & 3) << 2) | (a8 ? 16u : 0u) | (lin ? 32u : 0u) | (rpt ? 64u : 0u) | (cull ? 128u : 0u) | (lit ? 256u : 0u) | (uclip ? 512u : 0u) | (fog ? 1024u : 0u) | (ovl ? 2048u : 0u) | (thr ? 4096u : 0u);
	auto it = mesh_shaders.find(skey);
	if (it != mesh_shaders.end()) { return it->second; }
	String code = "shader_type spatial;\nrender_mode unshaded, shadows_disabled, fog_disabled, ";
	// the game culls back faces on the CPU (g3_check_normal_facing) for polys that carry the cull flag; the mesh does it on the GPU
	code += cull ? (kMeshFrontCCW ? "cull_back, " : "cull_front, ") : "cull_disabled, ";
	code += blend == FS2_SINK_BLEND_ADD ? "blend_premul_alpha, " : "blend_mix, ";
	code += (zmode == 2 || zmode == 3) ? "depth_draw_always" : "depth_draw_never";
	if (zmode == 0 || zmode == 3) { code += ", depth_test_disabled"; }
	code += ";\nuniform sampler2D tex : ";
	code += lin ? "filter_linear, " : "filter_nearest, ";
	code += rpt ? "repeat_enable;\n" : "repeat_disable;\n";
	code += "uniform sampler2D cols : filter_nearest, repeat_disable;\nuniform float zbias = 0.0;\nuniform vec4 r0;\nuniform vec4 r1;\nuniform vec4 r2;\nuniform vec4 cfg;\nuniform vec4 org;\nvarying vec3 pc;\nvarying vec3 vcol;\nvarying float wv;\n";
	if (uclip) { code += "uniform vec4 clipp;\nvarying float cz;\n"; }	// the game's user clip plane (eye space), kept where cz > 0
	if (crect) { code += "uniform vec4 crc;\n"; }	// the screen clip rect (x0, y0, x1, y1) in pixels
	if (thr) { code += "uniform vec2 thr;\n"; }	// an engine thruster: z below thr.y is scaled by thr.x (model_interp_defpoints)
	const bool fgpu = fog && g_nebfog == kNebFogGpu;
	if (fog) {
		// fogp = near, far, 32 / screen width, 32 / screen height; fogc = the fixed fog colour (not used by fog gpu)
		code += "uniform vec4 fogp;\nuniform vec4 fogc;\nvarying vec3 fgc;\nvarying float fgf;\n";
		if (fgpu) {
			// neb2_get_pixel (Neb.cpp 1229-1343) for the vertex's projected screen cell, on the 32x32 pre-render `bg`: the same integer average
			// over the cell and its neighbours as the poly path's fog gpu takes per poly vertex
			code += "uniform sampler2D bg : filter_nearest, repeat_disable;\n";
			if (stale) {
				// R3 row 9: the colour the last marked poly left (scell = its start cells, sinfo = count, 1 = of the frame before): the integer average
				// over its vertices of nbget, as the poly path (GrD3DRender.cpp 962-980); the pre-render of the frame before when it is that old
				code += "uniform sampler2D bgp : filter_nearest, repeat_disable;\nuniform vec4 scell[16];\nuniform vec2 sinfo;\n";
				code += "ivec3 nbpx(int x, int y) { return ivec3(round((sinfo.y > 0.5 ? texelFetch(bgp, ivec2(x, y), 0) : texelFetch(bg, ivec2(x, y), 0)).rgb * 255.0)); }\n";
			} else {
				code += "ivec3 nbpx(int x, int y) { return ivec3(round(texelFetch(bg, ivec2(x, y), 0).rgb * 255.0)); }\n";
			}
			code += "ivec3 nbget(int xs, int ys) {\n\tivec3 s = nbpx(xs, ys);\n\tint n = 1;\n";
			code += "\tif (xs > 0) { s += nbpx(xs - 1, ys); n++; }\n";
			code += "\tif (xs < 31) { s += nbpx(xs + 1, ys); n++; }\n";
			code += "\tif (ys > 0) { s += nbpx(xs, ys - 1); n++; }\n";
			code += "\tif (ys < 30) { s += nbpx(xs, ys + 1); n++; }\n";
			code += "\tif (xs > 0 && ys > 0) { s += nbpx(xs - 1, ys - 1); n++; }\n";
			code += "\tif (xs < 31 && ys < 31) { s += nbpx(xs + 1, ys + 1); n++; }\n";
			code += "\tif (ys > 0 && xs < 31) { s += nbpx(xs + 1, ys - 1); n++; }\n";
			code += "\tif (ys < 31 && xs > 0) { s += nbpx(xs - 1, ys + 1); n++; }\n";
			code += "\treturn ivec3(s.x / n, s.y / n, s.z / n);\n}\n";
		}
	}
	if (lit) {
		// light_apply_rgb (code/Lighting/Lighting.cpp) per vertex, then the host's gamma table; lt0 = ambient, reflective, level, darken;
		// lt1 = suns, points, 1/gamma, flat byte (< 0: none)
		code += "uniform vec4 lt0;\nuniform vec4 lt1;\nuniform vec4 sunD[4];\nuniform vec4 sunC[4];\nuniform vec4 ptA[16];\nuniform vec4 ptB[16];\nuniform vec4 ptC[16];\nuniform vec4 ptD[16];\n";
		code += "vec3 light_vertex(vec3 pos, vec3 nrm) {\n\tvec3 a = vec3(lt0.x);\n\tvec3 v;\n\tif (lt1.w >= 0.0) { v = vec3(lt1.w / 255.0); } else {\n";
		code += "\tfor (int i = 0; i < 4; i++) {\n\t\tif (float(i) >= lt1.x) { break; }\n\t\tfloat l = -dot(sunD[i].xyz, nrm) * sunD[i].w * lt0.y;\n";
		code += "\t\tif (l > 0.0) { if (lt0.w > 0.5) { a = max(a - vec3(l), vec3(0.0)); } else { a += sunC[i].rgb * l; } }\n\t}\n";
		code += "\ta = clamp(a, vec3(0.0), vec3(0.75)) * lt0.z;\n";
		code += "\tfor (int i = 0; i < 16; i++) {\n\t\tif (float(i) >= lt1.y) { break; }\n\t\tvec3 tl;\n\t\tfloat dist = -1.0;\n";
		code += "\t\tif (ptD[i].x > 0.5) {\n\t\t\tvec3 l0 = ptA[i].xyz;\n\t\t\tvec3 b = ptB[i].xyz - l0;\n\t\t\tfloat bm = length(b);\n\t\t\tfloat comp = dot(pos - l0, b) / bm;\n";
		code += "\t\t\tif (comp < 0.0 || comp > bm) { continue; }\n\t\t\ttl = l0 + b / bm * comp - pos;\n\t\t\tdist = dot(tl, tl);\n\t\t} else { tl = ptA[i].xyz - pos; }\n";
		code += "\t\tfloat d = dot(tl, nrm);\n\t\tif (d > 0.0) {\n\t\t\tif (dist < 0.0) { dist = dot(tl, tl); }\n\t\t\tfloat ra = ptA[i].w;\n\t\t\tfloat rb = ptB[i].w;\n";
		code += "\t\t\tif (dist < ra) { a += ptC[i].rgb * (ptC[i].w * d * 0.25); }\n\t\t\telse if (dist < rb) { a += ptC[i].rgb * ((1.0 - (dist - ra) / (rb - ra)) * d * ptC[i].w * 0.25); }\n\t\t}\n\t}\n";
		code += "\tfloat m = max(a.r, max(a.g, a.b));\n\tif (m > 1.0) { a /= m; }\n\tv = floor(clamp(a, vec3(0.0), vec3(1.0)) * 255.0) / 255.0;\n\t}\n";
		code += "\treturn floor(pow(v, vec3(lt1.z)) * 255.0) / 255.0;\n}\n";
	}
	code += "void vertex() {\n\tvec3 p = VERTEX;\n";
	if (thr) { code += "\tif (p.z < thr.y) { p.z *= thr.x; }\n"; }
	code += "\tvec3 e =vec3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);\n";
	code += "\tfloat z = max(e.z, 1.0);\n\tfloat d = cfg.z + min(z / 30000.0, 0.98) * 1000.0 - zbias * 0.05;\n";
	if (thr && stale) {	// the polygon path snaps screen positions to the 1/16 pixel grid (tmapper_internal): so does the flame; y is flipped, hence ceil
		code += "\tvec2 sp = vec2(floor((org.x + cfg.x * e.x / z) * 16.0), ceil((org.y + cfg.y * e.y / z) * 16.0)) / 16.0;\n";
		code += "\tPOSITION = PROJECTION_MATRIX * vec4(sp.x * e.z, sp.y * e.z, -d * e.z, e.z);\n\tpc = vec3(UV, 1.0);\n\twv = e.z;\n";
	} else {
		code += "\tPOSITION = PROJECTION_MATRIX * vec4(org.x * e.z + cfg.x * e.x, org.y * e.z + cfg.y * e.y, -d * e.z, e.z);\n\tpc = vec3(UV, 1.0);\n\twv = e.z;\n";
	}
	if (thr && stale) { code += "\tpc = vec3(pc.xy * e.z, e.z);\n"; }	// the polygon path draws a thruster without TMAP_FLAG_CORRECT: uvs affine in screen space
	if (uclip) { code += "\tcz = dot(clipp.xyz, e) + clipp.w;\n"; }
	if (fog) {
		// linear in the eye depth, as the poly path (gr_d3d_stuff_fog_value): depth = the capped vertex z * 30000
		code += "\tfgf = clamp((fogp.y - min(z / 30000.0, 0.98) * 30000.0) / (fogp.y - fogp.x), 0.0, 1.0) * e.z;\n";
		if (fgpu) {
			// the game-screen pixel of the vertex, truncated as the host passes (int)x, (int)y, then the start cell as Neb.cpp 1252-1273
			if (stale) {
				code += "\tivec3 tot = ivec3(0);\n\tint sn = int(sinfo.x + 0.5);\n\tfor (int i = 0; i < 32; i++) {\n\t\tif (i >= sn) { break; }\n";
				code += "\t\tvec4 sc = scell[i >> 1];\n\t\tvec2 c2 = (i & 1) == 0 ? sc.xy : sc.zw;\n\t\ttot += nbget(int(c2.x + 0.5), int(c2.y + 0.5));\n\t}\n";
				code += "\tint sd = max(sn, 1);\n\tfgc = vec3(float(tot.x / sd), float(tot.y / sd), float(tot.z / sd)) / 255.0 * e.z;\n";
			} else {
				code += "\tint xs = int(fogp.z * float(int(cfg.x * (1.0 + e.x / z))));\n\tint ys = int(fogp.w * float(int(cfg.y * (1.0 - e.y / z))));\n";
				code += "\tif (ys >= 31) { ys--; }\n\txs = clamp(xs, 0, 31);\n\tys = clamp(ys, 0, 31);\n\tfgc = vec3(nbget(xs, ys)) / 255.0 * e.z;\n";
			}
		} else {
			code += "\tfgc = fogc.rgb * e.z;\n";
		}
	}
	code += ovl ? "\tvcol = vec3(e.z);\n}\n" : lit ? "\tvcol = light_vertex(p, CUSTOM1.xyz) * e.z;\n}\n" : "\tint ci = int(CUSTOM0.x + 0.5);\n\tivec2 csz = textureSize(cols, 0);\n\tvcol = texelFetch(cols, ivec2(min(ci & 255, csz.x - 1), min(ci >> 8, csz.y - 1)), 0).rgb * e.z;\n}\n";
	code += "void fragment() {\n";
	code += "\tif (wv < 0.1) { discard; }\n";	// the near plane, eye z 0.1 (Tom's order; the GPU itself clips at w > 0, the host no longer clips)
	if (crect) { code += "\tif (FRAGCOORD.x < crc.x || FRAGCOORD.y < crc.y || FRAGCOORD.x >= crc.z || FRAGCOORD.y >= crc.w) { discard; }\n"; }
	if (uclip) { code += "\tif (cz <= 0.0) { discard; }\n"; }
	code += "\tvec4 t = texture(tex, pc.xy / pc.z);\n";
	if (a8) { code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n"; }
	code += "\tvec4 c = t;\n\tc.rgb *= vcol / wv;\n";
	if (blend == FS2_SINK_BLEND_OPAQUE) { code += "\tc.a = 1.0;\n"; } else if (blend == FS2_SINK_BLEND_ADD) { code += "\tc.a = 0.0;\n"; }
	if (fog) { code += "\tc.rgb = mix(fgc / wv, c.rgb, fgf / wv);\n"; }	// D3D: f * colour + (1 - f) * fog colour, rgb only
	code += "\tALBEDO = clamp(c.rgb, 0.0, 1.0);\n\tALPHA = c.a;\n}\n";
	RID sh = rs->shader_create();
	rs->shader_set_code(sh, code);
	mesh_shaders[skey] = sh;
	return sh;
}

void GodotSink::free_mesh_data(MeshData &md)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (MeshGroup &mg : md.groups) {
		if (mg.mesh_rid.is_valid()) { rs->free_rid(mg.mesh_rid); }
	}
	md.groups.clear();
	md.states.clear();
}

GodotSink::MeshGroupState GodotSink::mesh_group_state(const fs2_sink_mesh_group &g)
{
	MeshGroupState st;
	st.g = g;
	const bool zr = (g.flags & FS2_SINK_DRAWF_ZREAD) != 0, zw = (g.flags & FS2_SINK_DRAWF_ZWRITE) != 0;
	st.zmode = zr ? (zw ? 2 : 1) : (zw ? 3 : 0);
	return st;
}

// The state of the draws that follow. The geometry stays; a state seen before is reused, so the list stays as short as the game's variety.
void GodotSink::s_mesh_state(void *u, int32_t id, const fs2_sink_mesh_group *g, int ng)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	auto it = s->mesh_data.find(id);
	if (!s->in_frame || it == s->mesh_data.end() || g == nullptr || ng != (int)it->second.groups.size()) { return; }
	MeshData &md = it->second;
	std::vector<MeshGroupState> st((size_t)ng);
	for (int i = 0; i < ng; i++) { st[(size_t)i] = mesh_group_state(g[i]); }
	for (size_t k = 0; k < md.states.size(); k++) {
		bool same = true;
		for (int i = 0; i < ng && same; i++) {
			const fs2_sink_mesh_group &a = md.states[k][(size_t)i].g, &b = st[(size_t)i].g;
			same = a.texture == b.texture && a.blend == b.blend && a.flags == b.flags && a.zbias == b.zbias;
		}
		if (same) { md.cur = (int)k; return; }
	}
	md.states.push_back(std::move(st));
	md.cur = (int)md.states.size() - 1;
}

// A material for one group of one draw: the group's shader and texture, and the draw's colour table.
RID GodotSink::mesh_group_material(const MeshGroupState &mg, const RID &cols, bool lit, bool uclip, bool fog, bool thr, bool crect, bool stale)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	bool a8 = false;
	uint32_t tflags = 0;
	RID tex = white_texture();
	if (mg.g.texture != 0) {
		auto it = textures.find(mg.g.texture);
		if (it != textures.end()) {
			tex = it->second.rid;
			a8 = (it->second.format == FS2_SINK_TEX_A8);
			tflags = it->second.flags;
		}
	}
	RID mat;
	if (!mesh_mat_pool.empty()) { mat = mesh_mat_pool.back(); mesh_mat_pool.pop_back(); } else { mat = rs->material_create(); }
	const bool lin = (tflags & FS2_SINK_TEXF_LINEAR) != 0, rpt = (tflags & FS2_SINK_TEXF_REPEAT) != 0;
	// a glow overlay (FS2_SINK_DRAWF_OVERLAY, the ship glow pass) is unlit and a hair nearer than the lit group it covers, as the poly path's overlay draws
	const bool ovl = (mg.g.flags & FS2_SINK_DRAWF_OVERLAY) != 0;
	rs->material_set_shader(mat, mesh_shader(mg.g.blend, mg.zmode, a8, lin, rpt, (mg.g.flags & FS2_SINK_DRAWF_CULL) != 0, lit && !ovl, uclip, fog, ovl, thr, crect, stale));
	rs->material_set_param(mat, "tex", tex);
	rs->material_set_param(mat, "cols", cols);
	rs->material_set_param(mat, "zbias", (float)mg.g.zbias + (ovl ? 1.0f : 0.0f));	// 0.05 depth units a step: the poly path's overlay shift
	return mat;
}

void GodotSink::release_mesh_res(Layer &l, bool free_them)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (RID &m : l.mesh_materials) { if (free_them) { rs->free_rid(m); } else { mesh_mat_pool.push_back(m); } }
	for (auto &t : l.mesh_coltex) { if (free_them) { rs->free_rid(t.second); } else { mesh_col_pool[t.first].push_back(t.second); } }
	l.mesh_materials.clear();
	l.mesh_coltex.clear();
	for (auto &m : l.bb_multimeshes) { if (free_them) { rs->free_rid(m.second); } else { bb_mm_pool[m.first].push_back(m.second); } }
	l.bb_multimeshes.clear();
}

void GodotSink::s_mesh_create(void *u, int32_t id, const fs2_sink_mesh_vertex *v, int nv, const fs2_sink_mesh_group *g, int ng,
	const uint32_t *idx, int ni)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (id == 0 || v == nullptr || nv <= 0 || g == nullptr || ng <= 0 || idx == nullptr || ni <= 0) { return; }	// legal outside a frame (level load)
	RenderingServer *rs = RenderingServer::get_singleton();
	auto old = s->mesh_data.find(id);
	if (old != s->mesh_data.end()) { s->free_mesh_data(old->second); s->mesh_data.erase(old); }
	MeshData md;
	for (int gi = 0; gi < ng; gi++) {
		MeshGroup mg;
		// the vertices of this group only, renumbered
		std::map<uint32_t, int> remap;
		std::vector<uint32_t> used;
		PackedInt32Array indices;
		indices.resize(g[gi].index_count);
		for (int k = 0; k < g[gi].index_count; k++) {
			const uint32_t src = idx[g[gi].first_index + k];
			auto r = remap.find(src);
			if (r == remap.end()) { r = remap.emplace(src, (int)used.size()).first; used.push_back(src); }
			indices.ptrw()[k] = r->second;
		}
		PackedVector3Array pos;
		PackedVector2Array uvs;
		pos.resize((int64_t)used.size());
		uvs.resize((int64_t)used.size());
		PackedFloat32Array orig;
		orig.resize((int64_t)used.size());
		PackedFloat32Array nrm;		// CUSTOM1: the vertex normal, for the lit draws
		nrm.resize((int64_t)used.size() * 3);
		for (size_t k = 0; k < used.size(); k++) {
			const fs2_sink_mesh_vertex &sv = v[used[k]];
			pos.ptrw()[k] = Vector3(sv.pos[0], sv.pos[1], sv.pos[2]);
			uvs.ptrw()[k] = Vector2(sv.u, sv.v);
			orig.ptrw()[k] = (float)used[k];
			nrm.ptrw()[k * 3 + 0] = sv.normal[0]; nrm.ptrw()[k * 3 + 1] = sv.normal[1]; nrm.ptrw()[k * 3 + 2] = sv.normal[2];
		}
		Array arrays;
		arrays.resize(RenderingServer::ARRAY_MAX);
		arrays[RenderingServer::ARRAY_VERTEX] = pos;
		arrays[RenderingServer::ARRAY_TEX_UV] = uvs;
		arrays[RenderingServer::ARRAY_CUSTOM0] = orig;	// the vertex's index in the whole mesh: where its colour is in the draw's table
		arrays[RenderingServer::ARRAY_CUSTOM1] = nrm;
		arrays[RenderingServer::ARRAY_INDEX] = indices;
		mg.mesh_rid = rs->mesh_create();
		rs->mesh_add_surface_from_arrays(mg.mesh_rid, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
			(BitField<RenderingServer::ArrayFormat>)(((int64_t)RenderingServer::ARRAY_CUSTOM_R_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT)
				| ((int64_t)RenderingServer::ARRAY_CUSTOM_RGB_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM1_SHIFT)));
		// the vertices are model space, not where the camera looks: the culling box is made huge so Godot never drops the instance
		rs->mesh_set_custom_aabb(mg.mesh_rid, AABB(Vector3(-1.0e6f, -1.0e6f, -1.0e6f), Vector3(2.0e6f, 2.0e6f, 2.0e6f)));
		md.groups.push_back(mg);
	}
	std::vector<MeshGroupState> st((size_t)ng);
	for (int gi = 0; gi < ng; gi++) { st[(size_t)gi] = mesh_group_state(g[gi]); }
	md.states.push_back(std::move(st));
	s->mesh_data[id] = std::move(md);
}

void GodotSink::s_mesh_free(void *u, int32_t id)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	auto it = s->mesh_data.find(id);
	if (it == s->mesh_data.end()) { return; }
	s->free_mesh_data(it->second);
	s->mesh_data.erase(it);
}

void GodotSink::s_mesh_draw(void *u, int32_t id, const float *xform, const uint8_t *colours, int ncolours)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	auto md = s->mesh_data.find(id);
	if (!s->in_frame || !s->in_run || s->clip_empty || xform == nullptr || md == s->mesh_data.end()) { return; }
	s->batcher.flush();
	MeshDraw d;
	d.crect = s->clipped;
	if (d.crect) { std::memcpy(d.cr, s->clip, sizeof(d.cr)); }
	d.id = id;
	d.state = md->second.cur;
	std::memcpy(d.xf, xform, sizeof(d.xf));
	if (colours != nullptr && ncolours > 0) { d.col.assign(colours, colours + (size_t)ncolours * 4); }
	d.epoch = s->run_epoch;
	d.clip = s->mesh_clip_on;
	if (d.clip) { std::memcpy(d.plane, s->mesh_clip_plane, sizeof(d.plane)); }
	d.thr = s->mesh_thr_on;
	if (d.thr) { d.thrv[0] = s->mesh_thr[0]; d.thrv[1] = s->mesh_thr[1]; }
	d.fog = s->mesh_fog_state.mode == FS2_SINK_FOG_LINEAR;
	d.fogv = s->mesh_fog_state;
	if (d.fog && d.fogv.rgb[3] == 1 && g_nebfog == kNebFogGpu) { d.stale = true; d.sn = s->batcher.fog_stale(d.scell, d.sprev); }
	if (s->have_view) { d.cw = s->view.canvas_w; d.ch = s->view.canvas_h; d.ox = s->view.origin_x; d.oy = s->view.origin_y; }
	s->mesh_draws.push_back(d);
	Batch b;
	b.key.texture = -(int32_t)s->mesh_draws.size();
	b.key.depth = 1; b.key.zmode = 2; b.key.epoch = d.epoch;
	s->run_batches.push_back(b);
}

// R3 row 9: a poly marked fog_rgb[3] == 1 drawn on the host's mesh path; it only sets the colour state of the fogged draws after it
void GodotSink::s_mesh_fog_mark(void *u, const float *xy, int n)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame || xy == nullptr || g_nebfog != kNebFogGpu) { return; }
	s->batcher.note_marked(xy, n);
}

// the thruster scale for the draws that follow; NULL clears it
void GodotSink::s_mesh_thrust(void *u, const float t[2])
{
	GodotSink *s = static_cast<GodotSink *>(u);
	s->mesh_thr_on = t != nullptr;
	if (t != nullptr) { s->mesh_thr[0] = t[0]; s->mesh_thr[1] = t[1]; }
}

// the fog of the mesh draws that follow; NULL clears it
int GodotSink::s_mesh_fog(void *u, const fs2_sink_mesh_fog *fog)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (fog != nullptr && fog->mode == FS2_SINK_FOG_LINEAR && fog->fog_far > fog->fog_near) {
		// only the gpu nebula fog has the pixels behind the model to query; the other modes leave a fogged model on the CPU path
		if (g_nebfog != kNebFogGpu) { s->mesh_fog_state = fs2_sink_mesh_fog(); return 0; }
		s->mesh_fog_state = *fog;
	} else {
		s->mesh_fog_state = fs2_sink_mesh_fog();
	}
	return 1;
}

// the user clip plane for the draws that follow; NULL clears it
void GodotSink::s_mesh_clip(void *u, const float plane[4])
{
	GodotSink *s = static_cast<GodotSink *>(u);
	s->mesh_clip_on = plane != nullptr;
	if (plane != nullptr) { std::memcpy(s->mesh_clip_plane, plane, sizeof(s->mesh_clip_plane)); }
}

// as s_mesh_draw, but the vertex colours are worked out on the GPU from the lighting inputs
void GodotSink::s_mesh_draw_lit(void *u, int32_t id, const float *xform, const fs2_sink_mesh_light *light)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	auto md = s->mesh_data.find(id);
	if (!s->in_frame || !s->in_run || s->clip_empty || xform == nullptr || light == nullptr || md == s->mesh_data.end()) { return; }
	s->batcher.flush();
	MeshDraw d;
	d.crect = s->clipped;
	if (d.crect) { std::memcpy(d.cr, s->clip, sizeof(d.cr)); }
	d.id = id;
	d.state = md->second.cur;
	std::memcpy(d.xf, xform, sizeof(d.xf));
	d.lit = true;
	d.light = *light;
	d.epoch = s->run_epoch;
	d.clip = s->mesh_clip_on;
	if (d.clip) { std::memcpy(d.plane, s->mesh_clip_plane, sizeof(d.plane)); }
	d.fog = s->mesh_fog_state.mode == FS2_SINK_FOG_LINEAR;
	d.fogv = s->mesh_fog_state;
	if (s->have_view) { d.cw = s->view.canvas_w; d.ch = s->view.canvas_h; d.ox = s->view.origin_x; d.oy = s->view.origin_y; }
	s->mesh_draws.push_back(d);
	Batch b;
	b.key.texture = -(int32_t)s->mesh_draws.size();
	b.key.depth = 1; b.key.zmode = 2; b.key.epoch = d.epoch;
	s->run_batches.push_back(b);
}

void GodotSink::add_mesh_to_layer(Layer &l, const MeshDraw &d, int order)
{
	auto it = mesh_data.find(d.id);
	if (it == mesh_data.end() || d.state < 0 || d.state >= (int)it->second.states.size()) { return; }
	const std::vector<MeshGroupState> &states = it->second.states[(size_t)d.state];
	fs2perf::count(fs2perf::C_LAYER_BATCHES);
	fs2perf::count(fs2perf::C_MESH_DRAWS);
	stat_mesh_draws++;
	RenderingServer *rs = RenderingServer::get_singleton();
	const int epoch = d.epoch < MAX_EPOCH ? d.epoch : MAX_EPOCH;
	const float origin = 29000.0f - 0.25f * (float)order;
	const float dbase = 2.0f + (float)(MAX_EPOCH - epoch) * 1100.0f;
	// the draw's colour table: 256 texels wide, a row per 256 vertices, rows rounded up to a power of two so tables pool
	int rows = 1;
	while (rows * 256 < (int)(d.col.size() / 4)) { rows <<= 1; }
	RID cols = white_texture();
	if (!d.col.empty()) {
		std::vector<RID> &pool = mesh_col_pool[rows];
		PackedByteArray bytes;
		bytes.resize((int64_t)rows * 256 * 4);
		std::memset(bytes.ptrw(), 255, (size_t)bytes.size());
		std::memcpy(bytes.ptrw(), d.col.data(), d.col.size());
		Ref<Image> img = Image::create_from_data(256, rows, false, Image::FORMAT_RGBA8, bytes);
		if (!pool.empty()) {
			cols = pool.back();
			pool.pop_back();
			rs->texture_2d_update(cols, img, 0);
		} else {
			cols = rs->texture_2d_create(img);
		}
		l.mesh_coltex.push_back(std::make_pair(rows, cols));
	}
	for (size_t gi = 0; gi < it->second.groups.size(); gi++) {
		MeshGroup &mg = it->second.groups[gi];
		const RID mat = mesh_group_material(states[gi], cols, d.lit, d.clip, d.fog, d.thr, d.crect, d.stale);
		l.mesh_materials.push_back(mat);
		const float gorigin = (states[gi].g.flags & FS2_SINK_DRAWF_OVERLAY) ? origin - 0.1f : origin;	// the overlay sorts after the lit group (back to front by origin)
		RID inst;
		if (kInstancePool && !spare_instances.empty()) {
			inst = spare_instances.back();
			spare_instances.pop_back();
			rs->instance_set_base(inst, mg.mesh_rid);
			rs->instance_set_scenario(inst, l.scenario);
			rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(0.0f, 0.0f, -gorigin)));
		} else {
			inst = rs->instance_create2(mg.mesh_rid, l.scenario);
			rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(0.0f, 0.0f, -gorigin)));
			rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
			rs->instance_set_pivot_data(inst, 0.0f, false);	// sort by the origin, which carries the call order, not by the mesh's bounds
		}
		rs->instance_geometry_set_material_override(inst, mat);
		// per-draw values are material parameters (the material is per draw), not instance uniforms: those come from a fixed-size buffer
		// Vector4, not Color: a Color on a vec4 uniform can be taken as sRGB and converted, which wrecks values above 1
		rs->material_set_param(mat, "r0", Vector4(d.xf[0], d.xf[1], d.xf[2], d.xf[3]));
		rs->material_set_param(mat, "r1", Vector4(d.xf[4], d.xf[5], d.xf[6], d.xf[7]));
		rs->material_set_param(mat, "r2", Vector4(d.xf[8], d.xf[9], d.xf[10], d.xf[11]));
		const float cw = d.cw > 0 ? (float)d.cw : (float)width, ch = d.ch > 0 ? (float)d.ch : (float)height;
		rs->material_set_param(mat, "cfg", Vector4(0.5f * cw, 0.5f * ch, dbase, origin));
		// canvas centre relative to the screen centre, y up
		rs->material_set_param(mat, "org", Vector4((float)d.ox + 0.5f * cw - 0.5f * (float)width + FS2_SINK_D3D_PIXEL_CENTRE, -((float)d.oy + 0.5f * ch - 0.5f * (float)height + FS2_SINK_D3D_PIXEL_CENTRE), 0.0f, 0.0f));
		if (d.clip) { rs->material_set_param(mat, "clipp", Vector4(d.plane[0], d.plane[1], d.plane[2], d.plane[3])); }
		if (d.crect) { rs->material_set_param(mat, "crc", Vector4((float)d.cr[0], (float)d.cr[1], (float)(d.cr[0] + d.cr[2]), (float)(d.cr[1] + d.cr[3]))); }
		if (d.thr) { rs->material_set_param(mat, "thr", Vector2(d.thrv[0], d.thrv[1])); }
		if (d.fog) {
			rs->material_set_param(mat, "fogp", Vector4(d.fogv.fog_near, d.fogv.fog_far, batcher.fog_cell_x(), batcher.fog_cell_y()));
			rs->material_set_param(mat, "fogc", Vector4(d.fogv.rgb[0] / 255.0f, d.fogv.rgb[1] / 255.0f, d.fogv.rgb[2] / 255.0f, 0.0f));
			if (g_nebfog == kNebFogGpu) { rs->material_set_param(mat, "bg", gpu_bg_texture((int)(committed & 1))); }	// this frame's pre-render, as the poly materials
			if (d.stale) {
				rs->material_set_param(mat, "bgp", gpu_bg_texture((int)((committed & 1) ^ 1)));	// the frame before's pre-render (cells of a mark that old)
				PackedVector4Array sc;
				sc.resize(16);
				for (int i = 0; i < 16; i++) { sc.set(i, Vector4((float)d.scell[i * 4], (float)d.scell[i * 4 + 1], (float)d.scell[i * 4 + 2], (float)d.scell[i * 4 + 3])); }
				rs->material_set_param(mat, "scell", sc);
				rs->material_set_param(mat, "sinfo", Vector2((float)d.sn, d.sprev ? 1.0f : 0.0f));
			}
		}
		if (d.lit) {
			const fs2_sink_mesh_light &L = d.light;
			rs->material_set_param(mat, "lt0", Vector4(L.ambient, L.reflective, L.level, (float)L.darken));
			rs->material_set_param(mat, "lt1", Vector4((float)L.nsun, (float)L.npoint, L.inv_gamma, (float)L.flat));
			PackedVector4Array sd, sc, pa, pb, pc4, pd;
			sd.resize(FS2_SINK_MAX_SUN); sc.resize(FS2_SINK_MAX_SUN);
			for (int i = 0; i < FS2_SINK_MAX_SUN; i++) {
				sd.set(i, Vector4(L.sun_dir[i][0], L.sun_dir[i][1], L.sun_dir[i][2], L.sun_intensity[i]));
				sc.set(i, Vector4(L.sun_rgb[i][0], L.sun_rgb[i][1], L.sun_rgb[i][2], 0.0f));
			}
			pa.resize(FS2_SINK_MAX_POINT); pb.resize(FS2_SINK_MAX_POINT); pc4.resize(FS2_SINK_MAX_POINT); pd.resize(FS2_SINK_MAX_POINT);
			for (int i = 0; i < FS2_SINK_MAX_POINT; i++) {
				pa.set(i, Vector4(L.point_pos[i][0], L.point_pos[i][1], L.point_pos[i][2], L.point_rad1_sq[i]));
				pb.set(i, Vector4(L.point_pos2[i][0], L.point_pos2[i][1], L.point_pos2[i][2], L.point_rad2_sq[i]));
				pc4.set(i, Vector4(L.point_rgb[i][0], L.point_rgb[i][1], L.point_rgb[i][2], L.point_intensity[i]));
				pd.set(i, Vector4((float)L.point_tube[i], 0.0f, 0.0f, 0.0f));
			}
			rs->material_set_param(mat, "sunD", sd); rs->material_set_param(mat, "sunC", sc);
			rs->material_set_param(mat, "ptA", pa); rs->material_set_param(mat, "ptB", pb);
			rs->material_set_param(mat, "ptC", pc4); rs->material_set_param(mat, "ptD", pd);
		}
		l.instances.push_back(inst);
	}
}

// ---- v5 billboards (R3) -------------------------------------------------------------------------------------------------------------
// g3_draw_bitmap and g3_draw_laser on the GPU: the vertex shader works out the four corners from the eye-space points with the game's own
// formulas (docs/R3_billboards_design.md), in canvas pixels, snaps them to the 1/16 grid as the host does, and places them as the mesh shader
// does (x = org.x + sx - cw/2, y = org.y + ch/2 - sy, w = 1; the perspective-correct uvs are done by hand with the game's per-end sw).
RID GodotSink::billboard_shader(int kind, int blend, int zmode, bool a8, bool lin, bool crect, bool arr, bool rep, bool aff, bool nc)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const uint32_t skey = ((uint32_t)(kind & 7) << 9) | ((uint32_t)(blend & 3) << 1) | ((uint32_t)(zmode & 3) << 3) | (a8 ? 32u : 0u) | (lin ? 64u : 0u) | (crect ? 128u : 0u) | (arr ? 256u : 0u) | (rep ? 4096u : 0u) | (aff ? 8192u : 0u) | (nc ? 16384u : 0u);
	auto it = bb_shaders.find(skey);
	if (it != bb_shaders.end()) { return it->second; }
	if (kind == FS2_SINK_BB_STARS) {
		// R3 row 8: stars_draw's per-star work (docs/R3_stars_design.md). One instance per star (pA = unit vector, pA.w = the star's index i), VERTEX.x picks the end
		// (0 = tail p1, 1 = head p2), VERTEX.y the side. Every step is the game's own float expression in the same order (vm_vec_rotate, g3_code_vertex,
		// g3_project_vertex, vm_vec_dist_quick, the tail, the colour index) and (int) is trunc(); the strip is segment()'s: an axis aligned line is the pixel rect
		// from one end to the other inclusive, any other a one pixel wide strip through the pixel centres. Coordinates are Godot's (the pixel x covers [x, x + 1)); the
		// org uniform adds the D3D half pixel, so 0.5 comes off here. A star the CPU route drops (code, overflow) collapses to nothing.
		String sc;
		sc += "shader_type spatial;\n";
		sc += "render_mode unshaded, shadows_disabled, fog_disabled, cull_disabled, blend_mix, depth_draw_never, depth_test_disabled;\n";
		sc += "uniform vec4 cfg;\n";
		sc += "uniform vec4 org;\n";
		sc += "uniform vec4 vr;\n";
		sc += "uniform vec4 vu;\n";
		sc += "uniform vec4 vf;\n";
		sc += "uniform vec4 pr;\n";
		sc += "uniform vec4 pu;\n";
		sc += "uniform vec4 pf;\n";
		sc += "uniform vec4 sp;\n";
		sc += "uniform vec4 sfl;\n";
		sc += "varying flat float alp;\n";
		sc += "vec3 rot(vec3 s, vec4 a, vec4 b, vec4 c) { return vec3((s.x * a.x) + (s.y * a.y) + (s.z * a.z), (s.x * b.x) + (s.y * b.y) + (s.z * b.z), (s.x * c.x) + (s.y * c.y) + (s.z * c.z)); }\n";
		sc += "bool dropped(vec3 p) { return p.x > p.z || p.y > p.z || p.x < -p.z || p.y < -p.z || p.z <= 0.0; }\n";
		sc += "vec2 proj(vec3 p, vec2 cwh) { float w = 1.0 / p.z; return vec2((cwh.x + (p.x * cwh.x * w)) * 0.5, (cwh.y - (p.y * cwh.y * w)) * 0.5); }\n";
		sc += "float magq(vec3 v) { float a = abs(v.x); float b = abs(v.y); float c = abs(v.z); if (a < b) { float t1 = a; a = b; b = t1; } if (b < c) { float t2 = b; b = c; c = t2; if (a < b) { float t3 = a; a = b; b = t3; } } float bc = (b * 0.25) + (c * 0.125); return a + bc + (bc * 0.5); }\n";
		sc += "void vertex() {\n";
		sc += "\tmat4 M = MODEL_MATRIX;\n";
		sc += "\tvec3 pos = vec3(M[0][0], M[0][1], M[0][2]);\n";
		sc += "\tint idx = int(M[1][0] + 0.5);\n";
		sc += "\tvec2 q = VERTEX.xy;\n";
		sc += "\tvec2 cwh = 2.0 * cfg.xy;\n";
		sc += "\talp = 0.0;\n";
		sc += "\tbool skip = false;\n";
		sc += "\tvec3 p2 = rot(pos, vr, vu, vf);\n";
		sc += "\tif (dropped(p2)) { skip = true; }\n";
		sc += "\tvec2 s2 = skip ? vec2(0.0) : proj(p2, cwh);\n";
		sc += "\tint fl = int(sfl.x + 0.5);\n";
		sc += "\tvec3 p1 = vec3(0.0);\n";
		sc += "\tvec2 s1 = vec2(0.0);\n";
		sc += "\tfloat dist = 0.0;\n";
		sc += "\tif (!skip) {\n";
		sc += "\t\tvec3 last = rot(pos, pr, pu, pf);\n";
		sc += "\t\tdist = magq(last - p2);\n";
		sc += "\t\tfloat ratio;\n";
		sc += "\t\tif (dist > sp.w) { ratio = sp.w / dist; dist = sp.w; } else { ratio = 1.0; }\n";
		sc += "\t\tratio *= sp.x;\n";
		sc += "\t\tp1 = vec3(p2.x + ((last.x - p2.x) * ratio), p2.y + ((last.y - p2.y) * ratio), p2.z + ((last.z - p2.z) * ratio));\n";
		sc += "\t\tif (dropped(p1)) { skip = true; } else { s1 = proj(p1, cwh); }\n";
		sc += "\t}\n";
		sc += "\tint ci = idx & 7;\n";
		sc += "\tif ((fl & 2) != 0) { float colorf = 255.0 - (dist * sp.y); if (colorf < sp.z) { colorf = sp.z; } ci = (int(colorf) * (idx & 7)) / 256; }\n";
		sc += "\talp = float((ci + 1) * 24) / 255.0;\n";
		sc += "\tfloat x1f = s1.x;\n";
		sc += "\tif (int(s1.x) == int(s2.x) && int(s1.y) == int(s2.y)) { x1f = s1.x + 1.0; }\n";
		sc += "\tint ix1 = int(x1f); int iy1 = int(s1.y); int ix2 = int(s2.x); int iy2 = int(s2.y);\n";
		sc += "\tint cr = int(cwh.x) - 1;\n";
		sc += "\tif (ix1 > cr && iy1 == iy2) { ix1 = cr; }\n";
		sc += "\tvec2 c;\n";
		sc += "\tif (ix1 == ix2 || iy1 == iy2) {\n";
		sc += "\t\tint lx = min(ix1, ix2); int hx = max(ix1, ix2); int ly = min(iy1, iy2); int hy = max(iy1, iy2);\n";
		sc += "\t\tif (ix1 == ix2) { c = vec2(q.y > 0.5 ? float(lx + 1) : float(lx), q.x > 0.5 ? float(hy + 1) : float(ly)); } else { c = vec2(q.x > 0.5 ? float(hx + 1) : float(lx), q.y > 0.5 ? float(ly + 1) : float(ly)); }\n";
		sc += "\t} else {\n";
		sc += "\t\tvec2 a = vec2(float(ix1) + 0.5, float(iy1) + 0.5);\n";
		sc += "\t\tvec2 b = vec2(float(ix2) + 0.5, float(iy2) + 0.5);\n";
		sc += "\t\tvec2 d = b - a;\n";
		sc += "\t\tfloat len = sqrt(d.x * d.x + d.y * d.y);\n";
		sc += "\t\tvec2 n = vec2(-d.y / len * 0.5, d.x / len * 0.5);\n";
		sc += "\t\tc = (q.x > 0.5 ? b : a) + (q.y > 0.5 ? -n : n);\n";
		sc += "\t}\n";
		sc += "\tif (skip) { POSITION = vec4(0.0); } else { POSITION = PROJECTION_MATRIX * vec4(org.x + c.x - 0.5 - cfg.x, org.y + cfg.y - (c.y - 0.5), -cfg.z, 1.0); }\n";
		sc += "}\n";
		sc += "void fragment() {\n";
		sc += "\tALBEDO = vec3(1.0);\n";
		sc += "\tALPHA = alp;\n";
		sc += "}\n";
		RID ssh = rs->shader_create();
		rs->shader_set_code(ssh, sc);
		bb_shaders[skey] = ssh;
		return ssh;
	}
	String code = "shader_type spatial;\nrender_mode unshaded, shadows_disabled, fog_disabled, cull_disabled, ";
	code += blend == FS2_SINK_BLEND_ADD ? "blend_premul_alpha, " : "blend_mix, ";
	code += (zmode == 2 || zmode == 3) ? "depth_draw_always" : "depth_draw_never";
	if (zmode == 0 || zmode == 3) { code += ", depth_test_disabled"; }
	code += arr ? ";\nuniform sampler2DArray tex : " : ";\nuniform sampler2D tex : ";
	code += lin ? "filter_linear, " : "filter_nearest, ";
	code += rep ? "repeat_enable;\n" : "repeat_disable;\n";	// rep: the subspace tunnel's tiled polys (R3 row 6)
	// one draw call per run of draws that share this state: per run bwh = bw, bh; sc = Matrix_scale x, y; bcl = the scaler clip l, t, r, b; cfg = canvas/2, dbase;
	// per draw, in the instance's 3x4 transform (read back from MODEL_MATRIX, which is exact: the instance transform is the identity) and its custom data:
	// pA = p0 xyz, rad; pB = p1 xyz, rad_tail; bbp = max_len, orient, bw, bh; zbias; tint
	code += "uniform vec2 bwh;\nuniform vec4 sc;\nuniform vec4 bcl;\nuniform vec4 cfg;\nuniform vec4 org;\nvarying vec3 pc;\nvarying flat vec4 tintv;\nvarying flat float lyr;\n";
	if (crect) { code += "uniform vec4 crc;\n"; }
	code += "float snap16(float v) { return trunc(v * 16.0) / 16.0; }\n";
	code += "void vertex() {\n\tmat4 M = MODEL_MATRIX;\n\tvec4 pA = vec4(M[0][0], M[0][1], M[0][2], M[1][0]);\n\tvec4 pB = vec4(M[1][1], M[1][2], M[2][0], M[2][1]);\n";
	code += "\tvec4 bbp = vec4(M[2][2], M[3][0], bwh.x, bwh.y);\n\tfloat zbias = M[3][1];\n\ttintv = INSTANCE_CUSTOM;\n\tlyr = M[3][2];\n";
	code += "\tvec2 q = VERTEX.xy;\n\tvec2 cwh = 2.0 * cfg.xy;\n\tfloat sx; float sy; float zq; float sw = 1.0; vec2 uv = q;\n\tbool skip = false;\n";
	if (kind == FS2_SINK_BB_BITMAP) {
		code += "\tfloat rad = pA.w;\n\tfloat width; float height;\n";
		code += "\tif (bbp.z < bbp.w) { width = rad * 2.0; height = width * bbp.w / bbp.z; } else if (bbp.z > bbp.w) { height = rad * 2.0; width = height * bbp.z / bbp.w; } else { width = rad * 2.0; height = width; }\n";
		code += "\tfloat z = pA.z;\n\tfloat w = width * cfg.x / z * sc.x;\n\tfloat h = height * cfg.y / z * sc.y;\n";
		code += "\tfloat x0 = (cwh.x + pA.x * cwh.x / z) * 0.5 - w * 0.5;\n\tfloat y0 = (cwh.y - pA.y * cwh.y / z) * 0.5 - h * 0.5;\n\tfloat x1 = x0 + w;\n\tfloat y1 = y0 + h;\n";
		// h_scaler: nothing for an empty, outside or sub-pixel quad (after the clip)
		code += "\tif (y1 <= y0 || x1 <= x0 || x1 < bcl.x || x0 > bcl.z || y1 < bcl.y || y0 > bcl.w) { skip = true; }\n";
		code += "\tif (int(min(x1, bcl.z)) <= int(max(x0, bcl.x)) || int(min(y1, bcl.w)) <= int(max(y0, bcl.y))) { skip = true; }\n";
		// h_scaler's clip: the corner moves to the clip edge and the uv moves with it (the uv clamp below then sees the moved uv)
		code += "\tfloat cx = clamp(x0 + q.x * w, bcl.x, bcl.z);\n\tfloat cy = clamp(y0 + q.y * h, bcl.y, bcl.w);\n";
		code += "\tsx = snap16(cx);\n\tsy = snap16(cy);\n";
		code += "\tint ori = int(bbp.y + 0.5);\n\tuv = vec2((cx - x0) / w, (cy - y0) / h);\n\tif ((ori & 1) != 0) { uv.x = 1.0 - uv.x; }\n\tif ((ori & 2) != 0) { uv.y = 1.0 - uv.y; }\n";
		code += "\tzq = max(z - rad * 0.5, 0.0);\n";
	} else if (kind == FS2_SINK_BB_ROTATED) {
		// g3_draw_rotated_bitmap: the four corners rotated by the roll angle (pB.x), sized by the bitmap's aspect (the shorter side = rad), scaled by
		// Matrix_scale in eye space, all at the centre's z. The CPU draws it with a constant sw, so the uvs need no perspective divide.
		code += "\tfloat rad = pA.w;\n\tfloat width; float height;\n";
		code += "\tif (bbp.z < bbp.w) { width = rad; height = width * bbp.w / bbp.z; } else if (bbp.z > bbp.w) { height = rad; width = height * bbp.z / bbp.w; } else { width = rad; height = rad; }\n";
		code += "\tfloat sa = sin(pB.x);\n\tfloat ca = cos(pB.x);\n\tfloat su = 2.0 * q.x - 1.0;\n\tfloat tv = 2.0 * q.y - 1.0;\n";
		code += "\tfloat ex = (su * width * ca + tv * height * sa) * sc.x + pA.x;\n\tfloat ey = (su * width * sa - tv * height * ca) * sc.y + pA.y;\n\tfloat z = pA.z;\n";
		code += "\tsx = snap16((cwh.x + ex * cwh.x / z) * 0.5);\n\tsy = snap16((cwh.y - ey * cwh.y / z) * 0.5);\n\tzq = z;\n";
	} else if (kind == FS2_SINK_BB_QUAD) {
		// beam_render: corners = the end centre +- the facing vector (beam_calc_facing_pts: cross(fvec, eye - pos), worked out in the unscaled view: eye space is divided by Matrix_scale (sc.xy, max_len = z), the eye is the origin) times the half width, scaled back;
		// q.x picks the end, q.y the side. A corner nearer than the 0.1 near plane slides along its long edge to the plane (u moves with it); both ends nearer = nothing. Perspective-correct uvs with the game's per-vertex sw.
		code += "\tvec3 S = vec3(sc.x, sc.y, bbp.x);\n\tvec3 fv = normalize((pB.xyz - pA.xyz) / S);\n\tfloat sd = (2.0 * q.y - 1.0);\n";
		code += "\tvec3 eA = pA.xyz + normalize(cross(fv, -pA.xyz / S)) * S * (sd * pA.w);\n\tvec3 eB = pB.xyz + normalize(cross(fv, -pB.xyz / S)) * S * (sd * pB.w);\n";
		code += "\tvec3 ev = q.x < 0.5 ? eA : eB;\n\tvec3 eo = q.x < 0.5 ? eB : eA;\n\tuv.x = q.x;\n";
		code += "\tif (ev.z < 0.1) {\n\t\tif (eo.z < 0.1) { skip = true; } else { float tc = (0.1 - ev.z) / (eo.z - ev.z); ev = mix(ev, eo, tc); uv.x = q.x < 0.5 ? tc : 1.0 - tc; }\n\t}\n\tfloat z = max(ev.z, 0.1);\n";
		code += "\tsx = snap16((cwh.x + ev.x * cwh.x / z) * 0.5);\n\tsy = snap16((cwh.y - ev.y * cwh.y / z) * 0.5);\n\tzq = z;\n\tsw = min(1.0 / z, 1.0);\n";
	} else if (kind == FS2_SINK_BB_QUAD4) {
		// nebl_render_section: the four eye-space corners ride in the transform (corner i = M[i].xyz), tint / zbias / layer in the custom data (x, y, z; the tint is grey, r = g = b).
		// The polygon is clipped to the 0.1 near plane here: slot k of the five slot fan is the k-th corner of the clipped polygon (a kept corner, or the edge's crossing point; slots past the end repeat the last one), uvs lerp with the crossing.
		code += "\ttintv = vec4(INSTANCE_CUSTOM.x, INSTANCE_CUSTOM.x, INSTANCE_CUSTOM.x, 1.0);\n\tzbias = INSTANCE_CUSTOM.y;\n\tlyr = INSTANCE_CUSTOM.z;\n";
		code += "\tvec3 C[4] = vec3[4](M[0].xyz, M[1].xyz, M[2].xyz, M[3].xyz);\n\tvec2 U[4] = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));\n";
		code += "\tint cnt = 0;\n\tfor (int i = 0; i < 4; i++) {\n\t\tint j = (i + 1) % 4;\n\t\tbool ia = C[i].z >= 0.1;\n\t\tbool ib = C[j].z >= 0.1;\n\t\tif (ia) { cnt++; }\n\t\tif (ia != ib) { cnt++; }\n\t}\n";
		code += "\tint kk = min(int(VERTEX.x + 0.5), max(cnt - 1, 0));\n\tvec3 ev = C[0];\n\tvec2 eu = U[0];\n\tint n = 0;\n\tif (cnt == 0) { skip = true; }\n";
		code += "\tfor (int i = 0; i < 4; i++) {\n\t\tint j = (i + 1) % 4;\n\t\tbool ia = C[i].z >= 0.1;\n\t\tbool ib = C[j].z >= 0.1;\n\t\tif (ia) { if (n == kk) { ev = C[i]; eu = U[i]; } n++; }\n";
		code += "\t\tif (ia != ib) { if (n == kk) { float t = (0.1 - C[i].z) / (C[j].z - C[i].z); ev = mix(C[i], C[j], t); eu = mix(U[i], U[j], t); } n++; }\n\t}\n";
		code += "\tuv = eu;\n\tfloat z = max(ev.z, 0.1);\n\tsx = snap16((cwh.x + ev.x * cwh.x / z) * 0.5);\n\tsy = snap16((cwh.y - ev.y * cwh.y / z) * 0.5);\n\tzq = z;\n\tsw = min(1.0 / z, 1.0);\n";
	} else if (kind == FS2_SINK_BB_TRI3) {
		// R3 row 6: aff = the warp cone and the thruster glow polys have no CORRECT flag, so their uvs are affine in screen space (sw = 1), as the poly path draws them
		// trail_render, the tail triangle: as QUAD4 with three corners (corner i = M[i].xyz) and their own uvs (u0 v0 u1 = M[3].xyz, v1 u2 v2 = COLOR.xyz); tint r in custom.x, g in custom.w, b in colour.w (trails: grey), zbias, layer in the custom data. Clipped to the 0.1 near plane into a fan of up to four slots.
		code += "\ttintv = vec4(INSTANCE_CUSTOM.x, INSTANCE_CUSTOM.w, COLOR.w, 1.0);\n\tzbias = INSTANCE_CUSTOM.y;\n\tlyr = INSTANCE_CUSTOM.z;\n";
		code += "\tvec3 C[3] = vec3[3](M[0].xyz, M[1].xyz, M[2].xyz);\n\tvec2 U[3] = vec2[3](vec2(M[3][0], M[3][1]), vec2(M[3][2], COLOR.x), vec2(COLOR.y, COLOR.z));\n";
		code += "\tint cnt = 0;\n\tfor (int i = 0; i < 3; i++) {\n\t\tint j = (i + 1) % 3;\n\t\tbool ia = C[i].z >= 0.1;\n\t\tbool ib = C[j].z >= 0.1;\n\t\tif (ia) { cnt++; }\n\t\tif (ia != ib) { cnt++; }\n\t}\n";
		code += "\tint kk = min(int(VERTEX.x + 0.5), max(cnt - 1, 0));\n\tvec3 ev = C[0];\n\tvec2 eu = U[0];\n\tint n = 0;\n\tif (cnt == 0) { skip = true; }\n";
		code += "\tfor (int i = 0; i < 3; i++) {\n\t\tint j = (i + 1) % 3;\n\t\tbool ia = C[i].z >= 0.1;\n\t\tbool ib = C[j].z >= 0.1;\n\t\tif (ia) { if (n == kk) { ev = C[i]; eu = U[i]; } n++; }\n";
		code += "\t\tif (ia != ib) { if (n == kk) { float t = (0.1 - C[i].z) / (C[j].z - C[i].z); ev = mix(C[i], C[j], t); eu = mix(U[i], U[j], t); } n++; }\n\t}\n";
		code += "\tuv = eu;\n\tfloat z = max(ev.z, 0.1);\n\tsx = snap16((cwh.x + ev.x * cwh.x / z) * 0.5);\n\tsy = snap16((cwh.y - ev.y * cwh.y / z) * 0.5);\n\tzq = z;\n\tsw = min(1.0 / z, 1.0);\n";
		if (aff) { code += "\tsw = 1.0;\n"; }
		if (rep) { code += "\tsw = 1.0 / z;\n"; }	// true perspective (1/z, no clamp at 1): the tunnel polys sit nearer than 1 unit and the CPU frames match this, not the clamped sw
	} else {
		code += "\tfloat sw0 = min(1.0 / pA.z, 1.0);\n\tfloat sw1 = min(1.0 / pB.z, 1.0);\n";
		code += "\tfloat hx = (cwh.x + pA.x * cwh.x / pA.z) * 0.5;\n\tfloat hy = (cwh.y - pA.y * cwh.y / pA.z) * 0.5;\n";
		code += "\tfloat tx = (cwh.x + pB.x * cwh.x / pB.z) * 0.5;\n\tfloat ty = (cwh.y - pB.y * cwh.y / pB.z) * 0.5;\n";
		code += "\tfloat hr = pA.w * sc.x * cfg.x * sw0;\n\tfloat tr = pB.w * sc.x * cfg.x * sw1;\n";
		code += "\tfloat len2 = sqrt((tx - hx) * (tx - hx) + (ty - hy) * (ty - hy));\n";
		code += "\tif (bbp.x > 1.0 && len2 > bbp.x) {\n\t\tfloat ratio = bbp.x / len2;\n\t\ttx = hx + (tx - hx) * ratio;\n\t\tty = hy + (ty - hy) * ratio;\n\t\ttr = hr + (tr - hr) * ratio;\n";
		code += "\t\tlen2 = sqrt((tx - hx) * (tx - hx) + (ty - hy) * (ty - hy));\n\t}\n";
		code += "\tfloat max_r = max(max(hr, tr), 1.0);\n\tfloat h1; float h2; float a;\n";
		code += "\tif (len2 < max_r) {\n\t\th1 = min(hr + (max_r - len2), max_r);\n\t\th2 = min(tr + (max_r - len2), max_r);\n\t\tlen2 = max_r;\n";
		code += "\t\ta = abs(tx - hx) > 0.01 ? atan(ty - hy, tx - hx) : 0.0;\n\t} else {\n\t\th1 = hr;\n\t\th2 = tr;\n";
		code += "\t\ta = (tx == hx && ty == hy) ? 0.0 : atan(ty - hy, tx - hx);\n\t}\n";
		code += "\th1 = max(h1, 1.0);\n\th2 = max(h2, 1.0);\n\tfloat ca = cos(a);\n\tfloat sa = sin(a);\n";
		code += "\tfloat mx = (tx + hx) * 0.5;\n\tfloat my = (ty + hy) * 0.5;\n\tfloat along = (q.x - 0.5) * len2;\n\tfloat acr = (q.y - 0.5) * (q.x < 0.5 ? h1 : h2);\n";
		code += "\tsx = snap16(along * ca + acr * sa + mx);\n\tsy = snap16(along * sa - acr * ca + my);\n";
		code += "\tzq = q.x < 0.5 ? pA.z : pB.z;\n\tsw = q.x < 0.5 ? sw0 : sw1;\n";
	}
	code += "\tfloat d = cfg.z + min(zq / 30000.0, 0.98) * 1000.0 - zbias * 0.05;\n";
	code += "\tif (skip) { POSITION = vec4(0.0); } else { POSITION = PROJECTION_MATRIX * vec4(org.x + sx - cfg.x, org.y + cfg.y - sy, -d, 1.0); }\n";
	code += "\tvec2 u2 = uv;\n";
	code += (kind != FS2_SINK_BB_RIBBON && kind != FS2_SINK_BB_QUAD && kind != FS2_SINK_BB_QUAD4 && kind != FS2_SINK_BB_TRI3) ?"\tpc = vec3(u2, 1.0);\n}\n" : "\tpc = vec3(u2 * sw, sw);\n}\n";
	code += "void fragment() {\n";
	if (crect) { code += "\tif (FRAGCOORD.x < crc.x || FRAGCOORD.y < crc.y || FRAGCOORD.x >= crc.z || FRAGCOORD.y >= crc.w) { discard; }\n"; }
	code += arr ? "\tvec4 t = texture(tex, vec3(pc.xy / pc.z, lyr));\n" : "\tvec4 t = texture(tex, pc.xy / pc.z);\n";
	if (a8) { code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n"; }
	code += "\tvec4 c = t;\n\tc.rgb *= tintv.rgb;\n";
	if (blend == FS2_SINK_BLEND_OPAQUE) { code += "\tc.a = 1.0;\n"; } else if (blend == FS2_SINK_BLEND_ADD) { code += "\tc.a = 0.0;\n"; }
	code += "\tALBEDO = clamp(c.rgb, 0.0, 1.0);\n\tALPHA = c.a;\n}\n";
	RID sh = rs->shader_create();
	rs->shader_set_code(sh, code);
	bb_shaders[skey] = sh;
	return sh;
}

int GodotSink::s_billboard_draw(void *u, const fs2_sink_billboard *b)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	const bool stars = b != nullptr && b->kind == FS2_SINK_BB_STARS;	// R3 row 8: no texture
	if (b == nullptr || !s->in_frame || !s->in_run || !s->have_view || (!stars && s->textures.find(b->texture) == s->textures.end())) { return 0; }
	if (stars && (b->star_pos == nullptr || b->star_n <= 0 || b->star_n > kBbBucketCap[kBbBuckets - 1] || b->star_stride < 3)) { return 0; }
	if (s->clip_empty) { return 1; }	// everything is clipped away: deliberately drawn nothing
	s->batcher.flush();
	BbDraw d;
	d.b = *b;
	if (stars) {
		d.b.star_pos = nullptr;
		d.stars.resize((size_t)b->star_n * 3);
		for (int i = 0; i < b->star_n; i++) { std::memcpy(&d.stars[(size_t)i * 3], b->star_pos + (size_t)i * (size_t)b->star_stride, 3 * sizeof(float)); }
	}
	d.epoch = s->run_epoch;
	d.crect = s->clipped;
	if (d.crect) { std::memcpy(d.cr, s->clip, sizeof(d.cr)); }
	d.cw = s->view.canvas_w; d.ch = s->view.canvas_h; d.ox = s->view.origin_x; d.oy = s->view.origin_y;
	d.scale[0] = s->view.scale[0]; d.scale[1] = s->view.scale[1];
	if (!stars) {
		const Tex &tx = s->textures.find(b->texture)->second;
		d.arr = tx.arr; d.layer = tx.layer;
		if (d.arr < 0) { fs2perf::count(fs2perf::C_BB_NOARR); }
	}
	s->bb_draws.push_back(d);
	Batch bt;
	bt.key.texture = -(kBbBase + (int32_t)s->bb_draws.size());
	bt.key.depth = 1; bt.key.zmode = 2; bt.key.epoch = d.epoch;
	s->run_batches.push_back(bt);
	return 1;
}

// Additive and depth-read-only: no depth write, ONE:ONE blend, so the order among such draws does not change the picture.
bool GodotSink::bb_additive(const BbDraw &d) const
{
	return d.b.kind != FS2_SINK_BB_STARS && d.b.blend == FS2_SINK_BLEND_ADD && (d.b.flags & (FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_ZWRITE)) == FS2_SINK_DRAWF_ZREAD;
}

bool GodotSink::bb_same_run(const BbDraw &a, const BbDraw &b) const
{
	const unsigned zm = FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_ZWRITE;
	if (a.b.kind == FS2_SINK_BB_STARS || b.b.kind == FS2_SINK_BB_STARS) { return false; }	// one call = one run
	return a.b.kind == b.b.kind && (a.b.kind != FS2_SINK_BB_TRI3 || (a.b.orient & 14) == (b.b.orient & 14)) && a.b.blend == b.b.blend && (a.arr >= 0 ? a.arr == b.arr : (b.arr < 0 && a.b.texture == b.b.texture)) && (a.b.flags & zm) == (b.b.flags & zm)
		&& a.b.bw == b.b.bw && a.b.bh == b.b.bh && std::memcmp(a.b.clip, b.b.clip, sizeof(a.b.clip)) == 0
		&& a.epoch == b.epoch && a.cw == b.cw && a.ch == b.ch && a.ox == b.ox && a.oy == b.oy
		&& a.scale[0] == b.scale[0] && a.scale[1] == b.scale[1]
		&& a.crect == b.crect && (!a.crect || std::memcmp(a.cr, b.cr, sizeof(a.cr)) == 0);
}

// One run of billboard draws of one state: one pooled multimesh (bucket sized, visible_instances = the run), one material, one instance. The per-draw
// values ride in each instance's 3x4 transform and custom data (see billboard_shader); the draw order inside the run is the instance order.
void GodotSink::add_bb_run(Layer &l, const std::vector<size_t> &idx, int order)
{
	fs2perf::Zone _pz(fs2perf::Z_BB_RUN);
	const BbDraw &d = bb_draws[idx[0]];
	const bool st = d.b.kind == FS2_SINK_BB_STARS;	// R3 row 8: one instance per star, no texture
	auto ti = textures.find(d.b.texture);
	if ((!st && ti == textures.end()) || !bb_quad.is_valid() || !bb_fan5.is_valid()) { return; }
	const int n = st ? d.b.star_n : (int)idx.size();
	fs2perf::count(fs2perf::C_LAYER_BATCHES);
	stat_bb_draws += (unsigned long long)n;
	fs2perf::count(fs2perf::C_BB_RUNS);
	fs2perf::count(fs2perf::C_BB_DRAWS, (uint64_t)n);
	RenderingServer *rs = RenderingServer::get_singleton();
	int bucket = 0;
	while (bucket < kBbBuckets - 1 && kBbBucketCap[bucket] < n) { bucket++; }
	const int cap = kBbBucketCap[bucket];
	const int epoch = d.epoch < MAX_EPOCH ? d.epoch : MAX_EPOCH;
	const float origin = 29000.0f - 0.25f * (float)order;
	const float dbase = 2.0f + (float)(MAX_EPOCH - epoch) * 1100.0f;
	const bool zr = (d.b.flags & FS2_SINK_DRAWF_ZREAD) != 0, zw = (d.b.flags & FS2_SINK_DRAWF_ZWRITE) != 0;
	const int zmode = zr ? (zw ? 2 : 1) : (zw ? 3 : 0);
	const bool use_arr = !st && d.arr >= 0 && d.arr < (int)arr_chunks.size();
	const bool a8 = !st && ti->second.format == FS2_SINK_TEX_A8;
	const bool lin = !st && (ti->second.flags & FS2_SINK_TEXF_LINEAR) != 0;
	// per-draw data: transform rows r0..r2 (flat k = column * 3 + row, buffer index row * 4 + column), then the tint as custom data
	const bool t3 = d.b.kind == FS2_SINK_BB_TRI3;	// TRI3 also fills the instance colour (between the transform and the custom data)
	const int kStride = t3 ? 20 : 16;
	PackedFloat32Array buf;
	buf.resize((int64_t)cap * kStride);
	float *w = buf.ptrw();
	std::memset(w, 0, sizeof(float) * (size_t)cap * kStride);
	for (int i = 0; i < n; i++) {
		if (st) {	// the star unit vector and its index; the view, tail and colour rules ride in the material
			float *e = w + (size_t)i * kStride;
			const float sv[4] = { d.stars[(size_t)i * 3], d.stars[(size_t)i * 3 + 1], d.stars[(size_t)i * 3 + 2], (float)i };
			for (int k = 0; k < 4; k++) { e[(k % 3) * 4 + (k / 3)] = sv[k]; }
			continue;
		}
		const fs2_sink_billboard &b = bb_draws[idx[(size_t)i]].b;
		float *e = w + (size_t)i * kStride;
		const float v[12] = { b.p0[0], b.p0[1], b.p0[2], b.rad, (b.kind == FS2_SINK_BB_ROTATED ? b.angle : b.p1[0]), b.p1[1], b.p1[2], b.rad_tail, b.max_len, (float)b.orient, (float)b.zbias, (float)bb_draws[idx[(size_t)i]].layer };
		if (b.kind == FS2_SINK_BB_QUAD4) {	// corners fill the transform; tint (grey), zbias, layer ride in the custom data
			for (int k = 0; k < 12; k++) { e[(k % 3) * 4 + (k / 3)] = b.quad[k]; }
			e[12] = b.tint[0] / 255.0f; e[13] = (float)b.zbias; e[14] = (float)bb_draws[idx[(size_t)i]].layer; e[15] = 1.0f;
			continue;
		}
		if (b.kind == FS2_SINK_BB_TRI3) {	// corners in k 0..8, the first three uv floats in k 9..11, the rest in the instance colour; tint (grey), zbias, layer in the custom data
			float t[12] = { b.quad[0], b.quad[1], b.quad[2], b.quad[3], b.quad[4], b.quad[5], b.quad[6], b.quad[7], b.quad[8], b.tri_uv[0], b.tri_uv[1], b.tri_uv[2] };
			for (int k = 0; k < 12; k++) { e[(k % 3) * 4 + (k / 3)] = t[k]; }
			e[12] = b.tri_uv[3]; e[13] = b.tri_uv[4]; e[14] = b.tri_uv[5]; e[15] = b.tint[2] / 255.0f;
			e[16] = b.tint[0] / 255.0f; e[17] = (float)b.zbias; e[18] = (float)bb_draws[idx[(size_t)i]].layer; e[19] = b.tint[1] / 255.0f;
			continue;
		}
		for (int k = 0; k < 12; k++) { e[(k % 3) * 4 + (k / 3)] = v[k]; }
		e[12] = b.tint[0] / 255.0f; e[13] = b.tint[1] / 255.0f; e[14] = b.tint[2] / 255.0f; e[15] = b.tint[3] / 255.0f;
	}
	RID mm;
	const bool q4 = d.b.kind == FS2_SINK_BB_QUAD4 || t3;
	const int pool = t3 ? 2 * kBbBuckets + bucket : q4 ? kBbBuckets + bucket : bucket;
	if (!bb_mm_pool[pool].empty()) { mm = bb_mm_pool[pool].back(); bb_mm_pool[pool].pop_back(); }
	else {
		mm = rs->multimesh_create();
		rs->multimesh_set_mesh(mm, q4 ? bb_fan5 : bb_quad);
		rs->multimesh_allocate_data(mm, cap, RenderingServer::MULTIMESH_TRANSFORM_3D, t3, true);
	}
	rs->multimesh_set_buffer(mm, buf);
	rs->multimesh_set_visible_instances(mm, n);
	l.bb_multimeshes.push_back(std::make_pair(pool, mm));
	// everything the material holds (shader and parameters) is a function of this key; the per-draw data is in the multimesh, the sorting offset on the instance
	const bool rep = !st && (ti->second.flags & FS2_SINK_TEXF_REPEAT) != 0 && d.b.kind == FS2_SINK_BB_TRI3 && (d.b.orient & 2) != 0, aff = d.b.kind == FS2_SINK_BB_TRI3 && (d.b.orient & 4) != 0, nc = d.b.kind == FS2_SINK_BB_TRI3 && (d.b.orient & 8) != 0;
	BbMatKey mk = {};
	RID mat;
	bool have_mat = false;
	if (!st) {
		auto fbits = [](float f) { int32_t i; std::memcpy(&i, &f, sizeof(i)); return (int64_t)i; };
		mk = { d.b.kind, d.b.blend, zmode, a8, lin, d.crect, use_arr, rep, aff, nc, (int64_t)(use_arr ? arr_chunks[(size_t)d.arr].rid : ti->second.rid).get_id(), d.b.bw, d.b.bh, fbits(d.scale[0]), fbits(d.scale[1]),
			d.b.clip[0], d.b.clip[1], d.b.clip[2], d.b.clip[3], d.cw, d.ch, d.ox, d.oy, epoch, d.crect ? d.cr[0] : 0, d.crect ? d.cr[1] : 0, d.crect ? d.cr[2] : 0, d.crect ? d.cr[3] : 0, width, height };
		auto mit = bb_mats.find(mk);
		if (mit != bb_mats.end()) { mat = mit->second; have_mat = true; }
	}
	if (have_mat) { /* the cached material is as it was */ }
	else if (!mesh_mat_pool.empty()) { mat = mesh_mat_pool.back(); mesh_mat_pool.pop_back(); } else { mat = rs->material_create(); }
	if (st) {
		rs->material_set_shader(mat, billboard_shader(FS2_SINK_BB_STARS, FS2_SINK_BLEND_ALPHA, 0, false, false, false, false));
		l.mesh_materials.push_back(mat);
		const float *v = d.b.star_view, *p = d.b.star_prev, *r = d.b.star_par;
		rs->material_set_param(mat, "vr", Vector4(v[0], v[1], v[2], 0.0f)); rs->material_set_param(mat, "vu", Vector4(v[3], v[4], v[5], 0.0f)); rs->material_set_param(mat, "vf", Vector4(v[6], v[7], v[8], 0.0f));
		rs->material_set_param(mat, "pr", Vector4(p[0], p[1], p[2], 0.0f)); rs->material_set_param(mat, "pu", Vector4(p[3], p[4], p[5], 0.0f)); rs->material_set_param(mat, "pf", Vector4(p[6], p[7], p[8], 0.0f));
		rs->material_set_param(mat, "sp", Vector4(r[0], r[1], r[2], r[3]));
		rs->material_set_param(mat, "sfl", Vector4((float)d.b.star_flags, 0.0f, 0.0f, 0.0f));
		const float scw = d.cw > 0 ? (float)d.cw : (float)width, sch = d.ch > 0 ? (float)d.ch : (float)height;
		rs->material_set_param(mat, "cfg", Vector4(0.5f * scw, 0.5f * sch, dbase, 0.0f));
		rs->material_set_param(mat, "org", Vector4((float)d.ox + 0.5f * scw - 0.5f * (float)width + FS2_SINK_D3D_PIXEL_CENTRE, -((float)d.oy + 0.5f * sch - 0.5f * (float)height + FS2_SINK_D3D_PIXEL_CENTRE), 0.0f, 0.0f));
		RID sinst;
		if (kInstancePool && !spare_bb_instances.empty()) {
			sinst = spare_bb_instances.back();
			spare_bb_instances.pop_back();
			rs->instance_set_base(sinst, mm);
			rs->instance_set_scenario(sinst, l.scenario);
		} else {
			sinst = rs->instance_create2(mm, l.scenario);
			rs->instance_set_transform(sinst, Transform3D());
			rs->instance_geometry_set_cast_shadows_setting(sinst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
			rs->instance_set_custom_aabb(sinst, AABB(Vector3(-1.0e6f, -1.0e6f, -1.0e6f), Vector3(2.0e6f, 2.0e6f, 2.0e6f)));
		}
		rs->instance_set_pivot_data(sinst, -origin, false);
		rs->instance_geometry_set_material_override(sinst, mat);
		l.bb_instances.push_back(sinst);
		return;
	}
	if (!have_mat) {
		rs->material_set_shader(mat, billboard_shader(d.b.kind, d.b.blend, zmode, a8, lin, d.crect, use_arr, rep, aff, nc));
		rs->material_set_param(mat, "tex", use_arr ? arr_chunks[(size_t)d.arr].rid : ti->second.rid);
		rs->material_set_param(mat, "bwh", Vector2((float)d.b.bw, (float)d.b.bh));
		rs->material_set_param(mat, "sc", Vector4(d.scale[0], d.scale[1], 0.0f, 0.0f));
		rs->material_set_param(mat, "bcl", Vector4((float)d.b.clip[0], (float)d.b.clip[1], (float)d.b.clip[2], (float)d.b.clip[3]));
		const float cw = d.cw > 0 ? (float)d.cw : (float)width, ch = d.ch > 0 ? (float)d.ch : (float)height;
		rs->material_set_param(mat, "cfg", Vector4(0.5f * cw, 0.5f * ch, dbase, 0.0f));
		rs->material_set_param(mat, "org", Vector4((float)d.ox + 0.5f * cw - 0.5f * (float)width + FS2_SINK_D3D_PIXEL_CENTRE, -((float)d.oy + 0.5f * ch - 0.5f * (float)height + FS2_SINK_D3D_PIXEL_CENTRE), 0.0f, 0.0f));
		if (d.crect) { rs->material_set_param(mat, "crc", Vector4((float)d.cr[0], (float)d.cr[1], (float)(d.cr[0] + d.cr[2]), (float)(d.cr[1] + d.cr[3]))); }
		if (bb_mats.size() < kBbMatCap) { bb_mats[mk] = mat; } else { l.mesh_materials.push_back(mat); }
	}
	RID inst;
	if (kInstancePool && !spare_bb_instances.empty()) {
		inst = spare_bb_instances.back();
		spare_bb_instances.pop_back();
		rs->instance_set_base(inst, mm);
		rs->instance_set_scenario(inst, l.scenario);
	} else {
		inst = rs->instance_create2(mm, l.scenario);
		rs->instance_set_transform(inst, Transform3D());
		rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
		rs->instance_set_custom_aabb(inst, AABB(Vector3(-1.0e6f, -1.0e6f, -1.0e6f), Vector3(2.0e6f, 2.0e6f, 2.0e6f)));
	}
	// Godot sorts by depth + this offset; the instance is at the origin, so the offset is the old per-instance origin distance
	rs->instance_set_pivot_data(inst, -origin, false);
	rs->instance_geometry_set_material_override(inst, mat);
	l.bb_instances.push_back(inst);
}

} // namespace fs2sink
