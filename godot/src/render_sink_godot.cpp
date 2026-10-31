#include "render_sink_godot.h"
#include "perf_prof.h"

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
	sink.draw_tris = &GodotSink::s_draw_tris;
	sink.capture_to_texture = &GodotSink::s_capture;
	sink.read_region = &GodotSink::s_read_region;
	sink.set_gamma = &GodotSink::s_set_gamma;
	sink.depth_begin = &GodotSink::s_depth_begin;
	sink.depth_end = &GodotSink::s_depth_end;
	sink.depth_clear = &GodotSink::s_depth_clear;
	sink.set_view = &GodotSink::s_set_view;
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
		for (RID &r : spare_instances) {
			rs->free_rid(r);
		}
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
	spare_instances.clear();
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
	if (!s->in_frame || id == 0 || w <= 0 || h <= 0 || pixels == nullptr) {
		return;
	}
	fs2perf::Zone _pz(fs2perf::Z_TEX_CREATE);
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
	s->textures[id] = t;
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
	rs->free_rid(l.viewport);
	rs->free_rid(l.camera);
	rs->free_rid(l.scenario);
}

void GodotSink::drop_materials_of(const RID &tex)
{
	RenderingServer *rs = RenderingServer::get_singleton();
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
		rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(0.0f, 0.0f, -origin)));
	} else {
		inst = rs->instance_create2(mesh, l.scenario);
		rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(0.0f, 0.0f, -origin)));
		rs->instance_set_pivot_data(inst, 0.0f, false);
		rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
	}
	l.instances.push_back(inst);
}

} // namespace fs2sink
