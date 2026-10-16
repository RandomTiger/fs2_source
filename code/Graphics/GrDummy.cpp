// Claude: the null renderer (-nographics), after fs2_source_ue's GR_DUMMY.  Every drawing call does nothing; the
// state the game reads back (font, bitmap, shader) is kept, and fonts keep their real metrics.

#include "2d.h"
#include "GrInternal.h"
#include "GrDummy.h"
#include "Font.h"

// ---------------------------------------------------------------------------
// Stub implementations: all rendering calls are no-ops in dummy mode.
// State updates that affect game logic are preserved.

static void gr_dummy_flip() { }
static void gr_dummy_flip_window(uint /*_hdc*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/) { }
static void gr_dummy_set_palette(ubyte * /*new_pal*/, int /*restrict_alphacolor*/) { }
static void gr_dummy_fade_in(int /*instantaneous*/) { }
static void gr_dummy_fade_out(int /*instantaneous*/) { }
static void gr_dummy_flash(int /*r*/, int /*g*/, int /*b*/) { }
static void gr_dummy_set_clip(int /*x*/, int /*y*/, int /*w*/, int /*h*/) { }
static void gr_dummy_reset_clip() { }
static void gr_dummy_set_color(int /*r*/, int /*g*/, int /*b*/) { }
static void gr_dummy_get_color(int * /*r*/, int * /*g*/, int * /*b*/) { }
static void gr_dummy_init_color(color * /*dst*/, int /*r*/, int /*g*/, int /*b*/) { }
static void gr_dummy_init_alphacolor(color * /*dst*/, int /*r*/, int /*g*/, int /*b*/, int /*alpha*/, int /*type*/) { }
static void gr_dummy_set_color_fast(color * /*dst*/) { }
// Font measurement must work in dummy mode so string-splitting logic
// (split_str_once etc.) computes real pixel widths.  Drawing is a no-op.
static void gr_dummy_set_font(int fontnum) { grx_set_font(fontnum); }

static void gr_dummy_set_bitmap(int bitmap_num, int alphablend_mode, int bitblt_mode, float alpha, int /*sx*/, int /*sy*/)
{
	gr_screen.current_bitmap = bitmap_num;
	gr_screen.current_alphablend_mode = alphablend_mode;
	gr_screen.current_bitblt_mode = bitblt_mode;
	gr_screen.current_alpha = alpha;
}

static void gr_dummy_create_shader(shader *shade, float r, float g, float b, float c)
{
	shade->screen_sig = gr_screen.signature;
	shade->r = r;
	shade->g = g;
	shade->b = b;
	shade->c = c;
}

static void gr_dummy_set_shader(shader *shade)
{
	if (shade) {
		if (shade->screen_sig != gr_screen.signature) {
			gr_create_shader(shade, shade->r, shade->g, shade->b, shade->c);
		}
		gr_screen.current_shader = *shade;
	} else {
		gr_create_shader(&gr_screen.current_shader, 0.0f, 0.0f, 0.0f, 0.0f);
	}
}

static void gr_dummy_clear() { }
static void gr_dummy_aabitmap(int /*x*/, int /*y*/) { }
static void gr_dummy_aabitmap_ex(int /*x*/, int /*y*/, int /*w*/, int /*h*/, int /*sx*/, int /*sy*/) { }
static void gr_dummy_rect(int /*x*/, int /*y*/, int /*w*/, int /*h*/) { }
static void gr_dummy_shade(int /*x*/, int /*y*/, int /*w*/, int /*h*/) { }
static void gr_dummy_string(int /*x*/, int /*y*/, char * /*text*/) { }
static void gr_dummy_gradient(int /*x1*/, int /*y1*/, int /*x2*/, int /*y2*/) { }
static void gr_dummy_circle(int /*x*/, int /*y*/, int /*r*/) { }
static void gr_dummy_line(int /*x1*/, int /*y1*/, int /*x2*/, int /*y2*/) { }
static void gr_dummy_aaline(vertex * /*v1*/, vertex * /*v2*/) { }
static void gr_dummy_pixel(int /*x*/, int /*y*/) { }
static void gr_dummy_scaler(vertex * /*va*/, vertex * /*vb*/) { }
static void gr_dummy_aascaler(vertex * /*va*/, vertex * /*vb*/) { }
static void gr_dummy_tmapper(int /*nv*/, vertex * /*verts*/[], uint /*flags*/) { }
static void gr_dummy_print_screen(char * /*filename*/) { }
static void gr_dummy_start_frame() { }
static void gr_dummy_stop_frame() { }

static int  gr_dummy_zbuffer_get() { return GR_ZBUFF_NONE; }
static int  gr_dummy_zbuffer_set(int /*mode*/) { return GR_ZBUFF_NONE; }
static void gr_dummy_zbuffer_clear(int /*use_zbuffer*/) { }

static int  gr_dummy_save_screen() { return 0; }
static void gr_dummy_restore_screen(int /*id*/) { }
static void gr_dummy_free_screen(int /*id*/) { }

static void gr_dummy_dump_frame_start(int /*first_frame_number*/, int /*nframes_between_dumps*/) { }
static void gr_dummy_dump_frame() { }
static void gr_dummy_dump_frame_stop() { }

static void gr_dummy_set_gamma(float /*gamma*/) { }

static uint gr_dummy_lock() { return 1; }
static void gr_dummy_unlock() { }

static void gr_dummy_get_region(int /*front*/, int /*w*/, int /*h*/, ubyte * /*data*/) { }
static void gr_dummy_fog_set(int /*fog_mode*/, int /*r*/, int /*g*/, int /*b*/, float /*fog_near*/, float /*fog_far*/) { }
static void gr_dummy_get_pixel(int /*x*/, int /*y*/, int * /*r*/, int * /*g*/, int * /*b*/) { }
static void gr_dummy_set_cull(int /*cull*/) { }
static void gr_dummy_cross_fade(int /*bmap1*/, int /*bmap2*/, int /*x1*/, int /*y1*/, int /*x2*/, int /*y2*/, float /*pct*/) { }
static void gr_dummy_filter_set(int /*filter*/) { }
static int  gr_dummy_tcache_set(int /*bitmap_id*/, int /*bitmap_type*/, float * /*u_scale*/, float * /*v_scale*/, int /*fail_on_full*/, int /*sx*/, int /*sy*/, int /*force*/) { return 0; }
static void gr_dummy_set_clear_color(int /*r*/, int /*g*/, int /*b*/) { }

// ---------------------------------------------------------------------------

void gr_dummy_init()
{
	mprintf(("Initializing DUMMY graphics device...\n"));

	// the screen format Direct3D sets up in 32 bit, which the reference configuration uses
	gr_screen.bits_per_pixel = 32;
	gr_screen.bytes_per_pixel = 4;

	gr_screen.gf_flip              = gr_dummy_flip;
	gr_screen.gf_flip_window       = gr_dummy_flip_window;
	gr_screen.gf_set_palette       = gr_dummy_set_palette;
	gr_screen.gf_fade_in           = gr_dummy_fade_in;
	gr_screen.gf_fade_out          = gr_dummy_fade_out;
	gr_screen.gf_flash             = gr_dummy_flash;
	gr_screen.gf_set_clip          = gr_dummy_set_clip;
	gr_screen.gf_reset_clip        = gr_dummy_reset_clip;
	gr_screen.gf_set_color         = gr_dummy_set_color;
	gr_screen.gf_get_color         = gr_dummy_get_color;
	gr_screen.gf_init_color        = gr_dummy_init_color;
	gr_screen.gf_init_alphacolor   = gr_dummy_init_alphacolor;
	gr_screen.gf_set_color_fast    = gr_dummy_set_color_fast;
	gr_screen.gf_set_font          = gr_dummy_set_font;
	gr_screen.gf_set_bitmap        = gr_dummy_set_bitmap;
	gr_screen.gf_create_shader     = gr_dummy_create_shader;
	gr_screen.gf_set_shader        = gr_dummy_set_shader;
	gr_screen.gf_clear             = gr_dummy_clear;
	gr_screen.gf_aabitmap          = gr_dummy_aabitmap;
	gr_screen.gf_aabitmap_ex       = gr_dummy_aabitmap_ex;
	gr_screen.gf_rect              = gr_dummy_rect;
	gr_screen.gf_shade             = gr_dummy_shade;
	gr_screen.gf_string            = gr_dummy_string;
	gr_screen.gf_gradient          = gr_dummy_gradient;
	gr_screen.gf_circle            = gr_dummy_circle;
	gr_screen.gf_line              = gr_dummy_line;
	gr_screen.gf_aaline            = gr_dummy_aaline;
	gr_screen.gf_pixel             = gr_dummy_pixel;
	gr_screen.gf_scaler            = gr_dummy_scaler;
	gr_screen.gf_aascaler          = gr_dummy_aascaler;
	gr_screen.gf_tmapper           = gr_dummy_tmapper;
	gr_screen.gf_print_screen      = gr_dummy_print_screen;
	gr_screen.gf_start_frame       = gr_dummy_start_frame;
	gr_screen.gf_stop_frame        = gr_dummy_stop_frame;
	gr_screen.gf_zbuffer_get       = gr_dummy_zbuffer_get;
	gr_screen.gf_zbuffer_set       = gr_dummy_zbuffer_set;
	gr_screen.gf_zbuffer_clear     = gr_dummy_zbuffer_clear;
	gr_screen.gf_save_screen       = gr_dummy_save_screen;
	gr_screen.gf_restore_screen    = gr_dummy_restore_screen;
	gr_screen.gf_free_screen       = gr_dummy_free_screen;
	gr_screen.gf_dump_frame_start  = gr_dummy_dump_frame_start;
	gr_screen.gf_dump_frame        = gr_dummy_dump_frame;
	gr_screen.gf_dump_frame_stop   = gr_dummy_dump_frame_stop;
	gr_screen.gf_set_gamma         = gr_dummy_set_gamma;
	gr_screen.gf_lock              = gr_dummy_lock;
	gr_screen.gf_unlock            = gr_dummy_unlock;
	gr_screen.gf_get_region        = gr_dummy_get_region;
	gr_screen.gf_fog_set           = gr_dummy_fog_set;
	gr_screen.gf_get_pixel         = gr_dummy_get_pixel;
	gr_screen.gf_set_cull          = gr_dummy_set_cull;
	gr_screen.gf_cross_fade        = gr_dummy_cross_fade;
	gr_screen.gf_filter_set        = gr_dummy_filter_set;
	gr_screen.gf_tcache_set        = gr_dummy_tcache_set;
	gr_screen.gf_set_clear_color   = gr_dummy_set_clear_color;
}

void gr_dummy_cleanup()
{
}
