// sink_check.cpp  (R1-0)
// Exercises the recording fake sink against the rules of render_sink.h. No game, no Godot.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "tex_convert.h"
#include "batcher.h"
#include "fake_sink.h"
#include "depth_contract.h"
#include "selftest_scenes.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static void test_frame_rules()
{
	fs2sink::FakeSink fake;
	fs2_render_sink *s = fake.sink();
	CHECK(s->size == sizeof(fs2_render_sink) && s->version == FS2_RENDER_SINK_VERSION);

	// an accepted frame: begin, clear, texture, draw, end
	CHECK(s->frame_begin(s->user, 640, 480) == 1);
	s->clear(s->user, 0, 0, 0, 255);
	const uint8_t px[4 * 4] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
	s->texture_create(s->user, 7, 2, 2, FS2_SINK_TEX_RGBA8, 0, px);
	fs2_sink_vertex v[4] = {};
	v[1].x = 10; v[2].x = 10; v[2].y = 10; v[3].y = 10;
	const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	fs2_sink_draw d = { 7, FS2_SINK_BLEND_ALPHA, 0, { 255, 255, 255, 255 } };
	s->draw_tris(s->user, &d, v, 4, idx, 6);
	s->frame_end(s->user);
	CHECK(fake.errors.empty());
	CHECK(fake.triangles() == 2);
	CHECK(fake.frames_begun == 1 && fake.frames_ended == 1);

	// a refused frame: nothing may be called until the next frame_begin
	fake.reset();
	fake.want_frames = false;
	CHECK(s->frame_begin(s->user, 640, 480) == 0);
	CHECK(fake.errors.empty());
	s->clear(s->user, 0, 0, 0, 255);	// a host breaking the rule
	CHECK(fake.errors.size() == 1);

	// bad draws are caught
	fake.reset();
	fake.want_frames = true;
	CHECK(s->frame_begin(s->user, 640, 480) == 1);
	fs2_sink_draw nodraw = { 99, FS2_SINK_BLEND_OPAQUE, 0, { 255, 255, 255, 255 } };
	s->draw_tris(s->user, &nodraw, v, 4, idx, 6);	// unknown texture
	const uint16_t bad[3] = { 0, 1, 9 };
	fs2_sink_draw plain = { 0, FS2_SINK_BLEND_OPAQUE, 0, { 255, 255, 255, 255 } };
	s->draw_tris(s->user, &plain, v, 4, bad, 3);	// index out of range
	s->frame_end(s->user);
	CHECK(fake.errors.size() == 2);
}

// every self-test scene runs against the fake: rules kept, something drawn, probes inside the screen
static void test_scenes()
{
	for (int i = 0; i < fs2sink::all_scenes_count; i++) {
		const fs2sink::Scene &sc = *fs2sink::all_scenes[i];
		fs2sink::FakeSink fake;
		sc.run(fake.sink());
		if (!fake.errors.empty()) { std::printf("scene %s: %s\n", sc.name, fake.errors[0].c_str()); }
		CHECK(fake.errors.empty());
		CHECK(fake.frames_begun == 1 && fake.frames_ended == 1);
		CHECK(fake.triangles() > 0);
		for (int p = 0; p < sc.nprobes; p++) {
			CHECK(sc.probes[p].x >= 0 && sc.probes[p].x < sc.w && sc.probes[p].y >= 0 && sc.probes[p].y < sc.h);
		}
	}
}

// texture conversion for both depths: 32 bit (default) and 16 bit (option)
static void test_tex_convert()
{
	using namespace fs2host;
	CHECK(&layout_for_texture_bpp(32) == &LAYOUT_ARGB8888);
	CHECK(&layout_for_texture_bpp(16) == &LAYOUT_ARGB1555);
	// 32 bit: a pixel 0xAARRGGBB
	const uint32_t p32[2] = { 0x80FF4020u, 0xFF000000u };
	uint8_t out[8];
	to_rgba8(reinterpret_cast<const uint8_t *>(p32), 2, 1, 0, LAYOUT_ARGB8888, out);
	CHECK(out[0] == 0xFF && out[1] == 0x40 && out[2] == 0x20 && out[3] == 0x80);
	CHECK(out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0xFF);
	// 16 bit 1555: full scale is exactly 255, zero is 0, the alpha bit is 0 or 255
	const uint16_t p16[3] = { 0xFFFFu, 0x0000u, 0x7C1Fu };
	uint8_t o16[12];
	to_rgba8(reinterpret_cast<const uint8_t *>(p16), 3, 1, 0, LAYOUT_ARGB1555, o16);
	CHECK(o16[0] == 255 && o16[1] == 255 && o16[2] == 255 && o16[3] == 255);
	CHECK(o16[4] == 0 && o16[5] == 0 && o16[6] == 0 && o16[7] == 0);
	CHECK(o16[8] == 255 && o16[9] == 0 && o16[10] == 255 && o16[11] == 0);
	// 4444 and 565 (no alpha mask: opaque)
	const uint16_t q = 0x8F0Fu;
	uint8_t o4[4];
	to_rgba8(reinterpret_cast<const uint8_t *>(&q), 1, 1, 0, LAYOUT_ARGB4444, o4);
	CHECK(o4[0] == 0xFF && o4[1] == 0x00 && o4[2] == 0xFF && o4[3] == 0x88);
	const uint16_t r5 = 0xF800u;
	to_rgba8(reinterpret_cast<const uint8_t *>(&r5), 1, 1, 0, LAYOUT_RGB565, o4);
	CHECK(o4[0] == 255 && o4[1] == 0 && o4[2] == 0 && o4[3] == 255);
	// every 5 bit value expands monotonically and ends at 255
	int last = -1;
	for (uint32_t v = 0; v < 32; v++) {
		const int e = expand_channel(v, 5);
		CHECK(e > last);
		last = e;
	}
	CHECK(last == 255);
	// a row pitch wider than the row is skipped, and the A8 copy drops it too
	const uint16_t padded[4] = { 0xFFFFu, 0x0000u, 0xFFFFu, 0x0000u };	// 1 pixel wide, pitch 4 bytes, 2 rows
	uint8_t o2[8];
	to_rgba8(reinterpret_cast<const uint8_t *>(padded), 1, 2, 4, LAYOUT_ARGB1555, o2);
	CHECK(o2[0] == 255 && o2[4] == 255);
	const uint8_t a_src[6] = { 1, 2, 9, 3, 4, 9 };
	uint8_t a_dst[4];
	to_a8(a_src, 2, 2, 3, a_dst);
	CHECK(a_dst[0] == 1 && a_dst[1] == 2 && a_dst[2] == 3 && a_dst[3] == 4);
	CHECK(rgba8_bytes(3, 2) == 24 && a8_bytes(3, 2) == 6);
}

// ---- batcher (R1-3) ----

struct Collect : fs2sink::BatchEmitter {
	std::vector<fs2sink::Batch> out;
	long total_tris = 0;
	void emit(const fs2sink::Batch &b) override { out.push_back(b); total_tris += (long)b.idx.size() / 3; }
};

struct Count : fs2sink::BatchEmitter {
	long tris = 0, batches = 0;
	void emit(const fs2sink::Batch &b) override { tris += (long)b.idx.size() / 3; batches++; }
};

static fs2_sink_draw make_draw(int tex, int blend, uint32_t flags, uint8_t t = 255)
{
	fs2_sink_draw d;
	d.texture = tex; d.blend = blend; d.flags = flags;
	d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = t;
	return d;
}

static const uint16_t QIDX[6] = { 0, 1, 2, 0, 2, 3 };

static void qverts(fs2_sink_vertex *v, float x, float y, float sa, float sb)
{
	v[0] = fs2sink::vtx(x, y, 255, 255, 255, 255, 0, 0, sa);
	v[1] = fs2sink::vtx(x + 5, y, 255, 255, 255, 255, 1, 0, sb);
	v[2] = fs2sink::vtx(x + 5, y + 5, 255, 255, 255, 255, 1, 1, sb);
	v[3] = fs2sink::vtx(x, y + 5, 255, 255, 255, 255, 0, 1, sa);
}

static void test_batcher_merge()
{
	Collect c;
	fs2sink::TriBatcher b;
	b.set_emitter(&c);
	fs2_sink_vertex v[4];
	qverts(v, 0, 0, 1, 1);
	// three draws of one key are one batch; a different texture, blend or perspective breaks it; order is kept
	for (int i = 0; i < 3; i++) { b.add(make_draw(1, FS2_SINK_BLEND_ALPHA, 0), v, 4, QIDX, 6, 8, 8); }
	b.add(make_draw(2, FS2_SINK_BLEND_ALPHA, 0), v, 4, QIDX, 6, 8, 8);
	b.add(make_draw(2, FS2_SINK_BLEND_ADD, 0), v, 4, QIDX, 6, 8, 8);
	b.add(make_draw(1, FS2_SINK_BLEND_ALPHA, 0), v, 4, QIDX, 6, 8, 8);
	b.flush();
	CHECK(c.out.size() == 4);
	CHECK(c.out[0].key.texture == 1 && c.out[0].idx.size() == 18 && c.out[0].nverts() == 12);
	CHECK(c.out[1].key.texture == 2 && c.out[1].key.blend == FS2_SINK_BLEND_ALPHA);
	CHECK(c.out[2].key.blend == FS2_SINK_BLEND_ADD);
	CHECK(c.out[3].key.texture == 1);
	// indices of the 2nd draw of a batch are offset by the vertices before it
	CHECK(c.out[0].idx[6] == 4 && c.out[0].idx[17] == 11);
	CHECK(c.total_tris == 12);
}

static void test_batcher_tint_and_perspective_flag()
{
	Collect c;
	fs2sink::TriBatcher b;
	b.set_emitter(&c);
	fs2_sink_vertex v[4];
	qverts(v, 0, 0, 1, 1);
	// tint folds into the vertex colour (128/255 of 1.0)
	b.add(make_draw(0, FS2_SINK_BLEND_ALPHA, 0, 128), v, 4, QIDX, 6, 0, 0);
	b.flush();
	CHECK(std::fabs(c.out[0].col[0] - 128.0f / 255.0f) < 1e-6f && std::fabs(c.out[0].col[3] - 128.0f / 255.0f) < 1e-6f);
	// the flag with equal sw is exact affine: an ordinary batch; with unequal sw it is a perspective batch with custom = (u*sw, v*sw, sw)
	c.out.clear();
	b.add(make_draw(1, FS2_SINK_BLEND_OPAQUE, FS2_SINK_DRAWF_PERSPECTIVE), v, 4, QIDX, 6, 8, 8);
	b.flush();
	CHECK(c.out.size() == 1 && c.out[0].key.perspective == 0 && c.out[0].custom.empty());
	c.out.clear();
	qverts(v, 0, 0, 0.5f, 1.0f);
	b.add(make_draw(1, FS2_SINK_BLEND_OPAQUE, FS2_SINK_DRAWF_PERSPECTIVE), v, 4, QIDX, 6, 8, 8);
	b.flush();
	CHECK(c.out.size() == 1 && c.out[0].key.perspective == 1 && c.out[0].custom.size() == 16);
	CHECK(c.out[0].custom[2] == 0.5f && c.out[0].custom[4] == 1.0f && c.out[0].custom[6] == 1.0f);	// u*sw of vertex 1 = 1*1
	CHECK(b.stats().perspective_draws == 1);
}

// a plane Z = 2 + Y seen by a pinhole: screen position, sw = 1/Z; u = X/4, v = Y/2 are the texture coordinates of the plane
struct Plane {
	static float z_of(float y) { return 2.0f + y; }
	static void project(float X, float Y, float &xs, float &ys, float &sw)
	{
		const float Z = z_of(Y);
		xs = 320.0f + X / Z * 200.0f; ys = 240.0f + Y / Z * 200.0f; sw = 1.0f / Z;
	}
	static void truth(float xs, float ys, float &u, float &v)	// the texture coordinate under a screen point
	{
		const float k = (ys - 240.0f) / 200.0f;		// Y/Z
		const float Y = 2.0f * k / (1.0f - k);
		const float X = (xs - 320.0f) / 200.0f * z_of(Y);
		u = X / 4.0f; v = Y / 2.0f;
	}
};

// largest uv error in texels, over the pixel centres of the emitted triangles, against the truth of the plane
static double max_uv_error_texels(const std::vector<fs2sink::Batch> &batches, int tex_size)
{
	double worst = 0;
	for (const fs2sink::Batch &b : batches) {
		for (size_t t = 0; t + 2 < b.idx.size(); t += 3) {
			const int i0 = b.idx[t], i1 = b.idx[t + 1], i2 = b.idx[t + 2];
			const float x0 = b.pos[i0 * 2], y0 = b.pos[i0 * 2 + 1], x1 = b.pos[i1 * 2], y1 = b.pos[i1 * 2 + 1], x2 = b.pos[i2 * 2], y2 = b.pos[i2 * 2 + 1];
			const float den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
			if (std::fabs(den) < 1e-6f) { continue; }
			const int xa = (int)std::floor(std::fmin(x0, std::fmin(x1, x2))), xb = (int)std::ceil(std::fmax(x0, std::fmax(x1, x2)));
			const int ya = (int)std::floor(std::fmin(y0, std::fmin(y1, y2))), yb = (int)std::ceil(std::fmax(y0, std::fmax(y1, y2)));
			for (int py = ya; py < yb; py++) {
				for (int px = xa; px < xb; px++) {
					const float sx = (float)px + 0.5f, sy = (float)py + 0.5f;
					const float l0 = ((y1 - y2) * (sx - x2) + (x2 - x1) * (sy - y2)) / den;
					const float l1 = ((y2 - y0) * (sx - x2) + (x0 - x2) * (sy - y2)) / den;
					const float l2 = 1.0f - l0 - l1;
					if (l0 < 0 || l1 < 0 || l2 < 0) { continue; }
					float u, v;
					if (b.key.perspective) {	// what the shader does: interpolate custom, divide
						const float cu = l0 * b.custom[i0 * 4] + l1 * b.custom[i1 * 4] + l2 * b.custom[i2 * 4];
						const float cv = l0 * b.custom[i0 * 4 + 1] + l1 * b.custom[i1 * 4 + 1] + l2 * b.custom[i2 * 4 + 1];
						const float cw = l0 * b.custom[i0 * 4 + 2] + l1 * b.custom[i1 * 4 + 2] + l2 * b.custom[i2 * 4 + 2];
						u = cu / cw; v = cv / cw;
					} else {			// what the canvas does: interpolate uv
						u = l0 * b.uv[i0 * 2] + l1 * b.uv[i1 * 2] + l2 * b.uv[i2 * 2];
						v = l0 * b.uv[i0 * 2 + 1] + l1 * b.uv[i1 * 2 + 1] + l2 * b.uv[i2 * 2 + 1];
					}
					float tu, tv;
					Plane::truth(sx, sy, tu, tv);
					const double e = std::fmax(std::fabs((double)(u - tu)), std::fabs((double)(v - tv))) * tex_size;
					if (e > worst) { worst = e; }
				}
			}
		}
	}
	return worst;
}

static void plane_draw(fs2sink::TriBatcher &b, int tex_size, uint32_t flags)
{
	const float xs[4] = { -2, 2, 2, -2 }, ys[4] = { 0, 0, 2, 2 };	// plane X -2..2, Y 0..2: Z 2..4
	fs2_sink_vertex v[4];
	for (int i = 0; i < 4; i++) {
		float sx, sy, sw;
		Plane::project(xs[i], ys[i], sx, sy, sw);
		v[i] = fs2sink::vtx(sx, sy, 255, 255, 255, 255, xs[i] / 4.0f, ys[i] / 2.0f, sw);
	}
	b.add(make_draw(1, FS2_SINK_BLEND_OPAQUE, flags), v, 4, QIDX, 6, tex_size, tex_size);
	b.flush();
}

static void test_perspective_equivalence()
{
	const int tex = 64;
	Collect shader, subdiv, plain;
	fs2sink::TriBatcher a, s, p;
	a.set_emitter(&shader); s.set_emitter(&subdiv); p.set_emitter(&plain);
	s.set_perspective_mode(fs2sink::PERSPECTIVE_SUBDIVIDE);
	plane_draw(a, tex, FS2_SINK_DRAWF_PERSPECTIVE);
	plane_draw(s, tex, FS2_SINK_DRAWF_PERSPECTIVE);
	plane_draw(p, tex, 0);		// the same quad with the flag dropped is plain affine: the error S-2 removes
	const double e_shader = max_uv_error_texels(shader.out, tex);
	const double e_subdiv = max_uv_error_texels(subdiv.out, tex);
	const double e_affine = max_uv_error_texels(plain.out, tex);
	std::printf("S-2 uv error on a 64 texel plane: shader %.5f texel, subdivision %.3f texel (%ld triangles), affine %.2f texel\n",
		e_shader, e_subdiv, subdiv.total_tris, e_affine);
	CHECK(e_shader < 0.01);
	CHECK(e_subdiv < 0.5);
	CHECK(e_affine > 2.0);		// the test plane really needs the correction
	CHECK(shader.out.size() == 1 && shader.out[0].key.perspective == 1);
	CHECK(subdiv.out.size() == 1 && subdiv.out[0].key.perspective == 0 && subdiv.total_tris > 2);
}

// CPU cost of the batcher per input triangle, 50000 triangles of small quads
static void test_batcher_cost()
{
	const int quads = 25000;
	std::vector<fs2_sink_vertex> verts((size_t)quads * 4);
	for (int i = 0; i < quads; i++) {
		const float x = (float)((i % 100) * 6), y = (float)((i / 100) % 75 * 6);
		qverts(&verts[(size_t)i * 4], x, y, 0.8f, 1.0f);
	}
	struct Mode { const char *name; uint32_t flags; fs2sink::PerspectiveMode m; };
	const Mode modes[3] = { { "affine (no flag)", 0, fs2sink::PERSPECTIVE_SHADER },
		{ "perspective, shader (CUSTOM0 data)", FS2_SINK_DRAWF_PERSPECTIVE, fs2sink::PERSPECTIVE_SHADER },
		{ "perspective, CPU subdivision", FS2_SINK_DRAWF_PERSPECTIVE, fs2sink::PERSPECTIVE_SUBDIVIDE } };
	for (const Mode &md : modes) {
		Count c;
		fs2sink::TriBatcher b;
		b.set_emitter(&c);
		b.set_perspective_mode(md.m);
		double best = 1e30;
		for (int rep = 0; rep < 5; rep++) {
			c.tris = 0; c.batches = 0;
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < quads; i++) {
				b.add(make_draw(1, FS2_SINK_BLEND_OPAQUE, md.flags), &verts[(size_t)i * 4], 4, QIDX, 6, 64, 64);
			}
			b.flush();
			const double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
			if (ns < best) { best = ns; }
		}
		std::printf("batcher cost, %-36s %7.1f ns per input triangle (50000 in, %ld out, %ld batches)\n", md.name,
			best / (2.0 * quads), c.tris, c.batches);
		CHECK(c.tris >= 2L * quads);
	}
}

// P1: the camera call and the depth contract
static void test_view_and_depth()
{
	fs2sink::FakeSink fake;
	fs2_render_sink *s = fake.sink();
	fs2_sink_view v = {};
	v.canvas_w = 640; v.canvas_h = 480; v.scale[0] = v.scale[1] = v.scale[2] = 1.0f; v.zoom = 1.0f;
	CHECK(s->frame_begin(s->user, 640, 480) == 1);
	s->set_view(s->user, &v);
	CHECK(fake.count(fs2sink::Cmd::SetView) == 1 && fake.last_view.canvas_w == 640);
	s->frame_end(s->user);
	CHECK(fake.errors.empty());

	// the game's projection (g3_project_vertex) of eye space points, rebuilt from what the host sends
	const float pts[][3] = { { 10, 5, 100 }, { -300, 200, 5000 }, { 0.2f, -0.1f, 0.5f }, { 5000, -4000, 40000 }, { 3, 2, 29000 } };
	for (const auto &p : pts) {
		float w = 1.0f / p[2];
		const float sx = (640.0f + p[0] * 640.0f * w) * 0.5f, sy = (480.0f - p[1] * 480.0f * w) * 0.5f;
		if (w > 1.0f) { w = 1.0f; }
		float zd = p[2] / 30000.0f;
		if (zd > 0.98f) { zd = 0.98f; }
		float e[3];
		CHECK(fs2sink::reconstruct_eye(v, sx, sy, w, zd, e) == 1);
		const float tol = 1e-3f * p[2] + 1e-3f;
		CHECK(std::fabs(e[0] - p[0]) < tol && std::fabs(e[1] - p[1]) < tol && std::fabs(e[2] - p[2]) < tol);
	}
	float e[3];
	CHECK(fs2sink::reconstruct_eye(v, 0, 0, 1.0f, 0.0f, e) == 0);	// no depth and a clamped sw: not recoverable

	// the contract: nearer epoch, smaller z, larger bias are all nearer (smaller distance)
	CHECK(fs2sink::legacy_depth(0.5f, 3, 24, 0) == 2.0f + 21 * 1100.0f + 500.0f);
	CHECK(fs2sink::legacy_depth(0.1f, 0, 24, 0) < fs2sink::legacy_depth(0.2f, 0, 24, 0));
	CHECK(fs2sink::legacy_depth(0.5f, 1, 24, 0) < fs2sink::legacy_depth(0.5f, 0, 24, 0));
	CHECK(fs2sink::legacy_depth(0.5f, 0, 24, 4) < fs2sink::legacy_depth(0.5f, 0, 24, 0));
}

int main()
{
	test_scenes();
	test_tex_convert();
	test_batcher_merge();
	test_batcher_tint_and_perspective_flag();
	test_perspective_equivalence();
	test_batcher_cost();
	test_frame_rules();
	test_view_and_depth();
	if (failures) {
		std::printf("sink_check: %d FAILED\n", failures);
		return 1;
	}
	std::printf("sink_check: PASS\n");
	return 0;
}
