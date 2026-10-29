/*
 * Bitmap log: see BitmapLog.h.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pstypes.h"
#include "2d.h"
#include "GrInternal.h"
#include "SystemVars.h"
#include "BmpMan.h"
#include "BitmapLog.h"

#define BMLOG_MAX_RANGES	256

int Bmlog_on = 0;
unsigned int Bmlog_cosmetic_calls[2] = { 0, 0 };

static FILE *Bmlog_fp = NULL;
static int Bmlog_frame = 0;
static char Bmlog_shots[MAX_PATH] = "";
static int Bmlog_num_ranges = 0;
static int Bmlog_range[BMLOG_MAX_RANGES][2];

static void bmlog_close()
{
	if (Bmlog_fp) {
		fclose(Bmlog_fp);
		Bmlog_fp = NULL;
	}
}

void bmlog_set_cmdline(char *file, char *shots_dir, char *frames)
{
	char *p;

	if (file && file[0]) {
		Bmlog_fp = fopen(file, "a");
		if (Bmlog_fp) {
			setvbuf(Bmlog_fp, NULL, _IOFBF, 1 << 20);
			Bmlog_on = 1;
			atexit(bmlog_close);
		}
	}
	if (shots_dir && shots_dir[0] && frames && frames[0]) {
		strncpy(Bmlog_shots, shots_dir, MAX_PATH - 1);
		Bmlog_shots[MAX_PATH - 1] = 0;
		CreateDirectory(Bmlog_shots, NULL);
		for (p = frames; *p && (Bmlog_num_ranges < BMLOG_MAX_RANGES); ) {
			int a = atoi(p), b = a;
			while (*p && (*p != ',') && (*p != '-')) p++;
			if (*p == '-') {
				p++;
				b = atoi(p);
				while (*p && (*p != ',')) p++;
			}
			if (*p == ',') p++;
			Bmlog_range[Bmlog_num_ranges][0] = a;
			Bmlog_range[Bmlog_num_ranges][1] = b;
			Bmlog_num_ranges++;
		}
		Bmlog_on = 1;
	}
}

void bmlog_gauge(int result)
{
	if (Bmlog_fp) {
		fprintf(Bmlog_fp, "G %d\n", result ? 1 : 0);
	}
}

void bmlog_jitter(int xin, int yin, int xout, int yout)
{
	if (Bmlog_fp) {
		fprintf(Bmlog_fp, "J %d %d %d %d\n", xin, yin, xout, yout);
	}
}

void bmlog_bitmap(const char *kind, int x, int y, int w, int h, int sx, int sy)
{
	if (Bmlog_fp) {
		int bw = 0, bh = 0;
		if (gr_screen.current_bitmap >= 0) {
			bm_get_info(gr_screen.current_bitmap, &bw, &bh);
		}
		fprintf(Bmlog_fp, "B %s h=%d bw=%d bh=%d %d %d %d %d %d %d ab=%d bb=%d a=%.3f\n", kind, gr_screen.current_bitmap, bw, bh, x, y, w, h, sx, sy,
			gr_screen.current_alphablend_mode, gr_screen.current_bitblt_mode, gr_screen.current_alpha);
	}
}

static int bmlog_want_shot(int frame)
{
	for (int i = 0; i < Bmlog_num_ranges; i++) {
		if ((frame >= Bmlog_range[i][0]) && (frame <= Bmlog_range[i][1])) {
			return 1;
		}
	}
	return 0;
}

// the last frame drawn, from the back buffer, as a 24-bit BMP
static void bmlog_shot(int frame)
{
	char path[MAX_PATH];
	int w = gr_screen.max_w, h = gr_screen.max_h, bpp = gr_screen.bytes_per_pixel, row, x, y;
	ubyte *data, *line;
	uint pixel;
	color_gun *guns[3] = { &Gr_blue, &Gr_green, &Gr_red };
	BITMAPFILEHEADER fh;
	BITMAPINFOHEADER ih;
	FILE *fp;

	if ((bpp != 2) && (bpp != 4)) {
		return;
	}
	sprintf(path, "%s\\f%06d.bmp", Bmlog_shots, frame);
	row = (w * 3 + 3) & ~3;
	data = (ubyte *)malloc(w * h * bpp);
	line = (ubyte *)malloc(row);
	fp = fopen(path, "wb");
	if (data && line && fp) {
		memset(data, 0, w * h * bpp);
		memset(line, 0, row);
		gr_get_region(0, w, h, data);
		memset(&fh, 0, sizeof(fh));
		memset(&ih, 0, sizeof(ih));
		fh.bfType = 0x4d42;
		fh.bfOffBits = sizeof(fh) + sizeof(ih);
		fh.bfSize = fh.bfOffBits + row * h;
		ih.biSize = sizeof(ih);
		ih.biWidth = w;
		ih.biHeight = h;
		ih.biPlanes = 1;
		ih.biBitCount = 24;
		ih.biCompression = BI_RGB;
		fwrite(&fh, sizeof(fh), 1, fp);
		fwrite(&ih, sizeof(ih), 1, fp);
		for (y = h - 1; y >= 0; y--) {
			for (x = 0; x < w; x++) {
				pixel = (bpp == 2) ? ((ushort *)data)[y * w + x] : ((uint *)data)[y * w + x];
				for (int c = 0; c < 3; c++) {
					uint max = (uint)guns[c]->mask >> guns[c]->shift;
					line[x * 3 + c] = max ? (ubyte)((((pixel & guns[c]->mask) >> guns[c]->shift) * 255) / max) : 0;
				}
			}
			fwrite(line, row, 1, fp);
		}
	}
	if (fp) {
		fclose(fp);
	}
	free(data);
	free(line);
}

void bmlog_frame_end()
{
	if (!Bmlog_on) {
		return;
	}
	Bmlog_frame++;
	if (Bmlog_fp) {
		fprintf(Bmlog_fp, "F %d mf=%d cr=%u crd=%u\n", Bmlog_frame, Framecount, Bmlog_cosmetic_calls[0], Bmlog_cosmetic_calls[1]);
		if ((Bmlog_frame & 255) == 0) {
			fflush(Bmlog_fp);
		}
	}
	if (Bmlog_shots[0] && bmlog_want_shot(Bmlog_frame)) {
		bmlog_shot(Bmlog_frame);
	}
}
