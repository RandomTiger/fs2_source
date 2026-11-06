// gr_host.cpp  (R1-4 draft; depth runs R1-8)
// The neutral host renderer: fills gr_screen's gf_ table like gr_dummy_init does, but the drawing calls become
// fs2_render_sink draws (render_sink.h). It lives in the game DLL; the sink behind it is the Godot wrapper or a test fake.
//
// Rules kept here (D-20261027-190100..190107, -185304, -192331):
//  - never reads the clock, rand, hooks or os_poll; every value a caller branches on is the dummy's (zbuffer NONE,
//    lock 1, hardware 1, 32 bit screen), except where marked DECISION below
//  - game state (font, bitmap, shader, colour, clip) is updated the same whether or not the frame is drawn; a skipped
//    frame (frame_begin returned 0) skips sink calls only, including texture uploads (the sink allows them only inside
//    an accepted frame, so a texture the sink never got is uploaded on the first accepted frame that uses it)
//  - texture_bpp defaults to 32 (ARGB8888); 16 stays selectable through gr_host_set_texture_bpp()
//
// Draft scope: state, clip, colour, rect/shade/line/pixel/gradient/circle, tmapper, bitmap/aabitmap, string, frame
// calls, texture cache. Not yet: shaders on 3D polys, cross_fade. Ship glow (the D3D glow redraw pass): see "ship glow" below.
// Compile check: see README, "R1-4 draft". 2D bitmaps are never split into sections here: gr_bitmap is one whole quad
// (h_bitmap), and a TMAP_FLAG_BITMAP_2D poly samples the whole bitmap, unfiltered (D-20261029-222605).
//
// Depth (R1-8, sink v2): gr_zbuffer_get/set still return the dummy's GR_ZBUFF_NONE, so a caller's save/restore pair hands
// back NONE. The host keeps its own shadow of the D3D mode, never visible to the game: a get() followed by a set() saves
// the mode on a host stack, and a set(NONE) with no get() before it is taken as the matching restore. Draws then carry
// the D3D depth state of gr_d3d_tmapper_internal (FULL, or READ for scalers and additive polys) and the D3D sz. A
// gr_zbuffer_clear(TRUE) opens a sink depth run (or clears depth in the clip rect inside one); gr_zbuffer_clear(FALSE)
// and gr_flip close it. Known miss: a set(NONE) that is not a restore but comes inside a saved scope (the background
// model, MR_NO_ZBUFFER, under the starfield's save) pops the scope instead of turning depth off.

#include "2d.h"
#include "GrInternal.h"
#include "bmpman.h"
#include "Font.h"
#include "AnimPlay.h"
#include "PackUnpack.h"
#include "cmdline.h"
#include "Mouse.h"
// Line.h's INT_CLIPLINE scales with the Win32 MulDiv; this is the same (rounds to nearest, halves away from zero)
static inline int MulDiv(int number, int numerator, int denominator)
{
	if (denominator == 0) { return -1; }
	long long prod = (long long)number * (long long)numerator;
	const bool neg = (prod < 0) != (denominator < 0);
	if (prod < 0) { prod = -prod; }
	const long long den = denominator < 0 ? -(long long)denominator : (long long)denominator;
	long long q = (prod + den / 2) / den;
	return (int)(neg ? -q : q);
}
#include "Line.h"
#include "Cfile.h"
#include "Nebula.h"
#include "PalMan.h"
#include "Neb.h"
#include "pcxutils.h"
#include "Model.h"
#include "Ship.h"
#include "MissionParse.h"
#include <cstring>

#include "render_sink.h"
#include "tex_convert.h"

#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cmath>

// GrD3D.cpp's screen-depth flag: 1 = the screen is 32 bit. Only D3D init sets it; bm_get_components reads it to pick the
// 32 bit read of screen-format pixels (D-20261029-095513). host_init sets it for the host's 32 bit screen.
extern int D3D_32bit;

// ModelRead.cpp: the loaded models, for the ship glow's Shivan test (declared there, not in a header; D3D's GrD3DGlow.cpp does the same)
extern polymodel *Polygon_models[MAX_POLYGON_MODELS];

namespace {

#define NEBULA_COLORS 20		// as GrD3DRender.cpp

enum FrameState { FRAME_NONE = 0, FRAME_OPEN = 1, FRAME_SKIPPED = 2 };

struct HostTex {
	int sink_id;		// 0 = not uploaded (a TK_GLOW entry: no glow for this bitmap)
	int type;			// TCACHE_TYPE_* it was uploaded as
	int w, h;
	bool opaque;		// RGBA8 texture with every alpha 255 (a mesh group of it may be drawn opaque)
};

// one queued ship glow poly (see "ship glow")
struct GlowPoly {
	fs2_sink_draw d;
	int first, nv;		// into Host::glow_v
};

// texture cache key: (bitmap handle, bmpman signature, kind), see tex_get()
struct TexKey {
	int handle;
	uint32_t signature;
	int kind;
	bool operator==(const TexKey &o) const { return handle == o.handle && signature == o.signature && kind == o.kind; }
};
struct TexKeyHash {
	size_t operator()(const TexKey &k) const
	{
		const uint64_t v = ((uint64_t)(uint32_t)k.handle << 32) ^ ((uint64_t)k.signature << 4) ^ (uint64_t)(uint32_t)k.kind;
		return std::hash<uint64_t>()(v);
	}
};

struct Host {
	fs2_render_sink sink_copy = {};		// the DLL keeps its own copy of the sink table (the caller's may move or die)
	const fs2_render_sink *sink = nullptr;	// &sink_copy while attached
	int frame = FRAME_NONE;
	int texture_bpp = 32;			// 32 default; 16 selectable (ColourDepth=16 / -force_16bit)
	int next_sink_id = 1;
	int next_capture_id = 1;
	std::unordered_map<TexKey, HostTex, TexKeyHash> tex;	// see TexKey
	std::vector<int> release_pending;	// sink textures of released bitmaps, freed at the start of the next accepted frame (bm_release hook)
	std::vector<int> capture_pending;	// save_screen ids asked for in a skipped frame: captured at the start of the next accepted frame
	bool flush_pending = false;		// drop every sink texture at the start of the next accepted frame (flush, gamma change)
	int gamma_lookup[256];			// the host's own gamma table; gr_gamma_lookup stays untouched, as with the dummy
	float inv_gamma = 1.0f;			// the exponent the table was built with
	// fog state kept here (R1_checks 1c); the dummy writes none of gr_screen's fog fields
	int fog_mode = 0, fog_r = 0, fog_g = 0, fog_b = 0;
	float fog_near = 0.0f, fog_far = 0.0f;
	fs2_sink_mesh_fog mesh_fog_sent = {};	// the fog the sink last got for meshes (mesh_fog_valid: it got one)
	bool mesh_fog_valid = false, mesh_fog_ok = true;	// mesh_fog_ok: the sink took the fog it last got
	bool load_mode = false;	// h_level_loaded is uploading: texture_create and mesh_create are legal without a frame
	int bb_tex_late = 0;	// billboard textures uploaded at a draw because load had not
	int mesh_tex_late = 0;	// textures a mesh draw uploaded because load had not (tiled, thruster); reads 0 in play
	int mesh_skipped = 0;	// mesh draws dropped because the sink skipped the frame
	int mesh_fb[6] = {};	// mesh draws that fell back to the polygon path, by reason: cpu_only, no z, fog unsupported, no frame, texture, fog state
	int mesh_sink_late = 0;	// sink meshes created at a draw instead of at level load (D-20261104-084045); reads 0 in play
	std::unordered_map<uint64_t, int> head_layer;	// talking heads preloaded at level load: pixel hash of a frame (size included) -> its layer's texture id
	int head_frames = 0, head_hits = 0;	// frames preloaded, and play-time head frames that found their layer
	int beam_offered = 0, beam_gpu = 0;	// beams handed to the sink / drawn by it (R3 row 1); declined = offered - drawn, must be 0
	int trail_offered = 0, trail_gpu = 0;	// trail section polygons handed to the sink / drawn by it (R3 row 2); declined = offered - drawn, must be 0
	int shield_offered = 0, shield_gpu = 0;	// shield hit polygons (triangles; a quad is two) handed to the sink / drawn by it (R3 row 4); declined = offered - drawn, must be 0
	int warp_offered = 0, warp_gpu = 0;	// warp-in cone triangles handed to the sink / drawn by it (R3 row 6); declined = offered - drawn, must be 0
	int star_offered = 0, star_gpu = 0;	// starfield background bitmap triangles handed to the sink / drawn by it (R3 row 7); declined = offered - drawn, must be 0
	int sub_offered = 0, sub_gpu = 0;	// subspace tunnel triangles (hull and glow polys) handed to the sink / drawn by it (R3 row 6); declined = offered - drawn, must be 0
	int light_offered = 0, light_gpu = 0;	// lightning bolt quads handed to the sink / drawn by it (R3 row 5); declined = offered - drawn, must be 0
	int bb_sink = 0, bb_cpu = 0, bb_why[5] = {}; unsigned bb_flags = 0;	// billboard draws the sink took / that stayed on the CPU path (R3)
	// depth shadow (see top): host only, the game reads none of it
	int zmode = GR_ZBUFF_NONE;		// the mode D3D would have (gr_zbuffering_mode)
	bool zglobal = false;			// gr_zbuffer_clear(TRUE) seen since the last clear(FALSE) / flip
	int zstack[32];
	int zsp = 0;
	bool zget_pending = false;		// get() seen, the next set() is the save half of a pair
	int zget_saved = GR_ZBUFF_NONE;
	bool run_open = false;			// a sink depth run is open (drawn frames only)
	bool view_sent = false;			// H.view was sent to the sink in this run
	fs2_sink_view view = {};		// the camera last sent (v3)
	int zbias = 0;
	int cull = 0;
	// ship glow queue (see "ship glow"): host only
	std::vector<GlowPoly> glow_q;
	std::vector<fs2_sink_vertex> glow_v;
	bool glow_hold = false;			// a ship poly's lit draw is being made: it does not flush the queue
	// render 3D meshes (P3b): the model builder hands geometry over once; the sink copy is made on the first draw in an accepted frame
	struct HostMesh {
		std::vector<fs2_sink_mesh_vertex> v;
		std::vector<gr_host_mesh_group> g;
		std::vector<uint32_t> idx;
		bool sink_made = false;
		float bmin[3] = {}, bmax[3] = {};	// model space bounds: a draw that reaches the near plane stays on the polygon path (it clips there)
		bool cpu_only = false;				// the mesh cannot be drawn by the sink (a texture failed to upload): stays on the polygon path
		std::vector<int> poly_nv;			// vertex count of each polygon, in index order (a fan of nv-2 triangles)
		int zmode = -1, zbias = 0, cull = 0, tiled = 0;	// the state the sink copy was made for
		std::vector<int> tex;				// sink texture per group at creation
		std::vector<int> blend;
	};
	std::unordered_map<int, HostMesh> meshes;
	std::vector<uint8_t> mesh_rgba;
} H;

inline bool drawing() { return H.frame == FRAME_OPEN; }

// ---- ship glow: the queue ---------------------------------------------------------------------------------------
// Glide lit chosen texture colours on ship hulls at full brightness whatever the lighting; Enhanced D3D redraws each such poly
// unlit with a glow texture (the texture's colours, alpha only on the glowing texels), alpha blended over the lit draw, z read only
// (GrD3DGlow.cpp, docs/SHIVAN_GLOW.md). The host does the same: tmapper_internal queues the second draw here, and the queue is
// drawn, in batches of one texture, before anything that is not a ship poly reaches the sink (every flush point below), as D3D's
// d3d_glow_draw_queued. The sink gets them as FS2_SINK_DRAWF_OVERLAY draws.
void glow_flush()
{
	if (H.glow_q.empty()) { return; }
	std::vector<GlowPoly> q;
	std::vector<fs2_sink_vertex> verts;
	q.swap(H.glow_q);
	verts.swap(H.glow_v);
	if (!drawing()) { return; }

	// one texture after another, so a model's polys join into long runs; the glow pass only reads depth, so their order does not
	// change the picture (stable sort: same texture keeps call order)
	std::vector<int> order(q.size());
	for (size_t i = 0; i < q.size(); i++) { order[i] = (int)i; }
	std::stable_sort(order.begin(), order.end(), [&q](int a, int b) {
		if (q[(size_t)a].d.texture != q[(size_t)b].d.texture) { return q[(size_t)a].d.texture < q[(size_t)b].d.texture; }
		return q[(size_t)a].d.zbias < q[(size_t)b].d.zbias;
	});

	std::vector<fs2_sink_vertex> bv;
	std::vector<uint16_t> bi;
	const GlowPoly *head = nullptr;
	auto same = [](const fs2_sink_draw &a, const fs2_sink_draw &b) {
		return a.texture == b.texture && a.blend == b.blend && a.flags == b.flags && a.zbias == b.zbias && a.fog_mode == b.fog_mode
			&& std::memcmp(a.fog_rgb, b.fog_rgb, 4) == 0 && a.fog_near == b.fog_near && a.fog_far == b.fog_far;
	};
	auto emit = [&]() {
		if (head != nullptr && !bi.empty()) { H.sink->draw_tris(H.sink->user, &head->d, bv.data(), (int)bv.size(), bi.data(), (int)bi.size()); }
		bv.clear(); bi.clear(); head = nullptr;
	};
	for (int oi : order) {
		const GlowPoly &p = q[(size_t)oi];
		if (head != nullptr && (!same(head->d, p.d) || bv.size() + (size_t)p.nv > 3000u)) { emit(); }
		if (head == nullptr) { head = &p; }
		const uint16_t base = (uint16_t)bv.size();
		for (int k = 0; k < p.nv; k++) { bv.push_back(verts[(size_t)(p.first + k)]); }
		for (int k = 0; k < p.nv - 2; k++) {
			bi.push_back(base); bi.push_back((uint16_t)(base + k + 1)); bi.push_back((uint16_t)(base + k + 2));
		}
	}
	emit();
}

// ---- frame ----------------------------------------------------------------------------------------------------

void send_clip()
{
	glow_flush();		// queued glow polys belong to the clip they were drawn under
	H.sink->set_clip(H.sink->user, gr_screen.offset_x + gr_screen.clip_left, gr_screen.offset_y + gr_screen.clip_top,
		gr_screen.clip_width, gr_screen.clip_height);
}

// first call that needs the sink in a frame: ask whether this frame is drawn
void ensure_frame()
{
	H.zget_pending = false;		// a draw (or a clear) between get() and set(): no save pair
	if (H.frame != FRAME_NONE || H.sink == nullptr) { return; }
	H.frame = H.sink->frame_begin(H.sink->user, gr_screen.max_w, gr_screen.max_h) ? FRAME_OPEN : FRAME_SKIPPED;
	if (drawing()) {
		if (H.flush_pending) {
			// bitmap textures only: a screen saved with save_screen survives a flush, as in D3D
			for (auto &kv : H.tex) { if (kv.second.sink_id != 0) { H.sink->texture_free(H.sink->user, kv.second.sink_id); } }
			H.tex.clear(); H.head_layer.clear();
			H.flush_pending = false;
		}
		for (int id : H.release_pending) { H.sink->texture_free(H.sink->user, id); }
		H.release_pending.clear();
		// A screen saved in a skipped frame is captured now. The frames between drew nothing, so the frame on show is the one
		// it was when the game asked. A failed capture leaves no texture; the id then restores nothing, as one that failed at once.
		for (int id : H.capture_pending) { H.sink->capture_to_texture(H.sink->user, id); }
		H.capture_pending.clear();
		send_clip();
	}
}

// ---- helpers --------------------------------------------------------------------------------------------------

inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

void draw(const fs2_sink_draw &d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	if (!H.glow_hold) { glow_flush(); }		// anything but a ship poly's lit draw: the queued glow lands first
	H.sink->draw_tris(H.sink->user, &d, v, nv, idx, ni);
}

fs2_sink_draw plain_draw(int texture, int blend, unsigned flags = 0)
{
	fs2_sink_draw d = {};
	d.texture = texture; d.blend = blend; d.flags = flags;
	d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 255;
	// fog state of the call, reserved in sink v2 (R1-10)
	d.fog_mode = H.fog_mode;
	d.fog_rgb[0] = clamp8(H.fog_r); d.fog_rgb[1] = clamp8(H.fog_g); d.fog_rgb[2] = clamp8(H.fog_b); d.fog_rgb[3] = 255;
	d.fog_near = H.fog_near; d.fog_far = H.fog_far;
	return d;
}

fs2_sink_vertex vtx(float x, float y, float u, float v, int r, int g, int b, int a)
{
	fs2_sink_vertex s;
	s.x = x; s.y = y; s.sw = 1.0f; s.u = u; s.v = v;
	s.r = clamp8(r); s.g = clamp8(g); s.b = clamp8(b); s.a = clamp8(a);
	s.z = 0.99f;	// D3D's sz of 2D draws
	return s;
}

// untextured or textured axis aligned quad, screen coordinates already offset
void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, int texture, int blend,
	const int c0[4], const int c1[4], const int c2[4], const int c3[4])
{
	static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	const fs2_sink_vertex v[4] = {
		vtx(x0, y0, u0, v0, c0[0], c0[1], c0[2], c0[3]), vtx(x1, y0, u1, v0, c1[0], c1[1], c1[2], c1[3]),
		vtx(x1, y1, u1, v1, c2[0], c2[1], c2[2], c2[3]), vtx(x0, y1, u0, v1, c3[0], c3[1], c3[2], c3[3]) };
	const fs2_sink_draw d = plain_draw(texture, blend);
	draw(d, v, 4, idx, 6);
}

void solid_rect(float x0, float y0, float x1, float y1, int r, int g, int b, int a)
{
	const int c[4] = { r, g, b, a };
	quad(x0, y0, x1, y1, 0, 0, 0, 0, 0, a < 255 ? FS2_SINK_BLEND_ALPHA : FS2_SINK_BLEND_OPAQUE, c, c, c, c);
}

// ---- texture cache --------------------------------------------------------------------------------------------

// TK_2D: a TMAP_FLAG_BITMAP_2D poly's texture, the whole bitmap unfiltered
// TK_GLOW: a ship texture's glow texture (glow_tex_get); sink_id 0 = decided, this texture does not glow
enum TexKind { TK_NORMAL = 0, TK_AA = 1, TK_XPARENT = 2, TK_REPEAT = 3, TK_2D = 4, TK_GLOW = 5 };

// BmpMan is freeing a user bitmap (Bm_release_hook): drop every texture cached for the handle, so the cache does not grow and a
// reused handle never shows the old picture. Host side only; the sink frees at the next accepted frame.
void on_bm_release(int handle)
{
	for (auto it = H.tex.begin(); it != H.tex.end(); ) {
		if (it->first.handle != handle) { ++it; continue; }
		if (it->second.sink_id != 0) { H.release_pending.push_back(it->second.sink_id); }
		it = H.tex.erase(it);
	}
}

// AABITMAP data is a palette index 0..14 (15 = 1, above 15 = 0), turned into coverage through the gamma table as D3D does
void aa_to_a8(const uint8_t *src, int w, int h, int src_pitch, uint8_t *dst)
{
	uint8_t xlat[256];
	for (int i = 0; i < 256; i++) {
		int v = i;
		if (v == 15) { v = 1; } else if (v > 15) { v = 0; }
		xlat[i] = (uint8_t)H.gamma_lookup[(v * 255) / 15];
	}
	for (int y = 0; y < h; y++) {
		const uint8_t *row = src + (size_t)y * (size_t)src_pitch;
		for (int x = 0; x < w; x++) { dst[(size_t)y * (size_t)w + (size_t)x] = xlat[row[x]]; }
	}
}

// LEGACY PIXEL COMPATIBILITY (#133): copies a bug in the original D3D texture upload so pictures match retail.
// d3d_create_texture_sub (and GrD3DGlow) resample TCACHE_TYPE_NORMAL textures with a fixed-point step of (w-1)/w, so
// at full size texel i holds source texel i-1: every normal texture moves one texel down-right and loses its last
// column and row. It only shows where texels are big on screen (the nebula poofs, about 6 px).
// GodotEnhanced removes this (issue #134): there the textures go up as they are.
void legacy_d3d_texel_shift(std::vector<uint8_t> &px, int w, int h)
{
	if (w < 2 || h < 2) { return; }
	std::vector<uint8_t> out(px.size());
	const int du = ((w - 1) << 16) / w, dv = ((h - 1) << 16) / h;	// fix, as D3D: ((bmap_w-1)*F1_0) / tex_w
	for (int y = 0; y < h; y++) {
		const int sy = (y * dv) >> 16;
		for (int x = 0; x < w; x++) {
			memcpy(&out[((size_t)y * w + x) * 4], &px[((size_t)sy * w + ((x * du) >> 16)) * 4], 4);
		}
	}
	px.swap(out);
}

// The pixels of a bitmap as the sink takes them, for the kind of texture asked (see tex_get). False when BmpMan cannot lock it.
static bool tex_pixels(int bitmap_id, int type, bool bitmap_2d, std::vector<uint8_t> &px, int &w, int &h, int &format)
{
	const bool aa = (type == TCACHE_TYPE_AABITMAP);
	// the flags D3D's texture loaders ask bm_lock for (d3d_create_texture; a 2D bitmap's tiles as d3d_create_texture_sectioned)
	ubyte flags = 0;
	ubyte bpp = 16;			// the game's convention: ask for 16, BmpMan upgrades to 32 for gr_screen.texture_bpp == 32
	if (aa) { flags = BMP_AABITMAP; bpp = 8; }
	else if (bitmap_2d) { flags = BMP_TEX_XPARENT; }
	else if (type == TCACHE_TYPE_XPARENT) { flags = BMP_TEX_XPARENT; }
	else { flags = BMP_TEX_OTHER | BMP_TEX_XPARENT; }		// TCACHE_TYPE_NORMAL
	bitmap *bmp = bm_lock(bitmap_id, bpp, flags);
	if (bmp == nullptr) { return false; }

	w = bmp->w; h = bmp->h;
	const uint8_t *src = (const uint8_t *)bmp->data;
	if (aa) {
		px.resize(fs2host::a8_bytes(w, h));
		aa_to_a8(src, w, h, w, px.data());
		format = FS2_SINK_TEX_A8;
	} else {
		const int bytes = (bmp->bpp == 32) ? 4 : 2;
		const fs2host::PixLayout &l = fs2host::layout_for_texture_bpp(bmp->bpp == 32 ? 32 : 16);
		const int pitch = w * bytes;
		px.resize(fs2host::rgba8_bytes(w, h));
		fs2host::to_rgba8(src, w, h, pitch, l, px.data());
		format = FS2_SINK_TEX_RGBA8;
		if (type == TCACHE_TYPE_NORMAL && !bitmap_2d) { legacy_d3d_texel_shift(px, w, h); }	// LEGACY PIXEL COMPATIBILITY (#133)
	}
	bm_unlock(bitmap_id);

	return true;
}

// Talking heads (see head_preload): a frame's pixels hashed, with its size
static uint64_t pixel_hash(const std::vector<uint8_t> &px, int w, int h)
{
	uint64_t x = 1469598103934665603ull ^ ((uint64_t)w << 32) ^ (uint64_t)h;
	for (uint8_t c : px) { x = (x ^ c) * 1099511628211ull; }
	return x;
}

// Upload a bitmap as the kind asked for. Only legal in an accepted frame. Returns the sink id or 0.
// type is a TCACHE_TYPE_*; repeat = wrap u and v (TILED polys); bitmap_2d = the whole bitmap, unfiltered (TMAP_FLAG_BITMAP_2D).
int tex_get(int bitmap_id, int type, bool repeat = false, bool bitmap_2d = false)
{
	if ((!drawing() && !H.load_mode) || bitmap_id < 0) { return 0; }
	const bool aa = (type == TCACHE_TYPE_AABITMAP);
	int kind = TK_NORMAL;
	if (aa) { kind = TK_AA; }
	else if (bitmap_2d) { kind = TK_2D; }
	else if (type == TCACHE_TYPE_XPARENT) { kind = TK_XPARENT; }
	else if (repeat) { kind = TK_REPEAT; }
	TexKey key = { bitmap_id, bm_get_signature(bitmap_id), kind };
	auto found = H.tex.find(key);
	if (found != H.tex.end() && found->second.sink_id != 0) { return found->second.sink_id; }

	std::vector<uint8_t> px;
	int w, h, format;
	if (!tex_pixels(bitmap_id, type, bitmap_2d, px, w, h, format)) { return 0; }
	key.signature = (uint32_t)bm_get_signature(bitmap_id);		// loading the pixels bumps it: the entry goes under the one the next lookup will use
	if (kind == TK_AA && !H.head_layer.empty() && w == 160 && h == 120) {	// a talking head frame decoded at play: its layer from level load
		auto hl = H.head_layer.find(pixel_hash(px, w, h));
		if (hl != H.head_layer.end()) { H.head_hits++; return hl->second; }
	}
	HostTex &t = H.tex[key];
	t.sink_id = H.next_sink_id++;
	t.type = type; t.w = w; t.h = h;
	t.opaque = (format == FS2_SINK_TEX_RGBA8);
	if (t.opaque) {
		for (size_t i = 3; i < px.size(); i += 4) { if (px[i] != 255) { t.opaque = false; break; } }
	}
	// UI text and 2D bitmaps keep integer texels (D3D: TEXTURE_SOURCE_NO_FILTERING); everything else is bilinear
	uint32_t tf = (aa || bitmap_2d) ? 0 : FS2_SINK_TEXF_LINEAR;
	if (kind == TK_REPEAT) { tf |= FS2_SINK_TEXF_REPEAT; }
	H.sink->texture_create(H.sink->user, t.sink_id, w, h, format, tf, px.data());
	return t.sink_id;
}

// An animation at level load, the kind a billboard draws (TK_NORMAL): the frame count is known, so the sink gets one array of exactly that many
// layers, each loaded from its frame's own pixels, and every frame's texture id names its layer. Frames already uploaded, a sink without the array
// entries, or frames that differ in size or fail to lock fall back to one tex_get per frame.
static void tex_get_anim(int handle, int nframes)
{
	bool arrays = nframes > 1 && H.sink->texture_array_create != nullptr && H.sink->texture_layer != nullptr;
	for (int f = 0; arrays && f < nframes; f++) {
		if (H.tex.count(TexKey{ handle + f, bm_get_signature(handle + f), TK_NORMAL }) != 0) { arrays = false; }
	}
	std::vector<uint8_t> all;
	std::vector<char> opaque;
	int w = 0, h = 0, format = 0;
	for (int f = 0; arrays && f < nframes; f++) {
		std::vector<uint8_t> px;
		int fw, fh, ff;
		if (!tex_pixels(handle + f, TCACHE_TYPE_NORMAL, false, px, fw, fh, ff)) { arrays = false; break; }
		if (f == 0) { w = fw; h = fh; format = ff; }
		else if (fw != w || fh != h || ff != format) { arrays = false; break; }
		bool op = (ff == FS2_SINK_TEX_RGBA8);
		for (size_t i = 3; op && i < px.size(); i += 4) { if (px[i] != 255) { op = false; } }
		opaque.push_back(op ? 1 : 0);
		all.insert(all.end(), px.begin(), px.end());
	}
	if (!arrays) {
		for (int f = 0; f < nframes; f++) { tex_get(handle + f, TCACHE_TYPE_NORMAL, false); }
		return;
	}
	const int array_id = H.next_sink_id++;
	H.sink->texture_array_create(H.sink->user, array_id, w, h, nframes, format, FS2_SINK_TEXF_LINEAR, all.data());
	for (int f = 0; f < nframes; f++) {
		HostTex &t = H.tex[TexKey{ handle + f, bm_get_signature(handle + f), TK_NORMAL }];
		t.sink_id = H.next_sink_id++;
		t.type = TCACHE_TYPE_NORMAL; t.w = w; t.h = h;
		t.opaque = opaque[(size_t)f] != 0;
		H.sink->texture_layer(H.sink->user, t.sink_id, array_id, f);
	}
}


// ---- talking heads at level load ---------------------------------------------------------------------------------
// A head is not a BmpMan level bitmap: the game decodes its ANI at play time into a fresh 160x120 user bitmap per frame (anim_show_next_frame),
// so a head frame has no stable handle. At load each head the messages can play (bm_page_in_listed, message_page_in_heads) is decoded with
// the game's own decode (init_anim_instance, anim_get_next_raw_buffer, a user bitmap as the play path makes it, tex_pixels as bitmap_piece
// asks for it), uploaded as one exact-size array, and each layer is filed under a hash of its pixels. A play-time frame that hashes the same
// takes that layer (tex_get). Decode only: no game state, rand or clock; the anim is released again so play loads it as before.

static void head_preload(const char *name)
{
	if (H.sink->texture_array_create == nullptr || H.sink->texture_layer == nullptr) { return; }
	anim *a = anim_load((char *)name, 0);
	if (a == nullptr) { return; }
	const int bpp = 8;	// a head plays with the HUD colour (aa_color): 8-bit alpha frames, no palette translation
	anim_instance *inst = init_anim_instance(a, bpp);
	std::vector<uint8_t> all;
	std::vector<uint64_t> hashes;
	int w = 0, h = 0, format = 0, nframes = 0;
	bool ok = (inst != nullptr);
	while (ok) {
		ubyte *buf = anim_get_next_raw_buffer(inst, 0, 1, bpp);
		if (buf == nullptr) { break; }
		const int bm = bm_create(8, a->width, a->height, buf, BMP_AABITMAP);
		if (bm < 0) { ok = false; break; }
		std::vector<uint8_t> px;
		int fw, fh, ff;
		const bool got = tex_pixels(bm, TCACHE_TYPE_AABITMAP, false, px, fw, fh, ff);
		bm_release(bm);
		if (!got || ff != FS2_SINK_TEX_A8 || (nframes > 0 && (fw != w || fh != h))) { ok = false; break; }
		w = fw; h = fh; format = ff;
		hashes.push_back(pixel_hash(px, w, h));
		all.insert(all.end(), px.begin(), px.end());
		nframes++;
	}
	if (inst != nullptr) { free_anim_instance(inst); }
	anim_free(a);
	if (!ok || nframes == 0) { return; }
	const int array_id = H.next_sink_id++;
	H.sink->texture_array_create(H.sink->user, array_id, w, h, nframes, format, 0, all.data());
	for (int f = 0; f < nframes; f++) {
		if (H.head_layer.count(hashes[(size_t)f]) != 0) { continue; }	// the same picture as a frame already filed (a repeated key frame)
		HostTex &t = H.tex[TexKey{ -(1000 + H.head_frames), 0, TK_AA }];	// a handle no bitmap has: freed with the others on a flush
		t.sink_id = H.next_sink_id++;
		t.type = TCACHE_TYPE_AABITMAP; t.w = w; t.h = h; t.opaque = false;
		H.sink->texture_layer(H.sink->user, t.sink_id, array_id, f);
		H.head_layer[hashes[(size_t)f]] = t.sink_id;
		H.head_frames++;
	}
}

// ---- ship glow: the glow texture ------------------------------------------------------------------------------
// The same rules as GrD3DGlow.cpp (docs/SHIVAN_GLOW.md), minus the D3D texture plumbing:
//  - only textures used by a Shivan ship class glow (GLOW_SHIVAN_ONLY there), matched by POF file name;
//  - the colour list is that class's $ND: list, else the pixels.tbl +ND default;
//  - a palette entry glows on an exact RGB match, or (forced red, -noforceglow turns it off) red >= 170 with green and blue <= 10;
//  - the transparent colour (0 255 0) never glows and stays see-through (rgba 0);
//  - every other texel keeps its real colour, alpha 255 where it glows and 0 where it does not (bilinear filtering then mixes
//    colour and mask as Glide's masked texture did);
//  - read straight from the 8 bit PCX, which must be the bitmap's size (this skips ANI frames).
// The texture is the bitmap's full size (the host's lit texture is too; D3D's were shrunk with the texture detail setting).
// Built on first use in an accepted frame, kept in H.tex like any texture, so it is freed with the bitmap (on_bm_release) and
// on a flush. Unlike D3D this can build mid mission (the host's textures upload on first use too), only inside an accepted frame.
// Reads only tables, the PCX file and the loaded models: no game state, rand or clock.

#define GLOW_FORCE_MIN_RED	170
#define GLOW_FORCE_MAX_GB	10

// the Shivan ship class whose model uses this texture, or -1 (GrD3DGlow.cpp d3d_glow_get_shivan_class)
int glow_shivan_class(int bitmap_handle)
{
	for (int i = 0; i < MAX_POLYGON_MODELS; i++) {
		polymodel *pm = Polygon_models[i];
		if (pm == nullptr) { continue; }
		int j;
		for (j = 0; j < pm->n_textures; j++) {
			if (pm->original_textures[j] == bitmap_handle || pm->textures[j] == bitmap_handle) { break; }
		}
		if (j == pm->n_textures) { continue; }
		for (int si = 0; si < Num_ship_types; si++) {
			if (Ship_info[si].species == SPECIES_SHIVAN && !stricmp(Ship_info[si].pof_file, pm->filename)) { return si; }
		}
	}
	return -1;
}

// Returns the sink id of the glow texture for a bitmap, or 0: no glow for it (not a Shivan texture, no glowing texel, not a PCX
// of the bitmap's size, glow off). The answer is cached either way.
int glow_tex_get(int bitmap_id)
{
	if ((!drawing() && !H.load_mode) || bitmap_id < 0 || Cmdline_no_ship_glow) { return 0; }
	const TexKey key = { bitmap_id, bm_get_signature(bitmap_id), TK_GLOW };
	auto found = H.tex.find(key);
	if (found != H.tex.end()) { return found->second.sink_id; }
	{
		HostTex &none = H.tex[key];		// 0 = decided: no glow; replaced below when one is built
		none.sink_id = 0; none.type = TCACHE_TYPE_NORMAL; none.w = 0; none.h = 0;
	}

	const int si = glow_shivan_class(bitmap_id);
	if (si < 0) { return 0; }
	ubyte (*colors)[3] = Palman_non_darkening_default;
	int ncolors = Palman_num_nondarkening_default;
	if (Ship_info[si].num_nondark_colors > 0) {
		colors = Ship_info[si].nondark_colors;
		ncolors = Ship_info[si].num_nondark_colors;
	}
	if (ncolors < 1 && Cmdline_no_force_glow) { return 0; }

	char *filename = bm_get_filename(bitmap_id);
	if (filename == nullptr || filename[0] == 0) { return 0; }
	int pw = 0, ph = 0, bw = 0, bh = 0;
	if (pcx_read_header(filename, &pw, &ph, nullptr) != PCX_ERROR_NONE) { return 0; }
	bm_get_info(bitmap_id, &bw, &bh);
	if (pw < 1 || ph < 1 || pw != bw || ph != bh) { return 0; }
	std::vector<ubyte> idx((size_t)pw * (size_t)ph);
	ubyte palette[768];
	if (pcx_read_bitmap_8bpp(filename, idx.data(), palette) != PCX_ERROR_NONE) { return 0; }

	bool glows[256];
	for (int i = 0; i < 256; i++) {
		const ubyte *c = &palette[i * 3];
		glows[i] = false;
		if (c[0] == 0 && c[1] == 255 && c[2] == 0) { continue; }		// the transparent colour of pcx_read_bitmap_16bpp
		for (int k = 0; k < ncolors; k++) {
			if (c[0] == colors[k][0] && c[1] == colors[k][1] && c[2] == colors[k][2]) { glows[i] = true; break; }
		}
		if (!glows[i] && !Cmdline_no_force_glow && c[0] >= GLOW_FORCE_MIN_RED && c[1] <= GLOW_FORCE_MAX_GB && c[2] <= GLOW_FORCE_MAX_GB) { glows[i] = true; }
	}
	size_t nglow = 0;
	for (size_t i = 0; i < idx.size(); i++) { if (glows[idx[i]]) { nglow++; } }
	if (nglow == 0) { return 0; }

	std::vector<uint8_t> px(fs2host::rgba8_bytes(pw, ph));
	for (size_t i = 0; i < idx.size(); i++) {
		const ubyte *c = &palette[idx[i] * 3];
		uint8_t *o = &px[i * 4];
		if (c[0] == 0 && c[1] == 255 && c[2] == 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
		o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
		o[3] = glows[idx[i]] ? 255 : 0;
	}
	legacy_d3d_texel_shift(px, pw, ph);	// LEGACY PIXEL COMPATIBILITY (#133): keeps the glow on its shifted ship texture
	HostTex &t = H.tex[key];
	t.sink_id = H.next_sink_id++;
	t.type = TCACHE_TYPE_NORMAL; t.w = pw; t.h = ph;
	H.sink->texture_create(H.sink->user, t.sink_id, pw, ph, FS2_SINK_TEX_RGBA8, FS2_SINK_TEXF_LINEAR, px.data());
	return t.sink_id;
}

// ---- gf_ table: state (identical effects to the dummy / D3D bookkeeping) ----------------------------------------

void depth_run_end()
{
	glow_flush();
	if (H.run_open && drawing()) { H.sink->depth_end(H.sink->user); }
	H.run_open = false;
	H.view_sent = false;
}

// v3: the 3D camera, read from the g3 globals (g3_set_view_matrix runs after g3_start_frame has opened the run), sent when it differs
// from the one the sink has. Reads game state only.
} // namespace
extern void model_interp_mesh_preload(int modelnum);	// ModelInterp.cpp
extern int model_interp_mesh_late();
extern void model_interp_mesh_preload_all();
extern float View_zoom;
extern vector Matrix_scale;
extern matrix Eye_matrix;
extern vector Eye_position;
extern int Canvas_width, Canvas_height;
extern int G3_user_clip;
extern vector G3_user_clip_normal, G3_user_clip_point;
namespace {
void send_view()
{
	fs2_sink_view v = {};
	v.eye_pos[0] = Eye_position.x; v.eye_pos[1] = Eye_position.y; v.eye_pos[2] = Eye_position.z;
	v.eye_rvec[0] = Eye_matrix.rvec.x; v.eye_rvec[1] = Eye_matrix.rvec.y; v.eye_rvec[2] = Eye_matrix.rvec.z;
	v.eye_uvec[0] = Eye_matrix.uvec.x; v.eye_uvec[1] = Eye_matrix.uvec.y; v.eye_uvec[2] = Eye_matrix.uvec.z;
	v.eye_fvec[0] = Eye_matrix.fvec.x; v.eye_fvec[1] = Eye_matrix.fvec.y; v.eye_fvec[2] = Eye_matrix.fvec.z;
	v.scale[0] = Matrix_scale.x; v.scale[1] = Matrix_scale.y; v.scale[2] = Matrix_scale.z;
	v.zoom = View_zoom;
	v.canvas_w = Canvas_width; v.canvas_h = Canvas_height;
	v.origin_x = gr_screen.offset_x + gr_screen.clip_left; v.origin_y = gr_screen.offset_y + gr_screen.clip_top;
	v.user_clip = G3_user_clip;
	if (G3_user_clip) {
		v.clip_normal[0] = G3_user_clip_normal.x; v.clip_normal[1] = G3_user_clip_normal.y; v.clip_normal[2] = G3_user_clip_normal.z;
		v.clip_point[0] = G3_user_clip_point.x; v.clip_point[1] = G3_user_clip_point.y; v.clip_point[2] = G3_user_clip_point.z;
	}
	if (v.canvas_w <= 0 || v.canvas_h <= 0) { return; }
	if (H.view_sent && std::memcmp(&v, &H.view, sizeof(v)) == 0) { return; }
	H.view = v;
	H.view_sent = true;
	H.sink->set_view(H.sink->user, &v);
}

void h_flip()
{
	ensure_frame();
	depth_run_end();
	// The game's own cursor, drawn last like gr_d3d_flip does (the OS cursor is hidden); gr_flip_frame read the position
	if (mouse_is_visible() && Gr_cursor != -1) {
		gr_reset_clip();
		gr_set_bitmap(Gr_cursor);
		gr_bitmap(Gr_flip_mouse_x, Gr_flip_mouse_y);
	}
	if (drawing()) { H.sink->frame_end(H.sink->user); }
	H.frame = FRAME_NONE;
	// D3D keeps the mode across a flip; the save stack belongs to one frame
	H.zsp = 0;
	H.zget_pending = false;
}
void h_flip_window(uintptr_t, int, int, int, int) { h_flip(); }
void h_set_palette(ubyte *, int) { }				// same as dummy and D3D: nothing. BmpMan reads gr_palette, set by gr_set_palette_internal (R1_checks 1b)
void h_fade_in(int) { }
void h_fade_out(int) { }

// test switch (FS2_FORCE_CLIP=x,y,w,h): every full-screen clip becomes this rect, to compare the mesh and polygon paths under a letterbox clip (the death state)
static bool forced_clip(int rect[4])
{
	static int state = -1;
	static int r[4];
	if (state < 0) {
#pragma warning(suppress: 4996)
		const char *e = getenv("FS2_FORCE_CLIP");
		state = (e != NULL && sscanf(e, "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]) == 4) ? 1 : 0;
	}
	if (state) { for (int i = 0; i < 4; i++) { rect[i] = r[i]; } }
	return state == 1;
}

void h_set_clip(int x, int y, int w, int h)
{
	int fr[4];
	if (x == 0 && y == 0 && w >= gr_screen.max_w && h >= gr_screen.max_h && forced_clip(fr)) { x = fr[0]; y = fr[1]; w = fr[2]; h = fr[3]; }
	gr_screen.offset_x = x;
	gr_screen.offset_y = y;
	gr_screen.clip_left = 0;
	gr_screen.clip_right = w - 1;
	gr_screen.clip_top = 0;
	gr_screen.clip_bottom = h - 1;
	// the same sanity clamps as gr_d3d_set_clip
	if (gr_screen.clip_left + x < 0) { gr_screen.clip_left = -x; }
	else if (gr_screen.clip_left + x > gr_screen.max_w - 1) { gr_screen.clip_left = gr_screen.max_w - 1 - x; }
	if (gr_screen.clip_right + x < 0) { gr_screen.clip_right = -x; }
	else if (gr_screen.clip_right + x >= gr_screen.max_w - 1) { gr_screen.clip_right = gr_screen.max_w - 1 - x; }
	if (gr_screen.clip_top + y < 0) { gr_screen.clip_top = -y; }
	else if (gr_screen.clip_top + y > gr_screen.max_h - 1) { gr_screen.clip_top = gr_screen.max_h - 1 - y; }
	if (gr_screen.clip_bottom + y < 0) { gr_screen.clip_bottom = -y; }
	else if (gr_screen.clip_bottom + y > gr_screen.max_h - 1) { gr_screen.clip_bottom = gr_screen.max_h - 1 - y; }
	gr_screen.clip_width = gr_screen.clip_right - gr_screen.clip_left + 1;
	gr_screen.clip_height = gr_screen.clip_bottom - gr_screen.clip_top + 1;
	if (drawing()) { send_clip(); }
}

void h_reset_clip()
{
	int fr[4];
	if (forced_clip(fr)) { h_set_clip(0, 0, gr_screen.max_w, gr_screen.max_h); return; }
	gr_screen.offset_x = 0;
	gr_screen.offset_y = 0;
	gr_screen.clip_left = 0;
	gr_screen.clip_top = 0;
	gr_screen.clip_right = gr_screen.max_w - 1;
	gr_screen.clip_bottom = gr_screen.max_h - 1;
	gr_screen.clip_width = gr_screen.max_w;
	gr_screen.clip_height = gr_screen.max_h;
	if (drawing()) { send_clip(); }
}

void h_init_color(color *c, int r, int g, int b)
{
	c->screen_sig = gr_screen.signature;
	c->red = (ubyte)r; c->green = (ubyte)g; c->blue = (ubyte)b;
	c->alpha = 255;
	c->ac_type = AC_TYPE_NONE;
	c->alphacolor = -1;
	c->is_alphacolor = 0;
	c->magic = 0xAC01;
}

void h_init_alphacolor(color *clr, int r, int g, int b, int alpha, int type)
{
	r = r < 0 ? 0 : (r > 255 ? 255 : r);
	g = g < 0 ? 0 : (g > 255 ? 255 : g);
	b = b < 0 ? 0 : (b > 255 ? 255 : b);
	alpha = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
	h_init_color(clr, r, g, b);
	clr->alpha = (ubyte)alpha;
	clr->ac_type = (ubyte)type;
	clr->alphacolor = -1;
	clr->is_alphacolor = 1;
}

void h_set_color(int r, int g, int b) { h_init_color(&gr_screen.current_color, r, g, b); }
void h_get_color(int *r, int *g, int *b)
{
	if (r) { *r = gr_screen.current_color.red; }
	if (g) { *g = gr_screen.current_color.green; }
	if (b) { *b = gr_screen.current_color.blue; }
}
void h_set_color_fast(color *dst)
{
	if (dst->screen_sig != gr_screen.signature) {
		if (dst->is_alphacolor) { h_init_alphacolor(dst, dst->red, dst->green, dst->blue, dst->alpha, dst->ac_type); }
		else { h_init_color(dst, dst->red, dst->green, dst->blue); }
	}
	gr_screen.current_color = *dst;
}
void h_set_font(int fontnum) { grx_set_font(fontnum); }

void h_set_bitmap(int bitmap_num, int alphablend_mode, int bitblt_mode, float alpha, int sx, int sy)
{
	gr_screen.current_alpha = alpha;
	gr_screen.current_alphablend_mode = alphablend_mode;
	gr_screen.current_bitblt_mode = bitblt_mode;
	gr_screen.current_bitmap = bitmap_num;
	gr_screen.current_bitmap_sx = sx;
	gr_screen.current_bitmap_sy = sy;
}

void h_create_shader(shader *shade, float r, float g, float b, float c)
{
	shade->screen_sig = gr_screen.signature;
	shade->r = r; shade->g = g; shade->b = b; shade->c = c;
}

void h_set_shader(shader *shade)
{
	if (shade) {
		if (shade->screen_sig != gr_screen.signature) { gr_create_shader(shade, shade->r, shade->g, shade->b, shade->c); }
		gr_screen.current_shader = *shade;
	} else {
		gr_create_shader(&gr_screen.current_shader, 0.0f, 0.0f, 0.0f, 0.0f);
	}
}

void h_set_clear_color(int r, int g, int b) { gr_init_color(&gr_screen.current_clear_color, r, g, b); }

// ---- gf_ table: drawing -----------------------------------------------------------------------------------------

void h_clear()
{
	ensure_frame();
	if (!drawing()) { return; }
	// D3D clears the clip rectangle with the clear colour
	const int c[4] = { gr_screen.current_clear_color.red, gr_screen.current_clear_color.green, gr_screen.current_clear_color.blue, 255 };
	quad((float)(gr_screen.clip_left + gr_screen.offset_x), (float)(gr_screen.clip_top + gr_screen.offset_y),
		(float)(gr_screen.clip_right + 1 + gr_screen.offset_x), (float)(gr_screen.clip_bottom + 1 + gr_screen.offset_y),
		0, 0, 0, 0, 0, FS2_SINK_BLEND_OPAQUE, c, c, c, c);
}

void h_rect(int x, int y, int w, int h)
{
	ensure_frame();
	if (!drawing()) { return; }
	const color &c = gr_screen.current_color;
	solid_rect((float)(x + gr_screen.offset_x), (float)(y + gr_screen.offset_y), (float)(x + w + gr_screen.offset_x),
		(float)(y + h + gr_screen.offset_y), H.gamma_lookup[c.red], H.gamma_lookup[c.green], H.gamma_lookup[c.blue], c.alpha); // D3D draws rects through the tmapper, which runs the colour through the gamma table
}

void h_shade(int x, int y, int w, int h)
{
	ensure_frame();
	if (!drawing()) { return; }
	const shader &s = gr_screen.current_shader;
	// the same scaling as gr_d3d_shade
	solid_rect((float)(x + gr_screen.offset_x), (float)(y + gr_screen.offset_y), (float)(x + w + gr_screen.offset_x),
		(float)(y + h + gr_screen.offset_y), H.gamma_lookup[(int)(s.r * 255.0f)], H.gamma_lookup[(int)(s.g * 255.0f)], H.gamma_lookup[(int)(s.b * 255.0f)], (int)(s.c * 255.0f * 6.0f));
}

void h_pixel(int x, int y)
{
	ensure_frame();
	if (!drawing()) { return; }
	const color &c = gr_screen.current_color;
	solid_rect((float)(x + gr_screen.offset_x), (float)(y + gr_screen.offset_y), (float)(x + 1 + gr_screen.offset_x),
		(float)(y + 1 + gr_screen.offset_y), c.red, c.green, c.blue, c.alpha);
}

// A segment already clipped to the clip rectangle, colour ca at (x1,y1) and cb at (x2,y2). Axis aligned segments cover the
// pixels from one end to the other inclusive, as D3D's line list does; others are a one pixel wide strip along the segment.
void segment(int x1, int y1, int x2, int y2, const int ca[4], const int cb[4])
{
	const float ox = (float)gr_screen.offset_x, oy = (float)gr_screen.offset_y;
	const int blend = (ca[3] < 255 || cb[3] < 255) ? FS2_SINK_BLEND_ALPHA : FS2_SINK_BLEND_OPAQUE;
	static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	if (x1 == x2 || y1 == y2) {
		const bool flip = (x1 > x2) || (y1 > y2);
		const int *c0 = flip ? cb : ca, *c1 = flip ? ca : cb;
		const int lx = x1 < x2 ? x1 : x2, hx = x1 < x2 ? x2 : x1, ly = y1 < y2 ? y1 : y2, hy = y1 < y2 ? y2 : y1;
		if (x1 == x2) {
			quad(lx + ox, ly + oy, lx + 1 + ox, hy + 1 + oy, 0, 0, 0, 0, 0, blend, c0, c0, c1, c1);
		} else {
			quad(lx + ox, ly + oy, hx + 1 + ox, ly + 1 + oy, 0, 0, 0, 0, 0, blend, c0, c1, c1, c0);
		}
		return;
	}
	const float ax = (float)x1 + ox + 0.5f, ay = (float)y1 + oy + 0.5f, bx = (float)x2 + ox + 0.5f, by = (float)y2 + oy + 0.5f;
	const float dx = bx - ax, dy = by - ay;
	const float len = std::sqrt(dx * dx + dy * dy);
	const float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
	const fs2_sink_vertex v[4] = { vtx(ax + nx, ay + ny, 0, 0, ca[0], ca[1], ca[2], ca[3]), vtx(bx + nx, by + ny, 0, 0, cb[0], cb[1], cb[2], cb[3]),
		vtx(bx - nx, by - ny, 0, 0, cb[0], cb[1], cb[2], cb[3]), vtx(ax - nx, ay - ny, 0, 0, ca[0], ca[1], ca[2], ca[3]) };
	draw(plain_draw(0, blend), v, 4, idx, 6);
}

// gr_d3d_line: clipped like INT_CLIPLINE, drawn alpha blended in the current colour. Frame already checked.
void line_in_frame(int x1, int y1, int x2, int y2)
{
	int clipped = 0, swapped = 0;
	INT_CLIPLINE(x1, y1, x2, y2, gr_screen.clip_left, gr_screen.clip_top, gr_screen.clip_right, gr_screen.clip_bottom, return, clipped = 1, swapped = 1);
	(void)clipped; (void)swapped;
	const color &c = gr_screen.current_color;
	const int col[4] = { c.red, c.green, c.blue, c.alpha };
	segment(x1, y1, x2, y2, col, col);
}

void h_line(int x1, int y1, int x2, int y2)
{
	ensure_frame();
	if (!drawing()) { return; }
	line_in_frame(x1, y1, x2, y2);
}

void h_aaline(vertex *v1, vertex *v2) { h_line((int)v1->sx, (int)v1->sy, (int)v2->sx, (int)v2->sy); }

// gr_d3d_gradient: the colour (alpha as set) at the first point fades to alpha 0 at the second; a plain line without alpha
void h_gradient(int x1, int y1, int x2, int y2)
{
	ensure_frame();
	if (!drawing()) { return; }
	const color &c = gr_screen.current_color;
	if (!c.is_alphacolor) { line_in_frame(x1, y1, x2, y2); return; }
	int clipped = 0, swapped = 0;
	INT_CLIPLINE(x1, y1, x2, y2, gr_screen.clip_left, gr_screen.clip_top, gr_screen.clip_right, gr_screen.clip_bottom, return, clipped = 1, swapped = 1);
	(void)clipped;
	const int on[4] = { c.red, c.green, c.blue, c.alpha }, off[4] = { c.red, c.green, c.blue, 0 };
	// INT_CLIPLINE orders the ends top to bottom; when it swapped them the opaque colour belongs to the second end
	if (swapped) { segment(x1, y1, x2, y2, off, on); } else { segment(x1, y1, x2, y2, on, off); }
}

// gr_d3d_circle: the same midpoint walk, filled with horizontal lines, d = diameter
void h_circle(int xc, int yc, int d)
{
	ensure_frame();
	if (!drawing()) { return; }
	int p, x, y, r;
	r = d / 2;
	p = 3 - d;
	x = 0;
	y = r;
	if ((xc + r) < gr_screen.clip_left) { return; }
	if ((xc - r) > gr_screen.clip_right) { return; }
	if ((yc + r) < gr_screen.clip_top) { return; }
	if ((yc - r) > gr_screen.clip_bottom) { return; }
	while (x < y) {
		line_in_frame(xc - y, yc - x, xc + y, yc - x);
		line_in_frame(xc - y, yc + x, xc + y, yc + x);
		if (p < 0) {
			p = p + (x << 2) + 6;
		} else {
			line_in_frame(xc - x, yc - y, xc + x, yc - y);
			line_in_frame(xc - x, yc + y, xc + x, yc + y);
			p = p + ((x - y) << 2) + 10;
			y--;
		}
		x++;
	}
	if (x == y) {
		line_in_frame(xc - x, yc - y, xc + x, yc - y);
		line_in_frame(xc - x, yc + y, xc + x, yc + y);
	}
}

// gr_d3d_flash: the whole clip rectangle, added to the screen
void h_flash(int r, int g, int b)
{
	r = r < 0 ? 0 : (r > 255 ? 255 : r);
	g = g < 0 ? 0 : (g > 255 ? 255 : g);
	b = b < 0 ? 0 : (b > 255 ? 255 : b);
	if (!(r || g || b)) { return; }
	ensure_frame();
	if (!drawing()) { return; }
	const int c[4] = { r, g, b, 255 };
	quad((float)(gr_screen.clip_left + gr_screen.offset_x), (float)(gr_screen.clip_top + gr_screen.offset_y),
		(float)(gr_screen.clip_right + gr_screen.offset_x), (float)(gr_screen.clip_bottom + gr_screen.offset_y),
		0, 0, 0, 0, 0, FS2_SINK_BLEND_ADD, c, c, c, c);
}

// bitmap pieces: a rectangle of gr_screen.current_bitmap drawn clipped as one quad, the way gr_d3d_bitmap_ex clips
void bitmap_piece(int x, int y, int w, int h, int sx, int sy)
{
	ensure_frame();
	if (!drawing()) { return; }
	int bw, bh;
	bm_get_info(gr_screen.current_bitmap, &bw, &bh, nullptr);
	int dx1 = x, dx2 = x + w - 1, dy1 = y, dy2 = y + h - 1;
	if (dx1 > gr_screen.clip_right || dx2 < gr_screen.clip_left || dy1 > gr_screen.clip_bottom || dy2 < gr_screen.clip_top) { return; }
	if (dx1 < gr_screen.clip_left) { sx += gr_screen.clip_left - dx1; dx1 = gr_screen.clip_left; }
	if (dy1 < gr_screen.clip_top) { sy += gr_screen.clip_top - dy1; dy1 = gr_screen.clip_top; }
	if (dx2 > gr_screen.clip_right) { dx2 = gr_screen.clip_right; }
	if (dy2 > gr_screen.clip_bottom) { dy2 = gr_screen.clip_bottom; }
	w = dx2 - dx1 + 1; h = dy2 - dy1 + 1;
	if (sx + w > bw) { w = bw - sx; }
	if (sy + h > bh) { h = bh - sy; }
	if (w < 1 || h < 1 || sx < 0 || sy < 0) { return; }

	const int id = tex_get(gr_screen.current_bitmap, TCACHE_TYPE_XPARENT);
	if (id == 0) { return; }
	const int c[4] = { 255, 255, 255, 255 };
	quad((float)(dx1 + gr_screen.offset_x), (float)(dy1 + gr_screen.offset_y), (float)(dx1 + w + gr_screen.offset_x),
		(float)(dy1 + h + gr_screen.offset_y), (float)sx / bw, (float)sy / bh, (float)(sx + w) / bw, (float)(sy + h) / bh,
		id, FS2_SINK_BLEND_ALPHA, c, c, c, c);
}

void h_bitmap_ex(int x, int y, int w, int h, int sx, int sy) { bitmap_piece(x, y, w, h, sx, sy); }
void h_bitmap(int x, int y)
{
	int bw, bh;
	bm_get_info(gr_screen.current_bitmap, &bw, &bh, nullptr);
	bitmap_piece(x, y, bw, bh, 0, 0);
}

void aabitmap_piece(int x, int y, int w, int h, int sx, int sy)
{
	if (w < 1 || h < 1 || !gr_screen.current_color.is_alphacolor) { return; }
	ensure_frame();
	if (!drawing()) { return; }
	const int id = tex_get(gr_screen.current_bitmap, TCACHE_TYPE_AABITMAP);
	if (id == 0) { return; }
	int bw, bh;
	bm_get_info(gr_screen.current_bitmap, &bw, &bh, nullptr);
	const color &c = gr_screen.current_color;
	const int cc[4] = { c.red, c.green, c.blue, c.alpha };
	quad((float)(x + gr_screen.offset_x), (float)(y + gr_screen.offset_y), (float)(x + w + gr_screen.offset_x), (float)(y + h + gr_screen.offset_y),
		(float)sx / bw, (float)sy / bh, (float)(sx + w) / bw, (float)(sy + h) / bh, id, FS2_SINK_BLEND_ALPHA, cc, cc, cc, cc);
}

void h_aabitmap_ex(int x, int y, int w, int h, int sx, int sy) { aabitmap_piece(x, y, w, h, sx, sy); }
void h_aabitmap(int x, int y)
{
	int bw, bh;
	bm_get_info(gr_screen.current_bitmap, &bw, &bh, nullptr);
	aabitmap_piece(x, y, bw, bh, 0, 0);
}

// the same walk as gr_d3d_string, with the glyph blit going to aabitmap_piece
void h_string(int sx, int sy, char *s)
{
	if (!Current_font) { return; }
	gr_set_bitmap(Current_font->bitmap_id);
	int x = (sx == 0x8000) ? get_centered_x(s) : sx;
	int y = sy, width, spacing = 0;
	while (*s) {
		x += spacing;
		while (*s == '\n') {
			s++;
			y += Current_font->h;
			x = (sx == 0x8000) ? get_centered_x(s) : sx;
		}
		if (*s == 0) { break; }
		const int letter = get_char_width(s[0], s[1], &width, &spacing);
		s++;
		if (letter < 0) { continue; }
		if (x + width < gr_screen.clip_left || y + Current_font->h < gr_screen.clip_top) { continue; }
		if (x > gr_screen.clip_right || y > gr_screen.clip_bottom) { continue; }
		int xd = 0, yd = 0;
		if (x < gr_screen.clip_left) { xd = gr_screen.clip_left - x; }
		if (y < gr_screen.clip_top) { yd = gr_screen.clip_top - y; }
		const int xc = x + xd, yc = y + yd;
		int wc = width - xd, hc = Current_font->h - yd;
		if (xc + wc > gr_screen.clip_right) { wc = gr_screen.clip_right - xc; }
		if (yc + hc > gr_screen.clip_bottom) { hc = gr_screen.clip_bottom - yc; }
		if (wc < 1 || hc < 1) { continue; }
		aabitmap_piece(xc, yc, wc, hc, Current_font->bm_u[letter] + xd, Current_font->bm_v[letter] + yd);
	}
}

// 3D polys: a fan of nv vertices, drawn as gr_d3d_tmapper_internal draws them (colour rules, alpha, tiled uvs).
// The ship glow pass (TMAP_FLAG_NONDARKENING) is queued after the lit draw (see "ship glow"). Pixel fog (TMAP_FLAG_PIXEL_FOG) only
// sets the fog colour (see the PIXEL_FOG block below); the fog itself is the sink's (R2).
void tmapper_internal(int nv, vertex *verts[], uint flags, bool is_scaler = false)
{
	if (nv < 3 || nv > 32) { return; }
	ensure_frame();
	if (!drawing()) { return; }

	// depth state as gr_d3d_tmapper_internal picks it
	const bool zbuffering = H.zmode != GR_ZBUFF_NONE;
	unsigned zflags = 0;
	if (zbuffering) {
		zflags = (is_scaler || gr_screen.current_alphablend_mode == GR_ALPHABLEND_FILTER) ? FS2_SINK_DRAWF_ZREAD
			: (FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_ZWRITE);
		if (!H.run_open) { H.sink->depth_begin(H.sink->user); H.run_open = true; H.view_sent = false; }
		send_view();
	}

	int blend, alpha = 255;
	int tint_r = 255, tint_g = 255, tint_b = 255;
	if (!(flags & TMAP_FLAG_TEXTURED)) {
		tint_r = gr_screen.current_color.red; tint_g = gr_screen.current_color.green; tint_b = gr_screen.current_color.blue;
	}
	if (gr_screen.current_alphablend_mode == GR_ALPHABLEND_FILTER) {
		// D3D with additive caps: ONE:ONE (vertex and texture alpha unused), colour scaled by the bitmap alpha
		blend = FS2_SINK_BLEND_ADD;
		if (gr_screen.current_alpha <= 1.0f) {
			const int a = (int)(gr_screen.current_alpha * 255.0f);
			tint_r = tint_r * a / 255; tint_g = tint_g * a / 255; tint_b = tint_b * a / 255;
		}
	} else {
		blend = FS2_SINK_BLEND_ALPHA;
	}

	int tex = 0;
	if (flags & TMAP_FLAG_TEXTURED) {
		if (flags & TMAP_FLAG_BITMAP_2D) {
			// the whole bitmap as one texture; the poly's uvs span the whole bitmap (D-20261029-222605)
			tex = tex_get(gr_screen.current_bitmap, TCACHE_TYPE_NORMAL, false, true);
		} else {
			tex = tex_get(gr_screen.current_bitmap, TCACHE_TYPE_NORMAL, (flags & TMAP_FLAG_TILED) != 0);
		}
		if (tex == 0) { return; }
	}

	fs2_sink_vertex out[32];
	uint16_t idx[30 * 3];
	for (int i = 0; i < nv; i++) {
		const vertex *va = verts[i];
		int r = tint_r, g = tint_g, b = tint_b;
		if (flags & TMAP_FLAG_NEBULA) {
			const int pal = (va->b * (NEBULA_COLORS - 1)) / 255;
			const ubyte *neb_palette = Cfile_fs1_data ? Nebula_fs1_ramp : gr_palette;
			r = neb_palette[pal * 3 + 0]; g = neb_palette[pal * 3 + 1]; b = neb_palette[pal * 3 + 2];
		} else if ((flags & TMAP_FLAG_RAMP) && (flags & TMAP_FLAG_GOURAUD)) {
			r = g = b = H.gamma_lookup[va->b];
		} else if ((flags & TMAP_FLAG_RGB) && (flags & TMAP_FLAG_GOURAUD)) {
			r = H.gamma_lookup[va->r]; g = H.gamma_lookup[va->g]; b = H.gamma_lookup[va->b];
		}
		const int a = (flags & TMAP_FLAG_ALPHA) ? va->a : alpha;
		// positions on the 1/16 pixel grid, as D3D snaps them
		const float sx = (float)(int)(va->sx * 16.0f) / 16.0f + (float)gr_screen.offset_x;
		const float sy = (float)(int)(va->sy * 16.0f) / 16.0f + (float)gr_screen.offset_y;
		float u = 0.0f, v = 0.0f;
		if (flags & TMAP_FLAG_TEXTURED) { u = va->u; v = va->v; }
		// LEGACY PIXEL COMPATIBILITY (#133): D3D's rendition uvs clamp untiled, non-2D uvs to [2/w, 1-2/w]
		// (gr_d3d_tmapper_internal, flCAP). GodotEnhanced removes this (issue #134).
		if ((flags & TMAP_FLAG_TEXTURED) && !(flags & (TMAP_FLAG_TILED | TMAP_FLAG_BITMAP_2D))) {
			int bw, bh;
			bm_get_info(gr_screen.current_bitmap, &bw, &bh);
			const float mu = 2.0f / (float)bw, mv = 2.0f / (float)bh;
			u = u < mu ? mu : (u > 1.0f - mu ? 1.0f - mu : u);
			v = v < mv ? mv : (v > 1.0f - mv ? 1.0f - mv : v);
		}
		out[i] = vtx(sx, sy, u, v, r, g, b, a);
		out[i].sw = (flags & TMAP_FLAG_CORRECT) ? va->sw : 1.0f;
		if (zbuffering || (flags & TMAP_FLAG_NEBULA) || H.fog_mode != GR_FOGMODE_NONE) {
			out[i].z = va->z / 30000.0f;		// D3D: z_mult
			if (out[i].z > 0.98f) { out[i].z = 0.98f; }
		}
	}
	for (int i = 0; i < nv - 2; i++) { idx[i * 3] = 0; idx[i * 3 + 1] = (uint16_t)(i + 1); idx[i * 3 + 2] = (uint16_t)(i + 2); }

	// gr_d3d_tmapper_internal, PIXEL_FOG: the fog colour is the average of the pre-rendered background behind the vertices
	// (neb2_get_pixel reads the 32x32 buffer neb2_pre_render fills through h_get_region); the planes stay as they are
	if (flags & TMAP_FLAG_PIXEL_FOG) {
		int ra = 0, ga = 0, ba = 0;
		for (int i = 0; i < nv; i++) {
			int fr, fg, fb;
			neb2_get_pixel((int)out[i].x, (int)out[i].y, &fr, &fg, &fb);
			ra += fr; ga += fg; ba += fb;
		}
		gr_fog_set(GR_FOGMODE_FOG, ra / nv, ga / nv, ba / nv);
	}
	// D3D7 pixel centres are at integer coordinates, Godot's at +0.5 (#133). The game's screen positions are D3D's, so without this
	// every textured poly samples its texture half a pixel off: a fine grain over the whole frame against retail. Measured against
	// PrePort32 (frame captures, share of pixels exactly equal): VisualSpace 73% -> 99.8%; nebula 18% -> 27% exact, ~98% within
	// 1 shade (what is left and what was tried: the sink's spatial_shader, render_sink_godot.cpp). -0.5 was worse; nearest filtering in both renderers changed nothing.
	// Done after the PIXEL_FOG lookup above, which must read the game's own pixel: shifting first moved some polys' fog colour to
	// the next cell of the 32x32 pre-render (whole polys 1 shade off). The sink's GPU fog cell lookup takes it off again (batcher.h).
	// A correct translation of D3D, not a legacy quirk: GodotEnhanced keeps it.
	for (int i = 0; i < nv; i++) { out[i].x += FS2_SINK_D3D_PIXEL_CENTRE; out[i].y += FS2_SINK_D3D_PIXEL_CENTRE; }

	const unsigned dflags = ((flags & TMAP_FLAG_CORRECT) ? FS2_SINK_DRAWF_PERSPECTIVE : 0) | zflags | (H.cull ? FS2_SINK_DRAWF_CULL : 0);
	fs2_sink_draw d = plain_draw(tex, blend, dflags);
	d.zbias = H.zbias;
	// D-20261030-052453: fog_rgb[3] marks a poly that set its own fog colour above (render_sink.h); the colour itself is unchanged
	d.fog_rgb[3] = (flags & TMAP_FLAG_PIXEL_FOG) ? 1 : 0;
	// a ship poly's lit draw does not flush the glow queue (D3D_glow_hold)
	H.glow_hold = (flags & TMAP_FLAG_NONDARKENING) != 0;
	draw(d, out, nv, idx, (nv - 2) * 3);
	H.glow_hold = false;

	// ship glow, gr_d3d_tmapper_internal's second pass: the same polygon unlit (vertex colour white, alpha kept) with the glow
	// texture, alpha blended over the lit draw, depth read only. Fog fields as the lit draw, including its colour mark (a polygon
	// with its own pixel fog colour keeps it in the queue, whatever is drawn before the flush).
	if ((flags & TMAP_FLAG_NONDARKENING) && (flags & TMAP_FLAG_TEXTURED) && !(flags & (TMAP_FLAG_BITMAP_2D | TMAP_FLAG_TILED))
		&& blend != FS2_SINK_BLEND_ADD && (zflags & FS2_SINK_DRAWF_ZREAD)) {
		const int gtex = glow_tex_get(gr_screen.current_bitmap);
		if (gtex != 0) {
			if (H.glow_q.size() >= 2048 || H.glow_v.size() + (size_t)nv > 16384) { glow_flush(); }
			GlowPoly gp;
			gp.d = d;
			gp.d.texture = gtex;
			gp.d.blend = FS2_SINK_BLEND_ALPHA;
			gp.d.flags = (dflags & ~(unsigned)FS2_SINK_DRAWF_ZWRITE) | FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_OVERLAY;
			gp.first = (int)H.glow_v.size();
			gp.nv = nv;
			for (int i = 0; i < nv; i++) {
				fs2_sink_vertex gv = out[i];
				gv.r = gv.g = gv.b = 255;
				H.glow_v.push_back(gv);
			}
			H.glow_q.push_back(gp);
		}
	}
}

void h_tmapper(int nv, vertex *verts[], uint flags) { tmapper_internal(nv, verts, flags); }

// ---- R3: billboards (g3_draw_bitmap, g3_draw_laser) ---------------------------------------------------------------
// The game keeps the trivial culls and hands the eye-space points over; the sink applies the game's own size, placement and pixel rules
// (docs/R3_billboards_design.md). Same state rules as tmapper_internal. 0 only with FS2_BB_OFF (proof baseline, draw stays on the CPU path); faults are counted and draw nothing.
static int h_billboard_in(int kind, const float *p0, const float *p1, float rad, float rad_tail, float max_len, int orient, unsigned flags, int rgb)
{
	if (H.sink == nullptr || H.sink->billboard_draw == nullptr) { return 0; }
	static const bool bb_off = getenv("FS2_BB_OFF") != nullptr;	// proof baseline: billboards stay on the CPU path
	if (bb_off) { return 0; }
	ensure_frame();
	if (!drawing()) { H.bb_why[0]++; return 0; }	// as tmapper_internal: outside an open frame nothing is drawn on either path
	const bool zbuffering = H.zmode != GR_ZBUFF_NONE;
	// flags the sink does not model: a counted fault, nothing is drawn (D-20261104-174832)
	// R3 row 6: the warp cone and the subspace tunnel (TRI3, orient bits 8 / 16) are the only billboards that may be tiled (repeat sampler) or affine (no CORRECT flag)
	const bool wsub = kind == FS2_SINK_BB_TRI3 && (orient & (8 | 16)) != 0;
	const bool tiled = wsub && (flags & TMAP_FLAG_TILED) != 0;
	const bool affine = wsub && !(flags & TMAP_FLAG_CORRECT);
	if (!(flags & TMAP_FLAG_TEXTURED) || (flags & (TMAP_FLAG_NEBULA | TMAP_FLAG_RAMP | TMAP_FLAG_RGB | TMAP_FLAG_GOURAUD | TMAP_FLAG_ALPHA
		| (tiled ? 0 : TMAP_FLAG_TILED) | TMAP_FLAG_BITMAP_2D | TMAP_FLAG_PIXEL_FOG | TMAP_FLAG_NONDARKENING))) { H.bb_cpu++; H.bb_why[1]++; H.bb_flags = flags; return 1; }
	const bool filter = gr_screen.current_alphablend_mode == GR_ALPHABLEND_FILTER;
	if ((kind == FS2_SINK_BB_QUAD || kind == FS2_SINK_BB_QUAD4 || kind == FS2_SINK_BB_TRI3) && ((!filter && !(orient & 32)) || (orient & 1))) { return 0; }	// the callers are filter blended; orient bit 0 = a user clip plane is on (the sink has no such clip: declined and counted)
	if (kind == FS2_SINK_BB_ROTATED) { static const bool rot_on = getenv("FS2_BB_ROT") == nullptr || atoi(getenv("FS2_BB_ROT")) != 0; if (!rot_on || !filter) { return 0; } }	// rotated puffs draw on the GPU; FS2_BB_ROT=0 (pixel-comparison runs) keeps them on the poly path; every caller is filter blended, any other blend stays on the poly path

	fs2_sink_billboard b = {};
	b.kind = kind;
	const bool bb_had = H.tex.count(TexKey{ gr_screen.current_bitmap, bm_get_signature(gr_screen.current_bitmap), tiled ? TK_REPEAT : TK_NORMAL }) != 0;
	b.texture = tex_get(gr_screen.current_bitmap, TCACHE_TYPE_NORMAL, tiled);
	if (b.texture == 0) { return 1; }	// as tmapper_internal: no texture, no draw
	if (!bb_had && H.bb_tex_late++ == 0) { {
		const int bid = gr_screen.current_bitmap;
		const int n = bm_get_level_bitmaps(nullptr, 0);
		std::vector<bm_level_bitmap> l((size_t)(n > 0 ? n : 0));
		if (n > 0) { bm_get_level_bitmaps(l.data(), n); }
		int fl = -99; const char *fn = "(not in list)";
		for (auto &lvl : l) { if (bid >= lvl.handle && bid < lvl.handle + lvl.nframes) { fl = lvl.flags; fn = lvl.filename; break; } }	// Claude: was b, which hid the billboard (C4456)
		fprintf(stderr, "render3d: LATE billboard texture at a draw (bitmap %d, level flags %d, %s)\n", bid, fl, fn);
	} }
	// rgb >= 0: constant vertex colour (g3_draw_laser_rgb: RGB|GOURAUD), through the gamma table as tmapper_internal does
	int tint_r = 255, tint_g = 255, tint_b = 255;
	if (rgb >= 0) { tint_r = H.gamma_lookup[rgb & 255]; tint_g = H.gamma_lookup[(rgb >> 8) & 255]; tint_b = H.gamma_lookup[(rgb >> 16) & 255]; }
	if (filter) {
		b.blend = FS2_SINK_BLEND_ADD;
		if (gr_screen.current_alpha <= 1.0f && rgb < 0) {	// RGB|GOURAUD vertex colours replace the alpha-scaled tint in tmapper_internal
			const int a = (int)(gr_screen.current_alpha * 255.0f);
			tint_r = tint_r * a / 255; tint_g = tint_g * a / 255; tint_b = tint_b * a / 255;
		}
	} else {
		b.blend = FS2_SINK_BLEND_ALPHA;
	}
	b.tint[0] = (uint8_t)tint_r; b.tint[1] = (uint8_t)tint_g; b.tint[2] = (uint8_t)tint_b; b.tint[3] = 255;
	// scalers and filter-blended draws read depth only
	b.flags = !zbuffering ? 0 : (kind == FS2_SINK_BB_BITMAP || filter) ? FS2_SINK_DRAWF_ZREAD : (FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_ZWRITE);
	b.zbias = H.zbias;
	b.p0[0] = p0[0]; b.p0[1] = p0[1]; b.p0[2] = p0[2];
	if (p1 != nullptr && kind == FS2_SINK_BB_ROTATED) { b.angle = p1[0]; }	// g3_draw_rotated_bitmap passes its roll angle as p1[0]
	else if (kind == FS2_SINK_BB_TRI3) { for (int i = 0; i < 9; i++) { b.quad[i] = p0[i]; } for (int i = 0; i < 6; i++) { b.tri_uv[i] = p1[i]; } b.p0[0] = b.p0[1] = b.p0[2] = 0.0f; }	// trail tail: p0 = three eye-space corners, p1 = their uvs
	else if (kind == FS2_SINK_BB_QUAD4) { for (int i = 0; i < 12; i++) { b.quad[i] = p0[i]; } b.p0[0] = b.p0[1] = b.p0[2] = 0.0f; }	// lightning: p0 points at the four eye-space corners
	else if (p1 != nullptr) { b.p1[0] = p1[0]; b.p1[1] = p1[1]; b.p1[2] = p1[2]; }
	b.rad = rad; b.rad_tail = rad_tail; b.max_len = max_len; b.orient = (orient & 1) | (tiled ? 2 : 0) | (affine ? 4 : 0);	// sink bits: 0 user clip, 1 tiled (repeat sampler, no uv clamp), 2 affine uvs (TRI3 only)
	bm_get_info(gr_screen.current_bitmap, &b.bw, &b.bh);
	b.clip[0] = gr_screen.clip_left; b.clip[1] = gr_screen.clip_top; b.clip[2] = gr_screen.clip_right; b.clip[3] = gr_screen.clip_bottom;

	glow_flush();
	if (!H.run_open) { H.sink->depth_begin(H.sink->user); H.run_open = true; H.view_sent = false; }
	send_view();
	if (!H.sink->billboard_draw(H.sink->user, &b)) { H.bb_cpu++; H.bb_why[3]++; return 1; }	// counted fault, nothing is drawn
	H.bb_sink++;
	return 1;
}

static int h_billboard(int kind, const float *p0, const float *p1, float rad, float rad_tail, float max_len, int orient, unsigned flags, int rgb)
{
	if (kind == FS2_SINK_BB_TRI3 && (orient & 32)) {	// R3 row 7: starfield background bitmap triangles (orient bit 5); xparent ones are alpha blended, the rest filter (additive)
		static const bool star_on = getenv("FS2_STARFIELD_GPU") == nullptr || atoi(getenv("FS2_STARFIELD_GPU")) != 0;	// they draw on the GPU; FS2_STARFIELD_GPU=0 (pixel-comparison runs) keeps them on the poly path
		if (!star_on) { return 0; }
		const int xbefore = H.bb_sink, xidle = H.bb_why[0];
		const int xr = h_billboard_in(kind, p0, p1, rad, rad_tail, max_len, orient, flags, rgb);
		if (H.bb_why[0] != xidle) { return xr; }	// no frame is being drawn: not an offer
		H.star_offered++;
		if (H.bb_sink != xbefore) { H.star_gpu++; }
		return xr;
	}
	if (kind == FS2_SINK_BB_TRI3 && (orient & (8 | 16))) {	// R3 row 6: warp-in cone (orient bit 3) and subspace tunnel (bit 4)
		static const bool warp_on = getenv("FS2_WARP_GPU") == nullptr || atoi(getenv("FS2_WARP_GPU")) != 0;	// both draw on the GPU; FS2_WARP_GPU=0 (pixel-comparison runs) keeps them on the poly path
		if (!warp_on) { return 0; }
		const int wbefore = H.bb_sink, widle = H.bb_why[0];
		const int wr = h_billboard_in(kind, p0, p1, rad, rad_tail, max_len, orient, flags, rgb);
		if (H.bb_why[0] != widle) { return wr; }	// no frame is being drawn: not an offer
		int &woff = (orient & 8) ? H.warp_offered : H.sub_offered, &wgpu = (orient & 8) ? H.warp_gpu : H.sub_gpu;
		woff++;
		if (H.bb_sink != wbefore) { wgpu++; }
		return wr;
	}
	if (kind == FS2_SINK_BB_TRI3 && (orient & 4)) {	// shield hits (orient bit 2 marks them; the rgb argument is the polygon's constant vertex colour)
		static const bool shield_on = getenv("FS2_SHIELD_GPU") == nullptr || atoi(getenv("FS2_SHIELD_GPU")) != 0;	// shield hits draw on the GPU; FS2_SHIELD_GPU=0 (pixel-comparison runs) keeps them on the poly path
		if (!shield_on) { return 0; }
		const int sbefore = H.bb_sink, sidle = H.bb_why[0];
		const int sr = h_billboard_in(kind, p0, p1, rad, rad_tail, max_len, orient, flags, rgb);
		if (H.bb_why[0] != sidle) { return sr; }	// no frame is being drawn: not an offer
		H.shield_offered++;
		if (H.bb_sink != sbefore) { H.shield_gpu++; }
		return sr;
	}
	if ((kind == FS2_SINK_BB_QUAD4 && (orient & 2)) || kind == FS2_SINK_BB_TRI3) {	// weapon trails (orient bit 1 marks the quad callers)
		static const bool trails_on = getenv("FS2_TRAILS_GPU") == nullptr || atoi(getenv("FS2_TRAILS_GPU")) != 0;	// trails draw on the GPU; FS2_TRAILS_GPU=0 (pixel-comparison runs) keeps them on the poly path
		if (!trails_on) { return 0; }
		const int tbefore = H.bb_sink, tidle = H.bb_why[0];
		const int tr = h_billboard_in(kind, p0, p1, rad, rad_tail, max_len, orient, flags, rgb);
		if (H.bb_why[0] != tidle) { return tr; }	// no frame is being drawn: not an offer
		H.trail_offered++;
		if (H.bb_sink != tbefore) { H.trail_gpu++; }
		return tr;
	}
	if (kind == FS2_SINK_BB_QUAD4) {
		static const bool light_on = getenv("FS2_LIGHTNING_GPU") == nullptr || atoi(getenv("FS2_LIGHTNING_GPU")) != 0;	// lightning bolts draw on the GPU; FS2_LIGHTNING_GPU=0 (pixel-comparison runs) keeps them on the poly path
		if (!light_on) { return 0; }
		const int lbefore = H.bb_sink, lidle = H.bb_why[0];
		const int lr = h_billboard_in(kind, p0, p1, rad, rad_tail, max_len, orient, flags, rgb);
		if (H.bb_why[0] != lidle) { return lr; }	// no frame is being drawn: not an offer
		H.light_offered++;
		if (H.bb_sink != lbefore) { H.light_gpu++; }
		return lr;
	}
	if (kind != FS2_SINK_BB_QUAD) { return h_billboard_in(kind, p0, p1, rad, rad_tail, max_len, orient, flags, rgb); }
	static const bool beam_on = getenv("FS2_BEAM_GPU") == nullptr || atoi(getenv("FS2_BEAM_GPU")) != 0;	// beams draw on the GPU; FS2_BEAM_GPU=0 (pixel-comparison runs) keeps them on the poly path
	if (!beam_on) { return 0; }
	const int before = H.bb_sink, idle = H.bb_why[0];
	const int r = h_billboard_in(kind, p0, p1, rad, rad_tail, max_len, orient, flags, rgb);
	if (H.bb_why[0] != idle) { return r; }	// no frame is being drawn (a skipped frame): nothing to draw on either path, not an offer
	H.beam_offered++;
	if (H.bb_sink != before) { H.beam_gpu++; }
	return r;
}

// ---- render 3D: model meshes (P3b) ------------------------------------------------------------------------------
// Switch: FS2_RENDER3D (default on; FS2_RENDER3D=0 turns it off). The model builder (ModelInterp.cpp) hands over a submodel's triangles once
// (h_mesh_create keeps them on the host) and asks for a draw per frame (h_mesh_draw). The sink copy is made on the first draw
// in an accepted frame, when the textures can be uploaded, and again when the draw state (z mode, zbias, cull) or a texture
// changes. Groups are drawn opaque when the texture has no alpha below 255, else alpha blended; polygons that glow stay CPU.
bool mesh_switch()
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("FS2_RENDER3D");
		on = (e != nullptr) ? (e[0] - '0') : 1;
	}
	return on == 1;
}


int h_mesh_ready()
{
	return H.sink != nullptr && H.sink->mesh_create && H.sink->mesh_draw && H.sink->mesh_free && mesh_switch();
}

void h_mesh_free(int key);

int h_mesh_create(int key, const float *vtx, int nv, const gr_host_mesh_group *g, int ng, const unsigned *idx, int ni, const int *poly_nv, int npoly)
{
	if (nv <= 0 || ng <= 0 || ni <= 0 || vtx == nullptr || poly_nv == nullptr || npoly <= 0) { return 0; }
	if (H.meshes.find(key) != H.meshes.end()) { h_mesh_free(key); }
	Host::HostMesh &m = H.meshes[key];
	m.v.resize((size_t)nv);
	for (int i = 0; i < nv; i++) {
		fs2_sink_mesh_vertex &o = m.v[(size_t)i];
		const float *s = vtx + 8 * i;
		o.pos[0] = s[0]; o.pos[1] = s[1]; o.pos[2] = s[2];
		o.normal[0] = s[3]; o.normal[1] = s[4]; o.normal[2] = s[5];
		o.u = s[6]; o.v = s[7];
		for (int k = 0; k < 3; k++) {
			if (i == 0 || s[k] < m.bmin[k]) { m.bmin[k] = s[k]; }
			if (i == 0 || s[k] > m.bmax[k]) { m.bmax[k] = s[k]; }
		}
	}
	m.g.assign(g, g + ng);
	m.idx.assign(idx, idx + ni);
	m.poly_nv.assign(poly_nv, poly_nv + npoly);
	return 1;
}

void h_mesh_free(int key)
{
	auto it = H.meshes.find(key);
	if (it == H.meshes.end()) { return; }
	if (it->second.sink_made && H.sink != nullptr) { H.sink->mesh_free(H.sink->user, key); }
	H.meshes.erase(it);
}

// The sink groups of a mesh: one per host group, then one glow overlay per group whose texture glows (the ship glow pass, "ship glow":
// the same triangles unlit with the glow texture, alpha blended, z read only, FS2_SINK_DRAWF_OVERLAY). The set of glowing groups never
// changes for a mesh (glow_tex_get answers once per bitmap), so the group count is fixed and mesh_state can follow.
static void mesh_sink_groups(const Host::HostMesh &m, const std::vector<int> &tex, const std::vector<int> &blend, unsigned fl, int zbias,
	std::vector<fs2_sink_mesh_group> &sg)
{
	sg.assign(m.g.size(), fs2_sink_mesh_group());
	for (size_t i = 0; i < m.g.size(); i++) {
		sg[i].texture = tex[i]; sg[i].blend = blend[i]; sg[i].flags = fl; sg[i].zbias = zbias;
		sg[i].first_index = m.g[i].first_index; sg[i].index_count = m.g[i].index_count;
	}
	for (size_t i = 0; i < m.g.size(); i++) {
		const int gtex = m.g[i].bitmap < 0 ? 0 : glow_tex_get(m.g[i].bitmap);
		if (gtex == 0) { continue; }
		fs2_sink_mesh_group og = sg[i];
		og.texture = gtex; og.blend = FS2_SINK_BLEND_ALPHA;
		og.flags = (fl & ~(unsigned)FS2_SINK_DRAWF_ZWRITE) | FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_OVERLAY;
		sg.push_back(og);
	}
}

// load hooks: a model was paged in (its meshes are built on the host now), and level paging is done (the sink copies are made now,
// outside a frame, so the first draw of a ship does not stall on texture uploads). The copies are made for the usual state; a draw in
// another z mode, zbias or cull remakes only the group state (mesh_state), never the geometry.
void h_model_page_in(int modelnum)
{
	if (mesh_switch()) { model_interp_mesh_preload(modelnum); }
}

void h_level_loaded()
{
	if (!h_mesh_ready()) { return; }
	model_interp_mesh_preload_all();
	if (H.sink == nullptr) { return; }
	if (H.flush_pending) {		// a flush or gamma change before the load: do it now, or the first frame drops what the load uploads
		for (auto &kv : H.tex) { if (kv.second.sink_id != 0) { H.sink->texture_free(H.sink->user, kv.second.sink_id); } }
		H.tex.clear(); H.head_layer.clear();
		H.flush_pending = false;
	}
	H.load_mode = true;
	for (auto &e : H.meshes) {
		Host::HostMesh &m = e.second;
		if (m.sink_made || m.cpu_only) { continue; }
		std::vector<int> tex(m.g.size()), blend(m.g.size());
		bool ok = true;
		for (size_t i = 0; i < m.g.size() && ok; i++) {
			const int bmp = m.g[i].bitmap;
			if (bmp < 0) { tex[i] = 0; blend[i] = FS2_SINK_BLEND_OPAQUE; continue; }		// flat polys: the sink's white texture
			tex[i] = tex_get(bmp, TCACHE_TYPE_NORMAL, false);
			if (tex[i] == 0) { ok = false; break; }
			if (m.g[i].tileable) { tex_get(bmp, TCACHE_TYPE_NORMAL, true); }		// a tiled draw wants the repeat variant
			bool opaque = false;
			const TexKey k = { bmp, bm_get_signature(bmp), TK_NORMAL };
			auto f = H.tex.find(k);
			if (f != H.tex.end()) { opaque = f->second.opaque; }
			blend[i] = opaque ? FS2_SINK_BLEND_OPAQUE : FS2_SINK_BLEND_ALPHA;
		}
		if (!ok) { continue; }
		std::vector<fs2_sink_mesh_group> sg;
		mesh_sink_groups(m, tex, blend, FS2_SINK_DRAWF_PERSPECTIVE | FS2_SINK_DRAWF_ZREAD | FS2_SINK_DRAWF_ZWRITE | FS2_SINK_DRAWF_CULL, 0, sg);
		H.sink->mesh_create(H.sink->user, e.first, m.v.data(), (int)m.v.size(), sg.data(), (int)sg.size(), m.idx.data(), (int)m.idx.size());
		m.sink_made = true;
		m.zmode = GR_ZBUFF_FULL; m.zbias = 0; m.cull = 1; m.tiled = 0;
		m.tex = tex; m.blend = blend;
	}
	// the level's other model textures (the engine thruster frames): the game's own page-in list (W11), no lookup by name
	const int nlist = bm_get_level_bitmaps(nullptr, 0);
	std::vector<bm_level_bitmap> list((size_t)(nlist > 0 ? nlist : 0));
	if (nlist > 0) { bm_get_level_bitmaps(list.data(), nlist); }
	for (auto &b : list) {
		if (b.flags & BM_LEVEL_LISTED) { head_preload(b.filename); continue; }		// a talking head (or another file the level plays outside BmpMan): decoded now
		if (b.handle < 0) { continue; }
		if (b.flags & BM_LEVEL_AABITMAP) {		// HUD bitmaps and ANIs: the alpha kind the 2D draws ask for, every frame at load
			for (int f = 0; f < b.nframes; f++) { tex_get(b.handle + f, TCACHE_TYPE_AABITMAP, false); }
			continue;
		}
		for (int f = 0; f < b.nframes; f++) { tex_get(b.handle + f, (b.flags & BM_LEVEL_XPARENT) ? TCACHE_TYPE_XPARENT : TCACHE_TYPE_NORMAL, false); }
		tex_get_anim(b.handle, b.nframes);		// the kind a billboard draws, whole, at load
	}
	for (int i = 0; i < Num_fonts; i++) {		// the font atlases: made at init, outside the level list, drawn with the alpha kind
		if (Fonts[i].bitmap_id >= 0) { tex_get(Fonts[i].bitmap_id, TCACHE_TYPE_AABITMAP, false); }
	}
	if (Gr_cursor >= 0) {		// the game's own cursor: loaded at init, outside the level list, drawn by h_flip with the xparent kind
		int cf = 1;
		bm_get_info(Gr_cursor, nullptr, nullptr, nullptr, &cf);
		for (int f = 0; f < cf; f++) { tex_get(Gr_cursor + f, TCACHE_TYPE_XPARENT, false); }
	}
	H.load_mode = false;
}

// common to both draws: 1 ready (frame open, sink copy current, view sent), 0 draw on the CPU path this time (fog off in a scripted run), 2 nothing to draw (skipped frame or a counted fault), -1 mesh invalid.
// thr_bitmap >= 0: an engine thruster, every group drawn with that bitmap, additive, depth read only (the poly path's filter blend).
static int mesh_prepare(int key, const float *xform, int nv, int tiled, int thr_bitmap = -1, bool flat_all = false, bool bg = false)
{
	auto it = H.meshes.find(key);
	if (it == H.meshes.end()) { return -1; }
	Host::HostMesh &m = it->second;
	if ((int)m.v.size() != nv) { return -1; }
	static const int force_fb = getenv("FS2_MESH_FORCE_FB") ? atoi(getenv("FS2_MESH_FORCE_FB")) : 0;	// test only: 1 sends every mesh draw down the polygon path (the forced-path pixel pair, D-20261105-165753)
	if (force_fb == 1 || m.cpu_only) { H.mesh_fb[0]++; return 2; }	// a fault: counted, nothing drawn (D-20261105-165753); only the fog-off scripted case keeps the polygon path
	if (H.zmode == GR_ZBUFF_NONE && !bg) { H.mesh_fb[1]++; return 2; }
	if (H.fog_mode != GR_FOGMODE_NONE && H.sink->mesh_fog == nullptr) { H.mesh_fb[2]++; return 2; }
	ensure_frame();
	if (H.frame == FRAME_SKIPPED) { H.mesh_skipped++; return 2; }	// the sink dropped this frame: nothing shows, so no polygon path either
	if (!drawing()) { H.mesh_fb[3]++; return 2; }

	// the sink copy: made, or remade when the state or a texture it was made with has changed
	const int zmode = bg ? -2 : H.zmode;		// the background draw is its own state: no depth, no cull
	const int cull = bg ? 0 : H.cull;
	bool remake = !m.sink_made || m.zmode != zmode || m.zbias != H.zbias || m.cull != cull || m.tiled != tiled;
	std::vector<int> tex(m.g.size());
	std::vector<int> blend(m.g.size());
	for (size_t i = 0; i < m.g.size(); i++) {
		const int bmp = flat_all ? -1 : (thr_bitmap >= 0 ? thr_bitmap : m.g[i].bitmap);
		if (bmp < 0) {
			tex[i] = 0; blend[i] = FS2_SINK_BLEND_OPAQUE;
			if (!m.sink_made || m.tex[i] != tex[i] || m.blend[i] != blend[i]) { remake = true; }
			continue;
		}
		const int tkind = tiled ? TK_REPEAT : TK_NORMAL;
		const bool tex_had = H.tex.count(TexKey{ bmp, bm_get_signature(bmp), tkind }) != 0;
		tex[i] = tex_get(bmp, TCACHE_TYPE_NORMAL, tiled != 0);
		if (tex[i] == 0) { H.mesh_fb[4]++; return 2; }
		if (!tex_had && H.mesh_tex_late++ == 0) { fprintf(stderr, "render3d: LATE mesh texture at a draw (bitmap %d, kind %d, thruster %d, mesh key %d)\n", bmp, tkind, thr_bitmap, key); }
		const TexKey k = { bmp, bm_get_signature(bmp), tkind };
		auto f = H.tex.find(k);
		bool opaque = false;
		if (f == H.tex.end()) {
			for (auto &e : H.tex) { if (e.first.handle == bmp && e.first.kind == tkind) { opaque = e.second.opaque; break; } }
		} else {
			opaque = f->second.opaque;
		}
		blend[i] = (thr_bitmap >= 0 || bg) ? FS2_SINK_BLEND_ADD : (opaque ? FS2_SINK_BLEND_OPAQUE : FS2_SINK_BLEND_ALPHA);
		if (!m.sink_made || m.tex[i] != tex[i] || m.blend[i] != blend[i]) { remake = true; }
	}
	if (remake) {
		std::vector<fs2_sink_mesh_group> sg;
		const unsigned fl = FS2_SINK_DRAWF_PERSPECTIVE | (bg ? 0 : FS2_SINK_DRAWF_ZREAD)
			| ((thr_bitmap < 0 && (zmode == GR_ZBUFF_FULL || zmode == GR_ZBUFF_WRITE)) ? FS2_SINK_DRAWF_ZWRITE : 0) | (bg ? 0 : FS2_SINK_DRAWF_CULL);	// the polygon path back-face culls on the CPU, so the mesh must on the GPU
		mesh_sink_groups(m, tex, blend, fl, H.zbias, sg);
		if (m.sink_made && H.sink->mesh_state) {
			H.sink->mesh_state(H.sink->user, key, sg.data(), (int)sg.size());	// the geometry stays; only how it is drawn changes
		} else {
			// load created every sink copy it could; reaching here means a mesh load could not make (no texture then). Counted, never expected.
			if (H.mesh_sink_late++ == 0) { fprintf(stderr, "render3d: LATE sink mesh at a draw (key %d): the level load did not make it\n", key); }
			H.sink->mesh_create(H.sink->user, key, m.v.data(), (int)m.v.size(), sg.data(), (int)sg.size(), m.idx.data(), (int)m.idx.size());
		}
		m.sink_made = true;
		m.zmode = zmode; m.zbias = H.zbias; m.cull = cull; m.tiled = tiled;
		m.tex = tex; m.blend = blend;
	}

	glow_flush();
	if (!H.run_open) { H.sink->depth_begin(H.sink->user); H.run_open = true; H.view_sent = false; }
	send_view();
	if (H.sink->mesh_fog != nullptr) {
		// the fog state of the polys (plain_draw): on only with usable planes
		fs2_sink_mesh_fog f = {};
		if (H.fog_mode != GR_FOGMODE_NONE && H.fog_far > H.fog_near) {
			f.mode = FS2_SINK_FOG_LINEAR;
			f.rgb[0] = clamp8(H.fog_r); f.rgb[1] = clamp8(H.fog_g); f.rgb[2] = clamp8(H.fog_b);
			f.fog_near = H.fog_near; f.fog_far = H.fog_far;
		}
		if (!H.mesh_fog_valid || std::memcmp(&f, &H.mesh_fog_sent, sizeof(f)) != 0) {
			H.mesh_fog_ok = H.sink->mesh_fog(H.sink->user, &f) != 0;
			H.mesh_fog_sent = f;
			H.mesh_fog_valid = true;
		}
		if (f.mode != FS2_SINK_FOG_NONE && !H.mesh_fog_ok) { H.mesh_fb[5]++; return 0; }
	}
	return 1;
}

int h_mesh_draw(int key, const float *xform, const unsigned char *rgb, int nv, int tiled)
{
	const int p = mesh_prepare(key, xform, nv, tiled);
	if (p != 1) { return p == 2 ? 1 : p; }
	H.mesh_rgba.resize((size_t)nv * 4);
	for (int i = 0; i < nv; i++) {
		H.mesh_rgba[(size_t)i * 4 + 0] = (uint8_t)H.gamma_lookup[rgb[i * 3 + 0]];
		H.mesh_rgba[(size_t)i * 4 + 1] = (uint8_t)H.gamma_lookup[rgb[i * 3 + 1]];
		H.mesh_rgba[(size_t)i * 4 + 2] = (uint8_t)H.gamma_lookup[rgb[i * 3 + 2]];
		H.mesh_rgba[(size_t)i * 4 + 3] = 255;
	}
	H.sink->mesh_draw(H.sink->user, key, xform, H.mesh_rgba.data(), nv);
	return 1;
}

// the sink lights per vertex (step 4); the same inputs as the CPU path, the gamma table's exponent added
int h_mesh_draw_lit(int key, const float *xform, const gr_host_mesh_light *light, int nv, int tiled)
{
	if (H.sink->mesh_draw_lit == nullptr) { return 0; }
	static_assert(sizeof(gr_host_mesh_light) == sizeof(fs2_sink_mesh_light), "mesh light layouts differ");
	const int p = mesh_prepare(key, xform, nv, tiled);
	if (p != 1) { return p == 2 ? 1 : p; }
	fs2_sink_mesh_light sl;
	memcpy(&sl, light, sizeof(sl));
	sl.inv_gamma = H.inv_gamma;
	H.sink->mesh_draw_lit(H.sink->user, key, xform, &sl);
	return 1;
}
// an engine thruster (model_interp_sub's thruster branch): unlit white, the thruster bitmap, additive, depth read only; the flame's length
// is a z scale in the sink for the vertices in front of min_dist
int h_mesh_thrust(int key, const float *xform, int nv, int bitmap, float scale, float min_dist, const unsigned char *rgb)
{
	// fogged (nebula): a thruster poly has no PIXEL_FOG mark, so it takes the fog colour the last marked poly left (see fog_rgb[3]); not the mesh fog's
	if (H.sink->mesh_thrust == nullptr || H.fog_mode != GR_FOGMODE_NONE || (bitmap < 0 && rgb == nullptr)) { return 0; }
	const bool flat = bitmap < 0;	// no flame bitmap: the polys are flat and lit, like a hull's untextured polys, but the flame's z scale still applies
	const float ts[2] = { scale, min_dist };
	const int p = mesh_prepare(key, xform, nv, 0, bitmap, flat);
	if (p != 1) { return p == 2 ? 1 : p; }
	if (flat) {
		H.mesh_rgba.resize((size_t)nv * 4);
		for (int i = 0; i < nv; i++) {
			H.mesh_rgba[(size_t)i * 4 + 0] = (uint8_t)H.gamma_lookup[rgb[i * 3 + 0]];
			H.mesh_rgba[(size_t)i * 4 + 1] = (uint8_t)H.gamma_lookup[rgb[i * 3 + 1]];
			H.mesh_rgba[(size_t)i * 4 + 2] = (uint8_t)H.gamma_lookup[rgb[i * 3 + 2]];
			H.mesh_rgba[(size_t)i * 4 + 3] = 255;
		}
	}
	const float t[2] = { scale, min_dist };
	H.sink->mesh_thrust(H.sink->user, t);
	H.sink->mesh_draw(H.sink->user, key, xform, flat ? H.mesh_rgba.data() : nullptr, flat ? nv : 0);
	H.sink->mesh_thrust(H.sink->user, nullptr);
	return 1;
}

// the nebula background model (stars_draw_background): the forced bitmap on every poly, additive, no depth, no culling, vertex colour
// gray (191 unlit), as the polygon path draws MR_NO_ZBUFFER | MR_NO_CULL | MR_ALL_XPARENT | MR_NO_LIGHTING | MR_FORCE_TEXTURE
int h_mesh_bg(int key, const float *xform, int nv, int bitmap, int tiled, int gray)
{
	if (bitmap < 0 || H.fog_mode != GR_FOGMODE_NONE) { return 0; }
	ensure_frame();
	if (!drawing()) { return 1; }	// outside a frame the polygon path draws nothing either (tmapper_internal)
	const int p = mesh_prepare(key, xform, nv, tiled, bitmap, false, true);
	if (p != 1) { return p == 2 ? 1 : p; }
	const uint8_t g = (uint8_t)H.gamma_lookup[gray];
	H.mesh_rgba.resize((size_t)nv * 4);
	for (int i = 0; i < nv; i++) {
		H.mesh_rgba[(size_t)i * 4 + 0] = g; H.mesh_rgba[(size_t)i * 4 + 1] = g; H.mesh_rgba[(size_t)i * 4 + 2] = g; H.mesh_rgba[(size_t)i * 4 + 3] = 255;
	}
	H.sink->mesh_draw(H.sink->user, key, xform, H.mesh_rgba.data(), nv);
	return 1;
}

// the user clip plane of the draws that follow (a sink without the entry cannot clip: only "no clip" is accepted)
int h_mesh_clip(const float *plane)
{
	if (H.sink->mesh_clip == nullptr) { return plane == nullptr; }
	H.sink->mesh_clip(H.sink->user, plane);
	return 1;
}
#define SCALE_UV(x, x0, x1, y0, y1) (((((x) - (x0)) * ((y1) - (y0))) / ((x1) - (x0))) + (y0))

// gr_d3d_scaler: clip the two corner vertices to the clip rectangle, moving the uvs with them, then a textured quad
void h_scaler(vertex *va, vertex *vb)
{
	const float x0 = va->sx, y0 = va->sy, x1 = vb->sx, y1 = vb->sy;
	const float u0 = va->u, v0 = va->v, u1 = vb->u, v1 = vb->v;
	const float xmin = (float)gr_screen.clip_left, ymin = (float)gr_screen.clip_top;
	const float xmax = (float)gr_screen.clip_right, ymax = (float)gr_screen.clip_bottom;

	if ((y1 <= y0) || (x1 <= x0)) { return; }
	if ((x1 < xmin) || (x0 > xmax)) { return; }
	if ((y1 < ymin) || (y0 > ymax)) { return; }

	float cu0 = u0, cv0 = v0, cu1 = u1, cv1 = v1, cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1;
	if (x0 < xmin) { cu0 = SCALE_UV(xmin, x0, x1, u0, u1); cx0 = xmin; }
	if (x1 > xmax) { cu1 = SCALE_UV(xmax, x0, x1, u0, u1); cx1 = xmax; }
	if (y0 < ymin) { cv0 = SCALE_UV(ymin, y0, y1, v0, v1); cy0 = ymin; }
	if (y1 > ymax) { cv1 = SCALE_UV(ymax, y0, y1, v0, v1); cy1 = ymax; }

	if ((int)cx1 <= (int)cx0) { return; }
	if ((int)cy1 <= (int)cy0) { return; }

	vertex v[4];
	vertex *vl[4];
	std::memset(v, 0, sizeof(v));
	for (int i = 0; i < 4; i++) { vl[i] = &v[i]; v[i].sw = va->sw; v[i].z = va->z; }
	v[0].sx = cx0; v[0].sy = cy0; v[0].u = cu0; v[0].v = cv0;
	v[1].sx = cx1; v[1].sy = cy0; v[1].u = cu1; v[1].v = cv0;
	v[2].sx = cx1; v[2].sy = cy1; v[2].u = cu1; v[2].v = cv1;
	v[3].sx = cx0; v[3].sy = cy1; v[3].u = cu0; v[3].v = cv1;
	tmapper_internal(4, vl, TMAP_FLAG_TEXTURED, true);
}

void h_aascaler(vertex *, vertex *) { }			// D3D's is empty too

// ---- frame / misc ------------------------------------------------------------------------------------------------

void h_start_frame() { }
void h_stop_frame() { }
void h_print_screen(char *) { }
// Returns are the dummy's; the mode itself lives in the host shadow (see top).
int h_zbuffer_get()
{
	H.zget_pending = true;
	H.zget_saved = H.zglobal ? H.zmode : GR_ZBUFF_NONE;	// what gr_d3d_zbuffer_get would have returned
	return GR_ZBUFF_NONE;
}
int h_zbuffer_set(int mode)
{
	if (H.zget_pending) {
		if (H.zsp < 32) { H.zstack[H.zsp] = H.zget_saved; }
		H.zsp++;
		H.zmode = mode;
	} else if (mode != GR_ZBUFF_NONE) {
		H.zmode = mode;
	} else if (H.zsp > 0) {
		H.zsp--;
		H.zmode = H.zsp < 32 ? H.zstack[H.zsp] : GR_ZBUFF_NONE;
	} else {
		H.zmode = H.zglobal ? GR_ZBUFF_FULL : GR_ZBUFF_NONE;
	}
	H.zget_pending = false;
	return GR_ZBUFF_NONE;
}
void h_zbuffer_clear(int mode)
{
	ensure_frame();
	H.zsp = 0;
	if (mode) {
		H.zglobal = true;
		H.zmode = GR_ZBUFF_FULL;
		if (drawing()) {
			glow_flush();
			if (!H.run_open) { H.sink->depth_begin(H.sink->user); H.run_open = true; }
			H.sink->depth_clear(H.sink->user, gr_screen.clip_left + gr_screen.offset_x, gr_screen.clip_top + gr_screen.offset_y,
				gr_screen.clip_width, gr_screen.clip_height);
		}
	} else {
		H.zglobal = false;
		H.zmode = GR_ZBUFF_NONE;
		depth_run_end();
	}
}

// CHECKED (docs/R1_checks.md 1d): the dummy returns 0. The host returns a real id, because gr_restore_screen(id) must show
// it. Every caller branches on the sign only (id >= 0, id == -1), never on 0, and the host id is never negative (a failed
// capture returns 0 like the dummy), so each branch goes the way it goes with the dummy.
int h_save_screen()
{
	ensure_frame();
	glow_flush();
	if (H.sink == nullptr) { return 0; }
	const int id = 1000000 + H.next_capture_id++;
	if (!drawing()) {
		// A skipped frame (several game frames per Godot frame, only the last drawn) still saves the screen: a popup opened in
		// one would otherwise restore nothing, and show the window's clear colour behind it. The sink takes calls in accepted
		// frames only, so the capture waits for the next one (ensure_frame).
		H.capture_pending.push_back(id);
		return id;
	}
	return H.sink->capture_to_texture(H.sink->user, id) ? id : 0;
}
void h_restore_screen(int id)
{
	ensure_frame();
	if (!drawing() || id <= 0) { return; }
	const int c[4] = { 255, 255, 255, 255 };
	quad(0, 0, (float)gr_screen.max_w, (float)gr_screen.max_h, 0, 0, 1, 1, id, FS2_SINK_BLEND_OPAQUE, c, c, c, c);
}
void h_free_screen(int id)
{
	if (id <= 0) { return; }
	for (size_t i = 0; i < H.capture_pending.size(); i++) {
		if (H.capture_pending[i] == id) { H.capture_pending.erase(H.capture_pending.begin() + (long)i); return; }
	}
	if (drawing()) { glow_flush(); H.sink->texture_free(H.sink->user, id); }
	else if (H.sink != nullptr) { H.release_pending.push_back(id); }	// freed at the start of the next accepted frame
}

void h_dump_frame_start(int, int) { }
void h_dump_frame() { }
void h_dump_frame_stop() { }
// The gamma table the textures and vertex colours go through, as gr_d3d_set_gamma builds it; the game's own Gr_gamma and
// Gr_gamma_lookup stay as they are with the dummy. Textures baked with the old table are dropped on the next accepted frame.
void build_gamma(float gamma)
{
	if (gamma < 0.01f) { gamma = 1.0f; }
	H.inv_gamma = 1.0f / gamma;
	for (int i = 0; i < 256; i++) {
		int v = (int)(std::pow((float)i / 255.0f, 1.0f / gamma) * 255.0f);
		H.gamma_lookup[i] = v > 255 ? 255 : (v < 0 ? 0 : v);
	}
	H.flush_pending = true;
}
void h_set_gamma(float g) { build_gamma(g); }

uint h_lock() { return 1; }						// dummy's value; every caller discards it (R1_checks 1a)
void h_unlock() { }

// Packs the sink's RGBA8 into the screen's own pixel format (gr_screen.bytes_per_pixel and the Gr_red/green/blue/alpha guns that
// host_init set), the layout D3D's get_region copies out of its back buffer; callers (Autotest shots, neb2) read it with the guns.
inline uint32_t pack_gun(const color_gun &g, int v8)
{
	if (g.bits <= 0) { return 0; }
	return ((uint32_t)(v8 >> (8 - (g.bits > 8 ? 8 : g.bits))) << g.shift) & (uint32_t)g.mask;
}

void h_get_region(int, int w, int h, ubyte *data)
{
	ensure_frame();
	// a skipped frame (several game frames per Godot frame, only the last drawn) still reads the frame on show: -at_shots was black
	if (H.sink == nullptr || H.frame == FRAME_NONE || data == nullptr || w <= 0 || h <= 0) { return; }
	const int bytes = gr_screen.bytes_per_pixel;
	if (bytes != 2 && bytes != 4) { return; }
	if (drawing()) { depth_run_end(); }	// the read shows what the run drew (neb2_pre_render reads inside its 3D frame); later 3D opens a new run
	std::vector<uint8_t> px((size_t)w * (size_t)h * 4);
	if (!H.sink->read_region(H.sink->user, 0, 0, w, h, px.data())) { return; }
	for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
		const uint8_t *s = &px[i * 4];		// r g b a
		const uint32_t v = pack_gun(Gr_red, s[0]) | pack_gun(Gr_green, s[1]) | pack_gun(Gr_blue, s[2]) | pack_gun(Gr_alpha, s[3]);
		if (bytes == 4) { memcpy(data + i * 4, &v, 4); }
		else { const uint16_t v16 = (uint16_t)v; memcpy(data + i * 2, &v16, 2); }
	}
}

// Same as dummy: no gr_screen.fog_* / current_fog_* writes (ObjectSort reads them; with the dummy it always re-calls fog_set).
// The fog state lives in the host (R1_checks 1c) and goes to the sink on every draw (plain_draw). Mode NONE only switches fog
// off (colour and planes stay). Planes < 0 mean "unchanged" (as gr_d3d_fog_set): that is the tmapper's PIXEL_FOG call, which
// carries the colour. A call with planes is ObjectSort's; its colour argument is gr_screen.current_fog_color, which the host
// never writes (it stays 0, D3D keeps it current), so the colour is kept. Menus do not fog.
void h_fog_set(int mode, int r, int g, int b, float fnear, float ffar)
{
	H.fog_mode = mode;
	if (mode == GR_FOGMODE_NONE) { return; }
	if (fnear >= 0.0f && ffar >= 0.0f) { H.fog_near = fnear; H.fog_far = ffar; }
	else { H.fog_r = r; H.fog_g = g; H.fog_b = b; }
}
void h_get_pixel(int, int, int *, int *, int *) { }
void h_set_cull(int cull) { H.cull = cull; }
// gr_d3d_cross_fade: no blend, the first bitmap up to half way and the second after
void h_cross_fade(int bmap1, int bmap2, int x1, int y1, int x2, int y2, float pct)
{
	if (pct <= 50) { gr_set_bitmap(bmap1); gr_bitmap(x1, y1); }
	else { gr_set_bitmap(bmap2); gr_bitmap(x2, y2); }
}
void h_filter_set(int) { }

// uploads in accepted frames only; 0 when the frame is skipped (callers then draw nothing, as the sink would not either)
// (the sx, sy section arguments are unused: the host uploads whole bitmaps only, D-20261029-222605)
int h_tcache_set(int bitmap_id, int bitmap_type, float *u_scale, float *v_scale, int, int, int, int)
{
	ensure_frame();
	if (u_scale) { *u_scale = 1.0f; }
	if (v_scale) { *v_scale = 1.0f; }
	return tex_get(bitmap_id, bitmap_type) != 0;
}

void h_zbias(int bias) { H.zbias = bias; }
void h_flush() { }	// D3D's d3d_flush only ends and restarts the scene (the nebula calls it every frame); it never dropped textures
void h_preload_init() { }
int h_preload(int, int) { return 0; }
void h_preload_done() { }
void h_texture_memory(int *total, int *this_frame) { *total = 0; *this_frame = 0; }

// ---- backend -----------------------------------------------------------------------------------------------------

// the screen, texture and alpha-texture formats of a 32 bit ARGB8888 device (as Direct3D's 32 bit path sets them); BmpMan's
// BM_SELECT cases and the sink's texture formats agree with these. Restored in host_cleanup.
void set_argb8888(color_gun &g, int bits, int shift, int mask)
{
	g.bits = bits; g.shift = shift; g.scale = 1; g.mask = mask;
}

void set_formats_32()
{
	color_gun *sets[3][4] = { { &Gr_red, &Gr_green, &Gr_blue, &Gr_alpha }, { &Gr_t_red, &Gr_t_green, &Gr_t_blue, &Gr_t_alpha },
		{ &Gr_ta_red, &Gr_ta_green, &Gr_ta_blue, &Gr_ta_alpha } };
	for (int i = 0; i < 3; i++) {
		set_argb8888(*sets[i][0], 8, 16, 0x00ff0000);
		set_argb8888(*sets[i][1], 8, 8, 0x0000ff00);
		set_argb8888(*sets[i][2], 8, 0, 0x000000ff);
		set_argb8888(*sets[i][3], 8, 24, (int)0xff000000);
	}
	Gr_current_red = &Gr_red;
	Gr_current_green = &Gr_green;
	Gr_current_blue = &Gr_blue;
	Gr_current_alpha = &Gr_alpha;
}

gr_host_backend Backend;
int Saved_bitmap_poly = 0;
int Saved_d3d_32bit = 0;

void host_init()
{
	Bm_release_hook = on_bm_release;
	set_formats_32();
	Saved_bitmap_poly = Gr_bitmap_poly;
	Gr_bitmap_poly = 1;		// Barracks and Movie draw through gr_tmapper then, as under 32 bit Direct3D
	// the screen is 32 bit (bits_per_pixel below), so screen-format reads (bm_get_components: neb2_get_pixel on the fog read)
	// take the 32 bit path, as D3D init sets it for a 32 bit screen; at 0 they read the low 16 bits and lose red
	Saved_d3d_32bit = D3D_32bit;
	D3D_32bit = 1;
	// D7: a headless run counts as "no device" exactly as the null renderer does, so compared runs take the same branches
	Backend.no_device = Cmdline_nographics ? 1 : 0;
	gr_screen.bits_per_pixel = 32;
	gr_screen.bytes_per_pixel = 4;
	gr_screen.hardware = 1;
	gr_screen.texture_bpp = H.texture_bpp;
	build_gamma(Gr_gamma);

	gr_screen.gf_flip = h_flip;
	gr_screen.gf_flip_window = h_flip_window;
	gr_screen.gf_set_palette = h_set_palette;
	gr_screen.gf_fade_in = h_fade_in;
	gr_screen.gf_fade_out = h_fade_out;
	gr_screen.gf_flash = h_flash;
	gr_screen.gf_set_clip = h_set_clip;
	gr_screen.gf_reset_clip = h_reset_clip;
	gr_screen.gf_set_color = h_set_color;
	gr_screen.gf_get_color = h_get_color;
	gr_screen.gf_init_color = h_init_color;
	gr_screen.gf_init_alphacolor = h_init_alphacolor;
	gr_screen.gf_set_color_fast = h_set_color_fast;
	gr_screen.gf_set_font = h_set_font;
	gr_screen.gf_set_bitmap = h_set_bitmap;
	gr_screen.gf_create_shader = h_create_shader;
	gr_screen.gf_set_shader = h_set_shader;
	gr_screen.gf_clear = h_clear;
	gr_screen.gf_aabitmap = h_aabitmap;
	gr_screen.gf_aabitmap_ex = h_aabitmap_ex;
	gr_screen.gf_rect = h_rect;
	gr_screen.gf_shade = h_shade;
	gr_screen.gf_string = h_string;
	gr_screen.gf_gradient = h_gradient;
	gr_screen.gf_circle = h_circle;
	gr_screen.gf_line = h_line;
	gr_screen.gf_aaline = h_aaline;
	gr_screen.gf_pixel = h_pixel;
	gr_screen.gf_scaler = h_scaler;
	gr_screen.gf_aascaler = h_aascaler;
	gr_screen.gf_tmapper = h_tmapper;
	gr_screen.gf_print_screen = h_print_screen;
	gr_screen.gf_start_frame = h_start_frame;
	gr_screen.gf_stop_frame = h_stop_frame;
	gr_screen.gf_zbuffer_get = h_zbuffer_get;
	gr_screen.gf_zbuffer_set = h_zbuffer_set;
	gr_screen.gf_zbuffer_clear = h_zbuffer_clear;
	gr_screen.gf_save_screen = h_save_screen;
	gr_screen.gf_restore_screen = h_restore_screen;
	gr_screen.gf_free_screen = h_free_screen;
	gr_screen.gf_dump_frame_start = h_dump_frame_start;
	gr_screen.gf_dump_frame = h_dump_frame;
	gr_screen.gf_dump_frame_stop = h_dump_frame_stop;
	gr_screen.gf_set_gamma = h_set_gamma;
	gr_screen.gf_lock = h_lock;
	gr_screen.gf_unlock = h_unlock;
	gr_screen.gf_get_region = h_get_region;
	gr_screen.gf_fog_set = h_fog_set;
	gr_screen.gf_get_pixel = h_get_pixel;
	gr_screen.gf_set_cull = h_set_cull;
	gr_screen.gf_cross_fade = h_cross_fade;
	gr_screen.gf_filter_set = h_filter_set;
	gr_screen.gf_tcache_set = h_tcache_set;
	gr_screen.gf_set_clear_color = h_set_clear_color;
	gr_screen.gf_zbias = h_zbias;
	gr_screen.gf_flush = h_flush;
	gr_screen.gf_preload_init = h_preload_init;
	gr_screen.gf_preload = h_preload;
	gr_screen.gf_preload_done = h_preload_done;
	gr_screen.gf_texture_memory = h_texture_memory;
}

void host_cleanup()
{
	if (mesh_switch()) { fprintf(stderr, "render3d: billboards: %d on the sink, %d faults (not drawing %d, flags %d last 0x%x, cull %d, sink refused %d)\n", H.bb_sink, H.bb_cpu, H.bb_why[0], H.bb_why[1], H.bb_flags, H.bb_why[2], H.bb_why[3]); }
	if (mesh_switch()) { fprintf(stderr, "render3d: beams: %d offered, %d drawn on the GPU, %d declined\n", H.beam_offered, H.beam_gpu, H.beam_offered - H.beam_gpu); }
	if (mesh_switch()) { fprintf(stderr, "render3d: trails: %d offered, %d drawn on the GPU, %d declined\n", H.trail_offered, H.trail_gpu, H.trail_offered - H.trail_gpu); }
	if (mesh_switch()) { fprintf(stderr, "render3d: shields: %d offered, %d drawn on the GPU, %d declined\n", H.shield_offered, H.shield_gpu, H.shield_offered - H.shield_gpu); }
	if (mesh_switch()) { fprintf(stderr, "render3d: warp cone: %d offered, %d drawn on the GPU, %d declined\n", H.warp_offered, H.warp_gpu, H.warp_offered - H.warp_gpu); }
	if (mesh_switch()) { fprintf(stderr, "render3d: starfield bitmaps: %d offered, %d drawn on the GPU, %d declined\n", H.star_offered, H.star_gpu, H.star_offered - H.star_gpu); }
	if (mesh_switch()) { fprintf(stderr, "render3d: subspace: %d offered, %d drawn on the GPU, %d declined\n", H.sub_offered, H.sub_gpu, H.sub_offered - H.sub_gpu); }
	if (mesh_switch()) { fprintf(stderr, "render3d: lightning: %d offered, %d drawn on the GPU, %d declined\n", H.light_offered, H.light_gpu, H.light_offered - H.light_gpu); }
	if (mesh_switch()) { fprintf(stderr, "render3d: mesh draws on the polygon path: cpu_only %d, no z %d, fog unsupported %d, no frame %d, texture %d, fog state %d, dropped with a skipped frame %d\n", H.mesh_fb[0], H.mesh_fb[1], H.mesh_fb[2], H.mesh_fb[3], H.mesh_fb[4], H.mesh_fb[5], H.mesh_skipped); }
	if (mesh_switch()) { fprintf(stderr, "render3d: meshes built at first draw (late): %d\n", model_interp_mesh_late()); fprintf(stderr, "render3d: sink meshes created at a draw (late): %d\n", H.mesh_sink_late); fprintf(stderr, "render3d: textures uploaded at a mesh draw (late): %d\n", H.mesh_tex_late); fprintf(stderr, "render3d: textures uploaded at a billboard draw (late): %d\n", H.bb_tex_late); fprintf(stderr, "render3d: talking head frames preloaded: %d, found at play: %d\n", H.head_frames, H.head_hits); }
	depth_run_end();
	if (H.sink != nullptr && H.frame == FRAME_OPEN) { H.sink->frame_end(H.sink->user); }
	if (H.sink != nullptr && H.frame != FRAME_NONE) { }
	H.tex.clear(); H.head_layer.clear();
	H.meshes.clear();
	H.release_pending.clear();
	H.capture_pending.clear();
	Bm_release_hook = nullptr;
	H.frame = FRAME_NONE;
	Gr_bitmap_poly = Saved_bitmap_poly;
	D3D_32bit = Saved_d3d_32bit;
}

void host_force_windowed() { }
void host_activate(int) { }

} // namespace

// ---- entry point for fs2_api (fs2_set_render_sink) ---------------------------------------------------------------

// 32 (default) or 16. Call before gr_init.
void gr_host_set_texture_bpp(int bpp) { H.texture_bpp = (bpp == 16) ? 16 : 32; }

// Attach a sink and register the host backend, or with NULL (or a table that does not fit this build) detach and unregister,
// so the game falls back to the null renderer. The table is copied. Call before the game starts. Returns 1 when attached.
int gr_host_attach_sink(const fs2_render_sink *sink)
{
	H.sink = nullptr;
	gr_host_backend_register(nullptr);
	if (sink == nullptr || sink->version != FS2_RENDER_SINK_VERSION || sink->size < sizeof(fs2_render_sink)) { return 0; }
	if (!sink->frame_begin || !sink->frame_end || !sink->clear || !sink->set_clip || !sink->texture_create || !sink->texture_free
		|| !sink->texture_free_all || !sink->draw_tris || !sink->capture_to_texture || !sink->read_region || !sink->set_gamma
		|| !sink->depth_begin || !sink->depth_end || !sink->depth_clear || !sink->set_view) {
		return 0;
	}
	H.sink_copy = *sink;
	H.sink_copy.size = sizeof(fs2_render_sink);
	H.sink = &H.sink_copy;
	Backend.init = host_init;
	Backend.cleanup = host_cleanup;
	Backend.force_windowed = host_force_windowed;
	Backend.activate = host_activate;
	Backend.bitmap = h_bitmap;
	Backend.bitmap_ex = h_bitmap_ex;
	Backend.no_device = 0;
	Backend.mesh_ready = h_mesh_ready;
	Backend.mesh_create = h_mesh_create;
	Backend.billboard = h_billboard;
	Backend.mesh_draw = h_mesh_draw;
	Backend.mesh_draw_lit = h_mesh_draw_lit;
	Backend.mesh_clip = h_mesh_clip;
	Backend.mesh_thrust = h_mesh_thrust;
	Backend.mesh_bg = h_mesh_bg;
	Backend.mesh_free = h_mesh_free;
	Backend.model_page_in = h_model_page_in;
	Backend.level_loaded = h_level_loaded;
	gr_host_backend_register(&Backend);
	return 1;
}
