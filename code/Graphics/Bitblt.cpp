/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/Graphics/Bitblt.cpp $
 * $Revision: 3 $
 * $Date: 11/30/98 1:07p $
 * $Author: Dave $
 *
 * Code to do software bitblt type stuff
 *
 * $Log: /Freespace2/code/Graphics/Bitblt.cpp $
 * 
 * 3     11/30/98 1:07p Dave
 * 16 bit conversion, first run.
 * 
 * 2     10/07/98 10:52a Dave
 * Initial checkin.
 * 
 * 1     10/07/98 10:48a Dave
 * 
 * 2     4/14/98 12:15p John
 * Made 16-bpp movies work.
 * 
 * 1     3/25/98 8:07p John
 * Split software renderer into Win32 and DirectX
 *
 * $NoKeywords: $
 */

#include "osapi.h"
#include "2d.h"
#include "bmpman.h"
#include "key.h"
#include "floating.h"
#include "PalMan.h"
#include "GrSoft.h"
#include "GrInternal.h"

// Headers for 2d functions
#include "Bitblt.h"

MONITOR( Num2dBitmaps );	

void gr8_aabitmap_ex(int x,int y,int w,int h,int sx,int sy)
{
}

void grx_aabitmap_ex(int x,int y,int w,int h,int sx,int sy)
{
	int reclip;
	#ifndef NDEBUG
	int count = 0;
	#endif

	int dx1=x, dx2=x+w-1;
	int dy1=y, dy2=y+h-1;

	int bw, bh;
	bm_get_info( gr_screen.current_bitmap, &bw, &bh, NULL );

	do {
		reclip = 0;
		#ifndef NDEBUG
			if ( count > 1 ) Int3();
			count++;
		#endif
	
		if ((dx1 > gr_screen.clip_right ) || (dx2 < gr_screen.clip_left)) return;
		if ((dy1 > gr_screen.clip_bottom ) || (dy2 < gr_screen.clip_top)) return;
		if ( dx1 < gr_screen.clip_left ) { sx += gr_screen.clip_left-dx1; dx1 = gr_screen.clip_left; }
		if ( dy1 < gr_screen.clip_top ) { sy += gr_screen.clip_top-dy1; dy1 = gr_screen.clip_top; }
		if ( dx2 > gr_screen.clip_right )	{ dx2 = gr_screen.clip_right; }
		if ( dy2 > gr_screen.clip_bottom )	{ dy2 = gr_screen.clip_bottom; }

		if ( sx < 0 ) {
			dx1 -= sx;
			sx = 0;
			reclip = 1;
		}

		if ( sy < 0 ) {
			dy1 -= sy;
			sy = 0;
			reclip = 1;
		}

		w = dx2-dx1+1;
		h = dy2-dy1+1;

		if ( sx + w > bw ) {
			w = bw - sx;
			dx2 = dx1 + w - 1;
		}

		if ( sy + h > bh ) {
			h = bh - sy;
			dy2 = dy1 + h - 1;
		}

		if ( w < 1 ) return;		// clipped away!
		if ( h < 1 ) return;		// clipped away!

	} while (reclip);

	// Make sure clipping algorithm works
	#ifndef NDEBUG
		Assert( w > 0 );
		Assert( h > 0 );
		Assert( w == (dx2-dx1+1) );
		Assert( h == (dy2-dy1+1) );
		Assert( sx >= 0 );
		Assert( sy >= 0 );
		Assert( sx+w <= bw );
		Assert( sy+h <= bh );
		Assert( dx2 >= dx1 );
		Assert( dy2 >= dy1 );
		Assert( (dx1 >= gr_screen.clip_left ) && (dx1 <= gr_screen.clip_right) );
		Assert( (dx2 >= gr_screen.clip_left ) && (dx2 <= gr_screen.clip_right) );
		Assert( (dy1 >= gr_screen.clip_top ) && (dy1 <= gr_screen.clip_bottom) );
		Assert( (dy2 >= gr_screen.clip_top ) && (dy2 <= gr_screen.clip_bottom) );
	#endif

	// We now have dx1,dy1 and dx2,dy2 and sx, sy all set validly within clip regions.

	// Draw bitmap bm[sx,sy] into (dx1,dy1)-(dx2,dy2)

	switch( gr_screen.bits_per_pixel )	{
	case 8:	
		gr8_aabitmap_ex(dx1,dy1,dx2-dx1+1,dy2-dy1+1,sx,sy);
		break;

	default:
		Error( LOCATION, "Unsupported BPP=%d in grx_aabitmap_ex!\n", gr_screen.bytes_per_pixel );
	}


}


void gr8_bitmap_ex(int x,int y,int w,int h,int sx,int sy)
{
}



void grx_bitmap_ex(int x,int y,int w,int h,int sx,int sy)
{
	int reclip;
	#ifndef NDEBUG
	int count = 0;
	#endif

	int dx1=x, dx2=x+w-1;
	int dy1=y, dy2=y+h-1;

	int bw, bh;
	bm_get_info( gr_screen.current_bitmap, &bw, &bh, NULL );

	do {
		reclip = 0;
		#ifndef NDEBUG
			if ( count > 1 ) Int3();
			count++;
		#endif
	
		if ((dx1 > gr_screen.clip_right ) || (dx2 < gr_screen.clip_left)) return;
		if ((dy1 > gr_screen.clip_bottom ) || (dy2 < gr_screen.clip_top)) return;
		if ( dx1 < gr_screen.clip_left ) { sx += gr_screen.clip_left-dx1; dx1 = gr_screen.clip_left; }
		if ( dy1 < gr_screen.clip_top ) { sy += gr_screen.clip_top-dy1; dy1 = gr_screen.clip_top; }
		if ( dx2 > gr_screen.clip_right )	{ dx2 = gr_screen.clip_right; }
		if ( dy2 > gr_screen.clip_bottom )	{ dy2 = gr_screen.clip_bottom; }

		if ( sx < 0 ) {
			dx1 -= sx;
			sx = 0;
			reclip = 1;
		}

		if ( sy < 0 ) {
			dy1 -= sy;
			sy = 0;
			reclip = 1;
		}

		w = dx2-dx1+1;
		h = dy2-dy1+1;

		if ( sx + w > bw ) {
			w = bw - sx;
			dx2 = dx1 + w - 1;
		}

		if ( sy + h > bh ) {
			h = bh - sy;
			dy2 = dy1 + h - 1;
		}

		if ( w < 1 ) return;		// clipped away!
		if ( h < 1 ) return;		// clipped away!

	} while (reclip);

	// Make sure clipping algorithm works
	#ifndef NDEBUG
		Assert( w > 0 );
		Assert( h > 0 );
		Assert( w == (dx2-dx1+1) );
		Assert( h == (dy2-dy1+1) );
		Assert( sx >= 0 );
		Assert( sy >= 0 );
		Assert( sx+w <= bw );
		Assert( sy+h <= bh );
		Assert( dx2 >= dx1 );
		Assert( dy2 >= dy1 );
		Assert( (dx1 >= gr_screen.clip_left ) && (dx1 <= gr_screen.clip_right) );
		Assert( (dx2 >= gr_screen.clip_left ) && (dx2 <= gr_screen.clip_right) );
		Assert( (dy1 >= gr_screen.clip_top ) && (dy1 <= gr_screen.clip_bottom) );
		Assert( (dy2 >= gr_screen.clip_top ) && (dy2 <= gr_screen.clip_bottom) );
	#endif

	// We now have dx1,dy1 and dx2,dy2 and sx, sy all set validly within clip regions.

	// Draw bitmap bm[sx,sy] into (dx1,dy1)-(dx2,dy2)

	gr8_bitmap_ex(dx1,dy1,dx2-dx1+1,dy2-dy1+1,sx,sy);
}



void grx_bitmap(int x,int y)
{
	int w, h;

	bm_get_info( gr_screen.current_bitmap, &w, &h, NULL );
	int dx1=x, dx2=x+w-1;
	int dy1=y, dy2=y+h-1;
	int sx=0, sy=0;

	if ((dx1 > gr_screen.clip_right ) || (dx2 < gr_screen.clip_left)) return;
	if ((dy1 > gr_screen.clip_bottom ) || (dy2 < gr_screen.clip_top)) return;
	if ( dx1 < gr_screen.clip_left ) { sx = gr_screen.clip_left-dx1; dx1 = gr_screen.clip_left; }
	if ( dy1 < gr_screen.clip_top ) { sy = gr_screen.clip_top-dy1; dy1 = gr_screen.clip_top; }
	if ( dx2 > gr_screen.clip_right )	{ dx2 = gr_screen.clip_right; }
	if ( dy2 > gr_screen.clip_bottom )	{ dy2 = gr_screen.clip_bottom; }

	if ( sx < 0 ) return;
	if ( sy < 0 ) return;
	if ( sx >= w ) return;
	if ( sy >= h ) return;

	// Draw bitmap bm[sx,sy] into (dx1,dy1)-(dx2,dy2)

	gr8_bitmap_ex(dx1,dy1,dx2-dx1+1,dy2-dy1+1,sx,sy);
}

void grx_aabitmap(int x,int y)
{
	int w, h;

	bm_get_info( gr_screen.current_bitmap, &w, &h, NULL );
	int dx1=x, dx2=x+w-1;
	int dy1=y, dy2=y+h-1;
	int sx=0, sy=0;

	if ((dx1 > gr_screen.clip_right ) || (dx2 < gr_screen.clip_left)) return;
	if ((dy1 > gr_screen.clip_bottom ) || (dy2 < gr_screen.clip_top)) return;
	if ( dx1 < gr_screen.clip_left ) { sx = gr_screen.clip_left-dx1; dx1 = gr_screen.clip_left; }
	if ( dy1 < gr_screen.clip_top ) { sy = gr_screen.clip_top-dy1; dy1 = gr_screen.clip_top; }
	if ( dx2 > gr_screen.clip_right )	{ dx2 = gr_screen.clip_right; }
	if ( dy2 > gr_screen.clip_bottom )	{ dy2 = gr_screen.clip_bottom; }

	if ( sx < 0 ) return;
	if ( sy < 0 ) return;
	if ( sx >= w ) return;
	if ( sy >= h ) return;

	// Draw bitmap bm[sx,sy] into (dx1,dy1)-(dx2,dy2)

	gr8_aabitmap_ex(dx1,dy1,dx2-dx1+1,dy2-dy1+1,sx,sy);
}

