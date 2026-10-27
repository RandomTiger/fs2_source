// selftest_scenes.h  (R1-1..R1-3)
// Fixed command lists for the sink self-test. Plain C++ over render_sink.h, so the same list runs against the recording
// fake (sink_check: rules and counts, no Godot) and against the Godot sink (Fs2SinkTest: draws, reads the viewport back,
// checks the probes and saves a PNG). A probe is a pixel and the colour it must have.
#ifndef FS2_SELFTEST_SCENES_H
#define FS2_SELFTEST_SCENES_H

#include <cmath>
#include <cstdint>

#include "render_sink.h"

namespace fs2sink {

struct Probe {
	int x, y;
	int r, g, b;
	int tol;		// per channel
	const char *what;
};

struct Scene {
	const char *name;
	int w, h;
	void (*run)(fs2_render_sink *s);	// one whole frame: frame_begin .. frame_end
	const Probe *probes;
	int nprobes;
};

// ---- helpers: everything the game draws reaches the sink as triangles ----

inline fs2_sink_vertex vtx(float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float u = 0, float v = 0, float sw = 1)
{
	fs2_sink_vertex q;
	q.x = x; q.y = y; q.sw = sw; q.u = u; q.v = v; q.r = r; q.g = g; q.b = b; q.a = a;
	return q;
}

inline void draw(fs2_render_sink *s, int tex, int blend, uint32_t flags, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	fs2_sink_draw d;
	d.texture = tex; d.blend = blend; d.flags = flags;
	d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 255;
	s->draw_tris(s->user, &d, v, nv, idx, ni);
}

inline void quad(fs2_render_sink *s, float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
	int blend = FS2_SINK_BLEND_ALPHA, int tex = 0, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1)
{
	const fs2_sink_vertex v[4] = { vtx(x, y, r, g, b, a, u0, v0), vtx(x + w, y, r, g, b, a, u1, v0),
		vtx(x + w, y + h, r, g, b, a, u1, v1), vtx(x, y + h, r, g, b, a, u0, v1) };
	static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	draw(s, tex, blend, 0, v, 4, idx, 6);
}

// a line as a quad of width w (what gr_host does for gf_line)
inline void line(fs2_render_sink *s, float x1, float y1, float x2, float y2, float w, uint8_t r, uint8_t g, uint8_t b)
{
	const float dx = x2 - x1, dy = y2 - y1;
	const float len = (dx * dx + dy * dy) > 0 ? std::sqrt(dx * dx + dy * dy) : 1.0f;
	const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
	const fs2_sink_vertex v[4] = { vtx(x1 + nx, y1 + ny, r, g, b, 255), vtx(x2 + nx, y2 + ny, r, g, b, 255),
		vtx(x2 - nx, y2 - ny, r, g, b, 255), vtx(x1 - nx, y1 - ny, r, g, b, 255) };
	static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	draw(s, 0, FS2_SINK_BLEND_OPAQUE, 0, v, 4, idx, 6);
}

// ---- scene "basic" (R1-1): clear, rect, gouraud triangle, line, clip, alpha, draw order ----

inline void scene_basic_run(fs2_render_sink *s)
{
	s->frame_begin(s->user, 640, 480);
	s->clear(s->user, 16, 16, 64, 255);
	quad(s, 20, 20, 100, 60, 255, 0, 0, 255, FS2_SINK_BLEND_OPAQUE);
	{
		const fs2_sink_vertex v[3] = { vtx(250, 20, 255, 0, 0, 255), vtx(300, 100, 0, 255, 0, 255), vtx(200, 100, 0, 0, 255, 255) };
		static const uint16_t idx[3] = { 0, 1, 2 };
		draw(s, 0, FS2_SINK_BLEND_OPAQUE, 0, v, 3, idx, 3);
	}
	line(s, 20, 150, 300, 250, 4, 255, 255, 255);
	s->set_clip(s->user, 400, 50, 100, 80);
	quad(s, 380, 30, 160, 140, 0, 255, 0, 255, FS2_SINK_BLEND_OPAQUE);
	s->set_clip(s->user, 0, 0, 640, 480);
	quad(s, 50, 300, 100, 100, 255, 255, 255, 128, FS2_SINK_BLEND_ALPHA);
	quad(s, 300, 300, 80, 80, 255, 0, 0, 255, FS2_SINK_BLEND_OPAQUE);
	quad(s, 340, 340, 80, 80, 0, 0, 255, 255, FS2_SINK_BLEND_OPAQUE);
	s->frame_end(s->user);
}

static const Probe scene_basic_probes[] = {
	{ 3, 3, 16, 16, 64, 2, "clear colour" },
	{ 70, 50, 255, 0, 0, 2, "opaque rect" },
	{ 250, 73, 85, 85, 85, 8, "gouraud centroid is the mean of the corners" },
	{ 160, 200, 255, 255, 255, 2, "line middle" },
	{ 450, 90, 0, 255, 0, 2, "inside the clip" },
	{ 390, 90, 16, 16, 64, 2, "left of the clip" },
	{ 520, 90, 16, 16, 64, 2, "right of the clip" },
	{ 450, 140, 16, 16, 64, 2, "below the clip" },
	{ 100, 350, 136, 136, 160, 4, "alpha 128 white over the background" },
	{ 320, 320, 255, 0, 0, 2, "red, drawn first" },
	{ 360, 360, 0, 0, 255, 2, "blue over red where they overlap (draw order)" },
};

static const Scene scene_basic = { "basic", 640, 480, scene_basic_run, scene_basic_probes,
	(int)(sizeof(scene_basic_probes) / sizeof(scene_basic_probes[0])) };

static const Scene *const all_scenes[] = { &scene_basic };
static const int all_scenes_count = (int)(sizeof(all_scenes) / sizeof(all_scenes[0]));

} // namespace fs2sink

#endif
