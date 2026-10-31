// opaque_reorder.h  (P3a, GodotRender3D)
// Regroups the opaque, depth-written batches of a depth run by batch key. Plain C++17 over batcher.h, tested in sink_check.
#ifndef FS2_OPAQUE_REORDER_H
#define FS2_OPAQUE_REORDER_H

#include <utility>
#include <vector>

#include "batcher.h"

namespace fs2sink {

// P3a (D-20261031-120702): opaque, depth-written batches of one clear epoch may be reordered across textures, because the depth test
// makes their order irrelevant (equal-depth ties aside). Each such stretch is regrouped by batch key, groups in first-appearance order,
// and the batches of a group are joined. Alpha, additive and write-less batches keep their place and split the stretches.
inline bool reorderable(const Batch &b)
{
	return b.key.depth == 1 && b.key.blend == FS2_SINK_BLEND_OPAQUE && b.key.zmode == 2;
}

inline void join_batch(Batch &dst, const Batch &s)
{
	const int base = dst.nverts();
	dst.pos.insert(dst.pos.end(), s.pos.begin(), s.pos.end());
	dst.uv.insert(dst.uv.end(), s.uv.begin(), s.uv.end());
	dst.col.insert(dst.col.end(), s.col.begin(), s.col.end());
	dst.custom.insert(dst.custom.end(), s.custom.begin(), s.custom.end());
	dst.z.insert(dst.z.end(), s.z.begin(), s.z.end());
	dst.fogv.insert(dst.fogv.end(), s.fogv.begin(), s.fogv.end());
	for (int32_t i : s.idx) { dst.idx.push_back(base + i); }
}

inline void reorder_opaque_batches(std::vector<Batch> &v)
{
	std::vector<Batch> out;
	out.reserve(v.size());
	size_t i = 0;
	while (i < v.size()) {
		if (!reorderable(v[i])) { out.push_back(std::move(v[i])); i++; continue; }
		size_t j = i;
		while (j < v.size() && reorderable(v[j]) && v[j].key.epoch == v[i].key.epoch) { j++; }
		const size_t first_out = out.size();
		for (size_t k = i; k < j; k++) {
			Batch *g = nullptr;
			for (size_t o = out.size(); o > first_out; o--) {
				if (out[o - 1].key == v[k].key && out[o - 1].nverts() + v[k].nverts() <= TriBatcher::MAX_VERTS) { g = &out[o - 1]; break; }
			}
			if (g != nullptr) { join_batch(*g, v[k]); } else { out.push_back(std::move(v[k])); }
		}
		i = j;
	}
	v.swap(out);
}

} // namespace fs2sink

#endif
