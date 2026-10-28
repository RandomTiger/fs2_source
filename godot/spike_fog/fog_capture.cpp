// Fog spike: S-1's recording sink with the fog side channel (fs2_fog_side in FS2fog_64r.dll). Runs a replay, writes per-frame
// stats and dumps the frame with the most fogged draws. read_region software-renders the ops of the current frame so far
// (sw_raster.h), so neb2_pre_render's 32x32 grab sees this frame's background, as on D3D.
//   fog_capture <FS2fog dll> <root> <cmdline> <out dump> <out stats csv> [min_frame]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>
#include "fs2_loader.h"
#include "sw_raster.h"

using namespace fogspike;

static const Side *(*get_side)() = NULL;
static std::map<int, Tex> g_tex;
static std::vector<Op> g_ops, g_best;
static std::map<int, Tex> g_best_tex;
static int g_w = 0, g_h = 0, g_frame = 0, g_best_score = -1, g_best_frame = -1, g_min_frame = 0, g_clip[4] = {0, 0, 1024, 768};
static int g_reads = 0, g_reads_frame = 0, g_last_fogged_2d = 0;
static FILE *g_csv;

static int s_begin(void *, int w, int h)
{
	g_w = w; g_h = h; g_ops.clear(); g_clip[0] = g_clip[1] = 0; g_clip[2] = w; g_clip[3] = h; g_reads_frame = 0;
	return 1;
}
static void s_end(void *)
{
	g_frame++;
	int nfog = 0, npix = 0, nfogadd = 0, nz = 0, ntri = 0;
	for (auto &o : g_ops) if (o.kind == 0) {
		ntri += (int)o.idx.size() / 3;
		if (o.s.valid && o.s.zmode > 0) { nz++; }
		if (o.d.fog_mode != FS2_SINK_FOG_NONE) { nfog++; if (o.d.blend == FS2_SINK_BLEND_ADD) { nfogadd++; } }
		if (o.s.valid && o.s.pixfog) { npix++; }
	}
	const Side *s = get_side();
	const int f2d = s ? s->fogged_2d : 0;
	fprintf(g_csv, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n", g_frame, (int)g_ops.size(), nz, nfog, npix, nfogadd, f2d - g_last_fogged_2d, g_reads_frame, ntri);
	g_last_fogged_2d = f2d;
	const int score = nfog + npix + (g_reads_frame > 0 ? 1 : 0);
	if (g_frame >= g_min_frame && score > g_best_score) {
		g_best_score = score; g_best_frame = g_frame; g_best = g_ops;
		g_best_tex.clear();
		for (auto &o : g_best) if (o.kind == 0 && o.d.texture) g_best_tex[o.d.texture] = g_tex[o.d.texture];
	}
}
static void s_clear(void *, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	Op o; o.kind = 1; o.a[0] = r; o.a[1] = g; o.a[2] = b; o.a[3] = a; memcpy(o.clip, g_clip, sizeof(g_clip)); g_ops.push_back(o);
}
static void s_clip(void *, int x, int y, int w, int h)
{
	Op o; o.kind = 2; o.a[0] = x; o.a[1] = y; o.a[2] = w; o.a[3] = h;
	g_clip[0] = x; g_clip[1] = y; g_clip[2] = w; g_clip[3] = h; memcpy(o.clip, g_clip, sizeof(g_clip)); g_ops.push_back(o);
}
static void s_tc(void *, int32_t id, int w, int h, int fmt, uint32_t fl, const uint8_t *px)
{
	Tex t; t.w = w; t.h = h; t.fmt = fmt; t.flags = fl; t.px.assign(px, px + (size_t)w * h * (fmt == FS2_SINK_TEX_A8 ? 1 : 4)); g_tex[id] = t;
}
static void s_tf(void *, int32_t id) { g_tex.erase(id); }
static void s_tfa(void *) { g_tex.clear(); }
static void s_draw(void *, const fs2_sink_draw *d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	Op o; o.kind = 0; o.d = *d; o.v.assign(v, v + nv); o.idx.assign(idx, idx + ni); memcpy(o.clip, g_clip, sizeof(g_clip));
	const Side *s = get_side();
	if (s && s->valid) o.s = *s;
	g_ops.push_back(o);
}
static int s_cap(void *, int32_t) { return 0; }
// the frame so far, D3D fog rules; only the asked region is rasterised
static int s_rr(void *, int x, int y, int w, int h, uint8_t *out)
{
	if (g_w <= 0 || g_h <= 0) { return 0; }
	Options opt; opt.fog = 1; opt.rx0 = x; opt.ry0 = y; opt.rx1 = x + w; opt.ry1 = y + h;
	std::vector<float> fb;
	render(g_w, g_h, g_ops, g_tex, opt, fb);
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			const int sx = x + i, sy = y + j;
			uint8_t *q = out + ((size_t)j * w + i) * 4;
			if (sx < 0 || sy < 0 || sx >= g_w || sy >= g_h) { q[0] = q[1] = q[2] = 0; q[3] = 255; continue; }
			const float *p = &fb[((size_t)sy * g_w + sx) * 3];
			for (int c = 0; c < 3; c++) { q[c] = (uint8_t)std::max(0.0f, std::min(255.0f, p[c] + 0.5f)); }
			q[3] = 255;
		}
	}
	g_reads++; g_reads_frame++;
	return 1;
}
static void s_gam(void *, float) { }

int main(int argc, char **argv)
{
	if (argc < 6) { fprintf(stderr, "usage\n"); return 2; }
	if (argc > 6) g_min_frame = atoi(argv[6]);
	Fs2Api api;
	if (!api.load(argv[1])) { fprintf(stderr, "load fail\n"); return 2; }
	get_side = (const Side *(*)())GetProcAddress(api.module, "fs2_fog_side");
	if (!get_side) { fprintf(stderr, "no side\n"); return 2; }
	fs2_render_sink s = {};
	s.size = sizeof(s); s.version = FS2_RENDER_SINK_VERSION;
	s.frame_begin = s_begin; s.frame_end = s_end; s.clear = s_clear; s.set_clip = s_clip; s.texture_create = s_tc;
	s.texture_free = s_tf; s.texture_free_all = s_tfa; s.draw_tris = s_draw; s.capture_to_texture = s_cap; s.read_region = s_rr; s.set_gamma = s_gam;
	g_csv = fopen(argv[5], "w"); fprintf(g_csv, "frame,ops,zdraws,fogged,pixfog,fogged_add,fogged_2d,read_regions,tris\n");
	if (!api.set_render_sink(&s)) { fprintf(stderr, "sink rejected\n"); return 2; }
	if (!api.init(argv[3], argv[2])) { fprintf(stderr, "init fail\n"); return 1; }
	int r;
	while ((r = api.frame()) > 0) { }
	if (r < 0) fprintf(stderr, "fatal: %s\n", api.last_error());
	api.shutdown();
	fclose(g_csv);
	fprintf(stderr, "frames %d, best frame %d score %d ops %d tex %d size %dx%d, read_regions %d\n", g_frame, g_best_frame, g_best_score, (int)g_best.size(), (int)g_best_tex.size(), g_w, g_h, g_reads);
	FILE *f = fopen(argv[4], "wb");
	int hdr[6] = { 0x31474F46, g_w, g_h, g_best_frame, (int)g_best_tex.size(), (int)g_best.size() };
	fwrite(hdr, 4, 6, f);
	for (auto &kv : g_best_tex) {
		int h2[5] = { kv.first, kv.second.w, kv.second.h, kv.second.fmt, (int)kv.second.flags };
		fwrite(h2, 4, 5, f); fwrite(kv.second.px.data(), 1, kv.second.px.size(), f);
	}
	for (auto &o : g_best) {
		int h3[10] = { o.kind, o.a[0], o.a[1], o.a[2], o.a[3], (int)o.v.size(), o.clip[0], o.clip[1], o.clip[2], o.clip[3] };
		fwrite(h3, 4, 10, f);
		if (o.kind != 0) continue;
		int h4[2] = { (int)o.idx.size(), 0 };
		fwrite(h4, 4, 2, f);
		fwrite(&o.d, sizeof(o.d), 1, f); fwrite(&o.s, sizeof(Side), 1, f);
		fwrite(o.v.data(), sizeof(fs2_sink_vertex), o.v.size(), f); fwrite(o.idx.data(), 2, o.idx.size(), f);
	}
	fclose(f);
	return 0;
}
