#include "render_sink_godot.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

using namespace godot;

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
	}
	spare_items.clear();
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
		rs->canvas_item_set_parent(r, RID());
		spare_items.push_back(r);
	}
	items.clear();
	if (root.is_valid()) {
		rs->free_rid(root);
		root = RID();
	}
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
	clip_empty = (w <= 0 || h <= 0);
	const bool full = !clip_empty && x <= 0 && y <= 0 && x + w >= width && y + h >= height;
	if (clip_empty || full) {
		clipped = false;
		parent = pending_root;
		return;
	}
	if (clipped && clip[0] == x && clip[1] == y && clip[2] == w && clip[3] == h) {
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
	s->release_items(s->pending_items, s->pending_root);
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
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->canvas_item_set_visible(s->pending_root, true);
	s->release_items(s->shown_items, s->shown_root);
	s->shown_root = s->pending_root;
	s->shown_items.swap(s->pending_items);
	s->pending_root = RID();
	s->in_frame = false;
	s->committed++;
}

void GodotSink::s_clear(void *u, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame) {
		return;
	}
	// a clear is a full-screen rectangle at this point of the order, ignoring the clip
	RenderingServer *rs = RenderingServer::get_singleton();
	RID item = s->new_item(s->pending_root);
	s->pending_items.push_back(item);
	rs->canvas_item_add_rect(item, Rect2(0.0f, 0.0f, (float)s->width, (float)s->height), Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f));
	if (s->clipped) {
		s->open_clip_item();	// later items must come after the clear in the order
	}
}

void GodotSink::s_set_clip(void *u, int x, int y, int w, int h)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (s->in_frame) {
		s->set_clip_state(x, y, w, h);
	}
}

void GodotSink::s_texture_create(void *, int32_t, int, int, int, uint32_t, const uint8_t *) {}
void GodotSink::s_texture_free(void *, int32_t) {}
void GodotSink::s_texture_free_all(void *) {}

void GodotSink::s_draw_tris(void *u, const fs2_sink_draw *d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	GodotSink *s = static_cast<GodotSink *>(u);
	if (!s->in_frame || s->clip_empty || nv <= 0 || ni < 3) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	PackedInt32Array indices;
	PackedVector2Array points;
	PackedColorArray colors;
	indices.resize(ni);
	points.resize(nv);
	colors.resize(nv);
	for (int i = 0; i < ni; i++) {
		indices.set(i, idx[i]);
	}
	const float tr = d->tint[0] / 255.0f, tg = d->tint[1] / 255.0f, tb = d->tint[2] / 255.0f, ta = d->tint[3] / 255.0f;
	for (int i = 0; i < nv; i++) {
		points.set(i, Vector2(v[i].x, v[i].y));
		colors.set(i, Color(v[i].r / 255.0f * tr, v[i].g / 255.0f * tg, v[i].b / 255.0f * tb, v[i].a / 255.0f * ta));
	}
	RID item = s->new_item(s->parent);
	s->pending_items.push_back(item);
	rs->canvas_item_add_triangle_array(item, indices, points, colors);
}

int GodotSink::s_capture(void *, int32_t) { return 0; }
int GodotSink::s_read_region(void *, int, int, int, int, uint8_t *) { return 0; }
void GodotSink::s_set_gamma(void *, float) {}

} // namespace fs2sink
