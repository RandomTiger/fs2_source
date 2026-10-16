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

// The size of the game view's 3D view: what gr_screen.clip_width and clip_height are after
// game_set_view_clip, at the art resolution. Its clip region runs from 0,0 to w-1,h-1.
void game_view_size(int *w, int *h);

// The game view's full screen: the art resolution, 1024x768 (or 640x480), which is what gr_screen.max_w and max_h
// have always been.  game_view_project reports the same.
void game_view_screen_size(int *w, int *h);

// Rotate a world point into the game view and return its clip codes (CC_*), as
// g3_rotate_vector does for the renderer's view.
ubyte game_view_rotate_vector(vector *dest, vector *src);

// Project a world point into the game view as g3_rotate_vertex and g3_project_vertex do. Returns 0
// when the point is behind the eye (PF_OVERFLOW), else 1 with the screen position. max_w and max_h
// get the full screen size of the game view, which is what gr_screen.max_w and max_h have been.
int game_view_project(vector *src, float *sx, float *sy, int *max_w, int *max_h);

// 1 if g3_draw_laser would draw a laser from headp to tailp in the game view.
int game_view_laser_in_view(vector *headp, vector *tailp);

// 1 if the object may be inside the game view's view cone, as obj_in_view_cone.
int game_view_in_cone(object *objp);

// Set OF_WAS_RENDERED on every object in the game view's view cone and clear it on the
// rest. obj_render_all used to do this while drawing.
void game_view_update_was_rendered();

#endif
