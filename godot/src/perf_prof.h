// perf_prof.h  (performance pass, step 1: the profile)
// Measurement only, in the Godot wrapper. Nothing here is compiled into the game DLL, and gr_host / game logic never read the clock
// (D-20261027-190107, D-20261028-213350 b). Everything is off unless profiling was started (FS2_PERF_DIR in runner.gd);
// with it off a Zone is one branch and the compared (gate) runs are unchanged.
//
//  - Zones: time spent in the sink callbacks and their inner parts. Zones nest; each keeps exclusive and inclusive time.
//  - Counters: calls, vertices, textures and bytes uploaded, pixel reads, batches, canvas items, meshes, layers.
//  - Per game frame and per Godot frame records, written as CSV.
//  - A sampler thread: snapshots the game thread's stack at ~1 kHz (SuspendThread, copy of the stack top, resume at once; unwinding
//    and symbol lookup run afterwards), so time and spikes are attributed to game functions without a clock inside the game.
#ifndef FS2_PERF_PROF_H
#define FS2_PERF_PROF_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace fs2perf {

inline uint64_t now_ns()
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

enum ZoneId {
	Z_FRAME_BEGIN = 0,
	Z_FRAME_END,
	Z_CLEAR,
	Z_SET_CLIP,
	Z_TEX_CREATE,
	Z_TEX_FREE,
	Z_CAPTURE,
	Z_READ_REGION,
	Z_DEPTH,
	Z_DRAW_TRIS,
	Z_EMIT_CANVAS,
	Z_MESH_BUILD,
	Z_RUN_END,
	Z_LAYER_ADD,
	Z_FLUSH,
	Z_COUNT
};
extern const char *const zone_names[Z_COUNT];

enum CounterId {
	C_SINK_FRAME_BEGIN = 0,
	C_SINK_FRAME_ACCEPTED,
	C_SINK_DRAW_CALLS,
	C_SINK_VERTS,
	C_SINK_INDICES,
	C_SINK_SET_CLIP,
	C_SINK_CLEAR,
	C_TEX_CREATE,
	C_TEX_BYTES,
	C_TEX_FREE,
	C_CAPTURE,
	C_READ_REGION,
	C_READ_BYTES,
	C_BATCHES,
	C_BATCH_VERTS,
	C_CANVAS_ITEMS,
	C_MESHES,
	C_LAYERS,
	C_LAYER_BATCHES,
	C_COUNT
};
extern const char *const counter_names[C_COUNT];

struct Totals {
	uint64_t excl_ns[Z_COUNT] = {};
	uint64_t incl_ns[Z_COUNT] = {};
	uint64_t zone_calls[Z_COUNT] = {};
	uint64_t counter[C_COUNT] = {};
};

extern bool g_on;
extern Totals g_tot;
extern std::atomic<int> g_in_step;
extern std::atomic<uint32_t> g_game_frame;

struct Zone {
	int id = 0;
	bool live = false;
	uint64_t t0 = 0, child = 0;
	Zone *parent = nullptr;
	static Zone *top;
	explicit Zone(int z)
	{
		live = g_on;
		if (!live) { return; }
		id = z; parent = top; top = this;
		t0 = now_ns();
	}
	~Zone()
	{
		if (!live) { return; }
		const uint64_t dt = now_ns() - t0;
		g_tot.excl_ns[id] += dt - child;
		g_tot.incl_ns[id] += dt;
		g_tot.zone_calls[id]++;
		top = parent;
		if (parent) { parent->child += dt; }
	}
};

inline void count(int c, uint64_t n = 1) { if (g_on) { g_tot.counter[c] += n; } }

struct GameRec {
	uint32_t frame;
	double start_ms;
	uint32_t total_us;
	uint32_t zone_excl_us[Z_COUNT];
	uint32_t counter[C_COUNT];
};
struct GodotRec {
	double start_ms;
	uint32_t interval_us, step_us, sink_us;
	int games;
	int64_t rs_draw_calls, rs_objects, rs_prims;
	double vp_cpu_ms, vp_gpu_ms;
	int64_t tex_mem;
};

class Recorder {
public:
	std::vector<GameRec> game;
	std::vector<GodotRec> godot;
	uint64_t t_start_ns = 0;
	std::string dir, tag;

	bool start(const std::string &out_dir, const std::string &name, int sample_hz);
	void add_game(uint32_t frame, uint64_t t0_ns, uint64_t t1_ns, const Totals &before);
	void finish();
	bool active() const { return started; }
private:
	bool started = false;
};

bool sampler_start(int hz);
void sampler_stop_and_write(const std::string &path, uint64_t t_start_ns);

} // namespace fs2perf

#endif
