/*
 * Bitmap log: evidence for the removal of bitmap sections (BitmapSections branch, D-20261029-075126).  Off by default.
 *
 * Usage:
 *   -bmlog FILE                 append a text log to FILE
 *   -bmshots DIR -bmframes LIST save the back buffer of the listed frames as DIR\f<frame>.bmp (24-bit BMP);
 *                               LIST is comma separated frames or ranges, for example 100,250-260
 *
 * The log has, per frame, in the order they happen:
 *   B <kind> h=<handle> x y w h sx sy ab=<alphablend> bb=<bitblt> a=<alpha>    a 2D bitmap call at the GR layer
 *                               (gr_bitmap, gr_bitmap_ex, gr_aabitmap, gr_aabitmap_ex), above the section split
 *   J xin yin xout yout         one emp_hud_jitter result (only while EMP is active)
 *   G 0|1                       one emp_should_blit_gauge result (only while EMP is active)
 *   F <n> mf=<Framecount> cr=<logic calls> crd=<drawing calls>   end of frame n (counted at gr_flip_frame), cumulative
 *                               cosmetic_rand calls on the logic stream and on the drawing stream
 *
 * The log only reads: it draws no random number, reads no timer, and changes no game state, so a trace and a
 * recording replay the same with it on or off.  The cosmetic_rand call counts are kept even with the log off
 * (two increments of a global).
 */

#ifndef _BITMAPLOG_H
#define _BITMAPLOG_H

extern int Bmlog_on;
extern unsigned int Bmlog_cosmetic_calls[2];	// [0] logic stream, [1] drawing stream

void bmlog_set_cmdline(char *file, char *shots_dir, char *frames);
void bmlog_gauge(int result);
void bmlog_jitter(int xin, int yin, int xout, int yout);
void bmlog_bitmap(const char *kind, int x, int y, int w, int h, int sx, int sy);
void bmlog_frame_end();					// after the flip

#endif
