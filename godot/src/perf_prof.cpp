#include "perf_prof.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#endif

namespace fs2perf {

const char *const zone_names[Z_COUNT] = {
	"frame_begin", "frame_end", "clear", "set_clip", "tex_create", "tex_free", "capture", "read_region", "depth",
	"draw_tris", "emit_canvas", "mesh_build", "run_end", "layer_add", "flush",
};
const char *const counter_names[C_COUNT] = {
	"frame_begin", "frame_accepted", "draw_calls", "verts", "indices", "set_clip", "clear", "tex_create", "tex_bytes", "tex_free",
	"capture", "read_region", "read_bytes", "batches", "batch_verts", "canvas_items", "meshes", "layers", "layer_batches",
};

bool g_on = false;
Totals g_tot;
std::atomic<int> g_in_step{ 0 };
std::atomic<uint32_t> g_game_frame{ 0 };
Zone *Zone::top = nullptr;

// ---- recorder ---------------------------------------------------------------------------------------------------

static std::string join(const std::string &d, const std::string &f) { return d + "/" + f; }

bool Recorder::start(const std::string &out_dir, const std::string &name, int sample_hz)
{
	if (started) { return false; }
	dir = out_dir;
	tag = name;
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	game.clear();
	godot.clear();
	game.reserve(200000);
	godot.reserve(100000);
	g_tot = Totals();
	Zone::top = nullptr;
	t_start_ns = now_ns();
	started = true;
	g_on = true;
	if (sample_hz > 0) { sampler_start(sample_hz); }
	return true;
}

void Recorder::add_game(uint32_t frame, uint64_t t0_ns, uint64_t t1_ns, const Totals &before)
{
	GameRec r;
	r.frame = frame;
	r.start_ms = (double)(t0_ns - t_start_ns) / 1e6;
	r.total_us = (uint32_t)((t1_ns - t0_ns) / 1000);
	for (int i = 0; i < Z_COUNT; i++) { r.zone_excl_us[i] = (uint32_t)((g_tot.excl_ns[i] - before.excl_ns[i]) / 1000); }
	for (int i = 0; i < C_COUNT; i++) { r.counter[i] = (uint32_t)(g_tot.counter[i] - before.counter[i]); }
	game.push_back(r);
}

void Recorder::finish()
{
	if (!started) { return; }
	g_on = false;
	sampler_stop_and_write(join(dir, tag + "_samples.txt"), t_start_ns);
	if (FILE *f = fopen(join(dir, tag + "_game.csv").c_str(), "w")) {
		fprintf(f, "frame,start_ms,total_us");
		for (int i = 0; i < Z_COUNT; i++) { fprintf(f, ",z_%s", zone_names[i]); }
		for (int i = 0; i < C_COUNT; i++) { fprintf(f, ",c_%s", counter_names[i]); }
		fprintf(f, "\n");
		for (const GameRec &r : game) {
			fprintf(f, "%u,%.3f,%u", r.frame, r.start_ms, r.total_us);
			for (int i = 0; i < Z_COUNT; i++) { fprintf(f, ",%u", r.zone_excl_us[i]); }
			for (int i = 0; i < C_COUNT; i++) { fprintf(f, ",%u", r.counter[i]); }
			fprintf(f, "\n");
		}
		fclose(f);
	}
	if (FILE *f = fopen(join(dir, tag + "_godot.csv").c_str(), "w")) {
		fprintf(f, "start_ms,interval_us,step_us,sink_us,games,rs_draw_calls,rs_objects,rs_prims,vp_cpu_ms,vp_gpu_ms,tex_mem\n");
		for (const GodotRec &r : godot) {
			fprintf(f, "%.3f,%u,%u,%u,%d,%lld,%lld,%lld,%.4f,%.4f,%lld\n", r.start_ms, r.interval_us, r.step_us, r.sink_us, r.games,
				(long long)r.rs_draw_calls, (long long)r.rs_objects, (long long)r.rs_prims, r.vp_cpu_ms, r.vp_gpu_ms, (long long)r.tex_mem);
		}
		fclose(f);
	}
	if (FILE *f = fopen(join(dir, tag + "_totals.txt").c_str(), "w")) {
		for (int i = 0; i < Z_COUNT; i++) {
			fprintf(f, "zone %s calls %llu excl_ns %llu incl_ns %llu\n", zone_names[i], (unsigned long long)g_tot.zone_calls[i],
				(unsigned long long)g_tot.excl_ns[i], (unsigned long long)g_tot.incl_ns[i]);
		}
		for (int i = 0; i < C_COUNT; i++) { fprintf(f, "counter %s %llu\n", counter_names[i], (unsigned long long)g_tot.counter[i]); }
		fclose(f);
	}
	started = false;
}

// ---- stack sampler ----------------------------------------------------------------------------------------------

#ifdef _WIN32
namespace {
struct Sample {
	uint64_t ts;
	int step;
	uint32_t frame;
	int n;
	uint64_t pc[32];
};
std::vector<Sample> g_samples;
std::thread g_thread;
std::atomic<bool> g_run{ false };
HANDLE g_target = nullptr;
DWORD64 g_stack_base = 0;
int g_hz = 1000;
const size_t STACK_COPY = 96 * 1024;
unsigned char g_stack_buf[STACK_COPY + 64];

void sampler_main()
{
	HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	const LONGLONG period100 = 10000000LL / g_hz;
	g_samples.reserve(400000);
	while (g_run.load()) {
		if (timer != nullptr) {
			LARGE_INTEGER due;
			due.QuadPart = -period100;
			SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
			WaitForSingleObject(timer, 50);
		} else {
			Sleep(1);
		}
		if (!g_run.load()) { break; }
		CONTEXT ctx;
		memset(&ctx, 0, sizeof(ctx));
		ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		if (SuspendThread(g_target) == (DWORD)-1) { continue; }
		// nothing that allocates or locks between Suspend and Resume
		bool ok = GetThreadContext(g_target, &ctx) != 0;
		DWORD64 rsp0 = ctx.Rsp;
		size_t len = 0;
		int step = 0;
		uint32_t frame = 0;
		if (ok && rsp0 != 0 && g_stack_base > rsp0) {
			len = (size_t)(g_stack_base - rsp0);
			if (len > STACK_COPY) { len = STACK_COPY; }
			memcpy(g_stack_buf, (const void *)rsp0, len);
			step = g_in_step.load(std::memory_order_relaxed);
			frame = g_game_frame.load(std::memory_order_relaxed);
		}
		const uint64_t ts = now_ns();
		ResumeThread(g_target);
		if (!ok || len < 64) { continue; }
		Sample s;
		s.ts = ts;
		s.step = step;
		s.frame = frame;
		s.n = 0;
		const DWORD64 nb = (DWORD64)g_stack_buf;
		auto in_old = [&](DWORD64 v) { return v >= rsp0 && v < rsp0 + len; };
		auto rb = [&](DWORD64 &v) { if (in_old(v)) { v = v - rsp0 + nb; } };
		for (int depth = 0; depth < 32; depth++) {
			rb(ctx.Rsp); rb(ctx.Rbp); rb(ctx.Rbx); rb(ctx.Rdi); rb(ctx.Rsi);
			rb(ctx.R12); rb(ctx.R13); rb(ctx.R14); rb(ctx.R15);
			if (ctx.Rip == 0 || ctx.Rsp < nb || ctx.Rsp + 64 > nb + len) { break; }
			s.pc[s.n++] = ctx.Rip;
			DWORD64 base = 0;
			PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
			if (fe == nullptr) {
				ctx.Rip = *(DWORD64 *)ctx.Rsp;
				ctx.Rsp += 8;
			} else {
				PVOID hd = nullptr;
				DWORD64 frame_est = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fe, &ctx, &hd, &frame_est, nullptr);
			}
		}
		g_samples.push_back(s);
	}
	if (timer != nullptr) { CloseHandle(timer); }
}
} // namespace

bool sampler_start(int hz)
{
	if (g_run.load()) { return false; }
	HANDLE real = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &real, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
		return false;
	}
	g_target = real;
	g_stack_base = (DWORD64)((NT_TIB *)NtCurrentTeb())->StackBase;
	g_hz = hz < 10 ? 10 : (hz > 4000 ? 4000 : hz);
	g_samples.clear();
	g_run.store(true);
	g_thread = std::thread(sampler_main);
	return true;
}

void sampler_stop_and_write(const std::string &path, uint64_t t_start_ns)
{
	if (!g_run.load()) { return; }
	g_run.store(false);
	if (g_thread.joinable()) { g_thread.join(); }
	CloseHandle(g_target);
	g_target = nullptr;

	// symbols for the game DLL, from its PDB; other modules by name+rva
	HANDLE proc = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	char sp[1024] = "";
	{
		const char *e = getenv("FS2_PERF_SYMS");
		if (e != nullptr) { strncpy(sp, e, sizeof(sp) - 1); }
	}
	SymInitialize(proc, sp[0] ? sp : nullptr, FALSE);
	HMODULE mods[1024];
	DWORD need = 0;
	std::set<DWORD64> loaded;
	if (K32EnumProcessModules(proc, mods, sizeof(mods), &need)) {
		for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; i++) {
			char fn[MAX_PATH];
			if (!K32GetModuleFileNameExA(proc, mods[i], fn, MAX_PATH)) { continue; }
			std::string low(fn);
			std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)tolower(c); });
			if (low.find("fs2game") != std::string::npos || low.find("fs2godot") != std::string::npos) {
				MODULEINFO mi;
				K32GetModuleInformation(proc, mods[i], &mi, sizeof(mi));
				SymLoadModuleEx(proc, nullptr, fn, nullptr, (DWORD64)mods[i], mi.SizeOfImage, nullptr, 0);
			}
		}
	}
	std::map<uint64_t, std::string> names;
	auto resolve = [&](uint64_t pc) -> const std::string & {
		auto it = names.find(pc);
		if (it != names.end()) { return it->second; }
		char buf[sizeof(SYMBOL_INFO) + 512];
		SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
		memset(si, 0, sizeof(SYMBOL_INFO));
		si->SizeOfStruct = sizeof(SYMBOL_INFO);
		si->MaxNameLen = 500;
		DWORD64 disp = 0;
		std::string out;
		HMODULE hm = nullptr;
		char mn[MAX_PATH] = "?";
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pc, &hm)) {
			GetModuleFileNameA(hm, mn, MAX_PATH);
		}
		const char *slash = strrchr(mn, '\\');
		const char *mod = slash ? slash + 1 : mn;
		if (SymFromAddr(proc, pc, &disp, si)) {
			out = std::string(mod) + "!" + si->Name;
		} else {
			char t[64];
			snprintf(t, sizeof(t), "+0x%llx", (unsigned long long)(hm ? pc - (uint64_t)hm : pc));
			out = std::string(mod) + "!" + t;
		}
		return names.emplace(pc, out).first->second;
	};
	std::set<uint64_t> pcs;
	for (const Sample &s : g_samples) {
		for (int i = 0; i < s.n; i++) { pcs.insert(s.pc[i]); }
	}
	// symbols are per function: look up each pc, then collapse (the file maps pc -> name)
	if (FILE *f = fopen(path.c_str(), "w")) {
		for (uint64_t pc : pcs) {
			fprintf(f, "Y %llx %s\n", (unsigned long long)pc, resolve(pc).c_str());
		}
		for (const Sample &s : g_samples) {
			fprintf(f, "S %.3f %d %u", (double)(s.ts - t_start_ns) / 1e6, s.step, s.frame);
			for (int i = 0; i < s.n; i++) { fprintf(f, " %llx", (unsigned long long)s.pc[i]); }
			fprintf(f, "\n");
		}
		fclose(f);
	}
	SymCleanup(proc);
	g_samples.clear();
}
#else
bool sampler_start(int) { return false; }
void sampler_stop_and_write(const std::string &, uint64_t) {}
#endif

} // namespace fs2perf
