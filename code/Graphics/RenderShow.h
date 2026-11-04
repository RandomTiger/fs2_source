// Claude: GodotOptions. Switches for the game's visual systems, set only by the console's show command (GameOptions.cpp), which a
// compared run (autotest, record, replay) does not have, so they stay all on there. Each is tested as early as its system allows:
// where nothing else needs the thing (particles, trails, muzzle flashes), it is never made; where gameplay needs it (ships, beams,
// poofs moving), only the drawing stops.

#ifndef _RENDERSHOW_H
#define _RENDERSHOW_H

#define RS_ITEM(id, name) id,
enum {
#include "RenderShowList.h"
	RS_COUNT
};
#undef RS_ITEM

extern int Render_show[RS_COUNT];	// 2d.cpp

#endif
