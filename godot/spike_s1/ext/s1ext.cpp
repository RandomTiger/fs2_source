// S-1 spike: replays one captured FS2 mission frame (draw calls + per-vertex z + depth state, see ../s1_capture.cpp) through
//   ref     a CPU software rasteriser with D3D semantics (z-buffer, perspective uv, in-order blending): what the frame should look like
//   canvas  the current R1 route: everything as canvas items in submission order, no depth
//   opt2    option 2: every triangle as a 3D mesh seen by an orthographic Camera3D, depth from sz, order by sorting offset
//   opt3    option 3: canvas for the 2D before the first and after the last depth-tested draw, a SubViewport (3D ortho) between,
//           shown under the front 2D
// and saves screenshots, diff images and CPU / GPU numbers (FS2_SELFTEST_MEASURE style: viewport render times, mean of 150 frames).
// Env: S1_DUMP (dump file), S1_OUT (output folder), S1_MODES (default ref,canvas,opt2,opt3), S1_SORTSIGN (+1/-1, default -1).
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
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "render_sink.h"

using namespace godot;

namespace {

struct SideT { int valid, nv; float z[32]; int zmode, is_scaler, zbias, cull, zclear_seq, zc[4]; };
struct Op {
	int kind = 0, a[4] = {0, 0, 0, 0};
	fs2_sink_draw d = {};
	SideT s = {};
	std::vector<fs2_sink_vertex> v;
	std::vector<uint16_t> idx;
	int clip[4] = {0, 0, 1024, 768};	// at the time of the draw
};
struct TexInfo {
	int w = 0, h = 0, fmt = 0;
	uint32_t flags = 0;
	std::vector<uint8_t> px;
	Ref<ImageTexture> tex;
};
struct Vtx { float x, y, z, c0, c1, c2, r, g, b, a; };	// c0..c2 = custom0 (u*sw, v*sw, sw)
struct Batch {
	int tex = 0, blend = 0, persp = 0, zmode = 0, epoch = 0, zbias = 0;
	int clip[4] = {0, 0, 1024, 768};
	bool a8 = false;
	uint32_t texflags = 0;
	std::vector<Vtx> v;
	std::vector<int> idx;
};

}

class S1Spike : public Node2D {
	GDCLASS(S1Spike, Node2D)
public:
	static void _bind_methods() {}
	void _ready() override;
	void _process(double delta) override;
	void _exit_tree() override { teardown(); }

private:
	int W = 1024, H = 768;
	std::vector<Op> ops;
	std::map<int, TexInfo> texs;
	std::string out_dir;
	std::vector<std::string> modes;
	int mode_index = -1, phase = 0, wait = 0, samples = 0, measure_left = 0, ndrawn_frames = 0;
	double gpu_sum = 0, cpu_sum = 0, sub_gpu_sum = 0, sub_cpu_sum = 0;
	double build_us = 0, submit_us = 0;
	int nbatches = 0, nbatches3d = 0, ncanvas_batches = 0;
	float sortsign = -1.0f;
	std::string cur_mode;

	std::vector<RID> items, meshes, instances, shaders, materials;
	std::map<uint32_t, RID> shader_map;
	std::map<uint64_t, RID> mat_map;
	RID white_tex_rid;
	Ref<ImageTexture> white_tex;
	Camera3D *cam = nullptr;
	SubViewport *sub = nullptr;
	Camera3D *sub_cam = nullptr;
	std::vector<uint8_t> ref_rgb;	// software reference
	bool have_ref = false;

	bool load_dump(const std::string &path);
	void analyse();
	void start_mode(const std::string &m);
	void finish_mode();
	void teardown();
	void software_render(std::vector<uint8_t> &out);
	void save_rgb(const std::vector<uint8_t> &rgb, const std::string &name);
	void compare(const Ref<Image> &img, const std::string &name);
	std::vector<Batch> make_batches(int first, int last, bool clip3d, int *epochs_out);
	RID canvas_material(int blend, bool a8, bool persp);
	RID spatial_material(int blend, int zmode, bool a8, uint32_t texflags, int tex);
	void emit_canvas(const std::vector<Batch> &bs, RID parent);
	void emit_3d(const std::vector<Batch> &bs, RID scenario, int epochs);
};

// ---- dump ---------------------------------------------------------------------------------------------------

bool S1Spike::load_dump(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) { return false; }
	int hdr[6];
	if (fread(hdr, 4, 6, f) != 6 || hdr[0] != 0x31533146) { fclose(f); return false; }
	W = hdr[1]; H = hdr[2];
	const int ntex = hdr[4], nops = hdr[5];
	for (int i = 0; i < ntex; i++) {
		int h2[5];
		if (fread(h2, 4, 5, f) != 5) { fclose(f); return false; }
		TexInfo t; t.w = h2[1]; t.h = h2[2]; t.fmt = h2[3]; t.flags = (uint32_t)h2[4];
		t.px.resize((size_t)t.w * t.h * (t.fmt == FS2_SINK_TEX_A8 ? 1 : 4));
		if (fread(t.px.data(), 1, t.px.size(), f) != t.px.size()) { fclose(f); return false; }
		texs[h2[0]] = std::move(t);
	}
	int clip[4] = {0, 0, W, H};
	for (int i = 0; i < nops; i++) {
		int h3[6];
		if (fread(h3, 4, 6, f) != 6) { fclose(f); return false; }
		Op o; o.kind = h3[0]; for (int k = 0; k < 4; k++) { o.a[k] = h3[1 + k]; }
		if (o.kind == 0) {
			int h4[2];
			if (fread(h4, 4, 2, f) != 2) { fclose(f); return false; }
			if (fread(&o.d, sizeof(o.d), 1, f) != 1 || fread(&o.s, sizeof(SideT), 1, f) != 1) { fclose(f); return false; }
			o.v.resize(h3[5]); o.idx.resize(h4[0]);
			if (fread(o.v.data(), sizeof(fs2_sink_vertex), o.v.size(), f) != o.v.size() || fread(o.idx.data(), 2, o.idx.size(), f) != o.idx.size()) { fclose(f); return false; }
		} else if (o.kind == 2) {
			for (int k = 0; k < 4; k++) { clip[k] = o.a[k]; }
		}
		for (int k = 0; k < 4; k++) { o.clip[k] = clip[k]; }
		ops.push_back(std::move(o));
	}
	fclose(f);
	return true;
}

void S1Spike::analyse()
{
	// structure of the frame: runs of depth-tested draws and the 2D between them
	int nz2 = 0, nz1 = 0, nui = 0, tris = 0, add = 0, alpha = 0, opq = 0;
	int first_z = -1, last_z = -1;
	std::map<int, int> epochs;
	int lastseq = -1;
	for (size_t i = 0; i < ops.size(); i++) {
		const Op &o = ops[i];
		if (o.kind != 0) { continue; }
		tris += (int)o.idx.size() / 3;
		if (o.d.blend == FS2_SINK_BLEND_ADD) { add++; } else if (o.d.blend == FS2_SINK_BLEND_ALPHA) { alpha++; } else { opq++; }
		if (o.s.valid && o.s.zmode > 0) {
			(o.s.zmode == 2 ? nz2 : nz1)++;
			if (first_z < 0) { first_z = (int)i; }
			last_z = (int)i;
			if (o.s.zclear_seq != lastseq) { lastseq = o.s.zclear_seq; epochs[(int)i] = o.s.zc[0] * 1000000 + o.s.zc[1]; UtilityFunctions::print("S1 zclear at op ", (int64_t)i, " rect ", o.s.zc[0], ",", o.s.zc[1], " - ", o.s.zc[2], ",", o.s.zc[3]); }
		} else { nui++; }
	}
	UtilityFunctions::print("S1 frame: ", (int64_t)ops.size(), " ops, ", tris, " triangles, ", (int64_t)texs.size(), " textures; z-full ", nz2, ", z-read ", nz1, ", no-z ", nui,
		"; blend alpha ", alpha, " add ", add, " opaque ", opq, "; first z op ", first_z, " last z op ", last_z);
	if (OS::get_singleton()->has_environment("S1_LIST")) {
		for (size_t i = 0; i < ops.size(); i++) {
			const Op &o = ops[i];
			if (o.kind != 0) { UtilityFunctions::print("S1 op ", (int64_t)i, " kind ", o.kind, " ", o.a[0], ",", o.a[1], ",", o.a[2], ",", o.a[3]); continue; }
			if (i == 813) { auto &t = texs[o.d.texture]; UtilityFunctions::print("S1 tex ", t.w, "x", t.h, " fmt ", t.fmt, " flags ", (int64_t)t.flags); }
			if (i >= 813 && i <= 816) { for (auto &v : o.v) { UtilityFunctions::print("S1 v ", v.x, " ", v.y, " sw ", v.sw, " uv ", v.u, " ", v.v); } }
			UtilityFunctions::print("S1 op ", (int64_t)i, " draw tex ", o.d.texture, " blend ", o.d.blend, " flags ", (int64_t)o.d.flags, " tint ", o.d.tint[0], ",", o.d.tint[1], ",", o.d.tint[2], ",", o.d.tint[3], " nv ", (int64_t)o.v.size(), " ni ", (int64_t)o.idx.size(), " z ", o.s.valid ? o.s.zmode : -1,
				" v0 ", o.v[0].x, ",", o.v[0].y, " rgba ", o.v[0].r, ",", o.v[0].g, ",", o.v[0].b, ",", o.v[0].a, " clip ", o.clip[0], ",", o.clip[1], ",", o.clip[2], ",", o.clip[3]);
		}
	}
	// segments
	int seg_start = 0; int seg_z = -2; std::string line;
	int interleaved = 0;
	for (size_t i = 0; i <= ops.size(); i++) {
		int z = -1;
		if (i < ops.size()) {
			const Op &o = ops[i];
			if (o.kind == 0) { z = (o.s.valid && o.s.zmode > 0) ? 1 : 0; } else { z = (seg_z == -2) ? 0 : seg_z; }
		}
		if (z != seg_z || i == ops.size()) {
			if (seg_z != -2 && (int)i > seg_start) {
				if (seg_z == 0 && (int)seg_start > first_z && (int)i <= last_z) {
					interleaved++;
					// bounding box of the interleaved 2D
					float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; int nd = 0;
					for (size_t k = seg_start; k < i; k++) { if (ops[k].kind == 0) { nd++; for (auto &v : ops[k].v) { x0 = std::min(x0, v.x); y0 = std::min(y0, v.y); x1 = std::max(x1, v.x); y1 = std::max(y1, v.y); } } }
					UtilityFunctions::print("S1 2D inside the 3D range: ops ", seg_start, "-", (int64_t)i - 1, " (", nd, " draws) bbox ", x0, ",", y0, " - ", x1, ",", y1);
				}
			}
			seg_start = (int)i; seg_z = z;
		}
	}
	UtilityFunctions::print("S1 interleaved 2D segments between first and last depth-tested draw: ", interleaved);
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
			for (int k = 0; k < 10; k++) { pm[k] = pa[k] + (pb[k] - pa[k]) * t; }
			out.push_back(m);
		}
	}
	poly.swap(out);
}

std::vector<Batch> S1Spike::make_batches(int first, int last, bool clip3d, int *epochs_out)
{
	std::vector<Batch> out;
	int epoch = 0, lastseq = -1;
	for (int i = first; i <= last && i < (int)ops.size(); i++) {
		const Op &o = ops[i];
		if (o.kind != 0) { continue; }
		{ static int sa = -2, sb = -2; if (sa == -2) { String e = OS::get_singleton()->get_environment("S1_SKIP"); sa = sb = -1; if (!e.is_empty()) { sa = e.get_slice("-", 0).to_int(); sb = e.get_slice("-", 1).to_int(); } } if (i >= sa && i <= sb) { continue; } }
		if (o.d.blend == FS2_SINK_BLEND_OPAQUE && o.d.texture == 0 && o.clip[2] >= W && o.clip[3] >= H) { continue; }	// the frame clear: the viewport clear is black. Emitting it as a canvas item blanks the frame (see report)
		const bool zt = o.s.valid && o.s.zmode > 0;
		if (zt && o.s.zclear_seq != lastseq) { if (lastseq != -1) { epoch++; } lastseq = o.s.zclear_seq; }
		if (o.clip[2] <= 0 || o.clip[3] <= 0) { continue; }
		auto tit = texs.find(o.d.texture);
		if (o.d.texture != 0 && tit == texs.end()) { continue; }
		const bool a8 = o.d.texture != 0 && tit->second.fmt == FS2_SINK_TEX_A8;
		const uint32_t tfl = o.d.texture != 0 ? tit->second.flags : 0;
		const int persp = (o.d.flags & FS2_SINK_DRAWF_PERSPECTIVE) ? 1 : 0;
		const int zm = zt ? o.s.zmode : 0;
		Batch *b = out.empty() ? nullptr : &out.back();
		if (!b || b->tex != o.d.texture || b->blend != o.d.blend || b->persp != persp || b->zmode != zm || b->epoch != epoch || b->zbias != (zt ? o.s.zbias : 0)
			|| std::memcmp(b->clip, o.clip, sizeof(b->clip)) != 0) {
			out.emplace_back();
			b = &out.back();
			b->tex = o.d.texture; b->blend = o.d.blend; b->persp = persp; b->zmode = zm; b->epoch = epoch; b->zbias = zt ? o.s.zbias : 0;
			std::memcpy(b->clip, o.clip, sizeof(b->clip)); b->a8 = a8; b->texflags = tfl;
		}
		const int nv = (int)o.v.size();
		std::vector<Vtx> vv(nv);
		for (int k = 0; k < nv; k++) {
			const fs2_sink_vertex &s = o.v[k];
			Vtx &d = vv[k];
			d.x = s.x; d.y = s.y;
			d.z = (zt && k < 32) ? o.s.z[k] : 0.0f;
			const float sw = persp ? s.sw : 1.0f;
			d.c0 = s.u * sw; d.c1 = s.v * sw; d.c2 = sw;
			d.r = s.r * o.d.tint[0] / 255.0f / 255.0f; d.g = s.g * o.d.tint[1] / 255.0f / 255.0f; d.b = s.b * o.d.tint[2] / 255.0f / 255.0f; d.a = s.a * o.d.tint[3] / 255.0f / 255.0f;
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

RID S1Spike::canvas_material(int blend, bool a8, bool persp)
{
	if (blend == FS2_SINK_BLEND_ALPHA && !a8 && !persp && !OS::get_singleton()->has_environment("S1_EXPLICIT")) { return RID(); }
	const uint32_t key = 0x1000u | (uint32_t)blend | (a8 ? 4u : 0u) | (persp ? 8u : 0u);
	auto it = mat_map.find(key);
	if (it != mat_map.end()) { return it->second; }
	RenderingServer *rs = RenderingServer::get_singleton();
	const char *mode = blend == FS2_SINK_BLEND_ADD ? "blend_add" : "blend_mix";
	String code = String("shader_type canvas_item;\nrender_mode ") + mode + ";\nvarying vec4 vcol;\n";
	if (persp) { code += "varying vec3 pc;\n"; }
	code += "void vertex() {\n\tvcol = COLOR;\n";
	if (persp) { code += "\tpc = CUSTOM0.xyz;\n"; }
	code += "}\nvoid fragment() {\n";
	code += persp ? "\tvec4 t = texture(TEXTURE, pc.xy / pc.z);\n" : "\tvec4 t = texture(TEXTURE, UV);\n";
	if (a8) { code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n"; }
	code += "\tCOLOR = vcol * t;\n";
	if (blend == FS2_SINK_BLEND_OPAQUE) { code += "\tCOLOR.a = 1.0;\n"; }
	code += "}\n";
	RID sh = rs->shader_create(); rs->shader_set_code(sh, code);
	RID m = rs->material_create(); rs->material_set_shader(m, sh);
	shaders.push_back(sh); materials.push_back(m);
	mat_map[key] = m;
	return m;
}

RID S1Spike::spatial_material(int blend, int zmode, bool a8, uint32_t texflags, int tex)
{
	const bool lin = (texflags & FS2_SINK_TEXF_LINEAR) != 0, rep = (texflags & FS2_SINK_TEXF_REPEAT) != 0;
	const uint32_t skey = (uint32_t)blend | ((uint32_t)zmode << 2) | (a8 ? 16u : 0u) | (lin ? 32u : 0u) | (rep ? 64u : 0u);
	RenderingServer *rs = RenderingServer::get_singleton();
	RID sh;
	auto sit = shader_map.find(skey);
	if (sit != shader_map.end()) { sh = sit->second; }
	else {
		String code = "shader_type spatial;\nrender_mode unshaded, cull_disabled, shadows_disabled, fog_disabled, ";
		code += blend == FS2_SINK_BLEND_ADD ? "blend_add, " : "blend_mix, ";
		code += zmode == 2 ? "depth_draw_always" : "depth_draw_never";
		if (zmode == 0) { code += ", depth_test_disabled"; }
		code += ";\nuniform sampler2D tex : ";
		code += lin ? "filter_linear, " : "filter_nearest, ";
		code += rep ? "repeat_enable;\n" : "repeat_disable;\n";
		code += "varying vec4 vcol;\nvarying vec3 pc;\nvec3 s2l(vec3 c) { return mix(c * 0.0773993808, pow((c + 0.055) * 0.9478672986, vec3(2.4)), step(0.04045, c)); }\n";
		code += "void vertex() { vcol = COLOR; pc = CUSTOM0.xyz; }\nvoid fragment() {\n\tvec4 t = texture(tex, pc.xy / pc.z);\n";
		if (a8) { code += "\tt = vec4(1.0, 1.0, 1.0, t.r);\n"; }
		code += "\tvec4 c = vcol * t;\n";
		if (blend == FS2_SINK_BLEND_OPAQUE) { code += "\tc.a = 1.0;\n"; }
		code += "\tALBEDO = s2l(c.rgb);\n\tALPHA = c.a;\n}\n";
		sh = rs->shader_create(); rs->shader_set_code(sh, code);
		shaders.push_back(sh); shader_map[skey] = sh;
	}
	const uint64_t mkey = ((uint64_t)skey << 32) | (uint32_t)tex;
	auto mit = mat_map.find(mkey | (1ull << 63));
	if (mit != mat_map.end()) { return mit->second; }
	RID m = rs->material_create(); rs->material_set_shader(m, sh);
	rs->material_set_param(m, "tex", tex ? texs[tex].tex->get_rid() : white_tex_rid);
	materials.push_back(m);
	mat_map[mkey | (1ull << 63)] = m;
	return m;
}

// ---- emission -----------------------------------------------------------------------------------------------

void S1Spike::emit_canvas(const std::vector<Batch> &bs, RID parent)
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
			std::memcpy(cc, b.clip, sizeof(cc));
		}
		rs->canvas_item_set_parent(item, full ? parent : clip_item);
		items.push_back(item);
		rs->canvas_item_set_default_texture_filter(item, (b.texflags & FS2_SINK_TEXF_LINEAR) ? RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR : RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
		rs->canvas_item_set_default_texture_repeat(item, (b.texflags & FS2_SINK_TEXF_REPEAT) ? RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED : RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		rs->canvas_item_set_material(item, canvas_material(b.blend, b.a8, b.persp != 0));
		const int nv = (int)b.v.size(), ni = (int)b.idx.size();
		PackedInt32Array indices; PackedVector2Array points, uvs; PackedColorArray colors; PackedFloat32Array custom;
		indices.resize(ni); points.resize(nv); uvs.resize(nv); colors.resize(nv);
		for (int i = 0; i < ni; i++) { indices.ptrw()[i] = b.idx[i]; }
		if (b.persp) { custom.resize((int64_t)nv * 4); }
		for (int i = 0; i < nv; i++) {
			const Vtx &v = b.v[i];
			points.ptrw()[i] = Vector2(v.x, v.y);
			uvs.ptrw()[i] = Vector2(v.c0 / v.c2, v.c1 / v.c2);
			colors.ptrw()[i] = Color(v.r, v.g, v.b, v.a);
			if (b.persp) { float *c = custom.ptrw() + i * 4; c[0] = v.c0; c[1] = v.c1; c[2] = v.c2; c[3] = 0; }
		}
		RID trid = b.tex ? texs[b.tex].tex->get_rid() : white_tex_rid;
		if (!b.persp) {
			rs->canvas_item_add_triangle_array(item, indices, points, colors, uvs, PackedInt32Array(), PackedFloat32Array(), trid);
		} else {
			Array arrays; arrays.resize(RenderingServer::ARRAY_MAX);
			arrays[RenderingServer::ARRAY_VERTEX] = points; arrays[RenderingServer::ARRAY_COLOR] = colors;
			arrays[RenderingServer::ARRAY_TEX_UV] = uvs; arrays[RenderingServer::ARRAY_CUSTOM0] = custom; arrays[RenderingServer::ARRAY_INDEX] = indices;
			RID mesh = rs->mesh_create();
			const int64_t fmt = (int64_t)RenderingServer::ARRAY_FLAG_USE_2D_VERTICES | ((int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT);
			rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)fmt);
			meshes.push_back(mesh);
			rs->canvas_item_add_mesh(item, mesh, Transform2D(), Color(1, 1, 1, 1), trid);
		}
	}
}

void S1Spike::emit_3d(const std::vector<Batch> &bs, RID scenario, int epochs)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	const int n = (int)bs.size();
	for (int bi = 0; bi < n; bi++) {
		const Batch &b = bs[bi];
		const int nv = (int)b.v.size(), ni = (int)b.idx.size();
		PackedVector3Array pos; PackedColorArray col; PackedVector2Array uv; PackedFloat32Array custom; PackedInt32Array idx;
		pos.resize(nv); col.resize(nv); uv.resize(nv); custom.resize((int64_t)nv * 4); idx.resize(ni);
		// distance from the camera: later clear epochs are nearer (a z-buffer clear in a rectangle becomes a depth band), then sz, then zbias
		const float band = 1100.0f;
		for (int i = 0; i < nv; i++) {
			const Vtx &v = b.v[i];
			const float d = (float)(epochs - b.epoch) * band + 2.0f + v.z * 1000.0f - (float)b.zbias * 2.0f;
			pos.ptrw()[i] = Vector3(v.x - W * 0.5f, H * 0.5f - v.y, -d);
			col.ptrw()[i] = Color(v.r, v.g, v.b, v.a);
			uv.ptrw()[i] = Vector2(0, 0);
			float *c = custom.ptrw() + i * 4; c[0] = v.c0; c[1] = v.c1; c[2] = v.c2; c[3] = 0;
		}
		for (int i = 0; i < ni; i++) { idx.ptrw()[i] = b.idx[i]; }
		Array arrays; arrays.resize(RenderingServer::ARRAY_MAX);
		arrays[RenderingServer::ARRAY_VERTEX] = pos; arrays[RenderingServer::ARRAY_COLOR] = col; arrays[RenderingServer::ARRAY_TEX_UV] = uv;
		arrays[RenderingServer::ARRAY_CUSTOM0] = custom; arrays[RenderingServer::ARRAY_INDEX] = idx;
		RID mesh = rs->mesh_create();
		const int64_t fmt = (int64_t)RenderingServer::ARRAY_CUSTOM_RGBA_FLOAT << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT;
		rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (BitField<RenderingServer::ArrayFormat>)fmt);
		rs->mesh_surface_set_material(mesh, 0, spatial_material(b.blend, b.zmode, b.a8, b.texflags, b.tex));
		RID inst = rs->instance_create2(mesh, scenario);
		rs->instance_set_pivot_data(inst, sortsign * (float)bi, false);
		meshes.push_back(mesh); instances.push_back(inst);
	}
}

// ---- software reference -------------------------------------------------------------------------------------

void S1Spike::software_render(std::vector<uint8_t> &out)
{
	std::vector<float> fb((size_t)W * H * 3, 0.0f), depth((size_t)W * H, 1.0f);
	int lastseq = -1;
	for (const Op &o : ops) {
		if (o.kind == 1) {
			for (size_t i = 0; i < (size_t)W * H; i++) { fb[i * 3] = (float)o.a[0]; fb[i * 3 + 1] = (float)o.a[1]; fb[i * 3 + 2] = (float)o.a[2]; }
			continue;
		}
		if (o.kind != 0) { continue; }
		const bool zt = o.s.valid && o.s.zmode > 0;
		if (zt && o.s.zclear_seq != lastseq) {
			lastseq = o.s.zclear_seq;
			for (int y = std::max(0, o.s.zc[1]); y <= std::min(H - 1, o.s.zc[3]); y++) { for (int x = std::max(0, o.s.zc[0]); x <= std::min(W - 1, o.s.zc[2]); x++) { depth[(size_t)y * W + x] = 1.0f; } }
		}
		if (o.clip[2] <= 0 || o.clip[3] <= 0) { continue; }
		const TexInfo *t = nullptr;
		if (o.d.texture) { auto it = texs.find(o.d.texture); if (it == texs.end()) { continue; } t = &it->second; }
		const bool persp = (o.d.flags & FS2_SINK_DRAWF_PERSPECTIVE) != 0;
		const int cx0 = std::max(0, o.clip[0]), cy0 = std::max(0, o.clip[1]), cx1 = std::min(W, o.clip[0] + o.clip[2]), cy1 = std::min(H, o.clip[1] + o.clip[3]);
		for (size_t ti = 0; ti + 2 < o.idx.size(); ti += 3) {
			const fs2_sink_vertex *p[3] = { &o.v[o.idx[ti]], &o.v[o.idx[ti + 1]], &o.v[o.idx[ti + 2]] };
			const float area = (p[1]->x - p[0]->x) * (p[2]->y - p[0]->y) - (p[2]->x - p[0]->x) * (p[1]->y - p[0]->y);
			if (std::fabs(area) < 1e-6f) { continue; }
			const float minx = std::min({ p[0]->x, p[1]->x, p[2]->x }), maxx = std::max({ p[0]->x, p[1]->x, p[2]->x });
			const float miny = std::min({ p[0]->y, p[1]->y, p[2]->y }), maxy = std::max({ p[0]->y, p[1]->y, p[2]->y });
			const int x0 = std::max(cx0, (int)std::floor(minx)), x1 = std::min(cx1 - 1, (int)std::ceil(maxx));
			const int y0 = std::max(cy0, (int)std::floor(miny)), y1 = std::min(cy1 - 1, (int)std::ceil(maxy));
			float zz[3], sw[3];
			for (int k = 0; k < 3; k++) { const int vi = o.idx[ti + k]; zz[k] = (zt && vi < 32) ? o.s.z[vi] : 0.0f; sw[k] = persp ? p[k]->sw : 1.0f; }
			for (int y = y0; y <= y1; y++) {
				for (int x = x0; x <= x1; x++) {
					const float px = x + 0.5f, py = y + 0.5f;
					float w0 = ((p[1]->x - px) * (p[2]->y - py) - (p[2]->x - px) * (p[1]->y - py)) / area;
					float w1 = ((p[2]->x - px) * (p[0]->y - py) - (p[0]->x - px) * (p[2]->y - py)) / area;
					float w2 = 1.0f - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) { continue; }
					const size_t pi = (size_t)y * W + x;
					const float z = w0 * zz[0] + w1 * zz[1] + w2 * zz[2];
					if (zt) { if (z > depth[pi]) { continue; } }
					float swi = w0 * sw[0] + w1 * sw[1] + w2 * sw[2];
					float u = (w0 * p[0]->u * sw[0] + w1 * p[1]->u * sw[1] + w2 * p[2]->u * sw[2]) / swi;
					float v = (w0 * p[0]->v * sw[0] + w1 * p[1]->v * sw[1] + w2 * p[2]->v * sw[2]) / swi;
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
					float *dst = &fb[pi * 3];
					if (o.d.blend == FS2_SINK_BLEND_OPAQUE) { dst[0] = cr; dst[1] = cg; dst[2] = cb; }
					else if (o.d.blend == FS2_SINK_BLEND_ADD) { dst[0] = std::min(255.0f, dst[0] + cr * ca); dst[1] = std::min(255.0f, dst[1] + cg * ca); dst[2] = std::min(255.0f, dst[2] + cb * ca); }
					else { dst[0] = cr * ca + dst[0] * (1 - ca); dst[1] = cg * ca + dst[1] * (1 - ca); dst[2] = cb * ca + dst[2] * (1 - ca); }
					if (o.s.valid && o.s.zmode == 2) { depth[pi] = z; }
				}
			}
		}
	}
	out.resize((size_t)W * H * 3);
	for (size_t i = 0; i < out.size(); i++) { out[i] = (uint8_t)std::max(0.0f, std::min(255.0f, fb[i] + 0.5f)); }
}

void S1Spike::save_rgb(const std::vector<uint8_t> &rgb, const std::string &name)
{
	PackedByteArray pba; pba.resize((int64_t)W * H * 4);
	for (int i = 0; i < W * H; i++) { pba.ptrw()[i * 4] = rgb[i * 3]; pba.ptrw()[i * 4 + 1] = rgb[i * 3 + 1]; pba.ptrw()[i * 4 + 2] = rgb[i * 3 + 2]; pba.ptrw()[i * 4 + 3] = 255; }
	Ref<Image> img = Image::create_from_data(W, H, false, Image::FORMAT_RGBA8, pba);
	img->save_png(String(out_dir.c_str()) + "/" + name.c_str());
}

void S1Spike::compare(const Ref<Image> &img0, const std::string &name)
{
	if (!have_ref) { return; }
	Ref<Image> img = img0; img->convert(Image::FORMAT_RGBA8);
	PackedByteArray d = img->get_data();
	double sum = 0; long n8 = 0, n32 = 0, n64 = 0;
	std::vector<uint8_t> diff((size_t)W * H * 3);
	for (int i = 0; i < W * H; i++) {
		int m = 0;
		for (int c = 0; c < 3; c++) { const int e = std::abs((int)d[i * 4 + c] - (int)ref_rgb[i * 3 + c]); sum += e; m = std::max(m, e); diff[i * 3 + c] = (uint8_t)std::min(255, e * 4); }
		n8 += m > 8; n32 += m > 32; n64 += m > 64;
	}
	UtilityFunctions::print("S1 compare ", name.c_str(), " vs ref: mean abs err ", sum / ((double)W * H * 3), "/255, pixels off by >8: ", (double)n8 * 100.0 / (W * H), "%, >32: ", (double)n32 * 100.0 / (W * H), "%, >64: ", (double)n64 * 100.0 / (W * H), "%");
	save_rgb(diff, "diff_" + name + ".png");
}

// ---- driver -------------------------------------------------------------------------------------------------

void S1Spike::_ready()
{
	out_dir = OS::get_singleton()->get_environment("S1_OUT").utf8().get_data();
	if (out_dir.empty()) { out_dir = "."; }
	std::string dump = OS::get_singleton()->get_environment("S1_DUMP").utf8().get_data();
	std::string ms = OS::get_singleton()->get_environment("S1_MODES").utf8().get_data();
	if (ms.empty()) { ms = "ref,canvas,opt2,opt3"; }
	for (size_t p = 0; p <= ms.size();) { size_t q = ms.find(',', p); if (q == std::string::npos) { q = ms.size(); } modes.push_back(ms.substr(p, q - p)); p = q + 1; }
	String sg = OS::get_singleton()->get_environment("S1_SORTSIGN");
	if (!sg.is_empty()) { sortsign = (float)sg.to_float(); }
	if (!load_dump(dump)) { UtilityFunctions::printerr("S1: cannot load dump ", dump.c_str()); get_tree()->quit(2); return; }
	for (auto &kv : texs) {
		TexInfo &t = kv.second;
		PackedByteArray pba; pba.resize((int64_t)t.px.size());
		std::memcpy(pba.ptrw(), t.px.data(), t.px.size());
		Ref<Image> im = Image::create_from_data(t.w, t.h, false, t.fmt == FS2_SINK_TEX_A8 ? Image::FORMAT_L8 : Image::FORMAT_RGBA8, pba);
		t.tex = ImageTexture::create_from_image(im);
	}
	{
		Ref<Image> im = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8); im->fill(Color(1, 1, 1, 1));
		white_tex = ImageTexture::create_from_image(im); white_tex_rid = white_tex->get_rid();
	}
	RenderingServer::get_singleton()->set_default_clear_color(Color(0, 0, 0, 1));
	analyse();
	set_process(true);
	phase = 0; wait = 2;
}

void S1Spike::teardown()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	for (RID r : items) { rs->free_rid(r); }
	for (RID r : instances) { rs->free_rid(r); }
	for (RID r : meshes) { rs->free_rid(r); }
	items.clear(); instances.clear(); meshes.clear();
	if (cam) { cam->queue_free(); cam = nullptr; }
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

void S1Spike::start_mode(const std::string &m)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	cur_mode = m;
	build_us = submit_us = 0; nbatches = nbatches3d = ncanvas_batches = 0;
	const int nops = (int)ops.size();
	int first_z = -1, last_z = -1;
	for (int i = 0; i < nops; i++) { if (ops[i].kind == 0 && ops[i].s.valid && ops[i].s.zmode > 0) { if (first_z < 0) { first_z = i; } last_z = i; } }
	if (m == "ref") {
		const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
		software_render(ref_rgb);
		have_ref = true;
		UtilityFunctions::print("S1 ref: software rasteriser ", (Time::get_singleton()->get_ticks_usec() - t0) / 1000.0, " ms (not an engine cost)");
		save_rgb(ref_rgb, "ref.png");
		return;
	}
	if (m == "canvas") {
		uint64_t t0 = Time::get_singleton()->get_ticks_usec();
		int fo = 0, lo = nops - 1;
		if (OS::get_singleton()->has_environment("S1_FIRST")) { fo = OS::get_singleton()->get_environment("S1_FIRST").to_int(); }
		if (OS::get_singleton()->has_environment("S1_LAST")) { lo = OS::get_singleton()->get_environment("S1_LAST").to_int(); }
		auto bs = make_batches(fo, lo, false, nullptr);
		uint64_t t1 = Time::get_singleton()->get_ticks_usec();
		emit_canvas(bs, get_canvas_item());
		uint64_t t2 = Time::get_singleton()->get_ticks_usec();
		build_us = (double)(t1 - t0); submit_us = (double)(t2 - t1); nbatches = ncanvas_batches = (int)bs.size();
	} else if (m == "opt2") {
		cam = make_cam(this, W, H); cam->make_current();
		uint64_t t0 = Time::get_singleton()->get_ticks_usec();
		int ep = 1;
		int fo = 0, lo = nops - 1;
		if (OS::get_singleton()->has_environment("S1_FIRST")) { fo = OS::get_singleton()->get_environment("S1_FIRST").to_int(); }
		if (OS::get_singleton()->has_environment("S1_LAST")) { lo = OS::get_singleton()->get_environment("S1_LAST").to_int(); }
		auto bs = make_batches(fo, lo, true, &ep);
		uint64_t t1 = Time::get_singleton()->get_ticks_usec();
		emit_3d(bs, get_viewport()->get_world_3d()->get_scenario(), ep);
		uint64_t t2 = Time::get_singleton()->get_ticks_usec();
		build_us = (double)(t1 - t0); submit_us = (double)(t2 - t1); nbatches = nbatches3d = (int)bs.size();
		UtilityFunctions::print("S1 opt2: ", nbatches, " batches, ", ep, " depth epochs");
	} else if (m == "opt3") {
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
		{
			RID sh = rs->shader_create(); rs->shader_set_code(sh, "shader_type canvas_item;\nrender_mode blend_premul_alpha;\nvoid fragment() { COLOR = texture(TEXTURE, UV); }\n");
			RID m = rs->material_create(); rs->material_set_shader(m, sh); shaders.push_back(sh); materials.push_back(m);
			rs->canvas_item_set_material(vitem, m);
		}
		rs->canvas_item_add_texture_rect(vitem, Rect2(0, 0, W, H), sub->get_texture()->get_rid());
		emit_canvas(front, get_canvas_item());
		emit_3d(mid, sub->get_viewport()->get_world_3d()->get_scenario(), ep);
		uint64_t t2 = Time::get_singleton()->get_ticks_usec();
		build_us = (double)(t1 - t0); submit_us = (double)(t2 - t1); ncanvas_batches = (int)(back.size() + front.size()); nbatches3d = (int)mid.size(); nbatches = ncanvas_batches + nbatches3d;
		UtilityFunctions::print("S1 opt3: back canvas ", (int64_t)back.size(), " batches, 3D ", (int64_t)mid.size(), " batches, front canvas ", (int64_t)front.size(), " batches, ", ep, " depth epochs");
	}
	rs->viewport_set_measure_render_time(get_viewport()->get_viewport_rid(), true);
	gpu_sum = cpu_sum = sub_gpu_sum = sub_cpu_sum = 0; samples = 0; measure_left = 150;
}

void S1Spike::finish_mode()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (cur_mode == "ref") { return; }
	Ref<Image> img = get_viewport()->get_texture()->get_image();
	img->save_png(String(out_dir.c_str()) + "/" + cur_mode.c_str() + ".png");
	compare(img, cur_mode);
	if (samples > 0) {
		UtilityFunctions::print("S1 drawcost ", cur_mode.c_str(), ": GPU ", gpu_sum / samples, " ms, viewport CPU ", cpu_sum / samples, " ms (sub viewport GPU ", sub_gpu_sum / samples, ", CPU ", sub_cpu_sum / samples,
			") per frame, mean of ", samples, "; draw calls ", (int64_t)rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME), ", primitives ", (int64_t)rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME));
	}
	UtilityFunctions::print("S1 buildcost ", cur_mode.c_str(), ": batching ", build_us / 1000.0, " ms, RenderingServer submit ", submit_us / 1000.0, " ms per frame; ", nbatches, " batches (", nbatches3d, " in 3D, ", ncanvas_batches, " on canvas)");
}

void S1Spike::_process(double)
{
	if (wait > 0) { wait--; return; }
	RenderingServer *rs = RenderingServer::get_singleton();
	if (phase == 0) {
		mode_index++;
		if (mode_index >= (int)modes.size()) { UtilityFunctions::print("S1 done"); set_process(false); get_tree()->quit(0); return; }
		start_mode(modes[mode_index]);
		if (cur_mode == "ref") { return; }
		phase = 1; wait = 4;
		return;
	}
	if (phase == 1) {
		phase = 2; wait = 16;
		return;
	}
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

static void s1_init(ModuleInitializationLevel l)
{
	if (l == MODULE_INITIALIZATION_LEVEL_SCENE) { GDREGISTER_CLASS(S1Spike); }
}
static void s1_term(ModuleInitializationLevel) {}

extern "C" GDExtensionBool GDE_EXPORT s1_library_init(GDExtensionInterfaceGetProcAddress a, GDExtensionClassLibraryPtr l, GDExtensionInitialization *o)
{
	GDExtensionBinding::InitObject io(a, l, o);
	io.register_initializer(s1_init);
	io.register_terminator(s1_term);
	io.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return io.init();
}
