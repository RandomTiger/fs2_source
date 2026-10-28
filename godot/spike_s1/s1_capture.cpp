// S-1 spike: runs a replay with a recording sink, writes per-frame stats and dumps the best "ship-heavy" frame
// (draw calls with per-vertex z and depth state from the s1 DLL's side channel, plus the textures it uses).
//   s1_capture <FS2s1 dll> <root> <cmdline> <out dump> <out stats csv> [min_frame]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <vector>
#include "fs2_loader.h"

struct Side { int valid, nv; float z[32]; int zmode, is_scaler, zbias, cull, zclear_seq, zc[4]; };
struct Tex { int w, h, fmt; unsigned flags; std::vector<uint8_t> px; };
struct Op {
	int kind;	// 0 draw, 1 clear, 2 clip
	int a[4]; fs2_sink_draw d; Side side; std::vector<fs2_sink_vertex> v; std::vector<uint16_t> idx;
};

static const Side *(*get_side)() = NULL;
static std::map<int, Tex> g_tex;
static std::vector<Op> g_ops, g_best;
static std::map<int, Tex> g_best_tex;
static int g_w = 0, g_h = 0, g_frame = 0, g_best_score = -1, g_best_frame = -1, g_min_frame = 0;
static FILE *g_csv;

static int s_begin(void *, int w, int h) { g_w = w; g_h = h; g_ops.clear(); return 1; }
static void s_end(void *)
{
	g_frame++;
	int nz = 0, nzr = 0, nui = 0, ntri = 0, nadd = 0;
	for (auto &o : g_ops) if (o.kind == 0) {
		ntri += (int)o.idx.size() / 3;
		if (o.side.valid && o.side.zmode == 2) nz++;
		else if (o.side.valid && o.side.zmode == 1) { nzr++; if (o.d.blend == FS2_SINK_BLEND_ADD) nadd++; }
		else nui++;
	}
	fprintf(g_csv, "%d,%d,%d,%d,%d,%d,%d\n", g_frame, (int)g_ops.size(), nz, nzr, nadd, nui, ntri);
	int score = nz + 2 * nzr + (nz > 8 && nzr > 3 ? 1000 : 0);
	if (g_frame >= g_min_frame && score > g_best_score) {
		g_best_score = score; g_best_frame = g_frame; g_best = g_ops;
		g_best_tex.clear();
		for (auto &o : g_best) if (o.kind == 0 && o.d.texture) g_best_tex[o.d.texture] = g_tex[o.d.texture];
	}
}
static void s_clear(void *, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { Op o = {}; o.kind = 1; o.a[0] = r; o.a[1] = g; o.a[2] = b; o.a[3] = a; g_ops.push_back(o); }
static void s_clip(void *, int x, int y, int w, int h) { Op o = {}; o.kind = 2; o.a[0] = x; o.a[1] = y; o.a[2] = w; o.a[3] = h; g_ops.push_back(o); }
static void s_tc(void *, int32_t id, int w, int h, int fmt, uint32_t fl, const uint8_t *px)
{
	Tex t; t.w = w; t.h = h; t.fmt = fmt; t.flags = fl; t.px.assign(px, px + (size_t)w * h * (fmt == FS2_SINK_TEX_A8 ? 1 : 4)); g_tex[id] = t;
}
static void s_tf(void *, int32_t id) { g_tex.erase(id); }
static void s_tfa(void *) { g_tex.clear(); }
static void s_draw(void *, const fs2_sink_draw *d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	Op o = {}; o.kind = 0; o.d = *d; o.v.assign(v, v + nv); o.idx.assign(idx, idx + ni);
	const Side *s = get_side();
	if (s && s->valid) o.side = *s;
	g_ops.push_back(o);
}
static int s_cap(void *, int32_t) { return 0; }
static int s_rr(void *, int, int, int, int, uint8_t *) { return 0; }
static void s_gam(void *, float) { }

int main(int argc, char **argv)
{
	if (argc < 6) { fprintf(stderr, "usage\n"); return 2; }
	if (argc > 6) g_min_frame = atoi(argv[6]);
	Fs2Api api;
	if (!api.load(argv[1])) { fprintf(stderr, "load fail\n"); return 2; }
	get_side = (const Side *(*)())GetProcAddress(api.module, "fs2_s1_side");
	if (!get_side) { fprintf(stderr, "no side\n"); return 2; }
	fs2_render_sink s = {};
	s.size = sizeof(s); s.version = FS2_RENDER_SINK_VERSION;
	s.frame_begin = s_begin; s.frame_end = s_end; s.clear = s_clear; s.set_clip = s_clip; s.texture_create = s_tc;
	s.texture_free = s_tf; s.texture_free_all = s_tfa; s.draw_tris = s_draw; s.capture_to_texture = s_cap; s.read_region = s_rr; s.set_gamma = s_gam;
	g_csv = fopen(argv[5], "w"); fprintf(g_csv, "frame,ops,zfull,zread,zadd,ui,tris\n");
	if (!api.set_render_sink(&s)) { fprintf(stderr, "sink rejected\n"); return 2; }
	if (!api.init(argv[3], argv[2])) { fprintf(stderr, "init fail\n"); return 1; }
	int r;
	while ((r = api.frame()) > 0) { }
	if (r < 0) fprintf(stderr, "fatal: %s\n", api.last_error());
	api.shutdown();
	fclose(g_csv);
	fprintf(stderr, "frames %d, best frame %d score %d ops %d tex %d size %dx%d\n", g_frame, g_best_frame, g_best_score, (int)g_best.size(), (int)g_best_tex.size(), g_w, g_h);
	FILE *f = fopen(argv[4], "wb");
	int hdr[6] = { 0x31533146, g_w, g_h, g_best_frame, (int)g_best_tex.size(), (int)g_best.size() };
	fwrite(hdr, 4, 6, f);
	for (auto &kv : g_best_tex) {
		int h2[5] = { kv.first, kv.second.w, kv.second.h, kv.second.fmt, (int)kv.second.flags };
		fwrite(h2, 4, 5, f); fwrite(kv.second.px.data(), 1, kv.second.px.size(), f);
	}
	for (auto &o : g_best) {
		int h3[6] = { o.kind, o.a[0], o.a[1], o.a[2], o.a[3], (int)o.v.size() };
		fwrite(h3, 4, 6, f);
		if (o.kind != 0) continue;
		int h4[2] = { (int)o.idx.size(), 0 };
		fwrite(h4, 4, 2, f);
		fwrite(&o.d, sizeof(o.d), 1, f); fwrite(&o.side, sizeof(Side), 1, f);
		fwrite(o.v.data(), sizeof(fs2_sink_vertex), o.v.size(), f); fwrite(o.idx.data(), 2, o.idx.size(), f);
	}
	fclose(f);
	return 0;
}
