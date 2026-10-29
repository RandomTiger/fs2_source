/* batcher.h  (R1-3, depth runs R1-8)
 * Merges the sink's draw_tris() calls into batches: consecutive draws with the same (texture, blend, perspective) become one
 * set of arrays, so a mission frame of thousands of gf_tmapper polys is a few canvas items, not one per poly (D-20261027-190101).
 * Order is exact: only neighbours merge and triangles keep their index order, so the painted result is the one of the
 * unbatched calls. The vertex tint (fs2_sink_draw.tint) is folded into the vertex colour here, so it is not part of the key.
 *
 * The sink flushes the batcher whenever something that is not a draw changes the picture: set_clip, clear, texture
 * create/free, capture, read_region, frame_end.
 *
 * Perspective (S-2, D-20261027-190102): a draw flagged FS2_SINK_DRAWF_PERSPECTIVE whose vertices really differ in sw is
 * perspective batched: custom = (u*sw, v*sw, sw) per vertex and the shader divides per pixel. If all sw are equal, affine
 * interpolation is already exact and the draw joins an ordinary batch. The fallback mode subdivides such triangles on the
 * CPU until the affine error is under a tolerance, and emits plain affine batches.
 *
 * Depth runs (R1-8, sink v2): between set_depth_run(true) and set_depth_run(false) every draw becomes a depth batch
 * (key.depth = 1): the key adds the depth mode of the call, the clear epoch and the z bias, each vertex keeps its z and
 * always carries custom = (u*sw, v*sw, sw), and each triangle is clipped on the CPU to the clip rectangle (a 3D pass
 * has no per item scissor). The sink decides where depth batches are drawn.
 *
 * Fog (R2): a depth draw with fog_mode LINEAR gets key.fog = 1 and fogv = (fog r g b, factor) per vertex, the factor from the
 * vertex z (z * 30000 = the game depth) and the planes of the draw, as D3D vertex fog; the sink mixes it in the shader.
 *
 * Fog gpu (R2 option 5, kNebFog gpu): set_fog_gpu(true, w, h) makes a fogged depth draw key.fog = 2 and fogv = (table offset, nv, factor, 0)
 * per vertex. The table (fog_table(), cleared by the sink per frame) holds, for each vertex of the draw, the 32x32 lookup cell that
 * neb2_get_pixel(int(x), int(y)) starts from (scale, the ys-- quirk and the clamp done here); the sink's shader does the 3x3 average and
 * the per polygon average on the GPU from the pre-render texture.
 *
 * Plain C++17 over render_sink.h: no Godot type, tested against a software reference (sink_check) and timed there.
 */
#ifndef FS2_BATCHER_H
#define FS2_BATCHER_H

#include <cmath>
#include <cstdint>
#include <vector>

#include "render_sink.h"

namespace fs2sink {

struct BatchKey {
	int32_t texture = 0;
	int32_t blend = 0;
	int32_t perspective = 0;	// 1: custom holds (u*sw, v*sw, sw) and the shader divides
	int32_t depth = 0;		// 1: a depth batch (inside a depth run); the fields below are 0 otherwise
	int32_t zmode = 0;		// 0 no test, 1 test, 2 test and write, 3 write only
	int32_t epoch = 0;		// depth clears seen in the run before this batch
	int32_t zbias = 0;
	int32_t fog = 0;		// 1: a depth batch drawn with fog (R2): fogv is filled; 2: the same with the colour looked up on the GPU (fog gpu)
	bool operator==(const BatchKey &o) const
	{
		return texture == o.texture && blend == o.blend && perspective == o.perspective && depth == o.depth && zmode == o.zmode
			&& epoch == o.epoch && zbias == o.zbias && fog == o.fog;
	}
	bool operator!=(const BatchKey &o) const { return !(*this == o); }
};

struct Batch {
	BatchKey key;
	std::vector<float> pos;		// x y per vertex
	std::vector<float> uv;		// u v per vertex
	std::vector<float> col;		// r g b a per vertex, 0..1, tint applied
	std::vector<float> custom;	// u*sw v*sw sw 0 per vertex; only filled when key.perspective
	std::vector<float> z;		// depth 0..1 per vertex; only filled when key.depth
	std::vector<float> fogv;	// fog r g b factor per vertex (factor 1 = unfogged, 0 = all fog colour); only filled when key.fog
	std::vector<int32_t> idx;
	int nverts() const { return (int)(pos.size() / 2); }
	void clear() { pos.clear(); uv.clear(); col.clear(); custom.clear(); z.clear(); fogv.clear(); idx.clear(); }
};

class BatchEmitter {
public:
	virtual ~BatchEmitter() {}
	virtual void emit(const Batch &b) = 0;
};

enum PerspectiveMode {
	PERSPECTIVE_SHADER = 0,		// custom shader divides per pixel (the ruling)
	PERSPECTIVE_SUBDIVIDE = 1	// fallback: split triangles on the CPU, affine UVs
};

struct BatcherStats {
	long draws = 0, triangles_in = 0, triangles_out = 0, batches = 0, perspective_draws = 0;
};

class TriBatcher {
public:
	static const int MAX_VERTS = 60000;

	void set_emitter(BatchEmitter *e) { emitter = e; }
	void set_perspective_mode(PerspectiveMode m) { mode = m; }
	PerspectiveMode perspective_mode() const { return mode; }
	const BatcherStats &stats() const { return st; }
	void reset_stats() { st = BatcherStats(); }

	// Depth run state; the caller flushes first. epoch: depth clears so far in the run. clip: x y w h of later draws.
	void set_depth_run(bool on, int epoch, int cx, int cy, int cw, int ch)
	{
		run = on; run_epoch = epoch;
		clip_x0 = (float)cx; clip_y0 = (float)cy; clip_x1 = (float)(cx + cw); clip_y1 = (float)(cy + ch);
	}
	bool in_depth_run() const { return run; }

	// Fog gpu (see the header): sw/sh = the game screen size (gr_screen.max_w/max_h), ex_scale = 32 / sw as Neb.cpp computes it
	void set_fog_gpu(bool on, int sw, int sh)
	{
		fog_gpu = on;
		fog_ex = 32.0f / (float)(sw > 0 ? sw : 1);
		fog_ey = 32.0f / (float)(sh > 0 ? sh : 1);
	}
	const std::vector<uint8_t> &fog_table() const { return ftab; }	// x y per entry, cell coordinates 0..31
	void fog_table_clear() { ftab.clear(); }

	// tex_w/tex_h: size of the draw's texture in texels (0 = untextured); only the subdivision tolerance uses them.
	void add(const fs2_sink_draw &d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni, int tex_w, int tex_h)
	{
		if (nv <= 0 || ni < 3) { return; }
		if (run) { add_depth(d, v, nv, idx, ni); return; }
		st.draws++;
		st.triangles_in += ni / 3;
		bool persp = false;
		if (d.flags & FS2_SINK_DRAWF_PERSPECTIVE) {
			for (int i = 1; i < nv; i++) {
				if (v[i].sw != v[0].sw) { persp = true; break; }
			}
		}
		if (persp) { st.perspective_draws++; }
		const float tr = d.tint[0] / 255.0f, tg = d.tint[1] / 255.0f, tb = d.tint[2] / 255.0f, ta = d.tint[3] / 255.0f;

		if (persp && mode == PERSPECTIVE_SUBDIVIDE) {
			BatchKey key; key.texture = d.texture; key.blend = d.blend; key.perspective = 0;
			const float tol = (tex_w > 0 && tex_h > 0) ? 0.125f / (float)(tex_w > tex_h ? tex_w : tex_h) : 1.0f;
			for (int t = 0; t + 2 < ni; t += 3) {
				Vtx tri[3];
				for (int k = 0; k < 3; k++) { tri[k] = make(v[idx[t + k]], tr, tg, tb, ta); }
				split(key, tri[0], tri[1], tri[2], tol, 0);
			}
			return;
		}

		BatchKey key; key.texture = d.texture; key.blend = d.blend; key.perspective = persp ? 1 : 0;
		prepare(key, nv);
		const int base = cur.nverts();
		for (int i = 0; i < nv; i++) {
			const Vtx q = make(v[i], tr, tg, tb, ta);
			push_vertex(q, persp);
		}
		for (int i = 0; i < ni; i++) { cur.idx.push_back(base + idx[i]); }
		st.triangles_out += ni / 3;
	}

	void discard() { cur.clear(); }

	void flush()
	{
		if (cur.idx.empty()) { return; }
		st.batches++;
		if (emitter != nullptr) { emitter->emit(cur); }
		cur.clear();
	}

private:
	struct Vtx { float x, y, u, v, sw, r, g, b, a; };

	BatchEmitter *emitter = nullptr;
	PerspectiveMode mode = PERSPECTIVE_SHADER;
	Batch cur;
	BatcherStats st;
	bool fog_gpu = false;
	float fog_ex = 0.0f, fog_ey = 0.0f;
	std::vector<uint8_t> ftab;
	bool run = false;
	int run_epoch = 0;
	float clip_x0 = 0, clip_y0 = 0, clip_x1 = 0, clip_y1 = 0;

	// a vertex of a depth batch: everything that is linear in screen space
	struct DV { float x, y, z, us, vs, sw, r, g, b, a, f; };

	static void clip_edge(std::vector<DV> &poly, std::vector<DV> &tmp, int axis, float val, bool keep_greater)
	{
		tmp.clear();
		const size_t n = poly.size();
		for (size_t i = 0; i < n; i++) {
			const DV &a = poly[i], &b = poly[(i + 1) % n];
			const float av = axis == 0 ? a.x : a.y, bv = axis == 0 ? b.x : b.y;
			const bool ain = keep_greater ? av >= val : av <= val, bin = keep_greater ? bv >= val : bv <= val;
			if (ain) { tmp.push_back(a); }
			if (ain != bin) {
				const float t = (val - av) / (bv - av);
				DV m;
				float *pm = &m.x; const float *pa = &a.x, *pb = &b.x;
				for (int k = 0; k < 11; k++) { pm[k] = pa[k] + (pb[k] - pa[k]) * t; }
				tmp.push_back(m);
			}
		}
		poly.swap(tmp);
	}

	void add_depth(const fs2_sink_draw &d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
	{
		st.draws++;
		st.triangles_in += ni / 3;
		const bool zr = (d.flags & FS2_SINK_DRAWF_ZREAD) != 0, zw = (d.flags & FS2_SINK_DRAWF_ZWRITE) != 0;
		BatchKey key;
		key.texture = d.texture; key.blend = d.blend; key.perspective = 1; key.depth = 1;
		key.zmode = zr ? (zw ? 2 : 1) : (zw ? 3 : 0);
		key.epoch = run_epoch;
		key.zbias = key.zmode != 0 ? d.zbias : 0;
		// fog (R2): linear in the eye depth, per vertex, as D3D vertex fog (gr_d3d_stuff_fog_value); the depth is z * z_mult (30000)
		const bool fogged = d.fog_mode == FS2_SINK_FOG_LINEAR && d.fog_far > d.fog_near;
		key.fog = fogged ? (fog_gpu ? 2 : 1) : 0;
		float tab_off = 0.0f;
		if (fogged && fog_gpu) {
			// the start cell of neb2_get_pixel for each vertex of the poly (the host's tmapper_internal passes (int)x, (int)y), Neb.cpp 1252-1273
			tab_off = (float)(ftab.size() / 2);
			for (int i = 0; i < nv; i++) {
				const float fx = v[i].x, fy = v[i].y;
				const int ix = (fx > -1e9f && fx < 1e9f) ? (int)fx : 0, iy = (fy > -1e9f && fy < 1e9f) ? (int)fy : 0;
				int xs = (int)(fog_ex * (float)ix), ys = (int)(fog_ey * (float)iy);
				if (ys >= 31) { ys--; }
				xs = xs < 0 ? 0 : (xs > 31 ? 31 : xs);
				ys = ys < 0 ? 0 : (ys > 31 ? 31 : ys);
				ftab.push_back((uint8_t)xs); ftab.push_back((uint8_t)ys);
			}
		}
		const float fog_scale = fogged ? 1.0f / (d.fog_far - d.fog_near) : 0.0f;
		const bool persp = (d.flags & FS2_SINK_DRAWF_PERSPECTIVE) != 0;
		const float tr = d.tint[0] / 255.0f, tg = d.tint[1] / 255.0f, tb = d.tint[2] / 255.0f, ta = d.tint[3] / 255.0f;
		std::vector<DV> poly, tmp;
		for (int t = 0; t + 2 < ni; t += 3) {
			poly.clear();
			if (idx[t] >= nv || idx[t + 1] >= nv || idx[t + 2] >= nv) { continue; }
			bool inside = true;
			for (int k = 0; k < 3; k++) {
				const fs2_sink_vertex &s = v[idx[t + k]];
				DV q;
				q.x = s.x; q.y = s.y; q.z = s.z;
				q.sw = (persp && s.sw > 0.0f) ? s.sw : 1.0f;
				q.us = s.u * q.sw; q.vs = s.v * q.sw;
				q.r = s.r / 255.0f * tr; q.g = s.g / 255.0f * tg; q.b = s.b / 255.0f * tb; q.a = s.a / 255.0f * ta;
				q.f = 1.0f;
				if (fogged) {
					q.f = (d.fog_far - s.z * 30000.0f) * fog_scale;
					q.f = q.f < 0.0f ? 0.0f : (q.f > 1.0f ? 1.0f : q.f);
				}
				if (q.x < clip_x0 || q.x > clip_x1 || q.y < clip_y0 || q.y > clip_y1) { inside = false; }
				poly.push_back(q);
			}
			if (!inside) {
				clip_edge(poly, tmp, 0, clip_x0, true);
				if (poly.size() >= 3) { clip_edge(poly, tmp, 0, clip_x1, false); }
				if (poly.size() >= 3) { clip_edge(poly, tmp, 1, clip_y0, true); }
				if (poly.size() >= 3) { clip_edge(poly, tmp, 1, clip_y1, false); }
				if (poly.size() < 3) { continue; }
			}
			prepare(key, (int)poly.size());
			const int base = cur.nverts();
			for (const DV &q : poly) {
				cur.pos.push_back(q.x); cur.pos.push_back(q.y);
				cur.z.push_back(q.z);
				cur.uv.push_back(q.us / q.sw); cur.uv.push_back(q.vs / q.sw);
				cur.col.push_back(q.r); cur.col.push_back(q.g); cur.col.push_back(q.b); cur.col.push_back(q.a);
				cur.custom.push_back(q.us); cur.custom.push_back(q.vs); cur.custom.push_back(q.sw); cur.custom.push_back(0.0f);
				if (fogged && fog_gpu) {
					cur.fogv.push_back(tab_off); cur.fogv.push_back((float)nv); cur.fogv.push_back(q.f); cur.fogv.push_back(0.0f);
				} else if (fogged) {
					cur.fogv.push_back(d.fog_rgb[0] / 255.0f); cur.fogv.push_back(d.fog_rgb[1] / 255.0f); cur.fogv.push_back(d.fog_rgb[2] / 255.0f);
					cur.fogv.push_back(q.f);
				}
			}
			for (size_t k = 1; k + 1 < poly.size(); k++) {
				cur.idx.push_back(base); cur.idx.push_back(base + (int)k); cur.idx.push_back(base + (int)k + 1);
				st.triangles_out++;
			}
		}
	}

	static Vtx make(const fs2_sink_vertex &s, float tr, float tg, float tb, float ta)
	{
		Vtx q;
		q.x = s.x; q.y = s.y; q.u = s.u; q.v = s.v; q.sw = s.sw;
		q.r = s.r / 255.0f * tr; q.g = s.g / 255.0f * tg; q.b = s.b / 255.0f * tb; q.a = s.a / 255.0f * ta;
		return q;
	}

	void prepare(const BatchKey &key, int nv_more)
	{
		if (!cur.idx.empty() && (cur.key != key || cur.nverts() + nv_more > MAX_VERTS)) { flush(); }
		if (cur.idx.empty()) { cur.key = key; }
	}

	void push_vertex(const Vtx &q, bool persp)
	{
		cur.pos.push_back(q.x); cur.pos.push_back(q.y);
		cur.uv.push_back(q.u); cur.uv.push_back(q.v);
		cur.col.push_back(q.r); cur.col.push_back(q.g); cur.col.push_back(q.b); cur.col.push_back(q.a);
		if (persp) {
			cur.custom.push_back(q.u * q.sw); cur.custom.push_back(q.v * q.sw); cur.custom.push_back(q.sw); cur.custom.push_back(0.0f);
		}
	}

	// midpoint of a and b as a perspective-correct vertex: sw and u*sw are linear in screen space
	static Vtx mid(const Vtx &a, const Vtx &b)
	{
		Vtx m;
		m.x = 0.5f * (a.x + b.x); m.y = 0.5f * (a.y + b.y);
		m.sw = 0.5f * (a.sw + b.sw);
		const float us = 0.5f * (a.u * a.sw + b.u * b.sw), vs = 0.5f * (a.v * a.sw + b.v * b.sw);
		m.u = m.sw > 0 ? us / m.sw : 0.5f * (a.u + b.u);
		m.v = m.sw > 0 ? vs / m.sw : 0.5f * (a.v + b.v);
		m.r = 0.5f * (a.r + b.r); m.g = 0.5f * (a.g + b.g); m.b = 0.5f * (a.b + b.b); m.a = 0.5f * (a.a + b.a);
		return m;
	}

	// how far the affine midpoint UV is from the perspective-correct one, over an edge
	static float edge_error(const Vtx &a, const Vtx &b)
	{
		const Vtx m = mid(a, b);
		const float du = std::fabs(m.u - 0.5f * (a.u + b.u)), dv = std::fabs(m.v - 0.5f * (a.v + b.v));
		return du > dv ? du : dv;
	}

	static float len2(const Vtx &a, const Vtx &b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }

	// error of the affine uv at the centroid: edge midpoints alone miss the inside of a long thin triangle
	static float centroid_error(const Vtx &a, const Vtx &b, const Vtx &c)
	{
		const float sw = (a.sw + b.sw + c.sw) / 3.0f;
		if (!(sw > 0)) { return 0; }
		const float u = (a.u * a.sw + b.u * b.sw + c.u * c.sw) / 3.0f / sw, v = (a.v * a.sw + b.v * b.sw + c.v * c.sw) / 3.0f / sw;
		const float du = std::fabs(u - (a.u + b.u + c.u) / 3.0f), dv = std::fabs(v - (a.v + b.v + c.v) / 3.0f);
		return du > dv ? du : dv;
	}

	void split(const BatchKey &key, const Vtx &a, const Vtx &b, const Vtx &c, float tol, int depth)
	{
		const float e0 = edge_error(a, b), e1 = edge_error(b, c), e2 = edge_error(c, a), ec = centroid_error(a, b, c);
		float worst = e0; int which = 0;
		if (e1 > worst) { worst = e1; which = 1; }
		if (e2 > worst) { worst = e2; which = 2; }
		if (ec > worst) {	// the middle is the worst: cut the longest edge
			worst = ec;
			const float l0 = len2(a, b), l1 = len2(b, c), l2 = len2(c, a);
			which = (l0 >= l1 && l0 >= l2) ? 0 : (l1 >= l2 ? 1 : 2);
		}
		const float longest = std::fmax(len2(a, b), std::fmax(len2(b, c), len2(c, a)));
		if (worst <= tol || depth >= 8 || longest < 4.0f) {
			prepare(key, 3);
			const int base = cur.nverts();
			push_vertex(a, false); push_vertex(b, false); push_vertex(c, false);
			cur.idx.push_back(base); cur.idx.push_back(base + 1); cur.idx.push_back(base + 2);
			st.triangles_out++;
			return;
		}
		// bisect the edge with the largest error; the two halves keep the winding
		if (which == 0) { const Vtx m = mid(a, b); split(key, a, m, c, tol, depth + 1); split(key, m, b, c, tol, depth + 1); }
		else if (which == 1) { const Vtx m = mid(b, c); split(key, a, b, m, tol, depth + 1); split(key, a, m, c, tol, depth + 1); }
		else { const Vtx m = mid(c, a); split(key, a, b, m, tol, depth + 1); split(key, m, b, c, tol, depth + 1); }
	}
};

} // namespace fs2sink

#endif
