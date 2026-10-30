/*
 * GrD3DGlow.cpp
 *
 * Ship glow for the Direct3D renderer: the D3D version of the Glide-only
 * "nondarkening" pixels. See GrD3DGlow.h and docs/SHIVAN_GLOW.md.
 *
 * Under Glide a ship texture is loaded with an alpha mask (alpha set where the
 * pixel's palette colour is in the nondarkening list), and the Voodoo lights
 * masked pixels with constant white instead of the vertex colour.
 *
 * The D3D device here is IDirect3DDevice2 (DirectX 5): one texture, no texture
 * stages, so lit and unlit cannot be mixed in one pass. Instead:
 *   pass 1  the normal lit draw, unchanged;
 *   pass 2  the same polygon again, with a separate "glow texture" and white
 *           vertex colour, alpha blended over pass 1 with z read only.
 * The glow texture holds the ship texture's colours, with alpha set only where
 * the pixel glows, so the result is lerp(texture * light, texture, mask). With
 * bilinear filtering this matches Glide at the mask edges too.
 *
 * The normal texture cache and bitmap manager are not touched: the base
 * textures already use their alpha bit for transparency, so the mask lives in
 * its own texture, built here from the PCX file and cached by bitmap handle.
 *
 * When glow textures are built (never during play):
 *   - at the end of level load, for every texture of every loaded model
 *     (d3d_glow_level_load, called from bm_page_in_stop);
 *   - after a texture cache flush (detail or gamma change, alt-tab), on the
 *     next frame flip (d3d_glow_frame, called from d3d_tcache_frame);
 *   - outside a mission (tech room, briefing), on first use.
 * In a mission a texture with no glow texture is drawn without glow.
 *
 * Render-only. Nothing here reads or writes gameplay state, the random number
 * generator or timing.
 */

#include "GrD3DGlow.h"
#include "2d.H"
#include "PsTypes.h"
#include "BmpMan.h"
#include "PalMan.h"
#include "pcxutils.h"
#include "Ship.h"
#include "Model.h"
#include "SystemVars.h"
#include "cmdline.h"
#include "MissionParse.h"

int D3D_ship_glow = 1;
DCF_BOOL( ship_glow, D3D_ship_glow )

extern int D3D_texture_divider;
extern int vram_full;
extern polymodel *Polygon_models[MAX_POLYGON_MODELS];
extern int bm_get_cache_slot( int bitmap_id, int separate_ani_frames );
void d3d_tcache_get_adjusted_texture_size(int w_in, int h_in, int *w_out, int *h_out);

// 1 = only textures used by Shivan ship classes glow. 0 = every model texture,
// as under Glide (Terran and Vasudan hulls, weapon models, ...).
#define GLOW_SHIVAN_ONLY	1

// Forced red (on by default, -noforceglow turns it off): on Shivan ships, palette
// colours with red >= GLOW_FORCE_MIN_RED and green, blue <= GLOW_FORCE_MAX_GB glow
// as well as the table colours. Not in the original game.
// TODO: SC Rakshasa (and SFr Dis) have only dark reds (red 90-150), so this
// barely lights them. Undecided: see "Open issues" in docs/SHIVAN_GLOW.md.
#define GLOW_FORCE_MIN_RED	170
#define GLOW_FORCE_MAX_GB	10

#define GLOW_UNKNOWN	0		// not built yet
#define GLOW_NONE		1		// built: no glowing pixels, or the build failed
#define GLOW_READY	2		// built: texture_handle is valid

typedef struct glow_slot {
	int							bitmap_id;		// bitmap handle this slot was built for
	int							state;			// GLOW_*
	int							size;				// bytes of VRAM, when GLOW_READY
	LPDIRECTDRAWSURFACE		surface;
	LPDIRECT3DTEXTURE2		texture;
	D3DTEXTUREHANDLE			texture_handle;
} glow_slot;

// indexed like the texture cache, by bm_get_cache_slot()
static glow_slot Glow_slots[MAX_BITMAPS];
static int Glow_slots_inited = 0;

static int Glow_bytes = 0;						// VRAM used by glow textures
static int Glow_rebuild_pending = 0;		// glow textures were flushed: rebuild on the next flip
static int Glow_vram_disabled = 0;			// the texture cache ran out of VRAM: no glow until the next level

static void d3d_glow_free_slot(glow_slot *g)
{
	if ( g->texture )	{
		g->texture->Release();
		g->texture = NULL;
	}
	if ( g->surface )	{
		g->surface->Release();
		g->surface = NULL;
	}
	if ( g->state == GLOW_READY )	{
		Glow_bytes -= g->size;
	}
	g->size = 0;
	g->texture_handle = NULL;
	g->bitmap_id = -1;
	g->state = GLOW_UNKNOWN;
}

static void d3d_glow_init_slots()
{
	int i;

	for ( i=0; i<MAX_BITMAPS; i++ )	{
		memset(&Glow_slots[i], 0, sizeof(glow_slot));
		Glow_slots[i].bitmap_id = -1;
	}
	Glow_bytes = 0;
	Glow_slots_inited = 1;
}

static void d3d_glow_queue_clear();

void d3d_glow_flush()
{
	int i;

	d3d_glow_queue_clear();		// queued polys use the glow textures freed below

	if ( !Glow_slots_inited )	{
		d3d_glow_init_slots();
		return;
	}

	for ( i=0; i<MAX_BITMAPS; i++ )	{
		d3d_glow_free_slot(&Glow_slots[i]);
	}
	Glow_bytes = 0;

	// The texture cache flushes itself when it runs out of VRAM. Give the VRAM
	// back to it for the rest of this level rather than rebuilding.
	if ( vram_full )	{
		mprintf(( "Ship glow: texture VRAM full, glow off until the next level\n" ));
		Glow_vram_disabled = 1;
	}
	Glow_rebuild_pending = 1;
}

// The Shivan ship class whose model uses this texture, or -1. Only Shivan ship
// textures glow (GLOW_SHIVAN_ONLY): Glide also lit Terran and Vasudan hull
// lights, weapon models and so on; see docs/SHIVAN_GLOW.md for the list.
// Matches loaded models to ship classes by POF file name, so it works where
// Ship_info[].modelnum is not set (tech room) as well as in missions.
static int d3d_glow_get_shivan_class(int bitmap_handle)
{
	int i, j, si;

	for ( i=0; i<MAX_POLYGON_MODELS; i++ )	{
		polymodel *pm = Polygon_models[i];
		if ( pm == NULL )	{
			continue;
		}
		for ( j=0; j<pm->n_textures; j++ )	{
			if ( (pm->original_textures[j] == bitmap_handle) || (pm->textures[j] == bitmap_handle) )	{
				break;
			}
		}
		if ( j == pm->n_textures )	{
			continue;
		}
		for ( si=0; si<Num_ship_types; si++ )	{
			if ( (Ship_info[si].species == SPECIES_SHIVAN) && !stricmp(Ship_info[si].pof_file, pm->filename) )	{
				return si;
			}
		}
	}
	return -1;
}

// The nondarkening colour list for a texture, or 0 if the texture must not
// glow. The ship class's own list ($ND: in ships.tbl), else the default list
// (+ND in pixels.tbl), the same lists Glide uses (BMPMAN_SPECIAL_NONDARK in
// bm_page_in_stop). Read directly: palman's global current list is never changed.
static int d3d_glow_get_colors(int bitmap_handle, ubyte (**colors)[3], int *count)
{
	int ship_info_index;

	if ( GLOW_SHIVAN_ONLY )	{
		ship_info_index = d3d_glow_get_shivan_class(bitmap_handle);
		if ( ship_info_index < 0 )	{
			return 0;
		}
	} else {
		ship_info_index = ship_get_texture(bitmap_handle);
	}

	if ( (ship_info_index >= 0) && (Ship_info[ship_info_index].num_nondark_colors > 0) )	{
		*colors = Ship_info[ship_info_index].nondark_colors;
		*count = Ship_info[ship_info_index].num_nondark_colors;
	} else {
		*colors = Palman_non_darkening_default;
		*count = Palman_num_nondarkening_default;
	}
	return 1;
}

static int d3d_glow_is_xparent(ubyte *rgb)
{
	// the transparent colour in pcx_read_bitmap_16bpp
	return (rgb[0] == 0) && (rgb[1] == 255) && (rgb[2] == 0);
}

// Build the glow texture for one bitmap into g. Leaves g->state GLOW_READY or GLOW_NONE.
static void d3d_glow_build(int bitmap_handle, glow_slot *g)
{
	int i, j, c;
	int bmap_w, bmap_h, bm_w, bm_h, tex_w, tex_h, size;
	ubyte palette[768];
	ubyte glows[256];
	int num_glowing = 0;
	ubyte *pixels = NULL;
	ubyte (*colors)[3];
	int num_colors;
	LPDIRECTDRAWSURFACE sys_surface = NULL;
	LPDIRECT3DTEXTURE2 sys_texture = NULL;
	DDSURFACEDESC ddsd;
	HRESULT ddrval;

	g->bitmap_id = bitmap_handle;
	g->state = GLOW_NONE;
	g->size = 0;

	// texture format: needs an alpha channel. Prefer the normal texture format
	// (1555 when the card has it) so colours match pass 1; else the alpha format.
	DDPIXELFORMAT *format;
	color_gun *cr, *cg, *cb, *ca;
	if ( NonAlphaTextureFormat.dwFlags & DDPF_ALPHAPIXELS )	{
		format = &NonAlphaTextureFormat;
		cr = &Gr_t_red;  cg = &Gr_t_green;  cb = &Gr_t_blue;  ca = &Gr_t_alpha;
	} else if ( AlphaTextureFormat.dwFlags & DDPF_ALPHAPIXELS )	{
		format = &AlphaTextureFormat;
		cr = &Gr_ta_red;  cg = &Gr_ta_green;  cb = &Gr_ta_blue;  ca = &Gr_ta_alpha;
	} else {
		return;
	}
	if ( format->dwRGBBitCount != 16 )	{
		return;
	}

	// which colours glow, if this texture may glow at all (cheap: no file read)
	if ( !d3d_glow_get_colors(bitmap_handle, &colors, &num_colors) || (num_colors < 1) )	{
		return;
	}

	char *filename = bm_get_filename(bitmap_handle);
	if ( (filename == NULL) || (filename[0] == 0) )	{
		return;
	}

	// Only 8-bit PCX textures carry palette indices to match against. The PCX
	// must be the same size as the bitmap: this skips ANI frames, whose names
	// would otherwise resolve to an unrelated PCX.
	if ( pcx_read_header(filename, &bmap_w, &bmap_h, NULL) != PCX_ERROR_NONE )	{
		return;
	}
	bm_get_info(bitmap_handle, &bm_w, &bm_h);
	if ( (bmap_w < 1) || (bmap_h < 1) || (bmap_w != bm_w) || (bmap_h != bm_h) )	{
		return;
	}

	// which palette entries glow. Exact RGB match, as palman_is_nondarkening()
	// does. The transparent colour never glows: pass 1 does not draw those pixels.
	pixels = (ubyte*)malloc(bmap_w * bmap_h);
	if ( pixels == NULL )	{
		return;
	}
	if ( pcx_read_bitmap_8bpp(filename, pixels, palette) != PCX_ERROR_NONE )	{
		free(pixels);
		return;
	}

	for ( i=0; i<256; i++ )	{
		glows[i] = 0;
		if ( d3d_glow_is_xparent(&palette[i*3]) )	{
			continue;
		}
		for ( c=0; c<num_colors; c++ )	{
			if ( (palette[i*3] == colors[c][0]) && (palette[i*3+1] == colors[c][1]) && (palette[i*3+2] == colors[c][2]) )	{
				glows[i] = 1;
				break;
			}
		}
		// Forced red: many Shivan ships were never given exact glow colours, but
		// their lights are strong, nearly pure reds. Treat those as glowing too.
		if ( !glows[i] && !Cmdline_no_force_glow && (palette[i*3] >= GLOW_FORCE_MIN_RED) && (palette[i*3+1] <= GLOW_FORCE_MAX_GB) && (palette[i*3+2] <= GLOW_FORCE_MAX_GB) )	{
			glows[i] = 1;
		}
	}
	for ( i=0; i<bmap_w*bmap_h; i++ )	{
		if ( glows[pixels[i]] )	{
			num_glowing++;
		}
	}
	if ( num_glowing == 0 )	{
		free(pixels);
		return;
	}

	// same size the texture cache gives the base texture (d3d_create_texture)
	{
		int max_w = bmap_w / D3D_texture_divider;
		int max_h = bmap_h / D3D_texture_divider;
		max_w /= 16 >> Detail.hardware_textures;
		max_h /= 16 >> Detail.hardware_textures;
		d3d_tcache_get_adjusted_texture_size(max_w, max_h, &tex_w, &tex_h);
	}
	if ( (tex_w < 1) || (tex_h < 1) )	{
		free(pixels);
		return;
	}

	size = tex_w * tex_h * 2;

	// fill a system memory surface
	memset(&ddsd, 0, sizeof(DDSURFACEDESC));
	ddsd.dwSize = sizeof(DDSURFACEDESC);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
	ddsd.dwHeight = tex_h;
	ddsd.dwWidth = tex_w;
	ddsd.ddpfPixelFormat = *format;
	ddrval = lpDD->CreateSurface(&ddsd, &sys_surface, NULL);
	if ( (ddrval != DD_OK) || (sys_surface == NULL) )	{
		mprintf(( "Ship glow: CreateSurface (system) failed for '%s'\n", filename ));
		free(pixels);
		return;
	}

	memset(&ddsd, 0, sizeof(DDSURFACEDESC));
	ddsd.dwSize = sizeof(DDSURFACEDESC);
	ddrval = sys_surface->Lock(NULL, &ddsd, 0, NULL);
	if ( ddrval != DD_OK )	{
		sys_surface->Release();
		free(pixels);
		return;
	}

	{
		// same nearest-texel scaling as d3d_create_texture_sub, so glow texels
		// line up with the base texture's
		fix u, utmp, v, du, dv;
		ushort *dst;
		ubyte idx;
		ushort pixel;

		u = v = 0;
		du = ( (bmap_w-1)*F1_0 ) / tex_w;
		dv = ( (bmap_h-1)*F1_0 ) / tex_h;

		for ( j=0; j<tex_h; j++ )	{
			dst = (ushort*)(((char*)ddsd.lpSurface) + ddsd.lPitch * j);
			utmp = u;
			for ( i=0; i<tex_w; i++ )	{
				idx = pixels[f2i(v)*bmap_w + f2i(utmp)];
				if ( d3d_glow_is_xparent(&palette[idx*3]) )	{
					pixel = 0;
				} else {
					// Every texel keeps its real colour; only glowing ones get alpha.
					// Filtering then blends colours exactly as the base texture does,
					// so mask edges come out as Glide's did, with no dark fringe.
					pixel = (ushort)( ((palette[idx*3] / cr->scale) << cr->shift)
										 | ((palette[idx*3+1] / cg->scale) << cg->shift)
										 | ((palette[idx*3+2] / cb->scale) << cb->shift) );
					if ( glows[idx] )	{
						pixel |= (ushort)ca->mask;
					}
				}
				*dst++ = pixel;
				utmp += du;
			}
			v += dv;
		}
	}

	sys_surface->Unlock(NULL);
	free(pixels);
	pixels = NULL;

	ddrval = sys_surface->QueryInterface(IID_IDirect3DTexture2, (LPVOID *)&sys_texture);
	if ( (ddrval != DD_OK) || (sys_texture == NULL) )	{
		goto Fail;
	}

	// upload to a video memory texture
	memset(&ddsd, 0, sizeof(DDSURFACEDESC));
	ddsd.dwSize = sizeof(DDSURFACEDESC);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_ALLOCONLOAD | DDSCAPS_VIDEOMEMORY;
	ddsd.dwHeight = tex_h;
	ddsd.dwWidth = tex_w;
	ddsd.ddpfPixelFormat = *format;
	ddrval = lpDD->CreateSurface(&ddsd, &g->surface, NULL);
	if ( (ddrval != DD_OK) || (g->surface == NULL) )	{
		// out of VRAM: no glow for this texture rather than evicting real textures
		mprintf(( "Ship glow: no VRAM for '%s' (%dx%d)\n", filename, tex_w, tex_h ));
		g->surface = NULL;
		goto Fail;
	}
	ddrval = g->surface->QueryInterface(IID_IDirect3DTexture2, (LPVOID *)&g->texture);
	if ( (ddrval != DD_OK) || (g->texture == NULL) )	{
		g->texture = NULL;
		goto Fail;
	}
	ddrval = g->texture->GetHandle(lpD3DDevice, &g->texture_handle);
	if ( (ddrval != DD_OK) || (g->texture_handle == NULL) )	{
		goto Fail;
	}
	ddrval = g->texture->Load(sys_texture);
	if ( ddrval != DD_OK )	{
		goto Fail;
	}

	sys_texture->Release();
	sys_surface->Release();
	g->state = GLOW_READY;
	g->size = size;
	Glow_bytes += size;
	return;

Fail:
	if ( sys_texture )	{
		sys_texture->Release();
	}
	sys_surface->Release();
	d3d_glow_free_slot(g);
	g->bitmap_id = bitmap_handle;
	g->state = GLOW_NONE;		// don't retry until the next flush
}

// The slot for a bitmap, emptied if it was built for an older bitmap whose
// bmpman slot has since been reused.
static glow_slot *d3d_glow_get_slot(int bitmap_handle)
{
	if ( !Glow_slots_inited )	{
		d3d_glow_init_slots();
	}

	glow_slot *g = &Glow_slots[bm_get_cache_slot(bitmap_handle, 1)];

	if ( (g->state != GLOW_UNKNOWN) && (g->bitmap_id != bitmap_handle) )	{
		d3d_glow_free_slot(g);
	}
	return g;
}

// Build glow textures for every texture of every loaded model that has none yet.
static void d3d_glow_build_all()
{
	int i, j, k;

	Glow_rebuild_pending = 0;

	if ( (gr_screen.mode != GR_DIRECT3D) || Cmdline_no_ship_glow || Glow_vram_disabled )	{
		return;
	}

	for ( i=0; i<MAX_POLYGON_MODELS; i++ )	{
		polymodel *pm = Polygon_models[i];
		if ( pm == NULL )	{
			continue;
		}
		for ( j=0; j<pm->n_textures; j++ )	{
			// original_textures and textures are normally the same; do both
			int handles[2];
			handles[0] = pm->original_textures[j];
			handles[1] = pm->textures[j];
			for ( k=0; k<2; k++ )	{
				if ( handles[k] < 0 )	{
					continue;
				}
				glow_slot *g = d3d_glow_get_slot(handles[k]);
				if ( g->state == GLOW_UNKNOWN )	{
					d3d_glow_build(handles[k], g);
				}
			}
		}
	}
	mprintf(( "Ship glow: %d KB of glow textures\n", Glow_bytes / 1024 ));
}

void d3d_glow_level_load()
{
	if ( gr_screen.mode != GR_DIRECT3D )	{
		return;
	}
	Glow_vram_disabled = 0;
	d3d_glow_build_all();
}

void d3d_glow_frame()
{
	if ( Glow_rebuild_pending )	{
		d3d_glow_build_all();
	}
}

D3DTEXTUREHANDLE d3d_glow_get_texture(int bitmap_handle)
{
	if ( !D3D_ship_glow || Cmdline_no_ship_glow || Glow_vram_disabled )	{
		return NULL;
	}
	if ( bitmap_handle < 0 )	{
		return NULL;
	}

	glow_slot *g = d3d_glow_get_slot(bitmap_handle);

	// Never build during play: in a mission, glow textures come from level load
	// or the rebuild after a flush. Outside one (tech room, briefing) build now.
	if ( (g->state == GLOW_UNKNOWN) && !(Game_mode & GM_IN_MISSION) )	{
		d3d_glow_build(bitmap_handle, g);
	}

	if ( g->state == GLOW_READY )	{
		return g->texture_handle;
	}
	return NULL;
}

// ------------------------------------------------------------------------------
// Batching. Drawing each glow polygon straight after its lit pass switched the
// texture and render state twice per polygon, which cost more than the glow
// draws themselves. The glow pass only reads the z buffer, so it can be drawn
// after the polygons that follow it, as long as it is drawn before anything that
// is not a ship polygon (see d3d_glow_draw_queued).

#define GLOW_QUEUE_POLYS	2048
#define GLOW_QUEUE_VERTS	(GLOW_QUEUE_POLYS * 8)
#define GLOW_BATCH_VERTS	3072					// triangle list vertices per draw call, a multiple of 3

typedef struct glow_poly {
	D3DTEXTUREHANDLE	handle;
	int					zbuffer_type;
	int					first;						// into Glow_queue_verts
	int					count;
} glow_poly;

static glow_poly Glow_queue_polys[GLOW_QUEUE_POLYS];
static D3DTLVERTEX Glow_queue_verts[GLOW_QUEUE_VERTS];
static D3DTLVERTEX Glow_batch_verts[GLOW_BATCH_VERTS];
static int Glow_queue_num_polys = 0;
static int Glow_queue_num_verts = 0;
int D3D_glow_hold = 0;

extern int DrawPrim;
void gr_d3d_set_glow_state(int zbuffer_type);

static void d3d_glow_queue_clear()
{
	Glow_queue_num_polys = 0;
	Glow_queue_num_verts = 0;
}

void d3d_glow_queue_poly(D3DTEXTUREHANDLE glow_handle, int zbuffer_type, D3DTLVERTEX *verts, int nverts)
{
	int i;

	if ( nverts < 3 )	{
		return;
	}

	// full: draw what is queued now (the caller's lit pass is already drawn)
	if ( (Glow_queue_num_polys >= GLOW_QUEUE_POLYS) || (Glow_queue_num_verts + nverts > GLOW_QUEUE_VERTS) )	{
		int hold = D3D_glow_hold;
		D3D_glow_hold = 0;
		d3d_glow_draw_queued();
		D3D_glow_hold = hold;
	}

	glow_poly *p = &Glow_queue_polys[Glow_queue_num_polys++];
	p->handle = glow_handle;
	p->zbuffer_type = zbuffer_type;
	p->first = Glow_queue_num_verts;
	p->count = nverts;

	D3DTLVERTEX *dst = &Glow_queue_verts[Glow_queue_num_verts];
	for ( i=0; i<nverts; i++ )	{
		dst[i] = verts[i];
		dst[i].color = RGBA_MAKE(255, 255, 255, RGBA_GETALPHA(verts[i].color));
	}
	Glow_queue_num_verts += nverts;
}

void d3d_glow_draw_queued()
{
	int i, k, nb;

	if ( D3D_glow_hold || (Glow_queue_num_polys == 0) )	{
		return;
	}
	D3D_glow_hold = 1;		// the state and texture calls below must not flush again

	i = 0;
	while ( i < Glow_queue_num_polys )	{
		// one state change for each run of polygons with the same glow texture and z buffer mode
		glow_poly *first = &Glow_queue_polys[i];
		gr_d3d_set_glow_state( first->zbuffer_type );
		d3d_SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, first->handle );

		nb = 0;
		for ( ; i < Glow_queue_num_polys; i++ )	{
			glow_poly *p = &Glow_queue_polys[i];
			if ( (p->handle != first->handle) || (p->zbuffer_type != first->zbuffer_type) )	{
				break;
			}
			D3DTLVERTEX *v = &Glow_queue_verts[p->first];

			// execute buffers only take fans
			if ( !DrawPrim )	{
				d3d_DrawPrimitive(D3DPT_TRIANGLEFAN, D3DVT_TLVERTEX, (LPVOID)v, p->count, NULL);
				continue;
			}

			// fan to triangle list, same winding
			if ( nb + 3 * (p->count - 2) > GLOW_BATCH_VERTS )	{
				d3d_DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, (LPVOID)Glow_batch_verts, nb, NULL);
				nb = 0;
			}
			for ( k=1; k<p->count-1; k++ )	{
				Glow_batch_verts[nb++] = v[0];
				Glow_batch_verts[nb++] = v[k];
				Glow_batch_verts[nb++] = v[k+1];
			}
		}
		if ( nb > 0 )	{
			d3d_DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, (LPVOID)Glow_batch_verts, nb, NULL);
		}
	}
	d3d_glow_queue_clear();

	// the base texture is no longer bound, so make the texture cache rebind it next time
	gr_tcache_set(-1, -1, NULL, NULL );

	D3D_glow_hold = 0;
}
