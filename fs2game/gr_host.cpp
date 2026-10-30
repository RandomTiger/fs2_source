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
// calls, texture cache. Not yet: fog, shaders on 3D polys, cross_fade, glow redraw pass of tmapper, pixel fog.
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
#include <cstring>

#include "render_sink.h"
#include "tex_convert.h"

#include <vector>
#include <unordered_map>
#include <cmath>

// GrD3D.cpp's screen-depth flag: 1 = the screen is 32 bit. Only D3D init sets it; bm_get_components reads it to pick the
// 32 bit read of screen-format pixels (D-20261029-095513). host_init sets it for the host's 32 bit screen.
extern int D3D_32bit;

namespace {

#define NEBULA_COLORS 20		// as GrD3DRender.cpp

enum FrameState { FRAME_NONE = 0, FRAME_OPEN = 1, FRAME_SKIPPED = 2 };

struct HostTex {
	int sink_id;		// 0 = not uploaded
	int type;			// TCACHE_TYPE_* it was uploaded as
	int w, h;
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
	bool flush_pending = false;		// drop every sink texture at the start of the next accepted frame (flush, gamma change)
	int gamma_lookup[256];			// the host's own gamma table; gr_gamma_lookup stays untouched, as with the dummy
	// fog state kept here (R1_checks 1c); the dummy writes none of gr_screen's fog fields
	int fog_mode = 0, fog_r = 0, fog_g = 0, fog_b = 0;
	float fog_near = 0.0f, fog_far = 0.0f;
	// depth shadow (see top): host only, the game reads none of it
	int zmode = GR_ZBUFF_NONE;		// the mode D3D would have (gr_zbuffering_mode)
	bool zglobal = false;			// gr_zbuffer_clear(TRUE) seen since the last clear(FALSE) / flip
	int zstack[32];
	int zsp = 0;
	bool zget_pending = false;		// get() seen, the next set() is the save half of a pair
	int zget_saved = GR_ZBUFF_NONE;
	bool run_open = false;			// a sink depth run is open (drawn frames only)
	int zbias = 0;
	int cull = 0;
} H;

inline bool drawing() { return H.frame == FRAME_OPEN; }

// ---- frame ----------------------------------------------------------------------------------------------------

void send_clip()
{
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
			H.tex.clear();
			H.flush_pending = false;
		}
		for (int id : H.release_pending) { H.sink->texture_free(H.sink->user, id); }
		H.release_pending.clear();
		send_clip();
	}
}

// ---- helpers --------------------------------------------------------------------------------------------------

inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

void draw(const fs2_sink_draw &d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
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
enum TexKind { TK_NORMAL = 0, TK_AA = 1, TK_XPARENT = 2, TK_REPEAT = 3, TK_2D = 4 };

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

// Upload a bitmap as the kind asked for. Only legal in an accepted frame. Returns the sink id or 0.
// type is a TCACHE_TYPE_*; repeat = wrap u and v (TILED polys); bitmap_2d = the whole bitmap, unfiltered (TMAP_FLAG_BITMAP_2D).
int tex_get(int bitmap_id, int type, bool repeat = false, bool bitmap_2d = false)
{
	if (!drawing() || bitmap_id < 0) { return 0; }
	const bool aa = (type == TCACHE_TYPE_AABITMAP);
	int kind = TK_NORMAL;
	if (aa) { kind = TK_AA; }
	else if (bitmap_2d) { kind = TK_2D; }
	else if (type == TCACHE_TYPE_XPARENT) { kind = TK_XPARENT; }
	else if (repeat) { kind = TK_REPEAT; }
	const TexKey key = { bitmap_id, bm_get_signature(bitmap_id), kind };
	auto found = H.tex.find(key);
	if (found != H.tex.end() && found->second.sink_id != 0) { return found->second.sink_id; }

	// the flags D3D's texture loaders ask bm_lock for (d3d_create_texture; a 2D bitmap's tiles as d3d_create_texture_sectioned)
	ubyte flags = 0;
	ubyte bpp = 16;			// the game's convention: ask for 16, BmpMan upgrades to 32 for gr_screen.texture_bpp == 32
	if (aa) { flags = BMP_AABITMAP; bpp = 8; }
	else if (bitmap_2d) { flags = BMP_TEX_XPARENT; }
	else if (type == TCACHE_TYPE_XPARENT) { flags = BMP_TEX_XPARENT; }
	else { flags = BMP_TEX_OTHER | BMP_TEX_XPARENT; }		// TCACHE_TYPE_NORMAL
	bitmap *bmp = bm_lock(bitmap_id, bpp, flags);
	if (bmp == nullptr) { return 0; }

	int w = bmp->w, h = bmp->h;
	const uint8_t *src = (const uint8_t *)bmp->data;
	std::vector<uint8_t> px;
	int format;
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
	}
	bm_unlock(bitmap_id);

	HostTex &t = H.tex[key];
	t.sink_id = H.next_sink_id++;
	t.type = type; t.w = w; t.h = h;
	// UI text and 2D bitmaps keep integer texels (D3D: TEXTURE_SOURCE_NO_FILTERING); everything else is bilinear
	uint32_t tf = (aa || bitmap_2d) ? 0 : FS2_SINK_TEXF_LINEAR;
	if (kind == TK_REPEAT) { tf |= FS2_SINK_TEXF_REPEAT; }
	H.sink->texture_create(H.sink->user, t.sink_id, w, h, format, tf, px.data());
	return t.sink_id;
}

// ---- gf_ table: state (identical effects to the dummy / D3D bookkeeping) ----------------------------------------

void depth_run_end()
{
	if (H.run_open && drawing()) { H.sink->depth_end(H.sink->user); }
	H.run_open = false;
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

void h_set_clip(int x, int y, int w, int h)
{
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
// Not done here: the ship glow pass (TMAP_FLAG_NONDARKENING). Pixel fog (TMAP_FLAG_PIXEL_FOG) only sets the fog colour (see
// the PIXEL_FOG block below); the fog itself is the sink's (R2).
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
		if (!H.run_open) { H.sink->depth_begin(H.sink->user); H.run_open = true; }
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

	const unsigned dflags = ((flags & TMAP_FLAG_CORRECT) ? FS2_SINK_DRAWF_PERSPECTIVE : 0) | zflags | (H.cull ? FS2_SINK_DRAWF_CULL : 0);
	fs2_sink_draw d = plain_draw(tex, blend, dflags);
	d.zbias = H.zbias;
	// D-20261030-052453: fog_rgb[3] marks a poly that set its own fog colour above (render_sink.h); the colour itself is unchanged
	d.fog_rgb[3] = (flags & TMAP_FLAG_PIXEL_FOG) ? 1 : 0;
	draw(d, out, nv, idx, (nv - 2) * 3);
}

void h_tmapper(int nv, vertex *verts[], uint flags) { tmapper_internal(nv, verts, flags); }

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
	if (!drawing()) { return 0; }
	const int id = 1000000 + H.next_capture_id++;
	return H.sink->capture_to_texture(H.sink->user, id) ? id : 0;
}
void h_restore_screen(int id)
{
	ensure_frame();
	if (!drawing() || id <= 0) { return; }
	const int c[4] = { 255, 255, 255, 255 };
	quad(0, 0, (float)gr_screen.max_w, (float)gr_screen.max_h, 0, 0, 1, 1, id, FS2_SINK_BLEND_OPAQUE, c, c, c, c);
}
void h_free_screen(int id) { if (drawing() && id > 0) { H.sink->texture_free(H.sink->user, id); } }

void h_dump_frame_start(int, int) { }
void h_dump_frame() { }
void h_dump_frame_stop() { }
// The gamma table the textures and vertex colours go through, as gr_d3d_set_gamma builds it; the game's own Gr_gamma and
// Gr_gamma_lookup stay as they are with the dummy. Textures baked with the old table are dropped on the next accepted frame.
void build_gamma(float gamma)
{
	if (gamma < 0.01f) { gamma = 1.0f; }
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
	if (!drawing() || data == nullptr || w <= 0 || h <= 0) { return; }
	const int bytes = gr_screen.bytes_per_pixel;
	if (bytes != 2 && bytes != 4) { return; }
	depth_run_end();	// the read shows what the run drew (neb2_pre_render reads inside its 3D frame); later 3D opens a new run
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
void h_flush() { H.flush_pending = true; }
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
	depth_run_end();
	if (H.sink != nullptr && H.frame == FRAME_OPEN) { H.sink->frame_end(H.sink->user); }
	if (H.sink != nullptr && H.frame != FRAME_NONE) { }
	H.tex.clear();
	H.release_pending.clear();
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
		|| !sink->depth_begin || !sink->depth_end || !sink->depth_clear) {
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
	gr_host_backend_register(&Backend);
	return 1;
}
