// PadHud.cpp
// Draws the open pad tree node: four options in a cross below the reticle, after the
// category has been held for PAD_TREE_SHOW_MS. FS2-specific; the tree itself is in PadCore.

#include <stdio.h>

#include "PadGame.h"
#include "PadCore.h"

#include "2d.h"
#include "HUD.h"

#define PAD_HUD_Y_FRACTION		0.68f		// centre of the cross, as a fraction of screen height
#define PAD_HUD_GAP				12			// pixels between the centre and the Left/Right labels

// x is the left edge, the centre or the right edge for align -1, 0, 1
static void pad_hud_string(int x, int y, int align, const char *text)
{
	int w, h;

	gr_get_string_size(&w, &h, (char *) text);
	if (align == 0) {
		x -= w / 2;
	} else if (align > 0) {
		x -= w;
	}
	gr_string(x, y, (char *) text);
}

void pad_hud_render()
{
	const pad_output *out = pad_game_output();
	const pad_tree_node *node;
	char buf[64];
	int cx, cy, w, lh, i;

	if (!out->connected || (out->tree_node < 0) || !out->tree_show) {
		return;
	}

	node = pad_core_tree_node(out->tree_node);
	if (node == NULL) {
		return;
	}

	gr_get_string_size(&w, &lh, "X");
	lh += 2;
	cx = gr_screen.max_w / 2;
	cy = (int) (gr_screen.max_h * PAD_HUD_Y_FRACTION);

	hud_set_default_color();

	if (node->title) {
		pad_hud_string(cx, cy - 4 * lh, 0, node->title);
	}

	for (i = 0; i < PAD_TREE_NUM_DIRS; i++) {
		const pad_tree_option *opt = &node->option[i];
		const char *label = opt->label;

		if ((i == PAD_TREE_LEFT) && (out->tree_depth > 1)) {
			label = "Back";
		}
		if (label == NULL) {
			continue;
		}

		// arrows point the way the D-pad goes; a branch gets a trailing "..."
		switch (i) {
		case PAD_TREE_UP:		sprintf(buf, "^ %s%s", label, (opt->child >= 0) ? "..." : "");	break;
		case PAD_TREE_DOWN:	sprintf(buf, "v %s%s", label, (opt->child >= 0) ? "..." : "");	break;
		case PAD_TREE_LEFT:	sprintf(buf, "< %s%s", label, ((out->tree_depth <= 1) && (opt->child >= 0)) ? "..." : "");	break;
		case PAD_TREE_RIGHT:	sprintf(buf, "%s%s >", label, (opt->child >= 0) ? "..." : "");	break;
		}

		switch (i) {
		case PAD_TREE_UP:		pad_hud_string(cx, cy - 2 * lh, 0, buf);					break;
		case PAD_TREE_DOWN:	pad_hud_string(cx, cy + 2 * lh, 0, buf);					break;
		case PAD_TREE_LEFT:	pad_hud_string(cx - PAD_HUD_GAP - 40, cy, 1, buf);		break;
		case PAD_TREE_RIGHT:	pad_hud_string(cx + PAD_HUD_GAP + 40, cy, -1, buf);		break;
		}
	}
}
