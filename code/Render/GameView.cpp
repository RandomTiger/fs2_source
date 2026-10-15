// Claude: The game view: the camera gameplay sees, set once per frame in the logic phase.
// See GameView.h. The maths repeats g3_start_frame, g3_set_view_matrix, scale_matrix,
// g3_rotate_vector, g3_code_vector (3dSetup.cpp, 3dMath.cpp) and obj_in_view_cone
// (ObjectSort.cpp) operation for operation, so the results are bit-identical to what the
// renderer produced.

#include "GameView.h"
#include "3d.h"
#include "2d.h"
#include "object.h"
#include "systemvars.h"
#include "supernova.h"
#include "floating.h"

static matrix Game_view_matrix;		// scaled like View_matrix
static vector Game_view_position;

extern int View_percent;	// FreeSpace.cpp

// The game view's canvas: the art resolution, not the display, cut down the way
// game_set_view_clip (FreeSpace.cpp) cuts the 3D view, since g3_start_frame takes its
// aspect from that clip region: the letterbox when the player is dead or the supernova
// is at stage 2 or later, and the shrunken view below 100%.
static void game_view_canvas(int *w, int *h)
{
	int max_w, max_h;

	if ( gr_screen.res == GR_1024 ) {
		max_w = 1024;
		max_h = 768;
	} else {
		max_w = 640;
		max_h = 480;
	}

	*w = max_w;
	*h = max_h;

	if ((Game_mode & GM_DEAD) || (supernova_active() >= 2))	{
		int yborder = max_h/4;
		*h = max_h - yborder*2;
	} else if ( View_percent < 100 ) {
		// game_set_view_clip has already raised View_percent to at least 5 this frame
		float fp = i2fl(View_percent)/100.0f;
		int fi = fl2i(fl_sqrt(fp)*100.0f);
		if ( fi > 100 ) fi=100;

		int xborder = ( max_w*(100-fi) )/200;
		int yborder = ( max_h*(100-fi) )/200;
		*w = max_w - xborder*2;
		*h = max_h - yborder*2;
	}
}

void game_view_set(vector *eye_pos, matrix *eye_orient, float zoom)
{
	// what g3_set_view_matrix leaves behind for gameplay
	View_position = *eye_pos;
	Eye_matrix = *eye_orient;
	Eye_position = *eye_pos;

	// Window_scale as g3_start_frame computes it, for a 4:3 canvas at the art resolution
	int width, height;
	float s;
	float aspect = 1.0f;		// gr_screen.aspect, which gr_init always sets to 1
	vector window_scale, matrix_scale;

	game_view_canvas(&width, &height);
	s = aspect*(float)height/(float)width;
	if (s <= 0) {
		window_scale.x = s;
		window_scale.y = 1.0f;
	} else {
		window_scale.y = 1.0f / s;
		window_scale.x = 1.0f;
	}
	window_scale.z = 1.0f;

	// Matrix_scale and the scaled view matrix as scale_matrix computes them
	matrix_scale = window_scale;
	if (zoom <= 1.0) {
		matrix_scale.z = matrix_scale.z*zoom;
	} else {
		float zs = (float)1.0 / zoom;
		matrix_scale.x = matrix_scale.x*zs;
		matrix_scale.y = matrix_scale.y*zs;
	}

	Game_view_position = *eye_pos;
	Game_view_matrix = *eye_orient;
	vm_vec_scale(&Game_view_matrix.rvec, matrix_scale.x);
	vm_vec_scale(&Game_view_matrix.uvec, matrix_scale.y);
	vm_vec_scale(&Game_view_matrix.fvec, matrix_scale.z);
}

ubyte game_view_rotate_vector(vector *dest, vector *src)
{
	vector tempv;
	ubyte cc = 0;

	vm_vec_sub(&tempv, src, &Game_view_position);
	vm_vec_rotate(dest, &tempv, &Game_view_matrix);

	// g3_code_vector without the user clip plane, which g3_start_frame clears before the
	// renderer's view-cone test
	if (dest->x > dest->z)
		cc |= CC_OFF_RIGHT;
	if (dest->y > dest->z)
		cc |= CC_OFF_TOP;
	if (dest->x < -dest->z)
		cc |= CC_OFF_LEFT;
	if (dest->y < -dest->z)
		cc |= CC_OFF_BOT;
	if (dest->z < 0.0f)
		cc |= CC_BEHIND;

	return cc;
}

int game_view_in_cone(object *objp)
{
	static vector offsets[8] = {
		{ -1.0f, -1.0f, -1.0f },
		{ -1.0f, -1.0f,  1.0f },
		{ -1.0f,  1.0f, -1.0f },
		{ -1.0f,  1.0f,  1.0f },
		{  1.0f, -1.0f, -1.0f },
		{  1.0f, -1.0f,  1.0f },
		{  1.0f,  1.0f, -1.0f },
		{  1.0f,  1.0f,  1.0f }
	};
	int i;
	vector tmp, pt;
	ubyte codes;
	ubyte and_codes = 0xff;

	for (i=0; i<8; i++) {
		vm_vec_scale_add(&pt, &objp->pos, &offsets[i], objp->radius);
		codes = game_view_rotate_vector(&tmp, &pt);
		if ( !codes ) {
			return 1;		// this point is in
		}
		and_codes &= codes;
	}

	if (and_codes) {
		return 0;		// all points off screen
	}

	return 1;
}

void game_view_update_was_rendered()
{
	object *objp;
	int i;

	objp = Objects;
	for (i=0; i<=Highest_object_index; i++, objp++) {
		if ( (objp->type != OBJ_NONE) && (objp->flags & OF_RENDERS) ) {
			objp->flags &= ~OF_WAS_RENDERED;
			if ( game_view_in_cone(objp) ) {
				objp->flags |= OF_WAS_RENDERED;
			}
		}
	}
}
