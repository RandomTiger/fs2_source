/*
 * Cutscene playback: see Movie.h.
 *
 * movie_play() tries the .mve, which always fails because the MVE decoder was not part of
 * the source release, and then <name>.ogv.  The .ogv is found like any other game file, so it
 * can sit in Data\Movies, in the game folder or in a .vp.  It is an Ogg file with a Theora
 * video stream and optionally a Vorbis sound stream, decoded with libtheora and libvorbis
 * (lib\xiph).
 *
 * The picture is scaled to fit the screen, keeping its shape, and drawn as one screen-sized
 * 16-bit bitmap through gr_bitmap(), so it does not depend on the renderer.  The sound goes
 * to its own DirectSound buffer and the picture follows the sound's play position.  Any key
 * or a mouse click stops the movie.
 *
 * Movies never play while a recording is replayed, under -autotest, or while a mission's
 * stream is being recorded, so the recorder never sees one.  The player keeps its own clock
 * (QueryPerformanceCounter) and does not call the hooked timer functions.
 */

#include <windows.h>
#include <string.h>
#include <stdlib.h>

#include "vdsound.h"
#include "Movie.h"
#include "cfile.h"
#include "BmpMan.h"
#include "2d.h"
#include "GrInternal.h"
#include "ds.h"
#include "AudioStr.h"
#include "Key.h"
#include "Mouse.h"
#include "OsApi.h"
#include "Replay.h"
#include "Autotest.h"

#include <theora/theoradec.h>
#include <vorbis/codec.h>


#define MOVIE_READ_SIZE			4096
#define MOVIE_SOUND_SECONDS		2			// length of the looping DirectSound buffer
#define MOVIE_PCM_SECONDS		1			// decoded sound waiting for room in that buffer
#define MOVIE_PREBUFFER_MS		250		// sound decoded before it starts playing
#define MOVIE_MAX_SKIP_MS		200		// when running late, still draw a frame at least this often

static int Movie_aborted = 0;

// ---------------------------------------------------------------------------------------
// clock

static LARGE_INTEGER Movie_qpc_freq;
static LARGE_INTEGER Movie_qpc_start;

static double movie_wall_time()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (double)(now.QuadPart - Movie_qpc_start.QuadPart) / (double)Movie_qpc_freq.QuadPart;
}

// ---------------------------------------------------------------------------------------
// sound

typedef struct movie_sound {
	LPDIRECTSOUNDBUFFER	buf;
	int		buf_bytes;
	int		bytes_per_sec;
	int		block;				// bytes per sample frame
	int		channels;
	int		written;				// bytes of sound written into buf since the start
	int		silenced_to;		// buf is silent from written up to here
	int		played;				// bytes played since the start
	DWORD		last_play;
	int		started;
	int		finished;			// everything written has been played
	short		*pcm;					// decoded samples waiting for room in buf
	int		pcm_len;				// in shorts
	int		pcm_max;
} movie_sound;

// the clock follows the sound while there is sound, and the wall clock after it ends
static int Movie_clock_on_sound = 0;
static double Movie_clock_offset = 0.0;

static double movie_time(movie_sound *s)
{
	if (Movie_clock_on_sound) {
		if (!s->started) {
			return 0.0;
		}
		return (double)s->played / (double)s->bytes_per_sec;
	}

	return movie_wall_time() + Movie_clock_offset;
}

static int movie_sound_init(movie_sound *s, int rate, int channels)
{
	WAVEFORMATEX wf;
	DSBUFFERDESC desc;

	memset(s, 0, sizeof(movie_sound));

	if (!ds_initialized || (pDirectSound == NULL)) {
		return 0;
	}

	if (channels > 2) {
		channels = 2;			// the first two channels; the game's movies are stereo at most
	}

	s->channels = channels;
	s->block = channels * 2;
	s->bytes_per_sec = rate * s->block;
	s->buf_bytes = s->bytes_per_sec * MOVIE_SOUND_SECONDS;

	memset(&wf, 0, sizeof(wf));
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = (WORD)channels;
	wf.nSamplesPerSec = rate;
	wf.nAvgBytesPerSec = s->bytes_per_sec;
	wf.nBlockAlign = (WORD)s->block;
	wf.wBitsPerSample = 16;

	memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
	desc.dwBufferBytes = s->buf_bytes;
	desc.lpwfxFormat = &wf;

	if (pDirectSound->CreateSoundBuffer(&desc, &s->buf, NULL) != DS_OK) {
		mprintf(("Movie: could not create a sound buffer\n"));
		s->buf = NULL;
		return 0;
	}

	s->pcm_max = rate * channels * MOVIE_PCM_SECONDS;
	s->pcm = (short *)malloc(s->pcm_max * sizeof(short));
	if (s->pcm == NULL) {
		s->buf->Release();
		s->buf = NULL;
		return 0;
	}

	return 1;
}

static void movie_sound_close(movie_sound *s)
{
	if (s->buf) {
		s->buf->Stop();
		s->buf->Release();
		s->buf = NULL;
	}
	if (s->pcm) {
		free(s->pcm);
		s->pcm = NULL;
	}
}

// writes bytes at the stream position pos (since the start), or silence when data is NULL
static void movie_sound_write(movie_sound *s, int pos, ubyte *data, int bytes)
{
	void *p1, *p2;
	DWORD n1, n2;
	HRESULT hr;

	hr = s->buf->Lock(pos % s->buf_bytes, bytes, &p1, &n1, &p2, &n2, 0);
	if (hr == DSERR_BUFFERLOST) {
		s->buf->Restore();
		hr = s->buf->Lock(pos % s->buf_bytes, bytes, &p1, &n1, &p2, &n2, 0);
	}
	if (hr != DS_OK) {
		return;
	}

	if (data) {
		memcpy(p1, data, n1);
		if (p2) {
			memcpy(p2, data + n1, n2);
		}
	} else {
		memset(p1, 0, n1);
		if (p2) {
			memset(p2, 0, n2);
		}
	}

	s->buf->Unlock(p1, n1, p2, n2);
}

// moves decoded sound into the DirectSound buffer and starts it.  no_more: nothing more will be decoded.
static void movie_sound_update(movie_sound *s, int no_more)
{
	DWORD play, write;
	int space, bytes, end, from;

	if (!s->buf || s->finished) {
		return;
	}

	if (s->started) {
		if (s->buf->GetCurrentPosition(&play, &write) != DS_OK) {
			return;
		}
		int d = (int)play - (int)s->last_play;
		if (d < 0) {
			d += s->buf_bytes;
		}
		s->played += d;
		s->last_play = play;

		// ran dry: what played meanwhile was silence, so new sound goes in from here
		if (s->played > s->written) {
			s->written = s->played;
		}
	}

	space = s->buf_bytes - (s->written - s->played);
	bytes = s->pcm_len * 2;
	if (bytes > space) {
		bytes = space;
	}
	bytes -= bytes % s->block;
	if (bytes > 0) {
		movie_sound_write(s, s->written, (ubyte *)s->pcm, bytes);
		s->written += bytes;
		s->pcm_len -= bytes / 2;
		memmove(s->pcm, s->pcm + bytes / 2, s->pcm_len * sizeof(short));
	}

	// keep the part of the buffer not yet written silent, so running dry never replays old sound
	end = s->played + s->buf_bytes;
	from = (s->silenced_to > s->written) ? s->silenced_to : s->written;
	if (end > from) {
		movie_sound_write(s, from, NULL, end - from);
		s->silenced_to = end;
	}

	if (!s->started) {
		if ((s->written >= s->bytes_per_sec * MOVIE_PREBUFFER_MS / 1000) || (no_more && (s->pcm_len == 0))) {
			s->buf->SetCurrentPosition(0);
			if (s->buf->Play(0, 0, DSBPLAY_LOOPING) == DS_OK) {
				s->buf->GetCurrentPosition(&s->last_play, &write);
				s->started = 1;
			}
		}
	} else if (no_more && (s->pcm_len == 0) && (s->played >= s->written)) {
		s->buf->Stop();
		s->finished = 1;
	}
}

// ---------------------------------------------------------------------------------------
// picture

static int Movie_bitmap = -1;
static ushort *Movie_screen = NULL;			// screen-sized, 16-bit, in the format gr_bitmap() takes it in
static int *Movie_xmap = NULL;				// destination column -> luma column
static int Movie_dx, Movie_dy, Movie_dw, Movie_dh;	// where the picture goes on the screen
static int *Movie_ymap = NULL;				// destination row -> luma row
static int Movie_hdec, Movie_vdec;			// chroma is subsampled horizontally / vertically

// YCbCr (BT.601, studio range) to the screen format, through tables
static int Movie_ty[256], Movie_trv[256], Movie_tgu[256], Movie_tgv[256], Movie_tbu[256];
static ubyte Movie_clamp[1024];
#define MOVIE_CLAMP(v)	Movie_clamp[((v) >> 8) + 384]
static ushort Movie_pr[256], Movie_pg[256], Movie_pb[256];
static ushort Movie_key, Movie_key_swap;		// the colour gr_bitmap() skips as transparent, and what to draw instead

static void movie_picture_tables()
{
	int i;
	color_gun *red, *green, *blue, *alpha;

	// gr_bitmap() copies the bitmap straight to the screen, or (32-bit Direct3D, Glide) draws it
	// as textured polygons, which take it in the texture format
	if (((gr_screen.mode == GR_DIRECT3D) || (gr_screen.mode == GR_GLIDE)) && Gr_bitmap_poly) {
		red = &Gr_t_red;
		green = &Gr_t_green;
		blue = &Gr_t_blue;
		alpha = &Gr_t_alpha;
	} else {
		red = &Gr_red;
		green = &Gr_green;
		blue = &Gr_blue;
		alpha = NULL;
	}

	for (i = 0; i < 256; i++) {
		Movie_ty[i] = 298 * (i - 16) + 128;
		Movie_trv[i] = 409 * (i - 128);
		Movie_tgu[i] = -100 * (i - 128);
		Movie_tgv[i] = -208 * (i - 128);
		Movie_tbu[i] = 516 * (i - 128);

		Movie_pr[i] = (ushort)((i / red->scale) << red->shift);
		Movie_pg[i] = (ushort)((i / green->scale) << green->shift);
		Movie_pb[i] = (ushort)((i / blue->scale) << blue->shift);
	}

	// opaque, where the format has alpha
	if (alpha && (alpha->scale <= 255)) {
		for (i = 0; i < 256; i++) {
			Movie_pr[i] |= (ushort)((255 / alpha->scale) << alpha->shift);
		}
	}

	for (i = 0; i < 1024; i++) {
		int v = i - 384;
		Movie_clamp[i] = (ubyte)((v < 0) ? 0 : ((v > 255) ? 255 : v));
	}

	// only the straight copy skips a colour; textures here always have the alpha bit set
	Movie_key = (ushort)green->mask;
	Movie_key_swap = (alpha == NULL) ? (ushort)(Movie_key - (1 << green->shift)) : Movie_key;
}

static int movie_picture_init(th_info *ti)
{
	int sw = gr_screen.max_w;
	int sh = gr_screen.max_h;
	int i;
	double aspect;

	Movie_hdec = !(ti->pixel_fmt & 1);
	Movie_vdec = !(ti->pixel_fmt & 2);

	// fit the picture to the screen, keeping its shape
	aspect = (double)ti->pic_width / (double)ti->pic_height;
	if ((ti->aspect_numerator > 0) && (ti->aspect_denominator > 0)) {
		aspect *= (double)ti->aspect_numerator / (double)ti->aspect_denominator;
	}
	Movie_dw = sw;
	Movie_dh = (int)(sw / aspect + 0.5);
	if (Movie_dh > sh) {
		Movie_dh = sh;
		Movie_dw = (int)(sh * aspect + 0.5);
		if (Movie_dw > sw) {
			Movie_dw = sw;
		}
	}
	Movie_dx = (sw - Movie_dw) / 2;
	Movie_dy = (sh - Movie_dh) / 2;

	Movie_screen = (ushort *)malloc(sw * sh * sizeof(ushort));
	Movie_xmap = (int *)malloc(Movie_dw * sizeof(int));
	Movie_ymap = (int *)malloc(Movie_dh * sizeof(int));
	if (!Movie_screen || !Movie_xmap || !Movie_ymap) {
		return 0;
	}

	for (i = 0; i < Movie_dw; i++) {
		Movie_xmap[i] = ti->pic_x + (int)(((double)i + 0.5) * ti->pic_width / Movie_dw);
	}
	for (i = 0; i < Movie_dh; i++) {
		Movie_ymap[i] = ti->pic_y + (int)(((double)i + 0.5) * ti->pic_height / Movie_dh);
	}

	movie_picture_tables();

	// black borders
	ushort black = (ushort)(Movie_pr[0] | Movie_pg[0] | Movie_pb[0]);
	for (i = 0; i < sw * sh; i++) {
		Movie_screen[i] = black;
	}

	Movie_bitmap = bm_create(16, sw, sh, Movie_screen, 0);
	return (Movie_bitmap >= 0);
}

static void movie_picture_close()
{
	if (Movie_bitmap >= 0) {
		bm_release(Movie_bitmap);
		Movie_bitmap = -1;
	}
	if (Movie_screen) {
		free(Movie_screen);
		Movie_screen = NULL;
	}
	if (Movie_xmap) {
		free(Movie_xmap);
		Movie_xmap = NULL;
	}
	if (Movie_ymap) {
		free(Movie_ymap);
		Movie_ymap = NULL;
	}
}

static void movie_picture_draw(th_dec_ctx *dec)
{
	th_ycbcr_buffer yuv;
	int i, j;

	if (th_decode_ycbcr_out(dec, yuv) != 0) {
		return;
	}

	for (j = 0; j < Movie_dh; j++) {
		int ly = Movie_ymap[j];
		ubyte *yrow = yuv[0].data + ly * yuv[0].stride;
		ubyte *urow = yuv[1].data + (ly >> Movie_vdec) * yuv[1].stride;
		ubyte *vrow = yuv[2].data + (ly >> Movie_vdec) * yuv[2].stride;
		ushort *dst = Movie_screen + (Movie_dy + j) * gr_screen.max_w + Movie_dx;

		for (i = 0; i < Movie_dw; i++) {
			int lx = Movie_xmap[i];
			int cx = lx >> Movie_hdec;
			int y = Movie_ty[yrow[lx]];
			int u = urow[cx];
			int v = vrow[cx];
			ushort pixel = (ushort)(Movie_pr[MOVIE_CLAMP(y + Movie_trv[v])] | Movie_pg[MOVIE_CLAMP(y + Movie_tgu[u] + Movie_tgv[v])] | Movie_pb[MOVIE_CLAMP(y + Movie_tbu[u])]);

			dst[i] = (pixel == Movie_key) ? Movie_key_swap : pixel;
		}
	}

	// a new bitmap handle each frame: Direct3D caches a bitmap's textures by its handle
	bm_release(Movie_bitmap);
	Movie_bitmap = bm_create(16, gr_screen.max_w, gr_screen.max_h, Movie_screen, 0);
	if (Movie_bitmap < 0) {
		return;
	}

	gr_reset_clip();
	gr_set_bitmap(Movie_bitmap);
	gr_bitmap(0, 0);
	gr_flip();
}

// ---------------------------------------------------------------------------------------
// the .ogv player

static int movie_read(CFILE *cf, ogg_sync_state *oy)
{
	char *buf = ogg_sync_buffer(oy, MOVIE_READ_SIZE);
	int n = cfread(buf, 1, MOVIE_READ_SIZE, cf);

	if (n < 0) {
		n = 0;
	}
	ogg_sync_wrote(oy, n);
	return n;
}

// plays an .ogv.  Returns 1 if it was played (to the end or until the player stopped it).
static int movie_play_ogv(char *filename)
{
	CFILE *cf;
	ogg_sync_state oy;
	ogg_page og;
	ogg_packet op;
	ogg_stream_state to, vo;
	th_info ti;
	th_comment tc;
	th_setup_info *ts = NULL;
	th_dec_ctx *td = NULL;
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	movie_sound snd;
	int theora_p = 0, vorbis_p = 0, vorbis_dsp = 0;
	int ok = 0;
	int done, eof;

	cf = cfopen(filename, "rb", CFILE_NORMAL, CF_TYPE_MOVIES);
	if (cf == NULL) {
		mprintf(("Movie: %s not found\n", filename));
		return 0;
	}

	memset(&snd, 0, sizeof(snd));
	ogg_sync_init(&oy);
	th_info_init(&ti);
	th_comment_init(&tc);
	vorbis_info_init(&vi);
	vorbis_comment_init(&vc);

	// the first page of each stream says what it is
	done = 0;
	while (!done) {
		if (movie_read(cf, &oy) == 0) {
			break;
		}
		while (ogg_sync_pageout(&oy, &og) > 0) {
			ogg_stream_state test;

			if (!ogg_page_bos(&og)) {
				if (theora_p) {
					ogg_stream_pagein(&to, &og);
				}
				if (vorbis_p) {
					ogg_stream_pagein(&vo, &og);
				}
				done = 1;
				break;
			}

			ogg_stream_init(&test, ogg_page_serialno(&og));
			ogg_stream_pagein(&test, &og);
			ogg_stream_packetout(&test, &op);

			if (!theora_p && (th_decode_headerin(&ti, &tc, &ts, &op) >= 0)) {
				memcpy(&to, &test, sizeof(test));
				theora_p = 1;
			} else if (!vorbis_p && (vorbis_synthesis_headerin(&vi, &vc, &op) >= 0)) {
				memcpy(&vo, &test, sizeof(test));
				vorbis_p = 1;
			} else {
				ogg_stream_clear(&test);
			}
		}
	}

	if (!theora_p) {
		mprintf(("Movie: %s has no Theora video\n", filename));
		goto cleanup;
	}

	// the rest of the headers: three for each stream
	while ((theora_p < 3) || (vorbis_p && (vorbis_p < 3))) {
		int ret;

		while ((theora_p < 3) && ((ret = ogg_stream_packetout(&to, &op)) != 0)) {
			if ((ret < 0) || (th_decode_headerin(&ti, &tc, &ts, &op) <= 0)) {
				mprintf(("Movie: bad Theora headers in %s\n", filename));
				goto cleanup;
			}
			theora_p++;
		}
		while (vorbis_p && (vorbis_p < 3) && ((ret = ogg_stream_packetout(&vo, &op)) != 0)) {
			if ((ret < 0) || vorbis_synthesis_headerin(&vi, &vc, &op)) {
				mprintf(("Movie: bad Vorbis headers in %s, playing without sound\n", filename));
				ogg_stream_clear(&vo);
				vorbis_p = 0;
				break;
			}
			vorbis_p++;
		}

		if ((theora_p >= 3) && (!vorbis_p || (vorbis_p >= 3))) {
			break;
		}

		if (ogg_sync_pageout(&oy, &og) > 0) {
			ogg_stream_pagein(&to, &og);
			if (vorbis_p) {
				ogg_stream_pagein(&vo, &og);
			}
		} else if (movie_read(cf, &oy) == 0) {
			mprintf(("Movie: %s ends inside its headers\n", filename));
			goto cleanup;
		}
	}

	td = th_decode_alloc(&ti, ts);
	if (td == NULL) {
		mprintf(("Movie: cannot decode %s\n", filename));
		goto cleanup;
	}

	if (vorbis_p) {
		if (movie_sound_init(&snd, vi.rate, vi.channels)) {
			vorbis_synthesis_init(&vd, &vi);
			vorbis_block_init(&vd, &vb);
			vorbis_dsp = 1;
		} else {
			// no sound device: drop the sound stream
			ogg_stream_clear(&vo);
			vorbis_p = 0;
		}
	}

	if (!movie_picture_init(&ti)) {
		mprintf(("Movie: no memory or bitmap slot for %s\n", filename));
		goto cleanup;
	}

	mprintf(("Movie: playing %s, %dx%d at %.2f fps, %s\n", filename, ti.pic_width, ti.pic_height,
		(double)ti.fps_numerator / (double)ti.fps_denominator, vorbis_p ? "with sound" : "no sound"));

	ok = 1;
	Movie_aborted = 0;
	QueryPerformanceFrequency(&Movie_qpc_freq);
	QueryPerformanceCounter(&Movie_qpc_start);
	Movie_clock_on_sound = vorbis_p;
	Movie_clock_offset = 0.0;

	key_flush();
	mouse_flush();
	Mouse_hidden++;
	audiostream_pause_all();

	{
		double frame_len = (double)ti.fps_denominator / (double)ti.fps_numerator;
		double frame_time = 0.0;			// when the waiting frame should appear
		double last_drawn = -1.0;
		int frame_ready = 0;
		int theora_done = 0;

		eof = 0;
		while (1) {
			// decode sound until the waiting room is full
			while (vorbis_p) {
				float **pcm;
				int n = vorbis_synthesis_pcmout(&vd, &pcm);

				if (n > 0) {
					int room = (snd.pcm_max - snd.pcm_len) / snd.channels;
					int i, c;

					if (room <= 0) {
						break;
					}
					if (n > room) {
						n = room;
					}
					for (i = 0; i < n; i++) {
						for (c = 0; c < snd.channels; c++) {
							int v = (int)(pcm[c][i] * 32767.0f);
							if (v > 32767) {
								v = 32767;
							} else if (v < -32768) {
								v = -32768;
							}
							snd.pcm[snd.pcm_len++] = (short)v;
						}
					}
					vorbis_synthesis_read(&vd, n);
				} else if (ogg_stream_packetout(&vo, &op) > 0) {
					if (vorbis_synthesis(&vb, &op) == 0) {
						vorbis_synthesis_blockin(&vd, &vb);
					}
				} else {
					break;
				}
			}

			// decode the next frame
			while (!frame_ready && !theora_done) {
				if (ogg_stream_packetout(&to, &op) > 0) {
					ogg_int64_t granule = -1;

					if (op.granulepos >= 0) {
						th_decode_ctl(td, TH_DECCTL_SET_GRANPOS, &op.granulepos, sizeof(op.granulepos));
					}
					if (th_decode_packetin(td, &op, &granule) >= 0) {
						// th_granule_time() is when the frame ends
						frame_time = th_granule_time(td, granule) - frame_len;
						frame_ready = 1;
					}
				} else if (eof) {
					theora_done = 1;
				} else {
					break;
				}
			}

			// more of the file when a stream has run out of packets
			int sound_hungry = vorbis_p && (snd.pcm_len < snd.pcm_max / 2);
			if (!eof && ((!frame_ready && !theora_done) || sound_hungry)) {
				if (movie_read(cf, &oy) == 0) {
					eof = 1;
				}
				while (ogg_sync_pageout(&oy, &og) > 0) {
					ogg_stream_pagein(&to, &og);
					if (vorbis_p) {
						ogg_stream_pagein(&vo, &og);
					}
				}
			}

			movie_sound_update(&snd, eof);

			// the sound has ended: carry on with the wall clock from where it got to
			if (Movie_clock_on_sound && snd.finished) {
				Movie_clock_offset = movie_time(&snd) - movie_wall_time();
				Movie_clock_on_sound = 0;
			}

			if (frame_ready) {
				double now = movie_time(&snd);

				if (now >= frame_time) {
					// late by more than a frame: skip drawing, unless nothing has been drawn for a while
					if ((now < frame_time + frame_len) || (last_drawn < 0.0) || (now - last_drawn >= MOVIE_MAX_SKIP_MS / 1000.0)) {
						movie_picture_draw(td);
						last_drawn = now;
					}
					frame_ready = 0;
				}
			}

			if (theora_done && !frame_ready && (!vorbis_p || snd.finished)) {
				break;
			}

			os_poll();
			if (key_inkey() || mouse_down_count(MOUSE_LEFT_BUTTON)) {
				Movie_aborted = 1;
				break;
			}

			// nothing due yet
			if (frame_ready || theora_done) {
				Sleep(1);
			}
		}
	}

	audiostream_unpause_all();
	Mouse_hidden--;
	key_flush();
	mouse_flush();

	gr_clear();
	gr_flip();

cleanup:
	movie_picture_close();
	movie_sound_close(&snd);
	if (vorbis_dsp) {
		vorbis_block_clear(&vb);
		vorbis_dsp_clear(&vd);
	}
	if (vorbis_p) {
		ogg_stream_clear(&vo);
	}
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	if (td) {
		th_decode_free(td);
	}
	if (ts) {
		th_setup_free(ts);
	}
	if (theora_p) {
		ogg_stream_clear(&to);
	}
	th_comment_clear(&tc);
	th_info_clear(&ti);
	ogg_sync_clear(&oy);
	cfclose(cf);

	return ok;
}

// ---------------------------------------------------------------------------------------

// The Interplay MVE player was not part of the source release, so an .mve cannot be played.
static int movie_play_mve(char *filename)
{
	return 0;
}

// movies are kept away from the gameplay recorder: see the top of this file
static int movie_allowed()
{
	if (Replay_mode == REPLAY_PLAY) {
		return 0;
	}
	if (Autotest_harness) {
		return 0;
	}
	if (replay_stream_active()) {
		return 0;
	}

	return 1;
}

int movie_play(char *filename, int unused)
{
	char ogv_name[MAX_PATH];
	char *p;

	Movie_aborted = 0;

	if ((filename == NULL) || !strlen(filename) || (strlen(filename) + 5 > MAX_PATH)) {
		return 0;
	}

	if (!movie_allowed()) {
		mprintf(("Movie: %s skipped (replay, autotest or recording)\n", filename));
		return 0;
	}

	if (movie_play_mve(filename)) {
		return 1;
	}

	// the same name with .ogv
	strcpy(ogv_name, filename);
	p = strrchr(ogv_name, '.');
	if (p) {
		*p = 0;
	}
	strcat(ogv_name, ".ogv");

	return movie_play_ogv(ogv_name);
}

int movie_play_two(char *filename1, char *filename2, int unused)
{
	int played = movie_play(filename1, unused);

	if (Movie_aborted) {
		return played;
	}

	return movie_play(filename2, unused) || played;
}
