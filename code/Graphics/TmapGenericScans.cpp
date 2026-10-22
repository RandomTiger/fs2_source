/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/Graphics/TmapGenericScans.cpp $
 * $Revision: 2 $
 * $Date: 10/07/98 10:53a $
 * $Author: Dave $
 *
 * Some code for generic scanlines.  This isn't used, it is just
 * basically a dump area for inner loops I was experimenting with.
 * this entire file is #ifdef 0'd out.
 *
 * $Log: /Freespace2/code/Graphics/TmapGenericScans.cpp $
 * 
 * 2     10/07/98 10:53a Dave
 * Initial checkin.
 * 
 * 1     10/07/98 10:49a Dave
 * 
 * 7     4/24/97 4:45p John
 * Added tiled texture mappers for 64x64, 128x128, and 256x256 textures.
 * 
 * 6     4/24/97 3:01p John
 * added code to not crash on non-256x256 textures.
 * 
 * 5     3/14/97 3:55p John
 * Made tiled tmapper not always be zbuffered.
 * 
 * 4     3/13/97 10:32a John
 * Added code for tiled 256x256 textures in certain models.
 * 
 * 3     3/10/97 5:20p John
 * Differentiated between Gouraud and Flat shading.  Since we only do flat
 * shading as of now, we don't need to interpolate L in the outer loop.
 * This should save a few percent.
 * 
 * 2     12/10/96 10:37a John
 * Restructured texture mapper to remove some overhead from each scanline
 * setup.  This gave about a 30% improvement drawing trans01.pof, which is
 * a really complex model.  In the process, I cleaned up the scanline
 * functions and separated them into different modules for each pixel
 * depth.   
 *
 * $NoKeywords: $
 */

#include "3d.h"
#include "2d.h"
#include "tmapper.h"
#include "tmapscanline.h"
#include "floating.h"
#include "palman.h"
#include "fix.h"

#pragma warning(disable:4410)
 


