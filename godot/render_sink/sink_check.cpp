// sink_check.cpp  (R1-0)
// Exercises the recording fake sink against the rules of render_sink.h. No game, no Godot.
#include <cstdio>

#include "../gr_host/tex_convert.h"
#include "fake_sink.h"
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

int main()
{
	test_scenes();
	test_tex_convert();
	test_frame_rules();
	if (failures) {
		std::printf("sink_check: %d FAILED\n", failures);
		return 1;
	}
	std::printf("sink_check: PASS\n");
	return 0;
}
