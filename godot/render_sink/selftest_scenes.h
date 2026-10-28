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
	fs2_sink_vertex q = {};
	q.x = x; q.y = y; q.sw = sw; q.u = u; q.v = v; q.r = r; q.g = g; q.b = b; q.a = a;
	return q;
}

inline void draw(fs2_render_sink *s, int tex, int blend, uint32_t flags, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	fs2_sink_draw d = {};
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

// ---- scene "textures" (R1-2): nearest, linear, repeat, clamp, alpha, A8, add, tiles, id reuse ----

inline void tex_make(fs2_render_sink *s, int id, int w, int h, int fmt, uint32_t flags, const uint8_t *px)
{
	s->texture_create(s->user, id, w, h, fmt, flags, px);
}

inline void scene_textures_run(fs2_render_sink *s)
{
	static const uint8_t four[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 }; // red green / blue white
	static const uint8_t halfwhite[4] = { 255, 255, 255, 128 };
	static const uint8_t cover[2] = { 255, 0 };
	static const uint8_t yellow[4] = { 255, 255, 0, 255 };
	static const uint8_t cyan[4] = { 0, 255, 255, 255 };
	static const uint8_t red1[4] = { 255, 0, 0, 255 };
	static const uint8_t blue1[4] = { 0, 0, 255, 255 };
	s->frame_begin(s->user, 640, 480);
	s->clear(s->user, 16, 16, 64, 255);
	tex_make(s, 1, 2, 2, FS2_SINK_TEX_RGBA8, 0, four);
	tex_make(s, 2, 2, 2, FS2_SINK_TEX_RGBA8, FS2_SINK_TEXF_LINEAR, four);
	tex_make(s, 3, 2, 2, FS2_SINK_TEX_RGBA8, FS2_SINK_TEXF_REPEAT, four);
	tex_make(s, 4, 1, 1, FS2_SINK_TEX_RGBA8, 0, halfwhite);
	tex_make(s, 5, 2, 1, FS2_SINK_TEX_A8, 0, cover);
	tex_make(s, 6, 1, 1, FS2_SINK_TEX_RGBA8, FS2_SINK_TEXF_LINEAR, yellow);
	tex_make(s, 7, 1, 1, FS2_SINK_TEX_RGBA8, FS2_SINK_TEXF_LINEAR, cyan);
	tex_make(s, 8, 1, 1, FS2_SINK_TEX_RGBA8, 0, red1);
	quad(s, 20, 20, 100, 100, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 1);			// nearest 2x2
	quad(s, 140, 20, 100, 100, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 2);			// linear
	quad(s, 260, 20, 100, 100, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 3, 0, 0, 2, 2);	// repeat
	quad(s, 380, 20, 100, 100, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 1, 0, 0, 2, 2);	// clamp
	quad(s, 20, 140, 100, 60, 255, 0, 0, 255, FS2_SINK_BLEND_ALPHA, 4);			// texture alpha 128 times red
	quad(s, 140, 140, 100, 60, 0, 255, 0, 255, FS2_SINK_BLEND_ALPHA, 5);			// A8: left half green, right half nothing
	quad(s, 260, 140, 100, 60, 100, 0, 0, 255, FS2_SINK_BLEND_OPAQUE);
	quad(s, 260, 140, 100, 60, 0, 200, 0, 128, FS2_SINK_BLEND_ADD);				// add
	quad(s, 380, 140, 50, 60, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 6);		// two section tiles side by side
	quad(s, 430, 140, 50, 60, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 7);
	quad(s, 20, 220, 100, 60, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 8);		// id 8 is red here ...
	s->texture_free(s->user, 8);								// ... freed and made again as blue in the same frame
	tex_make(s, 8, 1, 1, FS2_SINK_TEX_RGBA8, 0, blue1);
	quad(s, 140, 220, 100, 60, 255, 255, 255, 255, FS2_SINK_BLEND_OPAQUE, 8);
	s->frame_end(s->user);
}

static const Probe scene_textures_probes[] = {
	{ 45, 45, 255, 0, 0, 2, "nearest: texel 0,0 red" },
	{ 95, 45, 0, 255, 0, 2, "nearest: texel 1,0 green" },
	{ 45, 95, 0, 0, 255, 2, "nearest: texel 0,1 blue" },
	{ 95, 95, 255, 255, 255, 2, "nearest: texel 1,1 white" },
	{ 190, 70, 128, 128, 128, 8, "linear: the centre is the mean of four texels" },
	{ 272, 32, 255, 0, 0, 2, "repeat: texel 0" },
	{ 297, 32, 0, 255, 0, 2, "repeat: texel 1" },
	{ 322, 32, 255, 0, 0, 2, "repeat: texel 2 wraps to red" },
	{ 347, 32, 0, 255, 0, 2, "repeat: texel 3 wraps to green" },
	{ 392, 32, 255, 0, 0, 2, "clamp: texel 0" },
	{ 417, 32, 0, 255, 0, 2, "clamp: texel 1" },
	{ 442, 32, 0, 255, 0, 2, "clamp: beyond the edge stays on the edge texel (not red)" },
	{ 467, 32, 0, 255, 0, 2, "clamp: far beyond the edge" },
	{ 70, 170, 136, 8, 32, 5, "texture alpha 128 times vertex red over the background" },
	{ 165, 170, 0, 255, 0, 2, "A8: covered half takes the vertex colour" },
	{ 215, 170, 16, 16, 64, 2, "A8: uncovered half shows the background" },
	{ 310, 170, 100, 200, 0, 5, "add: src + dst, alpha ignored (D3D ONE:ONE)" },
	{ 429, 170, 255, 255, 0, 2, "tile one, last pixel" },
	{ 430, 170, 0, 255, 255, 2, "tile two, first pixel: no seam" },
	{ 70, 250, 255, 0, 0, 2, "id 8 before the free is red" },
	{ 190, 250, 0, 0, 255, 2, "id 8 made again is blue" },
};

static const Scene scene_textures = { "textures", 640, 480, scene_textures_run, scene_textures_probes,
	(int)(sizeof(scene_textures_probes) / sizeof(scene_textures_probes[0])) };

// ---- scene "perspective" (S-2, R1-3): a trapezoid whose far edge has half the sw of the near edge ----
// A 1x2 texture (top red, bottom blue), v 0..1 down the quad. At screen fraction t the perspective-correct v is
// t*swn / ((1-t)*swf + t*swn); affine would give t. At y=110 (t=0.45) that is 0.62 (blue) against 0.45 (red).

inline void scene_persp_run(fs2_render_sink *s)
{
	static const uint8_t strip[8] = { 255, 0, 0, 255, 0, 0, 255, 255 };
	s->frame_begin(s->user, 640, 480);
	s->clear(s->user, 16, 16, 64, 255);
	tex_make(s, 20, 1, 2, FS2_SINK_TEX_RGBA8, 0, strip);
	const fs2_sink_vertex v[4] = { vtx(220, 20, 255, 255, 255, 255, 0, 0, 0.5f), vtx(420, 20, 255, 255, 255, 255, 1, 0, 0.5f),
		vtx(520, 220, 255, 255, 255, 255, 1, 1, 1.0f), vtx(120, 220, 255, 255, 255, 255, 0, 1, 1.0f) };
	static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	draw(s, 20, FS2_SINK_BLEND_OPAQUE, FS2_SINK_DRAWF_PERSPECTIVE, v, 4, idx, 6);
	s->frame_end(s->user);
}

static const Probe scene_persp_probes[] = {
	{ 320, 30, 255, 0, 0, 2, "perspective: far part is red" },
	{ 320, 60, 255, 0, 0, 2, "perspective: t=0.2 is still red" },
	{ 320, 110, 0, 0, 255, 2, "perspective: t=0.45 is blue (affine would be red)" },
	{ 320, 200, 0, 0, 255, 2, "perspective: near part is blue" },
	{ 100, 110, 16, 16, 64, 2, "outside the trapezoid" },
};

static const Scene scene_persp = { "perspective", 640, 480, scene_persp_run, scene_persp_probes,
	(int)(sizeof(scene_persp_probes) / sizeof(scene_persp_probes[0])) };

// ---- scene "perf" (R1-3): 15000 triangles of small quads, the first half plain, the second half perspective ----

inline void scene_perf_run(fs2_render_sink *s)
{
	static const uint8_t green[4] = { 0, 255, 0, 255 };
	s->frame_begin(s->user, 640, 480);
	s->clear(s->user, 16, 16, 64, 255);
	tex_make(s, 30, 1, 1, FS2_SINK_TEX_RGBA8, 0, green);
	static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	for (int cy = 0; cy < 75; cy++) {
		for (int cx = 0; cx < 100; cx++) {
			const float x = (float)(cx * 6), y = (float)(cy * 6);
			const bool persp = cy >= 38;
			const float sa = persp ? 0.8f : 1.0f;
			const fs2_sink_vertex v[4] = { vtx(x, y, 255, 255, 255, 255, 0, 0, sa), vtx(x + 5, y, 255, 255, 255, 255, 1, 0, 1.0f),
				vtx(x + 5, y + 5, 255, 255, 255, 255, 1, 1, 1.0f), vtx(x, y + 5, 255, 255, 255, 255, 0, 1, sa) };
			draw(s, 30, FS2_SINK_BLEND_OPAQUE, persp ? FS2_SINK_DRAWF_PERSPECTIVE : 0u, v, 4, idx, 6);
		}
	}
	s->frame_end(s->user);
}

static const Probe scene_perf_probes[] = {
	{ 2, 2, 0, 255, 0, 2, "plain cell" },
	{ 5, 2, 16, 16, 64, 2, "gap between cells" },
	{ 302, 302, 0, 255, 0, 2, "perspective cell" },
};

static const Scene scene_perf = { "perf", 640, 480, scene_perf_run, scene_perf_probes,
	(int)(sizeof(scene_perf_probes) / sizeof(scene_perf_probes[0])) };

static const Scene *const all_scenes[] = { &scene_basic, &scene_textures, &scene_persp, &scene_perf };
static const int all_scenes_count = (int)(sizeof(all_scenes) / sizeof(all_scenes[0]));

} // namespace fs2sink

#endif
