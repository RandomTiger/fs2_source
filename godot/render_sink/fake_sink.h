// fake_sink.h  (R1-0, v2 R1-8)
// A recording fake of the render sink: every call is appended to a list, and the rules of render_sink.h are checked
// (no call outside a frame, no call in a frame that was refused, textures exist when drawn, indices in range).
// Header only, plain C++17, no game and no Godot type, so gr_host and the batcher are testable without either.
#ifndef FS2_FAKE_SINK_H
#define FS2_FAKE_SINK_H

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "render_sink.h"

namespace fs2sink {

struct Cmd {
	enum Kind { FrameBegin, FrameEnd, Clear, SetClip, TexCreate, TexFree, TexFreeAll, Draw, Capture, ReadRegion, Gamma, DepthBegin, DepthEnd, DepthClear, SetView, MeshCreate, MeshFree, MeshDraw };
	Kind kind;
	int a = 0, b = 0, c = 0, d = 0;	// meaning per kind: Clip x,y,w,h; TexCreate id,w,h,format; Draw texture,blend,flags,ntris
	std::vector<fs2_sink_vertex> verts;	// Draw only
	fs2_sink_draw draw = {};		// Draw only
	float f = 0;
};

class FakeSink {
public:
	std::vector<Cmd> cmds;
	std::vector<std::string> errors;	// rule breaks seen
	bool want_frames = true;		// what frame_begin answers
	int frames_begun = 0, frames_ended = 0;
	fs2_sink_view last_view = {};		// the last set_view
	std::set<int32_t> meshes;		// live mesh ids (v4)

	fs2_render_sink *sink() { return &table; }

	FakeSink()
	{
		table.size = sizeof(table);
		table.version = FS2_RENDER_SINK_VERSION;
		table.user = this;
		table.frame_begin = &FakeSink::s_frame_begin;
		table.frame_end = &FakeSink::s_frame_end;
		table.clear = &FakeSink::s_clear;
		table.set_clip = &FakeSink::s_set_clip;
		table.texture_create = &FakeSink::s_texture_create;
		table.texture_free = &FakeSink::s_texture_free;
		table.texture_free_all = &FakeSink::s_texture_free_all;
		table.draw_tris = &FakeSink::s_draw_tris;
		table.capture_to_texture = &FakeSink::s_capture;
		table.read_region = &FakeSink::s_read_region;
		table.set_gamma = &FakeSink::s_set_gamma;
		table.depth_begin = &FakeSink::s_depth_begin;
		table.depth_end = &FakeSink::s_depth_end;
		table.depth_clear = &FakeSink::s_depth_clear;
		table.set_view = &FakeSink::s_set_view;
	}

	int count(Cmd::Kind k) const
	{
		int n = 0;
		for (const Cmd &c : cmds) {
			n += (c.kind == k);
		}
		return n;
	}
	int triangles() const
	{
		int n = 0;
		for (const Cmd &c : cmds) {
			n += (c.kind == Cmd::Draw) ? c.d : 0;
		}
		return n;
	}
	void reset() { cmds.clear(); errors.clear(); }

private:
	fs2_render_sink table = {};
	bool in_frame = false;
	bool in_depth = false;
	std::set<int32_t> textures;

	void err(const char *what) { errors.push_back(what); }
	void need_frame(const char *what) { if (!in_frame) err(what); }
	Cmd &add(Cmd::Kind k) { cmds.emplace_back(); cmds.back().kind = k; return cmds.back(); }
	static FakeSink *me(void *u) { return static_cast<FakeSink *>(u); }

	static int s_frame_begin(void *u, int w, int h)
	{
		FakeSink *s = me(u);
		if (s->in_frame) { s->err("frame_begin inside a frame"); }
		Cmd &c = s->add(Cmd::FrameBegin);
		c.a = w; c.b = h;
		s->frames_begun++;
		s->in_frame = s->want_frames;
		return s->want_frames ? 1 : 0;
	}
	static void s_frame_end(void *u)
	{
		FakeSink *s = me(u);
		s->need_frame("frame_end without an accepted frame_begin");
		s->add(Cmd::FrameEnd);
		s->frames_ended++;
		s->in_frame = false;
		s->in_depth = false;
	}
	static void s_clear(void *u, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		FakeSink *s = me(u);
		s->need_frame("clear outside a frame");
		Cmd &c = s->add(Cmd::Clear);
		c.a = r; c.b = g; c.c = b; c.d = a;
	}
	static void s_set_clip(void *u, int x, int y, int w, int h)
	{
		FakeSink *s = me(u);
		s->need_frame("set_clip outside a frame");
		Cmd &c = s->add(Cmd::SetClip);
		c.a = x; c.b = y; c.c = w; c.d = h;
	}
	static void s_texture_create(void *u, int32_t id, int w, int h, int format, uint32_t, const uint8_t *pixels)
	{
		FakeSink *s = me(u);
		s->need_frame("texture_create outside a frame");
		if (id == 0 || w <= 0 || h <= 0 || pixels == nullptr) { s->err("texture_create with bad arguments"); }
		s->textures.insert(id);
		Cmd &c = s->add(Cmd::TexCreate);
		c.a = id; c.b = w; c.c = h; c.d = format;
	}
	static void s_texture_free(void *u, int32_t id)
	{
		FakeSink *s = me(u);
		s->need_frame("texture_free outside a frame");
		if (s->textures.erase(id) == 0) { s->err("texture_free of an unknown texture"); }
		s->add(Cmd::TexFree).a = id;
	}
	static void s_texture_free_all(void *u)
	{
		FakeSink *s = me(u);
		s->need_frame("texture_free_all outside a frame");
		s->textures.clear();
		s->add(Cmd::TexFreeAll);
	}
	static void s_draw_tris(void *u, const fs2_sink_draw *d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
	{
		FakeSink *s = me(u);
		s->need_frame("draw_tris outside a frame");
		if (ni % 3 != 0 || ni <= 0 || nv <= 0) { s->err("draw_tris with a bad count"); }
		if (d->texture != 0 && s->textures.count(d->texture) == 0) { s->err("draw_tris with an unknown texture"); }
		for (int i = 0; i < ni; i++) {
			if (idx[i] >= nv) { s->err("draw_tris index out of range"); break; }
		}
		Cmd &c = s->add(Cmd::Draw);
		c.draw = *d;
		c.a = d->texture; c.b = d->blend; c.c = (int)d->flags; c.d = ni / 3;
		c.verts.assign(v, v + nv);
	}
	static int s_capture(void *u, int32_t id)
	{
		FakeSink *s = me(u);
		s->need_frame("capture_to_texture outside a frame");
		s->textures.insert(id);
		s->add(Cmd::Capture).a = id;
		return 1;
	}
	static int s_read_region(void *u, int x, int y, int w, int h, uint8_t *out)
	{
		FakeSink *s = me(u);
		s->need_frame("read_region outside a frame");
		Cmd &c = s->add(Cmd::ReadRegion);
		c.a = x; c.b = y; c.c = w; c.d = h;
		for (long i = 0; i < (long)w * h * 4; i++) { out[i] = 0; }
		return 1;
	}
	static void s_set_gamma(void *u, float g)
	{
		FakeSink *s = me(u);
		s->need_frame("set_gamma outside a frame");
		s->add(Cmd::Gamma).f = g;
	}
	static void s_depth_begin(void *u)
	{
		FakeSink *s = me(u);
		s->need_frame("depth_begin outside a frame");
		if (s->in_depth) { s->err("depth_begin inside a depth run"); }
		s->in_depth = true;
		s->add(Cmd::DepthBegin);
	}
	static void s_depth_end(void *u)
	{
		FakeSink *s = me(u);
		s->need_frame("depth_end outside a frame");
		if (!s->in_depth) { s->err("depth_end without a depth run"); }
		s->in_depth = false;
		s->add(Cmd::DepthEnd);
	}
	static void s_depth_clear(void *u, int x, int y, int w, int h)
	{
		FakeSink *s = me(u);
		s->need_frame("depth_clear outside a frame");
		if (!s->in_depth) { s->err("depth_clear outside a depth run"); }
		Cmd &c = s->add(Cmd::DepthClear);
		c.a = x; c.b = y; c.c = w; c.d = h;
	}
	static void s_set_view(void *u, const fs2_sink_view *v)
	{
		FakeSink *s = me(u);
		s->need_frame("set_view outside a frame");
		if (v == nullptr || v->canvas_w <= 0 || v->canvas_h <= 0) { s->err("set_view with no canvas"); return; }
		s->last_view = *v;
		s->add(Cmd::SetView);
	}

	static void s_mesh_create(void *u, int32_t id, const fs2_sink_mesh_vertex *v, int nv, const fs2_sink_mesh_group *g, int ng,
		const uint32_t *idx, int ni)
	{
		FakeSink *s = me(u);
		s->need_frame("mesh_create outside a frame");
		if (id == 0 || v == nullptr || nv <= 0 || g == nullptr || ng <= 0 || idx == nullptr || ni <= 0 || ni % 3 != 0) { s->err("mesh_create with bad arguments"); return; }
		for (int i = 0; i < ni; i++) {
			if (idx[i] >= (uint32_t)nv) { s->err("mesh_create index out of range"); return; }
		}
		for (int i = 0; i < ng; i++) {
			if (g[i].first_index < 0 || g[i].index_count <= 0 || g[i].index_count % 3 != 0 || g[i].first_index + g[i].index_count > ni) { s->err("mesh_create group out of range"); return; }
			if (g[i].texture != 0 && s->textures.count(g[i].texture) == 0) { s->err("mesh_create with an unknown texture"); return; }
		}
		s->meshes.insert(id);
		Cmd &c = s->add(Cmd::MeshCreate);
		c.a = id; c.b = nv; c.c = ng; c.d = ni / 3;
	}
	static void s_mesh_free(void *u, int32_t id)
	{
		FakeSink *s = me(u);
		s->need_frame("mesh_free outside a frame");
		s->meshes.erase(id);
		s->add(Cmd::MeshFree).a = id;
	}
	static void s_mesh_draw(void *u, int32_t id, const float *xform, const uint8_t *, int ncol)
	{
		FakeSink *s = me(u);
		s->need_frame("mesh_draw outside a frame");
		if (!s->in_depth) { s->err("mesh_draw outside a depth run"); }
		if (xform == nullptr || s->meshes.count(id) == 0) { s->err("mesh_draw of an unknown mesh"); }
		Cmd &c = s->add(Cmd::MeshDraw);
		c.a = id; c.b = ncol;
	}
};

} // namespace fs2sink

#endif
