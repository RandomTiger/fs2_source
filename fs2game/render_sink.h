/* render_sink.h  (R1-0, v2 R1-8)
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
 *
 * v2 (R1-8, D2 hybrid): depth runs. Draws between depth_begin() and depth_end() are depth tested against each other
 * (FS2_SINK_DRAWF_ZREAD / ZWRITE, vertex z) and the run is composited, in call order, with the draws around it.
 * depth_clear() clears depth in a rect inside a run (later draws are in front of everything before the clear).
 * Draws outside a run are plain 2D: flags ZREAD/ZWRITE and z are ignored. Fog fields: see below (R2, honoured by a sink that implements them).
 */
#ifndef FS2_RENDER_SINK_H
#define FS2_RENDER_SINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS2_RENDER_SINK_VERSION 4

/* v3 (P1, GodotRender3D): the game camera, for a sink that draws meshes. Informational: no draw changes with it. */
typedef struct fs2_sink_view {
	float eye_pos[3];		/* world position of the eye (Eye_position) */
	float eye_rvec[3], eye_uvec[3], eye_fvec[3];	/* world to eye rotation rows, unscaled (Eye_matrix) */
	float scale[3];			/* Matrix_scale: aspect and zoom, folded into the game's view rows */
	float zoom;			/* View_zoom */
	int32_t canvas_w, canvas_h;	/* the 3D canvas the projection maps to, in pixels */
	int32_t origin_x, origin_y;	/* the canvas's top left on the screen (screen offset plus clip origin) */
	int32_t user_clip;		/* 1: a user clip plane is on */
	float clip_normal[3], clip_point[3];	/* in eye space, as the game keeps them */
} fs2_sink_view;

/* v4 (P3b, GodotRender3D): meshes. Geometry is created once per (model, submodel, LOD) in model space and drawn each frame with a
 * transform and per-vertex colours (the game's CPU lighting, until the GPU lighting phase). All three mesh calls are optional (NULL = the
 * sink has no mesh path; the host then draws polygons as before). A group is a run of triangles that share one draw state, kept in the
 * retail submission order; a sink may join opaque, depth-written groups of equal state across meshes. */
typedef struct fs2_sink_mesh_vertex {
	float pos[3];		/* model space */
	float normal[3];
	float u, v;
} fs2_sink_mesh_vertex;

/* The game's lighting of one draw (lighting.cpp, light_apply_rgb): sun lights, ambient, point and tube lights in the mesh's model space.
 * flat >= 0: every vertex is that grey byte and nothing else applies. All colours are 0..1, the result goes through the game's byte
 * quantisation and then the gamma table (pow(byte / 255, inv_gamma)). */
#define FS2_SINK_MAX_SUN 4
#define FS2_SINK_MAX_POINT 16
typedef struct fs2_sink_mesh_light {
	int32_t flat;
	float ambient, reflective, level;	/* level: the static light level of the object */
	int32_t darken;				/* 1: sun lights darken (LM_DARKEN) instead of brighten */
	float inv_gamma;
	int32_t nsun;
	float sun_dir[FS2_SINK_MAX_SUN][3];	/* model space */
	float sun_intensity[FS2_SINK_MAX_SUN];
	float sun_rgb[FS2_SINK_MAX_SUN][3];
	int32_t npoint;
	int32_t point_tube[FS2_SINK_MAX_POINT];	/* 1: a tube between pos and pos2, 0: a point at pos */
	float point_pos[FS2_SINK_MAX_POINT][3];
	float point_pos2[FS2_SINK_MAX_POINT][3];
	float point_rad1_sq[FS2_SINK_MAX_POINT], point_rad2_sq[FS2_SINK_MAX_POINT];
	float point_intensity[FS2_SINK_MAX_POINT];
	float point_rgb[FS2_SINK_MAX_POINT][3];
} fs2_sink_mesh_light;

typedef struct fs2_sink_mesh_group {
	int32_t texture;	/* texture id as texture_create(); 0 = none */
	int32_t blend;		/* FS2_SINK_BLEND_* */
	uint32_t flags;		/* FS2_SINK_DRAWF_* (ZREAD, ZWRITE, CULL, OVERLAY) */
	int32_t zbias;
	int32_t first_index, index_count;	/* into the mesh's index array; a multiple of 3 */
} fs2_sink_mesh_group;

/* texture formats handed to texture_create() */
#define FS2_SINK_TEX_RGBA8	0	/* 4 bytes per pixel, r g b a in memory order */
#define FS2_SINK_TEX_A8		1	/* 1 byte per pixel, coverage only (AABITMAP, font atlas): drawn as tint colour with this alpha */

/* texture flags */
#define FS2_SINK_TEXF_REPEAT	1	/* wrap u and v (TILED polys); otherwise clamp */
#define FS2_SINK_TEXF_LINEAR	2	/* bilinear; otherwise nearest (UI text keeps integer texels) */

/* blend modes of a draw */
#define FS2_SINK_BLEND_OPAQUE	0	/* replace */
#define FS2_SINK_BLEND_ALPHA	1	/* src*a + dst*(1-a) */
#define FS2_SINK_BLEND_ADD	2	/* src + dst, alpha ignored: D3D's ONE:ONE for GR_ALPHABLEND_FILTER (R1-9) */

/* draw flags */
#define FS2_SINK_DRAWF_PERSPECTIVE	1	/* vertices carry a usable sw: divide u,v per pixel by the interpolated sw */
#define FS2_SINK_DRAWF_ZREAD		2	/* depth test against the run (inside a depth run only) */
#define FS2_SINK_DRAWF_ZWRITE		4	/* write depth (inside a depth run only) */
#define FS2_SINK_DRAWF_CULL		8	/* the game had back-face culling on; informational, the sink may ignore it */
#define FS2_SINK_DRAWF_OVERLAY	16	/* ship glow pass: the same vertices as a draw just made (or about to be drawn later in the run), z read only. A sink that
					   depth tests per batch moves the draw nearer by a hair, so the equal depth passes whatever the batch order; with fog colour looked
					   up from marked polys (fog_rgb[3] == 1) it must not make this draw the poly later unmarked draws take their colour from.
					   New bit: no other bit moved. Only gr_host (Shivan ship glow) sets it; a sink, the dummy and the headless sink may ignore it */

/* D3D7 puts pixel centres at integer coordinates, Godot at +0.5: the host moves every tmapper vertex by this (#133). A sink that
   maps a vertex back to the game's pixel (the fog cell of a marked poly) takes it off first. Why and what it measured: gr_host.cpp
   tmapper_internal */
#define FS2_SINK_D3D_PIXEL_CENTRE	0.5f

/* fog modes (R2) */
#define FS2_SINK_FOG_NONE	0
#define FS2_SINK_FOG_LINEAR	1

/* texture ids chosen by the host; 0 means "no texture" */
typedef struct fs2_sink_vertex {
	float x, y;		/* screen pixels */
	float sw;		/* 1/w of the projected vertex (D3D rhw); only meaningful with FS2_SINK_DRAWF_PERSPECTIVE */
	float u, v;		/* texel position in 0..1 of the texture (not premultiplied by sw) */
	uint8_t r, g, b, a;	/* vertex colour, straight alpha, 0..255 */
	float z;		/* v2: depth 0 (near) .. 1 (far), the D3D sz; only meaningful inside a depth run */
} fs2_sink_vertex;

typedef struct fs2_sink_draw {
	int32_t texture;	/* 0 = untextured */
	int32_t blend;		/* FS2_SINK_BLEND_* */
	uint32_t flags;		/* FS2_SINK_DRAWF_* */
	uint8_t tint[4];	/* r g b a multiplier on vertex colour (255 = none) */
	/* v2 */
	int32_t zbias;		/* gr_zbias of the call: larger is drawn nearer at equal z (decals) */
	int32_t fog_mode;	/* FS2_SINK_FOG_*, set by the host; a sink that does not implement fog ignores it */
	uint8_t fog_rgb[4];	/* fog colour r g b; [3] (D-20261030-052453): 1 = a tmapper poly that set this colour itself from the pixels behind
				   its vertices (TMAP_FLAG_PIXEL_FOG), 0 = a tmapper poly that did not (a fog-on poly keeps the last set colour, as D3D),
				   255 = any other draw call; a sink that does not implement fog ignores it */
	float fog_near, fog_far;	/* fog distances in world units; vertex z is depth / 30000 (capped 0.98), so depth = z * 30000 */
} fs2_sink_draw;

typedef struct fs2_sink_mesh_fog {
	int32_t mode;		/* FS2_SINK_FOG_* */
	uint8_t rgb[4];		/* fog colour r g b (a unused) */
	float fog_near, fog_far;	/* world units; depth = vertex z * 30000 with z capped at 0.98, as fs2_sink_draw */
} fs2_sink_mesh_fog;

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

	/* pixels are tightly packed (w*bytes_per_pixel per row) and copied by the sink before the call returns.
	 * texture_create and mesh_create are also legal outside a frame (between frames, at level load), so fixed data is uploaded once
	 * at load and never at a draw; every other call is inside a frame only. */
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

	/* v2: depth runs (see top). Runs do not nest; depth_end without a run, or depth_clear outside one, is ignored.
	 * frame_end closes an open run. A run holds a fresh, cleared depth buffer. */
	void (*depth_begin)(void *user);
	void (*depth_end)(void *user);
	void (*depth_clear)(void *user, int x, int y, int w, int h);

	/* v3: the camera of the draws that follow, sent before the first depth draw of a frame and whenever it changes. Inside a frame only. */
	void (*set_view)(void *user, const fs2_sink_view *view);

	/* v4: meshes (optional, see above). mesh_draw: xform = model to eye, 3x4 row-major (rotation rows then translation in column 4), in the same
	 * units and handedness as the game's Eye_matrix space; colours = one RGBA8 per vertex (ncolors == the mesh's vertex count) or NULL for
	 * white; only inside a depth run. mesh_free of an unknown id is ignored. */
	void (*mesh_create)(void *user, int32_t id, const fs2_sink_mesh_vertex *verts, int nverts,
		const fs2_sink_mesh_group *groups, int ngroups, const uint32_t *indices, int nindices);
	void (*mesh_free)(void *user, int32_t id);
	void (*mesh_draw)(void *user, int32_t id, const float xform[12], const uint8_t *colours, int ncolours);
	/* v4, optional: the draw state of a mesh (per group: texture, blend, flags, zbias; first_index and index_count are ignored) for the mesh_draw
	 * calls that follow, without touching its geometry, which mesh_create makes once. A sink without it has the host recreate the mesh. */
	void (*mesh_state)(void *user, int32_t id, const fs2_sink_mesh_group *groups, int ngroups);
	/* v4, optional: mesh_draw with the game's lighting done by the sink per vertex (from the mesh's vertex positions and normals) instead of
	 * host-made colours. Same rules as mesh_draw otherwise. A sink without it gets mesh_draw with host-made colours. */
	void (*mesh_draw_lit)(void *user, int32_t id, const float xform[12], const fs2_sink_mesh_light *light);
	/* v4, optional: the user clip plane for the mesh draws that follow (the game's g3_start_user_clip_plane): plane = nx, ny, nz, w in eye space; a point
	 * is kept where nx*x + ny*y + nz*z + w > 0. NULL = no clip. A sink without it makes the host draw a clipped model on the CPU path. */
	void (*mesh_clip)(void *user, const float plane[4]);

	/* v4, optional: animation frames as the layers of one array texture (P8). Allowed in or out of a frame (mission load).
	 * texture_array_create: pixels = nlayers * w * h * bytes per pixel, layer after layer, tightly packed, copied before the call returns; format and
	 * texflags as texture_create. texture_layer: the texture id `id` (unused) now names layer `layer` of the array, so draws and mesh groups that name
	 * `id` sample that layer and nothing else about a draw changes (order, blend, state). A sink may batch draws of one array across layers.
	 * texture_free(id) drops one such id; texture_array_free drops the array and every id naming it; texture_free_all drops both. A sink without these
	 * entries gets one texture_create per frame. */
	void (*texture_array_create)(void *user, int32_t array_id, int w, int h, int nlayers, int format, uint32_t texflags, const uint8_t *pixels);
	void (*texture_layer)(void *user, int32_t id, int32_t array_id, int layer);
	void (*texture_array_free)(void *user, int32_t array_id);

	/* v4, optional: the fog of the mesh draws that follow, as fs2_sink_draw carries it for polys: linear in the eye depth, per vertex. Where the sink
	 * takes the fog colour from the pixels behind the model (the nebula pre-render, as TMAP_FLAG_PIXEL_FOG does on the CPU) it queries it for each
	 * vertex at its projected screen position and ignores rgb. NULL or mode NONE = no fog. A sink without it makes the host draw a fogged model on
	 * the CPU path; so does a sink that returns 0 here (it cannot take this fog), until the next call. Returns 1 when it takes it. */
	int (*mesh_fog)(void *user, const fs2_sink_mesh_fog *fog);

	/* v4, optional: an engine thruster flame (the game's thruster submodel): for the mesh draws that follow, every vertex with z < t[1] (model space)
	 * has its z multiplied by t[0] before the transform (the flame's length). NULL = no scaling. A sink without it makes the host draw thrusters on
	 * the CPU path. */
	void (*mesh_thrust)(void *user, const float t[2]);
} fs2_render_sink;

#ifdef __cplusplus
}
#endif

#endif
