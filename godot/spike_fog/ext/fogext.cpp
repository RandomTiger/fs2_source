// Fog spike: replays one captured fogged FS2 frame (../fog_capture.cpp) in Godot, in S-1's option 3 layout (back canvas,
// SubViewport with an ortho Camera3D for the depth-tested draws, front canvas), with fog in the shaders, and compares each
// against a software reference with D3D vertex fog (../sw_raster.h). Modes (S1_MODES style list in FOG_MODES):
//   ref        software, D3D vertex fog: per-vertex f interpolated, mixed with the fog colour in gamma (framebuffer) values
//   ref_px     software, f per pixel from the interpolated view z (clamp per pixel)
//   ref_lin    software, mix in linear light
//   ref_nofog  software, no fog
//   canvas     everything on the canvas (no depth), fog in the canvas shader
//   opt3       option 3, fog per vertex (f and fog colour in CUSTOM1), mix in gamma values before s2l
//   opt3px     option 3, unclamped f per vertex, clamped per fragment (what the sink gets by computing f from z)
//   opt3lin    option 3, mix in linear light
//   opt3draw   option 3, fog colour per draw: batches split on fog colour (same shader), for the batch count
//   opt3nofog  option 3 without fog, as a baseline
// Env: FOG_DUMP, FOG_OUT, FOG_MODES. Screenshots and diffs go to FOG_OUT (scratch; never committed).
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "sw_raster.h"

using namespace godot;
using fogspike::Op;
using fogspike::Tex;

namespace {

// c0..c2 = custom0 (u*sw, v*sw, sw); f, fr, fg, fb = custom1 (fog factor, fog colour 0..1)
struct Vtx { float x, y, z, c0, c1, c2, r, g, b, a, f, fr, fg, fb; };
const int VTX_FLOATS = 14;
struct Batch {
	int tex = 0, blend = 0, persp = 0, zmode = 0, epoch = 0, zbias = 0, fogged = 0;
	uint32_t fogkey = 0;
	int clip[4] = {0, 0, 1024, 768};
	bool a8 = false;
	uint32_t texflags = 0;
	std::vector<Vtx> v;
	std::vector<int> idx;
};
struct FogVar { int fog = 1; bool px = false, lin = false, split = false, gam = false; };	// gam: the 3D layer blends gamma values (ALBEDO = raw colour, s2l on the composite)

}

class FogSpike : public Node2D {
	GDCLASS(FogSpike, Node2D)
public:
	static void _bind_methods() {}
	void _ready() override;
	void _process(double delta) override;
	void _exit_tree() override { teardown(); }

private:
	int W = 1024, H = 768;
	std::vector<Op> ops;
	std::map<int, Tex> texs;
	std::map<int, Ref<ImageTexture>> gtex;
	std::string out_dir;
	std::vector<std::string> modes;
	int mode_index = -1, phase = 0, wait = 0, samples = 0, measure_left = 0;
	double gpu_sum = 0, cpu_sum = 0, sub_gpu_sum = 0, sub_cpu_sum = 0;
	double build_us = 0, submit_us = 0;
	int nbatches = 0, nbatches3d = 0, ncanvas_batches = 0;
	std::string cur_mode;
	FogVar var;
	float sort_scale = -1.0f;
	bool no_persp = false, dbg = false;
	int opdbg = -1, draw_idx_mode = 0, draw_seq = 0;

	std::vector<RID> items, meshes, instances, shaders, materials;
	std::map<uint32_t, RID> shader_map;
	std::map<uint64_t, RID> mat_map;
	RID white_tex_rid;
	Ref<ImageTexture> white_tex;
	SubViewport *sub = nullptr;
	Camera3D *sub_cam = nullptr;
	std::vector<uint8_t> ref_rgb;
	bool have_ref = false;

	bool load_dump(const std::string &path);
	void analyse();
	void start_mode(const std::string &m);
	void finish_mode();
	void teardown();
	void save_rgb(const std::vector<uint8_t> &rgb, const std::string &name);
	void compare_rgb(const std::vector<uint8_t> &rgb, const std::string &name);
	std::vector<Batch> make_batches(int first, int last, bool clip3d, int *epochs_out);
	RID canvas_material(int blend, bool a8, bool mesh, bool fog);
	RID spatial_material(int blend, int zmode, bool a8, uint32_t texflags, int tex);
	void emit_canvas(const std::vector<Batch> &bs, RID parent);
	void emit_3d(const std::vector<Batch> &bs, RID scenario, int epochs);
};

// ---- dump ---------------------------------------------------------------------------------------------------

bool FogSpike::load_dump(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) { return false; }
	int hdr[6];
	if (fread(hdr, 4, 6, f) != 6 || hdr[0] != 0x31474F46) { fclose(f); return false; }
	W = hdr[1]; H = hdr[2];
	const int ntex = hdr[4], nops = hdr[5];
	for (int i = 0; i < ntex; i++) {
		int h2[5];
		if (fread(h2, 4, 5, f) != 5) { fclose(f); return false; }
		Tex t; t.w = h2[1]; t.h = h2[2]; t.fmt = h2[3]; t.flags = (uint32_t)h2[4];
		t.px.resize((size_t)t.w * t.h * (t.fmt == FS2_SINK_TEX_A8 ? 1 : 4));
		if (fread(t.px.data(), 1, t.px.size(), f) != t.px.size()) { fclose(f); return false; }
		texs[h2[0]] = std::move(t);
	}
	for (int i = 0; i < nops; i++) {
		int h3[10];
		if (fread(h3, 4, 10, f) != 10) { fclose(f); return false; }
		Op o; o.kind = h3[0]; for (int k = 0; k < 4; k++) { o.a[k] = h3[1 + k]; o.clip[k] = h3[6 + k]; }
		if (o.kind == 0) {
			int h4[2];
			if (fread(h4, 4, 2, f) != 2) { fclose(f); return false; }
			if (fread(&o.d, sizeof(o.d), 1, f) != 1 || fread(&o.s, sizeof(o.s), 1, f) != 1) { fclose(f); return false; }
			o.v.resize(h3[5]); o.idx.resize(h4[0]);
			if (fread(o.v.data(), sizeof(fs2_sink_vertex), o.v.size(), f) != o.v.size() || fread(o.idx.data(), 2, o.idx.size(), f) != o.idx.size()) { fclose(f); return false; }
		}
		ops.push_back(std::move(o));
	}
	fclose(f);
	return true;
}

static bool zdraw(const Op &o) { return o.kind == 0 && o.s.valid && o.s.zmode > 0; }

void FogSpike::analyse()
{
	int first_z = -1, last_z = -1;
	for (int i = 0; i < (int)ops.size(); i++) { if (zdraw(ops[i])) { if (first_z < 0) { first_z = i; } last_z = i; } }
	int nd = 0, nfog = 0, nfog_back = 0, nfog_front = 0, nfog_noz = 0, nfog_add = 0, nfog_alpha = 0, nfog_opq = 0, npix = 0, nclamp = 0, nfog_tex = 0;
	int ntris_fog = 0;
	std::set<uint32_t> cols; std::set<std::pair<float, float>> planes;
	float fmin = 1e9f, fmax = -1e9f, zmin = 1e9f, zmax = -1e9f;
	for (int i = 0; i < (int)ops.size(); i++) {
		const Op &o = ops[i];
		if (o.kind != 0) { continue; }
		nd++;
		if (o.d.fog_mode == FS2_SINK_FOG_NONE) { continue; }
		nfog++; ntris_fog += (int)o.idx.size() / 3;
		if (i < first_z) { nfog_back++; } else if (i > last_z) { nfog_front++; }
		if (!zdraw(o)) { nfog_noz++; }
		if (o.d.blend == FS2_SINK_BLEND_ADD) { nfog_add++; } else if (o.d.blend == FS2_SINK_BLEND_ALPHA) { nfog_alpha++; } else { nfog_opq++; }
		if (o.d.texture) { nfog_tex++; }
		if (o.s.pixfog) { npix++; }
		cols.insert((uint32_t)o.d.fog_rgb[0] | ((uint32_t)o.d.fog_rgb[1] << 8) | ((uint32_t)o.d.fog_rgb[2] << 16));
		planes.insert({ o.d.fog_near, o.d.fog_far });
		bool clamp = false;
		const float den = o.d.fog_far - o.d.fog_near;
		for (int k = 0; k < (int)o.v.size() && k < 32; k++) {
			const float fu = den != 0.0f ? (o.d.fog_far - o.s.vz[k]) / den : 0.0f;
			if (fu < 0.0f || fu > 1.0f) { clamp = true; }
			fmin = std::min(fmin, o.s.f[k]); fmax = std::max(fmax, o.s.f[k]); zmin = std::min(zmin, o.s.vz[k]); zmax = std::max(zmax, o.s.vz[k]);
		}
		if (clamp) { nclamp++; }
	}
	UtilityFunctions::print("FOG frame: ", (int64_t)ops.size(), " ops, ", nd, " draws, first z op ", first_z, ", last z op ", last_z, "; fogged ", nfog, " (", ntris_fog, " tris; pixel fog ", npix,
		"; textured ", nfog_tex, "; alpha ", nfog_alpha, ", add ", nfog_add, ", opaque ", nfog_opq, "; not depth-tested ", nfog_noz, "; before the 3D range ", nfog_back, ", after ", nfog_front, ")");
	UtilityFunctions::print("FOG fog colours ", (int64_t)cols.size(), ", plane pairs ", (int64_t)planes.size(), "; vertex f ", fmin, "..", fmax, ", view z ", zmin, "..", zmax,
		"; fogged draws with a vertex outside the planes (clamp inside the poly) ", nclamp);
	for (auto &p : planes) { UtilityFunctions::print("FOG planes near ", p.first, " far ", p.second); }
	// FOG_PROBE=x,y: every draw covering that pixel centre, in op order
	String pr = OS::get_singleton()->get_environment("FOG_PROBE");
	if (!pr.is_empty()) {
		const float px = (float)pr.get_slice(",", 0).to_float() + 0.5f, py = (float)pr.get_slice(",", 1).to_float() + 0.5f;
		for (int i = 0; i < (int)ops.size(); i++) {
			const Op &o = ops[i];
			if (o.kind != 0) { continue; }
			for (size_t t = 0; t + 2 < o.idx.size(); t += 3) {
				const fs2_sink_vertex *p[3] = { &o.v[o.idx[t]], &o.v[o.idx[t + 1]], &o.v[o.idx[t + 2]] };
				const float area = (p[1]->x - p[0]->x) * (p[2]->y - p[0]->y) - (p[2]->x - p[0]->x) * (p[1]->y - p[0]->y);
				if (std::fabs(area) < 1e-6f) { continue; }
				const float w0 = ((p[1]->x - px) * (p[2]->y - py) - (p[2]->x - px) * (p[1]->y - py)) / area;
				const float w1 = ((p[2]->x - px) * (p[0]->y - py) - (p[0]->x - px) * (p[2]->y - py)) / area;
				const float w2 = 1.0f - w0 - w1;
				if (w0 < 0 || w1 < 0 || w2 < 0) { continue; }
				const int i0 = o.idx[t], i1 = o.idx[t + 1], i2 = o.idx[t + 2];
				const float z = (i0 < 32 && i1 < 32 && i2 < 32) ? w0 * o.s.z[i0] + w1 * o.s.z[i1] + w2 * o.s.z[i2] : -1.0f;
				const float f = (i0 < 32 && i1 < 32 && i2 < 32) ? w0 * o.s.f[i0] + w1 * o.s.f[i1] + w2 * o.s.f[i2] : -1.0f;
				char buf[400];
				snprintf(buf, sizeof(buf), "FOG probe op %d: blend %d tex %d flags %x zvalid %d zmode %d zbias %d zseq %d z %.4f fog %d f %.3f rgb %d,%d,%d tint %d,%d,%d,%d vcol %d,%d,%d,%d pixfog %d",
					i, o.d.blend, o.d.texture, o.d.flags, o.s.valid, o.s.zmode, o.s.zbias, o.s.zclear_seq, z, o.d.fog_mode, f, o.d.fog_rgb[0], o.d.fog_rgb[1], o.d.fog_rgb[2],
					o.d.tint[0], o.d.tint[1], o.d.tint[2], o.d.tint[3], p[0]->r, p[0]->g, p[0]->b, p[0]->a, o.s.pixfog);
				UtilityFunctions::print(buf);
				UtilityFunctions::print("FOG probe   nv ", (int64_t)o.v.size(), " ni ", (int64_t)o.idx.size(), " tri ", (int64_t)t, " clip ", o.clip[0], ",", o.clip[1], ",", o.clip[2], ",", o.clip[3], " xy ", p[0]->x, ",", p[0]->y, " ", p[1]->x, ",", p[1]->y, " ", p[2]->x, ",", p[2]->y);
				auto tit = texs.find(o.d.texture);
				if (tit != texs.end() && tit->second.fmt != FS2_SINK_TEX_A8) {
					const Tex &tx = tit->second;
					const float u = w0 * p[0]->u + w1 * p[1]->u + w2 * p[2]->u, v = w0 * p[0]->v + w1 * p[1]->v + w2 * p[2]->v;
					int ix = (int)std::floor(u * tx.w), iy = (int)std::floor(v * tx.h); ix = ((ix % tx.w) + tx.w) % tx.w; iy = ((iy % tx.h) + tx.h) % tx.h;
					const uint8_t *q = &tx.px[((size_t)iy * tx.w + ix) * 4];
					UtilityFunctions::print("FOG probe   texel ", q[0], ",", q[1], ",", q[2], ",", q[3], " size ", tx.w, "x", tx.h, " uv ", u, ",", v, " sw ", p[0]->sw, ",", p[1]->sw, ",", p[2]->sw);
				}
				break;
			}
		}
	}
}

// ---- batching -----------------------------------------------------------------------------------------------

static void clip_poly(std::vector<Vtx> &poly, int axis, float val, bool keep_greater)
{
	std::vector<Vtx> out;
	const size_t n = poly.size();
	for (size_t i = 0; i < n; i++) {
		const Vtx &a = poly[i], &b = poly[(i + 1) % n];
		const float av = axis == 0 ? a.x : a.y, bv = axis == 0 ? b.x : b.y;
		const bool ain = keep_greater ? av >= val : av <= val, bin = keep_greater ? bv >= val : bv <= val;
		if (ain) { out.push_back(a); }
		if (ain != bin) {
			const float t = (val - av) / (bv - av);
			Vtx m;
			float *pm = &m.x; const float *pa = &a.x, *pb = &b.x;
			for (int k = 0; k < VTX_FLOATS; k++) { pm[k] = pa[k] + (pb[k] - pa[k]) * t; }
			out.push_back(m);
		}
	}
	poly.swap(out);
}

std::vector<Batch> FogSpike::make_batches(int first, int last, bool clip3d, int *epochs_out)
{
	std::vector<Batch> out;
	int epoch = 0, lastseq = -1;
	for (int i = first; i <= last && i < (int)ops.size(); i++) {
		const Op &o = ops[i];
		if (o.kind != 0) { continue; }
		if (o.d.blend == FS2_SINK_BLEND_OPAQUE && o.d.texture == 0 && o.clip[2] >= W && o.clip[3] >= H) { continue; }	// the frame clear (S-1)
		const bool zt = zdraw(o);
		if (zt && o.s.zclear_seq != lastseq) { if (lastseq != -1) { epoch++; } lastseq = o.s.zclear_seq; }
		if (o.clip[2] <= 0 || o.clip[3] <= 0) { continue; }
		auto tit = texs.find(o.d.texture);
		if (o.d.texture != 0 && tit == texs.end()) { continue; }
		const bool a8 = o.d.texture != 0 && tit->second.fmt == FS2_SINK_TEX_A8;
		const uint32_t tfl = o.d.texture != 0 ? tit->second.flags : 0;
		const int persp = (!no_persp && (o.d.flags & FS2_SINK_DRAWF_PERSPECTIVE)) ? 1 : 0;
		const int zm = zt ? o.s.zmode : 0;
		const int fogged = (var.fog && o.d.fog_mode != FS2_SINK_FOG_NONE) ? 1 : 0;
		const uint32_t fogkey = (var.split && fogged) ? (0x1000000u | (uint32_t)o.d.fog_rgb[0] | ((uint32_t)o.d.fog_rgb[1] << 8) | ((uint32_t)o.d.fog_rgb[2] << 16)) : 0u;
		Batch *b = out.empty() ? nullptr : &out.back();
		// fogged and unfogged draws share a batch on the 3D layer (unfogged carry f = 1); on the canvas a fogged batch needs the mesh path
		const bool fog_split_canvas = !clip3d && b && b->fogged != fogged;
		if (!b || b->tex != o.d.texture || b->blend != o.d.blend || b->persp != persp || b->zmode != zm || b->epoch != epoch || b->zbias != (zt ? o.s.zbias : 0)
			|| std::memcmp(b->clip, o.clip, sizeof(b->clip)) != 0 || b->fogkey != fogkey || fog_split_canvas) {
			out.emplace_back();
			b = &out.back();
			b->tex = o.d.texture; b->blend = o.d.blend; b->persp = persp; b->zmode = zm; b->epoch = epoch; b->zbias = zt ? o.s.zbias : 0;
			std::memcpy(b->clip, o.clip, sizeof(b->clip)); b->a8 = a8; b->texflags = tfl; b->fogkey = fogkey; b->fogged = fogged;
		}
		b->fogged |= fogged;
		if (i == opdbg) { UtilityFunctions::print("FOG opdbg op ", i, " -> batch ", (int64_t)out.size() - 1, " blend ", b->blend, " tex ", b->tex, " fogged ", b->fogged, " verts so far ", (int64_t)b->v.size(), " a ", o.v[0].a, " tint a ", o.d.tint[3]); }
		const int nv = (int)o.v.size();
		const float den = o.d.fog_far - o.d.fog_near;
		std::vector<Vtx> vv(nv);
		for (int k = 0; k < nv; k++) {
			const fs2_sink_vertex &s = o.v[k];
			Vtx &d = vv[k];
			d.x = s.x; d.y = s.y;
			d.z = (zt && k < 32) ? o.s.z[k] : 0.0f;
			const float sw = persp ? s.sw : 1.0f;
			d.c0 = s.u * sw; d.c1 = s.v * sw; d.c2 = sw;
			d.r = s.r * o.d.tint[0] / 255.0f / 255.0f; d.g = s.g * o.d.tint[1] / 255.0f / 255.0f; d.b = s.b * o.d.tint[2] / 255.0f / 255.0f; d.a = s.a * o.d.tint[3] / 255.0f / 255.0f;
			if (fogged && k < 32) {
				d.f = var.px ? (den != 0.0f ? (o.d.fog_far - o.s.vz[k]) / den : 0.0f) : o.s.f[k];
				d.fr = o.d.fog_rgb[0] / 255.0f; d.fg = o.d.fog_rgb[1] / 255.0f; d.fb = o.d.fog_rgb[2] / 255.0f;
			} else { d.f = 1.0f; d.fr = d.fg = d.fb = 0.0f; }
		}
		const int cx0 = o.clip[0], cy0 = o.clip[1], cx1 = o.clip[0] + o.clip[2], cy1 = o.clip[1] + o.clip[3];
		for (size_t t = 0; t + 2 < o.idx.size(); t += 3) {
			std::vector<Vtx> poly = { vv[o.idx[t]], vv[o.idx[t + 1]], vv[o.idx[t + 2]] };
			if (clip3d) {
				bool inside = true;
				for (auto &p : poly) { if (p.x < cx0 || p.x > cx1 || p.y < cy0 || p.y > cy1) { inside = false; } }
				if (!inside) {
					clip_poly(poly, 0, (float)cx0, true); if (poly.size() >= 3) { clip_poly(poly, 0, (float)cx1, false); }
					if (poly.size() >= 3) { clip_poly(poly, 1, (float)cy0, true); }
					if (poly.size() >= 3) { clip_poly(poly, 1, (float)cy1, false); }
					if (poly.size() < 3) { continue; }
				}
			}
			const int base = (int)b->v.size();
			for (auto &p : poly) { b->v.push_back(p); }
			for (size_t k = 1; k + 1 < poly.size(); k++) { b->idx.push_back(base); b->idx.push_back(base + (int)k); b->idx.push_back(base + (int)k + 1); }
		}
	}
	for (auto it = out.begin(); it != out.end();) { if (it->idx.empty()) { it = out.erase(it); } else { ++it; } }
	if (epochs_out) { *epochs_out = epoch + 1; }
	return out;
}

// ---- materials ----------------------------------------------------------------------------------------------

static const char *S2L = "vec3 s2l(vec3 c) { return mix(c * 0.0773993808, pow((c + 0.055) * 0.9478672986, vec3(2.4)), step(0.04045, c)); }\n"
	"vec3 l2s(vec3 c) { c = clamp(c, 0.0, 1.0); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }\n";

RID FogSpike::canvas_material(int blend, bool a8, bool mesh, bool fog)
{
	if (blend == FS2_SINK_BLEND_ALPHA && !a8 && !mesh) { return RID(); }
	const uint32_t key = 0x1000u | (uint32_t)blend | (a8 ? 4u : 0u) | (mesh ? 8u : 0u) | (fog ? 16u : 0u) | (var.lin ? 32u : 0u);
	auto it = mat_map.find(key);
	if (it != mat_map.end()) { return it->second; }
	RenderingServer *rs = RenderingServer::get_singleton();
	const char *mode = blend == FS2_SINK_BLEND_ADD ? "blend_add" : "blend_mix";
	String code = String("shader_type canvas_item;\nrender_mode ") + mode + ";\nvarying vec4 vcol;\n" + S2L;
	if (mesh) { code += "varying vec3 pc;\nvarying vec4 fogv;\n"; }
	code += "void vertex() {\n\tvcol = COLOR;\n";
	if (mesh) { code += "\tpc = CUSTOM0.xyz;\n\tfogv = CUSTOM1;\n"; }
	code += "}\nvoid fragment() {\n";
	code += mesh ? "\tvec4 t = texture(TEXTURE, pc.xy / pc.z);\n" : "\tvec4 t = texture(TEXTURE, UV);\n";
	if (a8) { code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n"; }
	code += "\tCOLOR = vcol * t;\n";
	if (fog) {
		code += "\tfloat f = clamp(fogv.x, 0.0, 1.0);\n";
		code += var.lin ? "\tCOLOR.rgb = l2s(mix(s2l(fogv.yzw), s2l(COLOR.rgb), f));\n" : "\tCOLOR.rgb = mix(fogv.yzw, COLOR.rgb, f);\n";
	}
	if (blend == FS2_SINK_BLEND_OPAQUE) { code += "\tCOLOR.a = 1.0;\n"; }
	if (dbg && mesh) { code += "\tCOLOR = vec4(pc.z, fogv.x, 0.5, 1.0);\n"; }
	code += "}\n";
	RID sh = rs->shader_create(); rs->shader_set_code(sh, code);
	RID m = rs->material_create(); rs->material_set_shader(m, sh);
	shaders.push_back(sh); materials.push_back(m);
	mat_map[key] = m;
	return m;
}

RID FogSpike::spatial_material(int blend, int zmode, bool a8, uint32_t texflags, int tex)
{
	const bool lin = (texflags & FS2_SINK_TEXF_LINEAR) != 0, rep = (texflags & FS2_SINK_TEXF_REPEAT) != 0;
	const bool fog = var.fog != 0;
	const uint32_t skey = (uint32_t)blend | ((uint32_t)zmode << 2) | (a8 ? 16u : 0u) | (lin ? 32u : 0u) | (rep ? 64u : 0u) | (fog ? 128u : 0u) | (var.lin ? 256u : 0u) | (var.gam ? 512u : 0u);
	RenderingServer *rs = RenderingServer::get_singleton();
	RID sh;
	auto sit = shader_map.find(skey);
	if (sit != shader_map.end()) { sh = sit->second; }
	else {
		String code = "shader_type spatial;\nrender_mode unshaded, cull_disabled, shadows_disabled, fog_disabled, ";
		// additive as premultiplied with alpha 0: Godot's blend_add also adds alpha, which makes the layer cover the canvas behind it
		code += blend == FS2_SINK_BLEND_ADD ? "blend_premul_alpha, " : "blend_mix, ";
		code += zmode == 2 ? "depth_draw_always" : "depth_draw_never";
		if (zmode == 0) { code += ", depth_test_disabled"; }
		code += ";\nuniform sampler2D tex : ";
		code += lin ? "filter_linear, " : "filter_nearest, ";
		code += rep ? "repeat_enable;\n" : "repeat_disable;\n";
		code += String("varying vec4 vcol;\nvarying vec3 pc;\nvarying vec4 fogv;\n") + S2L;
		code += "void vertex() { vcol = COLOR; pc = CUSTOM0.xyz; fogv = CUSTOM1; }\nvoid fragment() {\n\tvec4 t = texture(tex, pc.xy / pc.z);\n";
		if (a8) { code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n"; }
		code += "\tvec4 c = vcol * t;\n";
		if (blend == FS2_SINK_BLEND_OPAQUE) { code += "\tc.a = 1.0;\n"; }
		if (var.gam) { code += fog ? "\tALBEDO = mix(fogv.yzw, c.rgb, clamp(fogv.x, 0.0, 1.0));\n" : "\tALBEDO = c.rgb;\n"; }
		else if (!fog) { code += "\tALBEDO = s2l(c.rgb);\n"; }
		else if (!var.gam) {
			// D3D fogs the source colour in framebuffer (gamma) values before the blend; the 3D layer blends in linear light
			code += "\tfloat f = clamp(fogv.x, 0.0, 1.0);\n";
			code += var.lin ? "\tALBEDO = mix(s2l(fogv.yzw), s2l(c.rgb), f);\n" : "\tALBEDO = s2l(mix(fogv.yzw, c.rgb, f));\n";
		}
		code += blend == FS2_SINK_BLEND_ADD ? "\tALBEDO *= c.a;\n\tALPHA = 0.0;\n" : "\tALPHA = c.a;\n";
		if (dbg) { code += "\tALBEDO = vec3(pc.z, fogv.x, 0.5); ALPHA = 1.0;\n"; }
		code += "}\n";
		sh = rs->shader_create(); rs->shader_set_code(sh, code);
		shaders.push_back(sh); shader_map[skey] = sh;
	}
	const uint64_t mkey = ((uint64_t)skey << 32) | (uint32_t)tex;
	auto mit = mat_map.find(mkey | (1ull << 63));
	if (mit != mat_map.end()) { return mit->second; }
	RID m = rs->material_create(); rs->material_set_shader(m, sh);
	rs->material_set_param(m, "tex", tex ? gtex[tex]->get_rid() : white_tex_rid);
	materials.push_back(m);
	mat_map[mkey | (1ull << 63)] = m;
	return m;
}

// ---- emission -----------------------------------------------------------------------------------------------

void FogSpike::emit_canvas(const std::vector<Batch> &bs, RID parent)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	int cc[4] = {0, 0, W, H}; RID clip_item;
	for (const Batch &b : bs) {
		RID item = rs->canvas_item_create();
		const bool full = b.clip[0] == 0 && b.clip[1] == 0 && b.clip[2] == W && b.clip[3] == H;
		if (full) { clip_item = RID(); } else if (!clip_item.is_valid() || std::memcmp(cc, b.clip, sizeof(cc)) != 0) {
			clip_item = rs->canvas_item_create(); rs->canvas_item_set_parent(clip_item, parent); items.push_back(clip_item);
			rs->canvas_item_set_custom_rect(clip_item, true, Rect2(b.clip[0], b.clip[1], b.clip[2], b.clip[3]));
			rs->canvas_item_set_clip(clip_item, true);
			if (draw_idx_mode == 1) { rs->canvas_item_set_draw_index(clip_item, draw_seq++); }
			std::memcpy(cc, b.clip, sizeof(cc));
		}
		rs->canvas_item_set_parent(item, full ? parent : clip_item);
		items.push_back(item);
		if (draw_idx_mode == 1) { rs->canvas_item_set_draw_index(item, draw_seq++); } else if (draw_idx_mode == 2) { rs->canvas_item_set_z_index(item, (int)(&b - bs.data()) - 2000); }
		rs->canvas_item_set_default_texture_filter(item, (b.texflags & FS2_SINK_TEXF_LINEAR) ? RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR : RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
		rs->canvas_item_set_default_texture_repeat(item, (b.texflags & FS2_SINK_TEXF_REPEAT) ? RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED : RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		const bool fog = b.fogged != 0;
		const bool mesh = b.persp != 0 || fog;
		if (opdbg == -2) { UtilityFunctions::print("FOG batch blend ", b.blend, " tex ", b.tex, " persp ", b.persp, " fog ", b.fogged, " clip ", b.clip[0], ",", b.clip[1], ",", b.clip[2], ",", b.clip[3], " nv ", (int64_t)b.v.size(), " a8 ", b.a8); }
		rs->canvas_item_set_material(item, canvas_material(b.blend, b.a8, mesh, fog));
		const int nv = (int)b.v.size(), ni = (int)b.idx.size();
		PackedInt32Array indices; PackedVector2Array points, uvs; PackedColorArray colors; PackedFloat32Array custom, custom1;
		indices.resize(ni); points.resize(nv); uvs.resize(nv); colors.resize(nv);
		for (int i = 0; i < ni; i++) { indices.ptrw()[i] = b.idx[i]; }
		if (mesh) { custom.resize((int64_t)nv * 4); custom1.resize((int64_t)nv * 4); }
		for (int i = 0; i < nv; i++) {
			const Vtx &v = b.v[i];
			points.ptrw()[i] = Vector2(v.x, v.y);
			uvs.ptrw()[i] = Vector2(v.c0 / v.c2, v.c1 / v.c2);
			colors.ptrw()[i] = Color(v.r, v.g, v.b, v.a);
			if (mesh) {
				float *c = custom.ptrw() + i * 4; c[0] = v.c0; c[1] = v.c1; c[2] = v.c2; c[3] = 0;
				float *c1 = custom1.ptrw() + i * 4; c1[0] = v.f; c1[1] = v.fr; c1[2] = v.fg; c1[3] = v.fb;
			}
		}
		RID trid = b.tex ? gtex[b.tex]->get_rid() : white_tex_rid;
		if (!mesh) {
			rs->canvas_item_add_triangle_array(item, indices, points, colors, uvs, PackedInt32Array(), PackedFloat32Array(), trid);
		} else {
			Array arrays; arrays.resize(RenderingServer::ARRAY_MAX);
			arrays[RenderingServer::ARRAY_VERTEX] = points; arrays[RenderingServer::ARRAY_COLOR] = colors;
			arrays[RenderingServer::ARRAY_TEX_UV] = uvs; arrays[RenderingServer::ARRAY_CUSTOM0] = custom; arrays[RenderingServer::ARRAY_CUSTOM1] = custom1; arrays[RenderingServer::ARRAY_INDEX] = indices;
			RID m = rs->mesh_create();
			const int64_t fmt = (int64_t)RenderingServer::ARRAY_FLAG_USE_2D_VERTICES | ((int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT)
				| ((int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM1_SHIFT);
			rs->mesh_add_surface_from_arrays(m, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)fmt);
			meshes.push_back(m);
			rs->canvas_item_add_mesh(item, m, Transform2D(), Color(1, 1, 1, 1), trid);
		}
	}
}

void FogSpike::emit_3d(const std::vector<Batch> &bs, RID scenario, int epochs)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const int n = (int)bs.size();
	for (int bi = 0; bi < n; bi++) {
		const Batch &b = bs[bi];
		const int nv = (int)b.v.size(), ni = (int)b.idx.size();
		PackedVector3Array pos; PackedColorArray col; PackedVector2Array uv; PackedFloat32Array custom, custom1; PackedInt32Array idx;
		pos.resize(nv); col.resize(nv); uv.resize(nv); custom.resize((int64_t)nv * 4); custom1.resize((int64_t)nv * 4); idx.resize(ni);
		const float band = 1100.0f;
		for (int i = 0; i < nv; i++) {
			const Vtx &v = b.v[i];
			const float d = (float)(epochs - b.epoch) * band + 2.0f + v.z * 1000.0f - (float)b.zbias * 2.0f;
			pos.ptrw()[i] = Vector3(v.x - W * 0.5f, H * 0.5f - v.y, -d);
			col.ptrw()[i] = Color(v.r, v.g, v.b, v.a);
			uv.ptrw()[i] = Vector2(0, 0);
			float *c = custom.ptrw() + i * 4; c[0] = v.c0; c[1] = v.c1; c[2] = v.c2; c[3] = 0;
			float *c1 = custom1.ptrw() + i * 4; c1[0] = v.f; c1[1] = v.fr; c1[2] = v.fg; c1[3] = v.fb;
		}
		for (int i = 0; i < ni; i++) { idx.ptrw()[i] = b.idx[i]; }
		Array arrays; arrays.resize(RenderingServer::ARRAY_MAX);
		arrays[RenderingServer::ARRAY_VERTEX] = pos; arrays[RenderingServer::ARRAY_COLOR] = col; arrays[RenderingServer::ARRAY_TEX_UV] = uv;
		arrays[RenderingServer::ARRAY_CUSTOM0] = custom; arrays[RenderingServer::ARRAY_CUSTOM1] = custom1; arrays[RenderingServer::ARRAY_INDEX] = idx;
		RID mesh = rs->mesh_create();
		const int64_t fmt = ((int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT)
			| ((int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM1_SHIFT);
		rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)fmt);
		rs->mesh_surface_set_material(mesh, 0, spatial_material(b.blend, b.zmode, b.a8, b.texflags, b.tex));
		RID inst = rs->instance_create2(mesh, scenario);
		rs->instance_set_pivot_data(inst, sort_scale * (float)bi, false);	// FOG_SORT: S-1 used -1; a large positive scale makes submission order beat camera distance
		meshes.push_back(mesh); instances.push_back(inst);
	}
}

// ---- images -------------------------------------------------------------------------------------------------

void FogSpike::save_rgb(const std::vector<uint8_t> &rgb, const std::string &name)
{
	PackedByteArray pba; pba.resize((int64_t)W * H * 4);
	for (int i = 0; i < W * H; i++) { pba.ptrw()[i * 4] = rgb[i * 3]; pba.ptrw()[i * 4 + 1] = rgb[i * 3 + 1]; pba.ptrw()[i * 4 + 2] = rgb[i * 3 + 2]; pba.ptrw()[i * 4 + 3] = 255; }
	Ref<Image> img = Image::create_from_data(W, H, false, Image::FORMAT_RGBA8, pba);
	img->save_png(String(out_dir.c_str()) + "/" + name.c_str());
}

void FogSpike::compare_rgb(const std::vector<uint8_t> &rgb, const std::string &name)
{
	if (!have_ref) { return; }
	double sum = 0; long n8 = 0, n32 = 0, n64 = 0;
	std::vector<uint8_t> diff((size_t)W * H * 3);
	for (int i = 0; i < W * H; i++) {
		int m = 0;
		for (int c = 0; c < 3; c++) { const int e = std::abs((int)rgb[i * 3 + c] - (int)ref_rgb[i * 3 + c]); sum += e; m = std::max(m, e); diff[i * 3 + c] = (uint8_t)std::min(255, e * 4); }
		n8 += m > 8; n32 += m > 32; n64 += m > 64;
	}
	UtilityFunctions::print("FOG compare ", name.c_str(), " vs ref: mean abs err ", sum / ((double)W * H * 3), "/255, pixels off by >8: ", (double)n8 * 100.0 / (W * H), "%, >32: ", (double)n32 * 100.0 / (W * H), "%, >64: ", (double)n64 * 100.0 / (W * H), "%");
	save_rgb(diff, "diff_" + name + ".png");
}

// ---- driver -------------------------------------------------------------------------------------------------

void FogSpike::_ready()
{
	out_dir = OS::get_singleton()->get_environment("FOG_OUT").utf8().get_data();
	if (out_dir.empty()) { out_dir = "."; }
	{ String ss = OS::get_singleton()->get_environment("FOG_SORT"); if (!ss.is_empty()) { sort_scale = (float)ss.to_float(); } }
	{ String od = OS::get_singleton()->get_environment("FOG_OPDBG"); if (!od.is_empty()) { opdbg = (int)od.to_int(); } }
	{ String di = OS::get_singleton()->get_environment("FOG_DRAWIDX"); draw_idx_mode = di.is_empty() ? 1 : (int)di.to_int(); }	// 1 (default): canvas_item_set_draw_index per batch; without it Godot does not keep sibling creation order
	dbg = !OS::get_singleton()->get_environment("FOG_DBG").is_empty();
	no_persp = !OS::get_singleton()->get_environment("FOG_NOPERSP").is_empty();
	std::string dump = OS::get_singleton()->get_environment("FOG_DUMP").utf8().get_data();
	std::string ms = OS::get_singleton()->get_environment("FOG_MODES").utf8().get_data();
	if (ms.empty()) { ms = "ref,ref_px,ref_lin,ref_nofog,canvas,opt3,opt3px,opt3lin,opt3draw,opt3nofog"; }
	for (size_t p = 0; p <= ms.size();) { size_t q = ms.find(',', p); if (q == std::string::npos) { q = ms.size(); } modes.push_back(ms.substr(p, q - p)); p = q + 1; }
	if (!load_dump(dump)) { UtilityFunctions::printerr("FOG: cannot load dump ", dump.c_str()); get_tree()->quit(2); return; }
	for (auto &kv : texs) {
		Tex &t = kv.second;
		PackedByteArray pba; pba.resize((int64_t)t.px.size());
		std::memcpy(pba.ptrw(), t.px.data(), t.px.size());
		Ref<Image> im = Image::create_from_data(t.w, t.h, false, t.fmt == FS2_SINK_TEX_A8 ? Image::FORMAT_L8 : Image::FORMAT_RGBA8, pba);
		gtex[kv.first] = ImageTexture::create_from_image(im);
	}
	{
		Ref<Image> im = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8); im->fill(Color(1, 1, 1, 1));
		white_tex = ImageTexture::create_from_image(im); white_tex_rid = white_tex->get_rid();
	}
	RenderingServer::get_singleton()->set_default_clear_color(Color(0, 0, 0, 1));
	{ String fo = OS::get_singleton()->get_environment("FOG_ONLY"); if (!fo.is_empty()) { PackedStringArray l = fo.split(","); std::vector<Op> keep; for (int k = 0; k < l.size(); k++) { const int oi = (int)l[k].to_int(); if (oi >= 0 && oi < (int)ops.size()) { keep.push_back(ops[oi]); } } ops.swap(keep); } }	// debug: keep only these ops
	{ String fl = OS::get_singleton()->get_environment("FOG_LAST"); if (!fl.is_empty() && (int)fl.to_int() + 1 < (int)ops.size()) { ops.resize((size_t)fl.to_int() + 1); } }	// debug: keep ops 0..FOG_LAST
	analyse();
	set_process(true);
	phase = 0; wait = 2;
}

void FogSpike::teardown()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (RID r : items) { rs->free_rid(r); }
	for (RID r : instances) { rs->free_rid(r); }
	for (RID r : meshes) { rs->free_rid(r); }
	items.clear(); instances.clear(); meshes.clear();
	if (sub) { sub->queue_free(); sub = nullptr; sub_cam = nullptr; }
}

static Camera3D *make_cam(Node *parent, int W, int H)
{
	Camera3D *c = memnew(Camera3D);
	parent->add_child(c);
	c->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
	c->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);
	c->set_size((float)H);
	c->set_near(0.5f); c->set_far(30000.0f);
	c->set_position(Vector3(0, 0, 0));
	return c;
}

void FogSpike::start_mode(const std::string &m)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	cur_mode = m;
	build_us = submit_us = 0; nbatches = nbatches3d = ncanvas_batches = 0; draw_seq = 0;
	const int nops = (int)ops.size();
	int first_z = -1, last_z = -1;
	for (int i = 0; i < nops; i++) { if (zdraw(ops[i])) { if (first_z < 0) { first_z = i; } last_z = i; } }
	if (m.rfind("ref", 0) == 0) {
		fogspike::Options opt;
		opt.fog = m == "ref_nofog" ? 0 : (m == "ref_px" ? 2 : 1);
		opt.linear_mix = m == "ref_lin";
		const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
		std::vector<float> fb; std::vector<uint8_t> rgb;
		fogspike::render(W, H, ops, texs, opt, fb);
		fogspike::to_rgb8(fb, rgb);
		UtilityFunctions::print("FOG ", m.c_str(), ": software rasteriser ", (Time::get_singleton()->get_ticks_usec() - t0) / 1000.0, " ms (not an engine cost)");
		save_rgb(rgb, m + ".png");
		if (m == "ref") { ref_rgb = rgb; have_ref = true; } else { compare_rgb(rgb, m); }
		return;
	}
	var = FogVar();
	var.fog = m.find("nofog") == std::string::npos ? 1 : 0;
	var.px = m == "opt3px"; var.lin = m == "opt3lin"; var.split = m == "opt3draw"; var.gam = m.rfind("opt3g", 0) == 0;
	if (m.rfind("canvas", 0) == 0) {
		uint64_t t0 = Time::get_singleton()->get_ticks_usec();
		auto bs = make_batches(0, nops - 1, false, nullptr);
		uint64_t t1 = Time::get_singleton()->get_ticks_usec();
		emit_canvas(bs, get_canvas_item());
		uint64_t t2 = Time::get_singleton()->get_ticks_usec();
		build_us = (double)(t1 - t0); submit_us = (double)(t2 - t1); nbatches = ncanvas_batches = (int)bs.size();
	} else {
		sub = memnew(SubViewport);
		sub->set_size(Vector2i(W, H));
		{ Ref<World3D> w3; w3.instantiate(); sub->set_world_3d(w3); }
		sub->set_transparent_background(true);
		sub->set_update_mode(SubViewport::UPDATE_ALWAYS);
		add_child(sub);
		sub_cam = make_cam(sub, W, H); sub_cam->make_current();
		rs->viewport_set_measure_render_time(sub->get_viewport_rid(), true);
		uint64_t t0 = Time::get_singleton()->get_ticks_usec();
		auto back = make_batches(0, first_z - 1, false, nullptr);
		int ep = 1;
		auto mid = make_batches(first_z, last_z, true, &ep);
		auto front = make_batches(last_z + 1, nops - 1, false, nullptr);
		uint64_t t1 = Time::get_singleton()->get_ticks_usec();
		emit_canvas(back, get_canvas_item());
		RID vitem = rs->canvas_item_create(); rs->canvas_item_set_parent(vitem, get_canvas_item()); items.push_back(vitem);
		if (draw_idx_mode == 1) { rs->canvas_item_set_draw_index(vitem, draw_seq++); }	// the composite sits between back and front canvas in draw order
		{
			RID sh = rs->shader_create(); rs->shader_set_code(sh, var.gam ? String("shader_type canvas_item;\nrender_mode blend_premul_alpha;\n") + S2L + "void fragment() { COLOR = texture(TEXTURE, UV); COLOR.rgb = s2l(COLOR.rgb); }\n" : String("shader_type canvas_item;\nrender_mode blend_premul_alpha;\nvoid fragment() { COLOR = texture(TEXTURE, UV); }\n"));
			RID mm = rs->material_create(); rs->material_set_shader(mm, sh); shaders.push_back(sh); materials.push_back(mm);
			rs->canvas_item_set_material(vitem, mm);
		}
		rs->canvas_item_add_texture_rect(vitem, Rect2(0, 0, W, H), sub->get_texture()->get_rid());
		emit_canvas(front, get_canvas_item());
		emit_3d(mid, sub->get_viewport()->get_world_3d()->get_scenario(), ep);
		uint64_t t2 = Time::get_singleton()->get_ticks_usec();
		build_us = (double)(t1 - t0); submit_us = (double)(t2 - t1); ncanvas_batches = (int)(back.size() + front.size()); nbatches3d = (int)mid.size(); nbatches = ncanvas_batches + nbatches3d;
		int nfogb = 0; for (auto &b : mid) { nfogb += b.fogged; }
		UtilityFunctions::print("FOG ", m.c_str(), ": back canvas ", (int64_t)back.size(), " batches, 3D ", (int64_t)mid.size(), " batches (", nfogb, " with fogged draws), front canvas ", (int64_t)front.size(), " batches, ", ep, " depth epochs");
	}
	rs->viewport_set_measure_render_time(get_viewport()->get_viewport_rid(), true);
	gpu_sum = cpu_sum = sub_gpu_sum = sub_cpu_sum = 0; samples = 0; measure_left = 150;
}

void FogSpike::finish_mode()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (cur_mode.rfind("ref", 0) == 0) { return; }
	Ref<Image> img = get_viewport()->get_texture()->get_image();
	img->save_png(String(out_dir.c_str()) + "/" + cur_mode.c_str() + ".png");
	img->convert(Image::FORMAT_RGB8);
	PackedByteArray d = img->get_data();
	std::vector<uint8_t> rgb((size_t)W * H * 3);
	std::memcpy(rgb.data(), d.ptr(), rgb.size());
	compare_rgb(rgb, cur_mode);
	if (samples > 0) {
		UtilityFunctions::print("FOG drawcost ", cur_mode.c_str(), ": GPU ", gpu_sum / samples, " ms, viewport CPU ", cpu_sum / samples, " ms (sub viewport GPU ", sub_gpu_sum / samples, ", CPU ", sub_cpu_sum / samples,
			") per frame, mean of ", samples, "; draw calls ", (int64_t)rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME), ", primitives ", (int64_t)rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME));
	}
	UtilityFunctions::print("FOG buildcost ", cur_mode.c_str(), ": batching ", build_us / 1000.0, " ms, RenderingServer submit ", submit_us / 1000.0, " ms per frame; ", nbatches, " batches (", nbatches3d, " in 3D, ", ncanvas_batches, " on canvas)");
}

void FogSpike::_process(double)
{
	if (wait > 0) { wait--; return; }
	RenderingServer *rs = RenderingServer::get_singleton();
	if (phase == 0) {
		mode_index++;
		if (mode_index >= (int)modes.size()) { UtilityFunctions::print("FOG done"); set_process(false); get_tree()->quit(0); return; }
		start_mode(modes[mode_index]);
		if (cur_mode.rfind("ref", 0) == 0) { return; }
		phase = 1; wait = 4;
		return;
	}
	if (phase == 1) { phase = 2; wait = 16; return; }
	if (phase == 2) {
		if (measure_left > 0) {
			const double g = rs->viewport_get_measured_render_time_gpu(get_viewport()->get_viewport_rid());
			if (g > 0.0) {
				gpu_sum += g; cpu_sum += rs->viewport_get_measured_render_time_cpu(get_viewport()->get_viewport_rid());
				if (sub) { sub_gpu_sum += rs->viewport_get_measured_render_time_gpu(sub->get_viewport_rid()); sub_cpu_sum += rs->viewport_get_measured_render_time_cpu(sub->get_viewport_rid()); }
				samples++;
			}
			measure_left--;
			return;
		}
		finish_mode();
		teardown();
		phase = 3; wait = 3;
		return;
	}
	phase = 0;
}

static void fog_init(ModuleInitializationLevel l)
{
	if (l == MODULE_INITIALIZATION_LEVEL_SCENE) { GDREGISTER_CLASS(FogSpike); }
}
static void fog_term(ModuleInitializationLevel) {}

extern "C" GDExtensionBool GDE_EXPORT fog_library_init(GDExtensionInterfaceGetProcAddress a, GDExtensionClassLibraryPtr l, GDExtensionInitialization *o)
{
	GDExtensionBinding::InitObject io(a, l, o);
	io.register_initializer(fog_init);
	io.register_terminator(fog_term);
	io.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return io.init();
}
