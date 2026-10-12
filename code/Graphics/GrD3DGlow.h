/*
 * GrD3DGlow.h
 *
 * Ship glow for the Direct3D renderer: the D3D version of the Glide-only
 * "nondarkening" pixels. Chosen texture colours on ship models (the lights on
 * Shivan hulls) are drawn at full brightness whatever the lighting.
 *
 * How it works, and how to port it: docs/SHIVAN_GLOW.md.
 *
 * Render-only. Nothing here reads or writes gameplay state.
 */

#ifndef _GRD3DGLOW_H
#define _GRD3DGLOW_H

#include "GrD3DInternal.h"

// 1 = draw ship glow (default). The debug console command "ship_glow" toggles
// it. The -noshipglow command-line option (Cmdline_no_ship_glow) turns glow
// off for the whole run and stops glow textures being built at all.
extern int D3D_ship_glow;

// The glow texture for a ship texture, or NULL if glow is off, the texture has
// no glowing pixels, or no glow texture was built for it. NULL means draw no
// glow pass. Never builds a texture while in a mission.
D3DTEXTUREHANDLE d3d_glow_get_texture(int bitmap_handle);

// Build glow textures for every loaded model. Called at the end of level load
// (bm_page_in_stop), so nothing is built during play.
void d3d_glow_level_load();

// Once per frame flip (d3d_tcache_frame): rebuilds glow textures after a flush.
void d3d_glow_frame();

// Release every glow texture. Called from d3d_tcache_flush(), so glow
// textures follow the normal texture cache's lifetime (level load, detail
// change, video mode change, alt-tab, shutdown). If the flush is because the
// texture cache ran out of VRAM, glow stays off until the next level.
void d3d_glow_flush();

#endif
