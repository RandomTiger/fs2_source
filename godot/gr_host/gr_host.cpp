// gr_host.cpp  (R1-4 draft)
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
// calls, texture cache. Not yet: sectioned bitmaps (TMAP_FLAG_BITMAP_SECTION), fog, shaders on 3D polys, cross_fade,
// glow redraw pass of tmapper, pixel fog. Compile check: see README, "R1-4 draft".

#include "2d.h"
#include "GrInternal.h"
#include "bmpman.h"
#include "Font.h"

#include "host_stub.h"
#include "../render_sink/render_sink.h"
#include "tex_convert.h"

#include <vector>
#include <cmath>

namespace {

enum FrameState { FRAME_NONE = 0, FRAME_OPEN = 1, FRAME_SKIPPED = 2 };

struct HostTex {
	int sink_id;		// 0 = not uploaded
	int type;			// TCACHE_TYPE_* it was uploaded as
	int w, h;
};

struct Host {
	const fs2_render_sink *sink = nullptr;
	int frame = FRAME_NONE;
	int texture_bpp = 32;			// 32 default; 16 selectable (ColourDepth=16 / -force_16bit)
	int next_sink_id = 1;
	int next_capture_id = 1;
	std::vector<HostTex> tex;		// indexed by bm handle * 2 + (aabitmap ? 1 : 0)
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
	if (H.frame != FRAME_NONE || H.sink == nullptr) { return; }
	H.frame = H.sink->frame_begin(H.sink->user, gr_screen.max_w, gr_screen.max_h) ? FRAME_OPEN : FRAME_SKIPPED;
	if (drawing()) { send_clip(); }
}

// ---- helpers --------------------------------------------------------------------------------------------------

inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

void draw(const fs2_sink_draw &d, const fs2_sink_vertex *v, int nv, const uint16_t *idx, int ni)
{
	H.sink->draw_tris(H.sink->user, &d, v, nv, idx, ni);
}

fs2_sink_draw plain_draw(int texture, int blend, unsigned flags = 0)
{
	fs2_sink_draw d;
	d.texture = texture; d.blend = blend; d.flags = flags;
	d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 255;
	return d;
}

fs2_sink_vertex vtx(float x, float y, float u, float v, int r, int g, int b, int a)
{
	fs2_sink_vertex s;
	s.x = x; s.y = y; s.sw = 1.0f; s.u = u; s.v = v;
	s.r = clamp8(r); s.g = clamp8(g); s.b = clamp8(b); s.a = clamp8(a);
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

// Upload bitmap as the type asked for. Only legal in an accepted frame. Returns the sink id or 0.
int tex_get(int bitmap_id, int type)
{
	if (!drawing() || bitmap_id < 0) { return 0; }
	const bool aa = (type == TCACHE_TYPE_AABITMAP);
	const size_t slot = (size_t)bitmap_id * 2 + (aa ? 1 : 0);
	if (slot >= H.tex.size()) { H.tex.resize(slot + 64, HostTex{ 0, 0, 0, 0 }); }
	HostTex &t = H.tex[slot];
	if (t.sink_id != 0 && t.type == type) { return t.sink_id; }

	ubyte flags = 0;
	ubyte bpp = (ubyte)H.texture_bpp;
	switch (type) {
	case TCACHE_TYPE_AABITMAP: flags = BMP_AABITMAP; bpp = 8; break;
	case TCACHE_TYPE_NORMAL: flags = BMP_TEX_OTHER; break;
	case TCACHE_TYPE_XPARENT: flags = BMP_TEX_XPARENT; break;
	default: flags = BMP_TEX_OTHER; break;
	}
	bitmap *bmp = bm_lock(bitmap_id, bpp, flags);
	if (bmp == nullptr) { return 0; }

	const int w = bmp->w, h = bmp->h;
	const uint8_t *src = (const uint8_t *)bmp->data;
	std::vector<uint8_t> px;
	int format;
	if (aa) {
		px.resize(fs2host::a8_bytes(w, h));
		fs2host::to_a8(src, w, h, w, px.data());
		format = FS2_SINK_TEX_A8;
	} else {
		const fs2host::PixLayout &l = fs2host::layout_for_texture_bpp(H.texture_bpp);
		px.resize(fs2host::rgba8_bytes(w, h));
		fs2host::to_rgba8(src, w, h, w * l.bytes, l, px.data());
		format = FS2_SINK_TEX_RGBA8;
	}
	bm_unlock(bitmap_id);

	if (t.sink_id != 0) { H.sink->texture_free(H.sink->user, t.sink_id); }
	t.sink_id = H.next_sink_id++;
	t.type = type; t.w = w; t.h = h;
	// 16 bit textures keep integer texels for the UI and bilinear elsewhere, as the D3D path does
	const uint32_t tf = (aa ? 0 : FS2_SINK_TEXF_LINEAR);
	H.sink->texture_create(H.sink->user, t.sink_id, w, h, format, tf, px.data());
	return t.sink_id;
}

// ---- gf_ table: state (identical effects to the dummy / D3D bookkeeping) ----------------------------------------

void h_flip()
{
	ensure_frame();
	if (drawing()) { H.sink->frame_end(H.sink->user); }
	H.frame = FRAME_NONE;
}
void h_flip_window(uintptr_t, int, int, int, int) { h_flip(); }
void h_set_palette(ubyte *, int) { }				// same as dummy and D3D: nothing. BmpMan reads gr_palette, set by gr_set_palette_internal (R1_checks 1b)
void h_fade_in(int) { }
void h_fade_out(int) { }
void h_flash(int, int, int) { }

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
		(float)(y + h + gr_screen.offset_y), c.red, c.green, c.blue, c.alpha);
}

void h_shade(int x, int y, int w, int h)
{
	ensure_frame();
	if (!drawing()) { return; }
	const shader &s = gr_screen.current_shader;
	// the same scaling as gr_d3d_shade
	solid_rect((float)(x + gr_screen.offset_x), (float)(y + gr_screen.offset_y), (float)(x + w + gr_screen.offset_x),
		(float)(y + h + gr_screen.offset_y), (int)(s.r * 255.0f), (int)(s.g * 255.0f), (int)(s.b * 255.0f), (int)(s.c * 255.0f * 6.0f));
}

void h_pixel(int x, int y)
{
	ensure_frame();
	if (!drawing()) { return; }
	const color &c = gr_screen.current_color;
	solid_rect((float)(x + gr_screen.offset_x), (float)(y + gr_screen.offset_y), (float)(x + 1 + gr_screen.offset_x),
		(float)(y + 1 + gr_screen.offset_y), c.red, c.green, c.blue, c.alpha);
}

// a 1 pixel wide line as a quad along the segment
void h_line(int x1, int y1, int x2, int y2)
{
	ensure_frame();
	if (!drawing()) { return; }
	const color &c = gr_screen.current_color;
	const float ax = (float)(x1 + gr_screen.offset_x) + 0.5f, ay = (float)(y1 + gr_screen.offset_y) + 0.5f;
	const float bx = (float)(x2 + gr_screen.offset_x) + 0.5f, by = (float)(y2 + gr_screen.offset_y) + 0.5f;
	float dx = bx - ax, dy = by - ay;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 0.001f) { h_pixel(x1, y1); return; }
	const float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
	static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
	const fs2_sink_vertex v[4] = { vtx(ax + nx, ay + ny, 0, 0, c.red, c.green, c.blue, c.alpha), vtx(bx + nx, by + ny, 0, 0, c.red, c.green, c.blue, c.alpha),
		vtx(bx - nx, by - ny, 0, 0, c.red, c.green, c.blue, c.alpha), vtx(ax - nx, ay - ny, 0, 0, c.red, c.green, c.blue, c.alpha) };
	draw(plain_draw(0, c.alpha < 255 ? FS2_SINK_BLEND_ALPHA : FS2_SINK_BLEND_OPAQUE), v, 4, idx, 6);
}

void h_aaline(vertex *v1, vertex *v2) { h_line((int)v1->sx, (int)v1->sy, (int)v2->sx, (int)v2->sy); }

// the colour fades from the current colour (alpha as set) to nothing along the line direction
void h_gradient(int x1, int y1, int x2, int y2)
{
	ensure_frame();
	if (!drawing()) { return; }
	const color &c = gr_screen.current_color;
	const int a = c.is_alphacolor ? c.alpha : 255;
	const int on[4] = { c.red, c.green, c.blue, a }, off[4] = { c.red, c.green, c.blue, 0 };
	const float ox = (float)gr_screen.offset_x, oy = (float)gr_screen.offset_y;
	if (std::abs(x2 - x1) >= std::abs(y2 - y1)) {
		const int lo = x1 < x2 ? x1 : x2;
		const int hi = x1 < x2 ? x2 : x1;
		const bool fade_right = x2 >= x1;
		quad(lo + ox, y1 + oy, hi + ox, y1 + 1 + oy, 0, 0, 0, 0, 0, FS2_SINK_BLEND_ALPHA, fade_right ? on : off, fade_right ? off : on,
			fade_right ? off : on, fade_right ? on : off);
	} else {
		const int lo = y1 < y2 ? y1 : y2;
		const int hi = y1 < y2 ? y2 : y1;
		const bool fade_down = y2 >= y1;
		quad(x1 + ox, lo + oy, x1 + 1 + ox, hi + oy, 0, 0, 0, 0, 0, FS2_SINK_BLEND_ALPHA, fade_down ? on : off, fade_down ? on : off,
			fade_down ? off : on, fade_down ? off : on);
	}
}

// a filled disc, d = diameter as in gr_d3d_circle; DRAFT: D3D's is an outline-and-fill walk, to be matched in R1-5
void h_circle(int xc, int yc, int d)
{
	ensure_frame();
	if (!drawing()) { return; }
	const color &c = gr_screen.current_color;
	const float r = d * 0.5f, cx = (float)(xc + gr_screen.offset_x), cy = (float)(yc + gr_screen.offset_y);
	const int N = 32;
	fs2_sink_vertex v[N + 1];
	uint16_t idx[N * 3];
	v[0] = vtx(cx, cy, 0, 0, c.red, c.green, c.blue, c.alpha);
	for (int i = 0; i < N; i++) {
		const float a = 6.2831853f * (float)i / (float)N;
		v[1 + i] = vtx(cx + r * std::cos(a), cy + r * std::sin(a), 0, 0, c.red, c.green, c.blue, c.alpha);
		idx[i * 3] = 0; idx[i * 3 + 1] = (uint16_t)(1 + i); idx[i * 3 + 2] = (uint16_t)(1 + (i + 1) % N);
	}
	draw(plain_draw(0, c.alpha < 255 ? FS2_SINK_BLEND_ALPHA : FS2_SINK_BLEND_OPAQUE), v, N + 1, idx, N * 3);
}

// bitmap pieces: every section of gr_screen.current_bitmap drawn clipped, the way gr_d3d_bitmap_ex does
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

// 3D polys: a fan of nv vertices. Colour rules follow gr_d3d_tmapper_internal (RGB gouraud through the gamma table).
void h_tmapper(int nv, vertex *verts[], uint flags)
{
	if (nv < 3 || nv > 32) { return; }
	ensure_frame();
	if (!drawing()) { return; }

	int blend = FS2_SINK_BLEND_OPAQUE, alpha = 255;
	int tint_r = 255, tint_g = 255, tint_b = 255;
	if (!(flags & TMAP_FLAG_TEXTURED)) {
		tint_r = gr_screen.current_color.red; tint_g = gr_screen.current_color.green; tint_b = gr_screen.current_color.blue;
	}
	if (gr_screen.current_alphablend_mode == GR_ALPHABLEND_FILTER) {
		// D3D with additive caps: src*alpha + dst, colour scaled by the bitmap alpha
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
		tex = tex_get(gr_screen.current_bitmap, TCACHE_TYPE_XPARENT);
		if (tex == 0) { return; }
	}

	fs2_sink_vertex out[32];
	uint16_t idx[30 * 3];
	for (int i = 0; i < nv; i++) {
		const vertex *va = verts[i];
		int r = tint_r, g = tint_g, b = tint_b;
		if ((flags & TMAP_FLAG_RAMP) && (flags & TMAP_FLAG_GOURAUD)) {
			r = g = b = Gr_gamma_lookup[va->b];
		} else if ((flags & TMAP_FLAG_RGB) && (flags & TMAP_FLAG_GOURAUD)) {
			r = Gr_gamma_lookup[va->r]; g = Gr_gamma_lookup[va->g]; b = Gr_gamma_lookup[va->b];
		}
		const int a = (flags & TMAP_FLAG_ALPHA) ? va->a : alpha;
		out[i] = vtx(va->sx + (float)gr_screen.offset_x, va->sy + (float)gr_screen.offset_y, va->u, va->v, r, g, b, a);
		out[i].sw = (flags & TMAP_FLAG_CORRECT) ? va->sw : 1.0f;
	}
	for (int i = 0; i < nv - 2; i++) { idx[i * 3] = 0; idx[i * 3 + 1] = (uint16_t)(i + 1); idx[i * 3 + 2] = (uint16_t)(i + 2); }

	unsigned dflags = (flags & TMAP_FLAG_CORRECT) ? FS2_SINK_DRAWF_PERSPECTIVE : 0;
	fs2_sink_draw d = plain_draw(tex, blend, dflags);
	// TILED polys repeat the texture (uv > 1); the sink fixes that per texture, so the host needs a repeat variant (R1-5)
	draw(d, out, nv, idx, (nv - 2) * 3);
}

void h_scaler(vertex *va, vertex *vb)
{
	// scaler quads are two screen points plus size in sw; mirrored in R1-5 with the other bitmap scalers
	(void)va; (void)vb;
}

// ---- frame / misc ------------------------------------------------------------------------------------------------

void h_start_frame() { }
void h_stop_frame() { }
void h_print_screen(char *) { }
int h_zbuffer_get() { return GR_ZBUFF_NONE; }			// dummy's value
int h_zbuffer_set(int) { return GR_ZBUFF_NONE; }
void h_zbuffer_clear(int) { }

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
void h_set_gamma(float g) { ensure_frame(); if (drawing()) { H.sink->set_gamma(H.sink->user, g); } }

uint h_lock() { return 1; }						// dummy's value; every caller discards it (R1_checks 1a)
void h_unlock() { }

void h_get_region(int, int w, int h, ubyte *data)
{
	ensure_frame();
	if (!drawing() || data == nullptr) { return; }
	std::vector<uint8_t> px((size_t)w * (size_t)h * 4);
	if (!H.sink->read_region(H.sink->user, 0, 0, w, h, px.data())) { return; }
	// DRAFT: the caller's pixel layout is the screen's; 32 bit host screen, so ARGB8888 bytes (b g r a in memory)
	for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
		data[i * 4 + 0] = px[i * 4 + 2]; data[i * 4 + 1] = px[i * 4 + 1]; data[i * 4 + 2] = px[i * 4 + 0]; data[i * 4 + 3] = px[i * 4 + 3];
	}
}

// Same as dummy: no gr_screen.fog_* / current_fog_* writes (ObjectSort reads them; with the dummy it always re-calls fog_set).
// Fog state, when added, stays in the host's own state (R1_checks 1c).
void h_fog_set(int, int, int, int, float, float) { }
void h_get_pixel(int, int, int *, int *, int *) { }
void h_set_cull(int) { }
void h_cross_fade(int, int, int, int, int, int, float) { }
void h_filter_set(int) { }

// uploads in accepted frames only; 0 when the frame is skipped (callers then draw nothing, as the sink would not either)
int h_tcache_set(int bitmap_id, int bitmap_type, float *u_scale, float *v_scale, int, int, int, int)
{
	ensure_frame();
	if (u_scale) { *u_scale = 1.0f; }
	if (v_scale) { *v_scale = 1.0f; }
	return tex_get(bitmap_id, bitmap_type) != 0;
}

void h_zbias(int) { }
void h_flush() { }
void h_preload_init() { }
int h_preload(int, int) { return 0; }
void h_preload_done() { }
void h_texture_memory(int *total, int *this_frame) { *total = 0; *this_frame = 0; }

// ---- backend -----------------------------------------------------------------------------------------------------

void host_init()
{
	gr_screen.bits_per_pixel = 32;
	gr_screen.bytes_per_pixel = 4;
	gr_screen.hardware = 1;
	gr_screen.texture_bpp = H.texture_bpp;

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
	gr_screen.gf_aascaler = h_scaler;
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
	if (H.sink != nullptr && H.frame == FRAME_OPEN) { H.sink->frame_end(H.sink->user); }
	if (H.sink != nullptr && H.frame != FRAME_NONE) { }
	H.tex.clear();
	H.frame = FRAME_NONE;
}

void host_force_windowed() { }
void host_activate(int) { }

} // namespace

// ---- entry points for the Godot wrapper ---------------------------------------------------------------------------

// The sink must outlive the backend. Call before gr_init(.., GR_HOST).
void gr_host_set_sink(const fs2_render_sink *sink) { H.sink = sink; }

// 32 (default) or 16. Call before gr_init.
void gr_host_set_texture_bpp(int bpp) { H.texture_bpp = (bpp == 16) ? 16 : 32; }

// The backend to hand to gr_host_backend_register().
gr_host_backend gr_host_make_backend()
{
	gr_host_backend b;
	b.init = host_init;
	b.cleanup = host_cleanup;
	b.force_windowed = host_force_windowed;
	b.activate = host_activate;
	b.bitmap = h_bitmap;
	b.bitmap_ex = h_bitmap_ex;
	b.no_device = 0;
	return b;
}
