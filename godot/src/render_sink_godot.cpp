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
	batcher.set_emitter(this);
	return true;
}

void GodotSink::shutdown()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr) {
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
		parent = pending_root;
		return;
	}
	clip[0] = x; clip[1] = y; clip[2] = w; clip[3] = h;
	clipped = true;
	open_clip_item();
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
	s->batcher.flush();
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->canvas_item_set_visible(s->pending_root, true);
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

// A batch becomes one canvas item with one array draw (affine) or one mesh draw (perspective: the mesh carries CUSTOM0).
void GodotSink::emit(const Batch &b)
{
	if (!in_frame) {
		return;
	}
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

} // namespace fs2sink
