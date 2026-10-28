#include "render_sink_godot.h"

#include <cstring>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
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
	batcher.set_emitter(this);
	return true;
}

void GodotSink::shutdown()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr) {
		release_layers(pending_layers);
		release_layers(shown_layers);
		for (Layer &l : spare_layers) {
			free_layer(l);
		}
		for (auto &m : spatial_materials) {
			rs->free_rid(m.second);
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
	run_batches.clear();
	in_run = false;
	spare_items.clear();
	spare_meshes.clear();
	pending_meshes.clear();
	shown_meshes.clear();
	textures.clear();
	dying.clear();
	vp = nullptr;
	in_frame = false;
}

RID GodotSink::new_item(const RID &under)
{
	RenderingServer *rs = RenderingServer::get_singleton();
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
	if (s->vp == nullptr || s->frames_left > 1) {
		return 0;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	if (w != s->width || h != s->height) {
		s->width = w;
		s->height = h;
		s->vp->set_size(Vector2i(w, h));
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
	if (s->in_run) {
		s->end_run();
	}
	s->batcher.flush();
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
}

void GodotSink::s_clear(void *u, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame) {
		return;
	}
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
		s->set_clip_state(x, y, w, h);
	}
}

void GodotSink::retire(const RID &r)
{
	Dying d;
	d.rid = r;
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
			rs->free_rid(dying[i].rid);
		} else {
			dying[keep++] = dying[i];
		}
	}
	dying.resize(keep);
}

void GodotSink::s_texture_create(void *u, int32_t id, int w, int h, int format, uint32_t flags, const uint8_t *pixels)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame || id == 0 || w <= 0 || h <= 0 || pixels == nullptr) {
		return;
	}
	s->batcher.flush(); // pending draws resolve their texture id at emit: they must see the old one
	const bool a8 = (format == FS2_SINK_TEX_A8);
	PackedByteArray bytes;
	const int64_t n = (int64_t)w * h * (a8 ? 1 : 4);
	bytes.resize(n);
	std::memcpy(bytes.ptrw(), pixels, (size_t)n);
	Ref<Image> img = Image::create_from_data(w, h, false, a8 ? Image::FORMAT_R8 : Image::FORMAT_RGBA8, bytes);
	auto it = s->textures.find(id);
	if (it != s->textures.end()) {
		s->retire(it->second.rid);
	}
	Tex t;
	t.rid = RenderingServer::get_singleton()->texture_2d_create(img);
	t.w = w; t.h = h; t.format = format; t.flags = flags;
	s->textures[id] = t;
}

void GodotSink::s_texture_free(void *u, int32_t id)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	s->batcher.flush();
	auto it = s->textures.find(id);
	if (it != s->textures.end()) {
		s->retire(it->second.rid);
		s->textures.erase(it);
	}
}

void GodotSink::s_texture_free_all(void *u)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	s->batcher.flush();
	for (auto &t : s->textures) {
		s->retire(t.second.rid);
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
		const char *mode = blend == FS2_SINK_BLEND_ADD ? "blend_add" : (blend == FS2_SINK_BLEND_OPAQUE ? "blend_disabled" : "blend_mix");
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
	if (b.key.depth != 0) {
		run_batches.push_back(b); // placed at depth_end
		return;
	}
	emit_canvas(b);
}

// A batch becomes one canvas item with one array draw (affine) or one mesh draw (perspective: the mesh carries CUSTOM0).
void GodotSink::emit_canvas(const Batch &b)
{
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
	int tw = 0, th = 0;
	if (d->texture != 0) {
		auto it = s->textures.find(d->texture);
		if (it == s->textures.end()) {
			return; // the host drew with a texture it never made: nothing to show
		}
		tw = it->second.w;
		th = it->second.h;
	}
	s->batcher.add(*d, v, nv, idx, ni, tw, th);
}

// The capture is the last frame the viewport has rendered (the frame on show), as read_region does.
int GodotSink::s_capture(void *u, int32_t id)
{
	GodotSink *s = static_cast<GodotSink *>(u);
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
		s->retire(it->second.rid);
	}
	Tex t;
	t.rid = RenderingServer::get_singleton()->texture_2d_create(img);
	t.w = img->get_width(); t.h = img->get_height(); t.format = FS2_SINK_TEX_RGBA8; t.flags = 0;
	s->textures[id] = t;
	return 1;
}

int GodotSink::s_read_region(void *u, int x, int y, int w, int h, uint8_t *out)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	s->batcher.flush();
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

// Depth cleared in a rectangle: everything drawn after it is in front of everything before it. Approximation: the
// whole layer, not only the rectangle.
void GodotSink::s_depth_clear(void *u, int, int, int, int)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame || !s->in_run) {
		return;
	}
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
	c.z.clear();
	return c;
}

void GodotSink::end_run()
{
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
			rs->free_rid(i);
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
// texture and vertex colour are the game's sRGB values, turned linear for the 3D pass (blending there is linear).
RID GodotSink::spatial_material(int blend, int zmode, bool a8, uint32_t texflags, const RID &tex)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const bool lin = (texflags & FS2_SINK_TEXF_LINEAR) != 0, rpt = (texflags & FS2_SINK_TEXF_REPEAT) != 0;
	const uint32_t skey = (uint32_t)(blend & 3) | ((uint32_t)(zmode & 3) << 2) | (a8 ? 16u : 0u) | (lin ? 32u : 0u) | (rpt ? 64u : 0u);
	const std::pair<uint32_t, int64_t> mkey(skey, (int64_t)tex.get_id());
	auto mit = spatial_materials.find(mkey);
	if (mit != spatial_materials.end()) {
		return mit->second;
	}
	RID sh;
	auto sit = spatial_shaders.find(skey);
	if (sit != spatial_shaders.end()) {
		sh = sit->second;
	} else {
		String code = "shader_type spatial;\nrender_mode unshaded, cull_disabled, shadows_disabled, fog_disabled, ";
		code += blend == FS2_SINK_BLEND_ADD ? "blend_add, " : "blend_mix, ";
		code += (zmode == 2 || zmode == 3) ? "depth_draw_always" : "depth_draw_never";
		if (zmode == 0 || zmode == 3) {
			code += ", depth_test_disabled";
		}
		code += ";\nuniform sampler2D tex : ";
		code += lin ? "filter_linear, " : "filter_nearest, ";
		code += rpt ? "repeat_enable;\n" : "repeat_disable;\n";
		code += "varying vec4 vcol;\nvarying vec3 pc;\n";
		code += "vec3 s2l(vec3 c) { return mix(c * 0.0773993808, pow((c + 0.055) * 0.9478672986, vec3(2.4)), step(0.04045, c)); }\n";
		code += "void vertex() { vcol = COLOR; pc = CUSTOM0.xyz; }\nvoid fragment() {\n\tvec4 t = texture(tex, pc.xy / pc.z);\n";
		if (a8) {
			code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n";
		}
		code += "\tvec4 c = vcol * t;\n";
		if (blend == FS2_SINK_BLEND_OPAQUE) {
			code += "\tc.a = 1.0;\n";
		}
		code += "\tALBEDO = s2l(clamp(c.rgb, 0.0, 1.0));\n\tALPHA = c.a;\n}\n";
		sh = rs->shader_create();
		rs->shader_set_code(sh, code);
		spatial_shaders[skey] = sh;
	}
	RID m = rs->material_create();
	rs->material_set_shader(m, sh);
	rs->material_set_param(m, "tex", tex);
	spatial_materials[mkey] = m;
	return m;
}

// One mesh instance per batch. The camera looks down -z from the origin; a vertex at screen (x, y) is at
// (x - w/2, h/2 - y). Its distance: later clear epochs nearer, then the game's z, then the z bias. Godot sorts the
// (all transparent) instances back to front by their origin, so each instance's origin carries the call order.
void GodotSink::add_to_layer(Layer &l, const Batch &b, int order)
{
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
		if (!white_tex.is_valid()) {
			PackedByteArray px;
			px.resize(4);
			px.fill(255);
			white_tex = rs->texture_2d_create(Image::create_from_data(1, 1, false, Image::FORMAT_RGBA8, px));
		}
		tex = white_tex;
	}
	const int nv = b.nverts();
	const int ni = (int)b.idx.size();
	if (nv <= 0 || ni < 3 || (int)b.z.size() != nv || (int)b.custom.size() != nv * 4) {
		return;
	}
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
		const float d = 2.0f + (float)(MAX_EPOCH - epoch) * 1100.0f + b.z[i] * 1000.0f - (float)b.key.zbias * 2.0f;
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
	const int64_t fmt = (int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT;
	rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)fmt);
	rs->mesh_surface_set_material(mesh, 0, spatial_material(b.key.blend, b.key.zmode, a8, tflags, tex));
	pending_meshes.push_back(mesh);
	RID inst = rs->instance_create2(mesh, l.scenario);
	rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(0.0f, 0.0f, -origin)));
	rs->instance_set_pivot_data(inst, 0.0f, false);
	rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
	l.instances.push_back(inst);
}

} // namespace fs2sink
