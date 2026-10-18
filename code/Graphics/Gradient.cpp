/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/Graphics/Gradient.cpp $
 * $Revision: 3 $
 * $Date: 12/02/98 5:47p $
 * $Author: Dave $
 *
 * Routines to draw rectangular gradients.
 *
 * $Log: /Freespace2/code/Graphics/Gradient.cpp $
 * 
 * 3     12/02/98 5:47p Dave
 * Put in interface xstr code. Converted barracks screen to new format.
 * 
 * 2     10/07/98 10:52a Dave
 * Initial checkin.
 * 
 * 1     10/07/98 10:49a Dave
 * 
 * 17    5/06/98 5:30p John
 * Removed unused cfilearchiver.  Removed/replaced some unused/little used
 * graphics functions, namely gradient_h and _v and pixel_sp.   Put in new
 * DirectX header files and libs that fixed the Direct3D alpha blending
 * problems.
 * 
 * 16    3/10/98 4:18p John
 * Cleaned up graphics lib.  Took out most unused gr functions.   Made D3D
 * & Glide have popups and print screen.  Took out all >8bpp software
 * support.  Made Fred zbuffer.  Made zbuffer allocate dynamically to
 * support Fred.  Made zbuffering key off of functions rather than one
 * global variable.
 * 
 * 15    1/19/98 6:15p John
 * Fixed all my Optimized Build compiler warnings
 * 
 * 14    11/30/97 4:04p John
 * 
 * 13    11/30/97 12:18p John
 * added more 24 & 32-bpp primitives
 * 
 * 12    10/19/97 12:55p John
 * new code to lock / unlock surfaces for smooth directx integration.
 * 
 * 11    10/14/97 4:50p John
 * more 16 bpp stuff.
 * 
 * 10    10/14/97 8:08a John
 * added a bunch more 16 bit support
 * 
 * 9     10/09/97 5:23p John
 * Added support for more 16-bpp functions
 * 
 * 8     6/11/97 1:12p John
 * Started fixing all the text colors in the game.
 * 
 * 7     5/12/97 12:27p John
 * Restructured Graphics Library to add support for multiple renderers.
 * 
 * 6     12/12/96 4:59p Lawrance
 * made clipping for horizontal and vertical gradient lines right
 * 
 * 5     11/19/96 2:46p Allender
 * fix up gradient support for 15bpp
 * 
 * 4     11/18/96 4:34p Allender
 * new 16 bit gradient functions
 * 
 * 3     10/27/96 1:21a Lawrance
 * added check to avoid divide by zero when calculating gradients
 * 
 * 2     10/26/96 2:56p John
 * Got gradient code working.
 * 
 * 1     10/26/96 1:45p John
 * Initial skeletion code.
 *
 * $NoKeywords: $
 */

#include <windows.h>
#include <windowsx.h>

#include "2d.h"
#include "GrInternal.h"
#include "gradient.h"
#include "Floating.h"
#include "Line.h"
#include "PalMan.h"
		
void gr8_gradient(int x1,int y1,int x2,int y2)
{
	Int3();
}


		


