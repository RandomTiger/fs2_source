/* render_sink.h  (R1-0)
 * The render sink: what gr_host (the neutral gf_* backend inside the game DLL) needs from whoever shows the pixels.
 * The Godot wrapper implements it with RenderingServer (render_sink_godot.*); a recording fake implements it for tests
 * (fake_sink.h). Plain C types only (D-20261027-190100): no CRT object, allocation or free crosses it, no Godot type,
 * no game type.
 *
 * Rules every implementation keeps (D-20261027-190100, -190107):
 *  - All calls arrive on one thread (the Godot main thread, where the game fiber runs).
 *  - Nothing passed in is kept after the call returns: copy what you need. Pointers are valid for the call only.
 *  - The sink never reads the clock, calls back into the game, or touches any game state.
 *  - frame_begin() returns 0 when the host does not want this frame drawn (replays run several game frames per
 *    Godot frame). gr_host then makes NO other sink call until the next frame_begin; the game's own state is updated
 *    the same either way. frame_end() is only called for a frame that frame_begin() accepted.
 *
 * Coordinates are game screen pixels, y down, origin top left, in the size given to frame_begin().
 */
#ifndef FS2_RENDER_SINK_H
#define FS2_RENDER_SINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS2_RENDER_SINK_VERSION 1

/* texture formats handed to texture_create() */
#define FS2_SINK_TEX_RGBA8	0	/* 4 bytes per pixel, r g b a in memory order */
#define FS2_SINK_TEX_A8		1	/* 1 byte per pixel, coverage only (AABITMAP, font atlas): drawn as tint colour with this alpha */

/* texture flags */
#define FS2_SINK_TEXF_REPEAT	1	/* wrap u and v (TILED polys); otherwise clamp */
#define FS2_SINK_TEXF_LINEAR	2	/* bilinear; otherwise nearest (UI text keeps integer texels) */

/* blend modes of a draw */
#define FS2_SINK_BLEND_OPAQUE	0	/* replace */
#define FS2_SINK_BLEND_ALPHA	1	/* src*a + dst*(1-a) */
#define FS2_SINK_BLEND_ADD	2	/* src*a + dst (GR_ALPHABLEND_FILTER where the D3D device allows it) */

/* draw flags */
#define FS2_SINK_DRAWF_PERSPECTIVE	1	/* vertices carry a usable sw: divide u,v per pixel by the interpolated sw */
#define FS2_SINK_DRAWF_ZREAD		2	/* depth state of the call, carried for the mission path (R1-8); ignored on canvas */
#define FS2_SINK_DRAWF_ZWRITE		4

/* texture ids chosen by the host; 0 means "no texture" */
typedef struct fs2_sink_vertex {
	float x, y;		/* screen pixels */
	float sw;		/* 1/w of the projected vertex (D3D rhw); only meaningful with FS2_SINK_DRAWF_PERSPECTIVE */
	float u, v;		/* texel position in 0..1 of the texture (not premultiplied by sw) */
	uint8_t r, g, b, a;	/* vertex colour, straight alpha, 0..255 */
} fs2_sink_vertex;

typedef struct fs2_sink_draw {
	int32_t texture;	/* 0 = untextured */
	int32_t blend;		/* FS2_SINK_BLEND_* */
	uint32_t flags;		/* FS2_SINK_DRAWF_* */
	uint8_t tint[4];	/* r g b a multiplier on vertex colour (255 = none) */
} fs2_sink_draw;

typedef struct fs2_render_sink {
	uint32_t size;		/* sizeof(fs2_render_sink) as the caller built it */
	uint32_t version;	/* FS2_RENDER_SINK_VERSION */
	void *user;		/* passed back as the first argument of every call */

	/* 1 = draw this frame, 0 = skip it (see rules). w,h = the game screen size, constant between calls. */
	int  (*frame_begin)(void *user, int w, int h);
	/* The frame is complete (gr_flip): show it. "Latest frame wins": the sink may drop an older one it has not shown. */
	void (*frame_end)(void *user);
	void (*clear)(void *user, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
	/* Scissor for later draws, in screen pixels. w<=0 or h<=0 means nothing is visible. */
	void (*set_clip)(void *user, int x, int y, int w, int h);

	/* pixels are tightly packed (w*bytes_per_pixel per row) and copied by the sink before the call returns */
	void (*texture_create)(void *user, int32_t id, int w, int h, int format, uint32_t texflags, const uint8_t *pixels);
	void (*texture_free)(void *user, int32_t id);
	void (*texture_free_all)(void *user);

	/* A triangle list. indices are into verts; nindices is a multiple of 3. A fan of nv vertices is nv-2 triangles. */
	void (*draw_tris)(void *user, const fs2_sink_draw *draw, const fs2_sink_vertex *verts, int nverts,
		const uint16_t *indices, int nindices);

	/* Copy the frame drawn so far into a new RGBA8 texture with this id (save_screen). 1 = done. */
	int  (*capture_to_texture)(void *user, int32_t id);
	/* Read back a region of the frame drawn so far as RGBA8, w*h*4 bytes into out (get_region, screenshots). 1 = done. */
	int  (*read_region)(void *user, int x, int y, int w, int h, uint8_t *out);
	void (*set_gamma)(void *user, float gamma);
} fs2_render_sink;

#ifdef __cplusplus
}
#endif

#endif
