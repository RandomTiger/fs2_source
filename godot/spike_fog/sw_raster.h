// Fog spike: the dumped frame's types and a software rasteriser with D3D semantics (S-1's reference plus fog), shared by
// fog_capture (read_region during the capture run) and the Godot test extension (the reference image).
//   fog 0  no fog
//   fog 1  D3D vertex fog: the per-vertex factor f (specular alpha) interpolated over the triangle, rgb = f*src + (1-f)*fog
//          before blending, for every blend mode (D3D fogs additive draws too)
//   fog 2  per-pixel factor from the interpolated view z (the clamp at the planes happens per pixel)
//   linear_mix  mix source and fog colour in linear light instead of the framebuffer's gamma values
#pragma once
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include "render_sink.h"

namespace fogspike {

// must match Fs2S1Side in gr_host_fog.cpp
struct Side { int valid, nv; float z[32]; int zmode, is_scaler, zbias, cull, zclear_seq, zc[4];
	float f[32]; float vz[32]; int fog_loop, pixfog, tflags, fogged_2d; };
struct Tex { int w = 0, h = 0, fmt = 0; uint32_t flags = 0; std::vector<uint8_t> px; };
struct Op {
	int kind = 0;		// 0 draw, 1 clear, 2 clip
	int a[4] = {0, 0, 0, 0};
	fs2_sink_draw d = {};
	Side s = {};
	std::vector<fs2_sink_vertex> v;
	std::vector<uint16_t> idx;
	int clip[4] = {0, 0, 1024, 768};	// at the time of the op
};

struct Options { int fog = 1; bool linear_mix = false; int rx0 = 0, ry0 = 0, rx1 = 1 << 30, ry1 = 1 << 30; };	// region [rx0,rx1) x [ry0,ry1)

inline float s2l(float c) { c /= 255.0f; return 255.0f * (c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f)); }
inline float l2s(float c) { c /= 255.0f; c = std::max(0.0f, std::min(1.0f, c)); return 255.0f * (c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f); }

// fb: W*H*3 floats 0..255
inline void render(int W, int H, const std::vector<Op> &ops, const std::map<int, Tex> &texs, const Options &opt, std::vector<float> &fb)
{
	fb.assign((size_t)W * H * 3, 0.0f);
	std::vector<float> depth((size_t)W * H, 1.0f);
	const int RX0 = std::max(0, opt.rx0), RY0 = std::max(0, opt.ry0), RX1 = std::min(W, opt.rx1), RY1 = std::min(H, opt.ry1);
	int lastseq = -1;
	for (const Op &o : ops) {
		if (o.kind == 1) {
			for (int y = RY0; y < RY1; y++) { for (int x = RX0; x < RX1; x++) { float *p = &fb[((size_t)y * W + x) * 3]; p[0] = (float)o.a[0]; p[1] = (float)o.a[1]; p[2] = (float)o.a[2]; } }
			continue;
		}
		if (o.kind != 0) { continue; }
		const bool zt = o.s.valid && o.s.zmode > 0;
		if (zt && o.s.zclear_seq != lastseq) {
			lastseq = o.s.zclear_seq;
			for (int y = std::max(0, o.s.zc[1]); y <= std::min(H - 1, o.s.zc[3]); y++) { for (int x = std::max(0, o.s.zc[0]); x <= std::min(W - 1, o.s.zc[2]); x++) { depth[(size_t)y * W + x] = 1.0f; } }
		}
		if (o.clip[2] <= 0 || o.clip[3] <= 0) { continue; }
		const Tex *t = nullptr;
		if (o.d.texture) { auto it = texs.find(o.d.texture); if (it == texs.end()) { continue; } t = &it->second; }
		const bool persp = (o.d.flags & FS2_SINK_DRAWF_PERSPECTIVE) != 0;
		const bool fogged = opt.fog != 0 && o.d.fog_mode == FS2_SINK_FOG_LINEAR && o.s.valid;
		float fogc[3] = { (float)o.d.fog_rgb[0], (float)o.d.fog_rgb[1], (float)o.d.fog_rgb[2] };
		if (fogged && opt.linear_mix) { for (float &c : fogc) { c = s2l(c); } }
		const float fden = o.d.fog_far - o.d.fog_near;
		const int cx0 = std::max(RX0, o.clip[0]), cy0 = std::max(RY0, o.clip[1]), cx1 = std::min(RX1, o.clip[0] + o.clip[2]), cy1 = std::min(RY1, o.clip[1] + o.clip[3]);
		if (cx0 >= cx1 || cy0 >= cy1) { continue; }
		for (size_t ti = 0; ti + 2 < o.idx.size(); ti += 3) {
			const fs2_sink_vertex *p[3] = { &o.v[o.idx[ti]], &o.v[o.idx[ti + 1]], &o.v[o.idx[ti + 2]] };
			const float area = (p[1]->x - p[0]->x) * (p[2]->y - p[0]->y) - (p[2]->x - p[0]->x) * (p[1]->y - p[0]->y);
			if (std::fabs(area) < 1e-6f) { continue; }
			const float minx = std::min({ p[0]->x, p[1]->x, p[2]->x }), maxx = std::max({ p[0]->x, p[1]->x, p[2]->x });
			const float miny = std::min({ p[0]->y, p[1]->y, p[2]->y }), maxy = std::max({ p[0]->y, p[1]->y, p[2]->y });
			const int x0 = std::max(cx0, (int)std::floor(minx)), x1 = std::min(cx1 - 1, (int)std::ceil(maxx));
			const int y0 = std::max(cy0, (int)std::floor(miny)), y1 = std::min(cy1 - 1, (int)std::ceil(maxy));
			float zz[3], sw[3], ff[3], vz[3];
			for (int k = 0; k < 3; k++) {
				const int vi = o.idx[ti + k];
				zz[k] = (zt && vi < 32) ? o.s.z[vi] : 0.0f; sw[k] = persp ? p[k]->sw : 1.0f;
				ff[k] = vi < 32 ? o.s.f[vi] : 1.0f; vz[k] = vi < 32 ? o.s.vz[vi] : 0.0f;
			}
			for (int y = y0; y <= y1; y++) {
				for (int x = x0; x <= x1; x++) {
					const float px = x + 0.5f, py = y + 0.5f;
					const float w0 = ((p[1]->x - px) * (p[2]->y - py) - (p[2]->x - px) * (p[1]->y - py)) / area;
					const float w1 = ((p[2]->x - px) * (p[0]->y - py) - (p[0]->x - px) * (p[2]->y - py)) / area;
					const float w2 = 1.0f - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) { continue; }
					const size_t pi = (size_t)y * W + x;
					const float z = w0 * zz[0] + w1 * zz[1] + w2 * zz[2];
					if (zt && z > depth[pi]) { continue; }
					const float swi = w0 * sw[0] + w1 * sw[1] + w2 * sw[2];
					const float u = (w0 * p[0]->u * sw[0] + w1 * p[1]->u * sw[1] + w2 * p[2]->u * sw[2]) / swi;
					const float v = (w0 * p[0]->v * sw[0] + w1 * p[1]->v * sw[1] + w2 * p[2]->v * sw[2]) / swi;
					float cr = w0 * p[0]->r + w1 * p[1]->r + w2 * p[2]->r, cg = w0 * p[0]->g + w1 * p[1]->g + w2 * p[2]->g;
					float cb = w0 * p[0]->b + w1 * p[1]->b + w2 * p[2]->b, ca = w0 * p[0]->a + w1 * p[1]->a + w2 * p[2]->a;
					cr = cr * o.d.tint[0] / 255.0f; cg = cg * o.d.tint[1] / 255.0f; cb = cb * o.d.tint[2] / 255.0f; ca = ca * o.d.tint[3] / 255.0f / 255.0f;
					if (t) {
						const bool rep = (t->flags & FS2_SINK_TEXF_REPEAT) != 0, lin = (t->flags & FS2_SINK_TEXF_LINEAR) != 0;
						auto fetch = [&](int tx, int ty, float *rgba) {
							if (rep) { tx = ((tx % t->w) + t->w) % t->w; ty = ((ty % t->h) + t->h) % t->h; } else { tx = std::max(0, std::min(t->w - 1, tx)); ty = std::max(0, std::min(t->h - 1, ty)); }
							if (t->fmt == FS2_SINK_TEX_A8) { rgba[0] = rgba[1] = rgba[2] = 255.0f; rgba[3] = t->px[(size_t)ty * t->w + tx]; }
							else { const uint8_t *q = &t->px[((size_t)ty * t->w + tx) * 4]; rgba[0] = q[0]; rgba[1] = q[1]; rgba[2] = q[2]; rgba[3] = q[3]; }
						};
						float tc[4];
						if (!lin) { fetch((int)std::floor(u * t->w), (int)std::floor(v * t->h), tc); }
						else {
							const float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f;
							const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
							const float ax = fx - ix, ay = fy - iy;
							float a[4], b[4], c[4], d[4];
							fetch(ix, iy, a); fetch(ix + 1, iy, b); fetch(ix, iy + 1, c); fetch(ix + 1, iy + 1, d);
							for (int k = 0; k < 4; k++) { tc[k] = (a[k] * (1 - ax) + b[k] * ax) * (1 - ay) + (c[k] * (1 - ax) + d[k] * ax) * ay; }
						}
						cr *= tc[0] / 255.0f; cg *= tc[1] / 255.0f; cb *= tc[2] / 255.0f; ca *= tc[3] / 255.0f;
					}
					if (fogged) {
						float f;
						if (opt.fog == 2) {
							const float zi = w0 * vz[0] + w1 * vz[1] + w2 * vz[2];
							f = fden != 0.0f ? (o.d.fog_far - zi) / fden : 0.0f;
							f = std::max(0.0f, std::min(1.0f, f));
						} else {
							f = w0 * ff[0] + w1 * ff[1] + w2 * ff[2];
						}
						if (opt.linear_mix) {
							cr = l2s(f * s2l(cr) + (1 - f) * fogc[0]); cg = l2s(f * s2l(cg) + (1 - f) * fogc[1]); cb = l2s(f * s2l(cb) + (1 - f) * fogc[2]);
						} else {
							cr = f * cr + (1 - f) * fogc[0]; cg = f * cg + (1 - f) * fogc[1]; cb = f * cb + (1 - f) * fogc[2];
						}
					}
					float *dst = &fb[pi * 3];
					if (o.d.blend == FS2_SINK_BLEND_OPAQUE) { dst[0] = cr; dst[1] = cg; dst[2] = cb; }
					else if (o.d.blend == FS2_SINK_BLEND_ADD) { dst[0] = std::min(255.0f, dst[0] + cr * ca); dst[1] = std::min(255.0f, dst[1] + cg * ca); dst[2] = std::min(255.0f, dst[2] + cb * ca); }
					else { dst[0] = cr * ca + dst[0] * (1 - ca); dst[1] = cg * ca + dst[1] * (1 - ca); dst[2] = cb * ca + dst[2] * (1 - ca); }
					if (o.s.valid && o.s.zmode == 2) { depth[pi] = z; }
				}
			}
		}
	}
}

inline void to_rgb8(const std::vector<float> &fb, std::vector<uint8_t> &out)
{
	out.resize(fb.size());
	for (size_t i = 0; i < out.size(); i++) { out[i] = (uint8_t)std::max(0.0f, std::min(255.0f, fb[i] + 0.5f)); }
}

}
