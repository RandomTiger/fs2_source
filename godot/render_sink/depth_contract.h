/* depth_contract.h  (P1, GodotRender3D)
 * The depth scale of the sink in one place, so legacy (CPU projected) draws and later mesh draws land on the same one.
 * Plain C++17 over render_sink.h: no Godot type. See godot/docs/P1_camera_depth_design.md.
 */
#ifndef FS2_DEPTH_CONTRACT_H
#define FS2_DEPTH_CONTRACT_H

#include <cmath>

#include "render_sink.h"

namespace fs2sink {

// Distance from the layer origin of a depth vertex: later clear epochs nearer, then the game's z (0..0.98 = eye depth / 30000),
// then the z bias. Moved out of GodotSink::add_to_layer unchanged (render_sink_godot.cpp).
inline float legacy_depth(float z, int epoch, int max_epoch, int zbias)
{
	return 2.0f + (float)(max_epoch - epoch) * 1100.0f + z * 1000.0f - (float)zbias * 0.05f;
}

// The eye space vertex (scaled eye space, as g3_rotate_vertex leaves it) of a projected one. The game's projection is
// sx = (W + x*W/z)/2, sy = (H - y*H/z)/2, so x = (2*sx/W - 1)*z and y = (1 - 2*sy/H)*z. z comes from the vertex depth
// (z_eye / 30000, capped 0.98: exact below 29400) or, where that is capped or absent, from 1/sw (sw is capped at 1: exact
// above 1 game unit). Returns 0 when neither is usable.
inline int reconstruct_eye(const fs2_sink_view &v, float sx, float sy, float sw, float zdepth, float out[3])
{
	float z;
	if (zdepth > 0.0f && zdepth < 0.98f) {
		z = zdepth * 30000.0f;
	} else if (sw > 0.0f && sw < 1.0f) {
		z = 1.0f / sw;
	} else {
		return 0;
	}
	out[0] = (2.0f * sx / (float)v.canvas_w - 1.0f) * z;
	out[1] = (1.0f - 2.0f * sy / (float)v.canvas_h) * z;
	out[2] = z;
	return 1;
}

} // namespace fs2sink

#endif
