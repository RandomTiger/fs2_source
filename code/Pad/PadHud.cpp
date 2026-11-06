// PadHud.cpp
// Pad colours for the HUD gauges. FS2-specific.

#include <stdio.h>

#include "PadGame.h"
#include "PadCore.h"

#include "2d.h"
#include "HUD.h"
#include "AlphaColors.h"

// white shows against each of the game's HUD colours
void pad_ets_recolor(int gauge, int bright)
{
	if (pad_ets_highlight(gauge)) {
		gr_set_color_fast(bright ? &Color_bright_white : &Color_white);
	}
}
