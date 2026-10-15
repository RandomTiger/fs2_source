// Claude: The game view: the camera gameplay sees, set once per frame in the logic phase,
// before and independent of drawing (docs/PREPORT.md, step 3).
//
// Gameplay used to read the camera from whatever the renderer last set up: the eye position
// and orientation, View_position, and whether an object was inside the view cone
// (OF_WAS_RENDERED). The game view sets the same values without drawing anything, so the
// game plays the same headless, and so a later display (another resolution, widescreen,
// another engine) cannot change gameplay. Projections use the art resolution (640x480 or
// 1024x768) at 4:3, which is what the display has always been.

#ifndef _GAMEVIEW_H
#define _GAMEVIEW_H

#include "PsTypes.h"

struct object;

// Set the camera for this frame: Eye_position, Eye_matrix and View_position, which gameplay
// reads, and the game view's own projection.
void game_view_set(vector *eye_pos, matrix *eye_orient, float zoom);

// Rotate a world point into the game view and return its clip codes (CC_*), as
// g3_rotate_vector does for the renderer's view.
ubyte game_view_rotate_vector(vector *dest, vector *src);

// 1 if the object may be inside the game view's view cone, as obj_in_view_cone.
int game_view_in_cone(object *objp);

// Set OF_WAS_RENDERED on every object in the game view's view cone and clear it on the
// rest. obj_render_all used to do this while drawing.
void game_view_update_was_rendered();

#endif
