#include "fs2_node.h"
#include "fs2_screen.h"
#include "render_sink_godot.h"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void Fs2Game::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("start", "dll_path", "root_dir", "cmdline"), &Fs2Game::start);
	ClassDB::bind_method(D_METHOD("step"), &Fs2Game::step);
	ClassDB::bind_method(D_METHOD("is_running"), &Fs2Game::is_running);
	ClassDB::bind_method(D_METHOD("get_frame_count"), &Fs2Game::get_frame_count);
	ClassDB::bind_method(D_METHOD("set_frames_per_process", "n"), &Fs2Game::set_frames_per_process);
	ClassDB::bind_method(D_METHOD("get_frames_per_process"), &Fs2Game::get_frames_per_process);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frames_per_process", PROPERTY_HINT_RANGE, "1,1000"),
		"set_frames_per_process", "get_frames_per_process");
	ClassDB::bind_method(D_METHOD("set_fatal_return", "on"), &Fs2Game::set_fatal_return);
	ClassDB::bind_method(D_METHOD("get_fatal_return"), &Fs2Game::get_fatal_return);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fatal_return"), "set_fatal_return", "get_fatal_return");
	ClassDB::bind_method(D_METHOD("set_fiber_mode", "mode"), &Fs2Game::set_fiber_mode);
	ClassDB::bind_method(D_METHOD("get_fiber_mode"), &Fs2Game::get_fiber_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fiber_mode", PROPERTY_HINT_ENUM, "Off,Fiber,Stress"), "set_fiber_mode", "get_fiber_mode");
	ClassDB::bind_method(D_METHOD("set_forward_input", "on"), &Fs2Game::set_forward_input);
	ClassDB::bind_method(D_METHOD("get_forward_input"), &Fs2Game::get_forward_input);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "forward_input"), "set_forward_input", "get_forward_input");
	ClassDB::bind_method(D_METHOD("set_skip_stale_draws", "on"), &Fs2Game::set_skip_stale_draws);
	ClassDB::bind_method(D_METHOD("get_skip_stale_draws"), &Fs2Game::get_skip_stale_draws);
	ClassDB::bind_method(D_METHOD("set_pace_realtime", "on"), &Fs2Game::set_pace_realtime);
	ClassDB::bind_method(D_METHOD("get_pace_realtime"), &Fs2Game::get_pace_realtime);
	ClassDB::bind_method(D_METHOD("set_pace_hz", "hz"), &Fs2Game::set_pace_hz);
	ClassDB::bind_method(D_METHOD("get_stats"), &Fs2Game::get_stats);
	ClassDB::bind_method(D_METHOD("perf_start", "out_dir", "tag", "sample_hz"), &Fs2Game::perf_start);
	ClassDB::bind_method(D_METHOD("perf_finish"), &Fs2Game::perf_finish);
	ClassDB::bind_method(D_METHOD("set_screen", "screen"), &Fs2Game::set_screen);
	ClassDB::bind_method(D_METHOD("get_screen"), &Fs2Game::get_screen);
	ClassDB::bind_method(D_METHOD("on_focus_in"), &Fs2Game::on_focus_in);
	ClassDB::bind_method(D_METHOD("on_focus_out"), &Fs2Game::on_focus_out);
	ClassDB::bind_method(D_METHOD("on_close_requested"), &Fs2Game::on_close_requested);
	ADD_SIGNAL(MethodInfo("finished", PropertyInfo(Variant::INT, "exit_code")));
	ADD_SIGNAL(MethodInfo("fatal", PropertyInfo(Variant::STRING, "message")));
}

void Fs2Game::set_screen(Node *n)
{
	screen = (n != nullptr && Object::cast_to<Fs2Screen>(n) != nullptr) ? n : nullptr;
}

bool Fs2Game::start(const String &dll_path, const String &root_dir, const String &cmdline)
{
	if (running) {
		UtilityFunctions::printerr("Fs2Game: already running");
		return false;
	}
	if (api.module == NULL && !api.load(dll_path.utf8().get_data())) {
		UtilityFunctions::printerr("Fs2Game: cannot load ", dll_path);
		return false;
	}
	frame_count = 0;
	// G4: Godot fills the host hooks: time, events, mouse, joystick (replays take their values from the recording, not these)
	fs2_hooks hooks;
	fs2host::attach(&api);
	fs2host::fill(&hooks);
	if (api.set_input_script_sink != nullptr) {
		api.set_input_script_sink(fs2host::input_script_sink());
	}
	if (!api.set_hooks(&hooks)) {
		UtilityFunctions::printerr("Fs2Game: fs2_set_hooks refused the hook table");
		return false;
	}
	if (screen != nullptr) {
		if (!api.set_render_sink(static_cast<Fs2Screen *>(screen)->get_sink())) {
			UtilityFunctions::printerr("Fs2Game: fs2_set_render_sink refused the sink, the null renderer stays");
		}
	} else {
		api.set_render_sink(nullptr);
	}
	api.set_fatal_return(fatal_return ? 1 : 0);
	api.set_fiber(fiber_mode);
	if (api.set_host_pacing != nullptr) {
		api.set_host_pacing(pace_realtime ? 1 : 0);
	}
	// D-20261029-060953: interactive shown play gives DirectSound Godot's own window. A headless Godot has none, and a game started with
	// -headless or -nosounddevice has no sound device, so compared runs (gates, record, replay) keep the no-device side untouched.
	if (api.set_window_handle != nullptr) {
		const String cmd_padded = String(" ") + cmdline + " ";
		const bool no_device = cmd_padded.contains(" -nosounddevice ") || cmd_padded.contains(" -headless ");
		DisplayServer *ds = DisplayServer::get_singleton();
		if (ds != nullptr && ds->get_name() != "headless" && !no_device) {
			const int64_t hwnd = ds->window_get_native_handle(DisplayServer::WINDOW_HANDLE, 0);
			api.set_window_handle((unsigned long long)hwnd);
			UtilityFunctions::print("Fs2Game: sound window handle 0x", String::num_int64(hwnd, 16), " passed to the game (fs2_set_window_handle)");
		} else {
			api.set_window_handle(0);
		}
	}
	// D-20261030-063743: nebula fog state. gpu for live shown play only; every scripted, trace-compared or gate run (-replay, -autotest, -record,
	// -at_*) and every headless run keeps off, so no baseline moves. FS2_NEBFOG=off|same|lag|gpu overrides (gate C gpu runs, measurements).
	{
		const String cmd_padded = String(" ") + cmdline + " ";
		const bool scripted = cmd_padded.contains(" -replay ") || cmd_padded.contains(" -autotest ") || cmd_padded.contains(" -record ") ||
			cmd_padded.contains(" -at_") || cmd_padded.contains(" -headless ");
		DisplayServer *nds = DisplayServer::get_singleton();
		const bool shown = nds != nullptr && nds->get_name() != "headless" && screen != nullptr;
		fs2sink::g_nebfog = (!scripted && shown) ? fs2sink::kNebFogGpu : fs2sink::kNebFogOff;
		const String ov = OS::get_singleton()->get_environment("FS2_NEBFOG");
		if (ov == "off") { fs2sink::g_nebfog = fs2sink::kNebFogOff; } else if (ov == "same") { fs2sink::g_nebfog = fs2sink::kNebFogSame; } else if (ov == "lag") { fs2sink::g_nebfog = fs2sink::kNebFogLag; } else if (ov == "gpu") { fs2sink::g_nebfog = fs2sink::kNebFogGpu; }
		UtilityFunctions::print("Fs2Game: kNebFog ", fs2sink::g_nebfog == fs2sink::kNebFogGpu ? "gpu" : fs2sink::g_nebfog == fs2sink::kNebFogSame ? "same" : fs2sink::g_nebfog == fs2sink::kNebFogLag ? "lag" : "off");
	}
	fs2sink::g_opaque_reorder = OS::get_singleton()->get_environment("FS2_OPAQUE_REORDER") != "0";
	pace_acc_usec = 0;
	pace_last_usec = Time::get_singleton()->get_ticks_usec();
	if (!api.init(cmdline.utf8().get_data(), root_dir.utf8().get_data())) {
		UtilityFunctions::printerr("Fs2Game: fs2_init failed");
		return false;
	}
	running = true;
	return true;
}

bool Fs2Game::step()
{
	if (!running) {
		return false;
	}
	const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	int n = frames_per_process;
	if (pace_realtime) {
		pace_acc_usec += (double)(t0 - pace_last_usec);
		pace_last_usec = t0;
		const double period = 1000000.0 / pace_hz;
		n = (int)(pace_acc_usec / period);
		if (n > pace_max_catchup) {
			n = pace_max_catchup;
			pace_acc_usec = 0;
		} else {
			pace_acc_usec -= n * period;
		}
		if (n == 0) {
			return true;
		}
	}
	for (int i = 0; i < n; i++) {
		if (screen != nullptr && skip_stale_draws) {
			static_cast<Fs2Screen *>(screen)->set_frames_left(n - i);
		}
		int r;
		if (fs2perf::g_on) {
			fs2perf::g_game_frame.store((uint32_t)frame_count, std::memory_order_relaxed);
			const fs2perf::Totals before = fs2perf::g_tot;
			const uint64_t g0 = fs2perf::now_ns();
			fs2perf::g_in_step.store(1, std::memory_order_relaxed);
			r = api.frame();
			fs2perf::g_in_step.store(0, std::memory_order_relaxed);
			perf.add_game((uint32_t)frame_count, g0, fs2perf::now_ns(), before);
		} else {
			r = api.frame();
		}
		if (r < 0) {
			running = false;
			String message = String::utf8(api.last_error());
			api.shutdown();
			emit_signal("fatal", message);
			return false;
		}
		if (r == 0) {
			running = false;
			int code = api.shutdown();
			emit_signal("finished", code);
			return false;
		}
		frame_count++;
	}
	stat_last_step_usec = Time::get_singleton()->get_ticks_usec() - t0;
	stat_game_usec += stat_last_step_usec;
	stat_calls++;
	return true;
}

bool Fs2Game::perf_start(const String &out_dir, const String &tag, int sample_hz)
{
	if (!perf.start(out_dir.utf8().get_data(), tag.utf8().get_data(), sample_hz)) {
		return false;
	}
	if (screen != nullptr) {
		RenderingServer::get_singleton()->viewport_set_measure_render_time(static_cast<Fs2Screen *>(screen)->get_viewport_rid(), true);
	}
	perf_last_process_ns = 0;
	return true;
}

void Fs2Game::perf_finish()
{
	perf.finish();
}

void Fs2Game::_process(double)
{
	if (!running) {
		return;
	}
	if (!fs2perf::g_on) {
		step();
		return;
	}
	// per Godot frame record: the interval since the last call, what the game step cost, what the sink took, and what Godot reports for the frame before
	const uint64_t p0 = fs2perf::now_ns();
	const fs2perf::Totals before = fs2perf::g_tot;
	const int f0 = frame_count;
	step();
	const uint64_t p1 = fs2perf::now_ns();
	fs2perf::GodotRec r = {};
	r.start_ms = (double)(p0 - perf.t_start_ns) / 1e6;
	r.interval_us = perf_last_process_ns ? (uint32_t)((p0 - perf_last_process_ns) / 1000) : 0;
	perf_last_process_ns = p0;
	r.step_us = (uint32_t)((p1 - p0) / 1000);
	uint64_t sink = 0;
	for (int i = 0; i < fs2perf::Z_COUNT; i++) {
		sink += fs2perf::g_tot.excl_ns[i] - before.excl_ns[i];
	}
	r.sink_us = (uint32_t)(sink / 1000);
	r.games = frame_count - f0;
	RenderingServer *rs = RenderingServer::get_singleton();
	r.rs_draw_calls = rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME);
	r.rs_objects = rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME);
	r.rs_prims = rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME);
	r.tex_mem = rs->get_rendering_info(RenderingServer::RENDERING_INFO_TEXTURE_MEM_USED);
	if (screen != nullptr) {
		const RID vp = static_cast<Fs2Screen *>(screen)->get_viewport_rid();
		r.vp_cpu_ms = rs->viewport_get_measured_render_time_cpu(vp);
		r.vp_gpu_ms = rs->viewport_get_measured_render_time_gpu(vp);
	}
	perf.godot.push_back(r);
}

void Fs2Game::_ready()
{
	set_process_input(true);
	Window *w = get_window();
	if (w != NULL) {
		w->connect("focus_entered", Callable(this, "on_focus_in"));
		w->connect("focus_exited", Callable(this, "on_focus_out"));
		w->connect("close_requested", Callable(this, "on_close_requested"));
	}
}

void Fs2Game::_input(const Ref<InputEvent> &event)
{
	if (!forward_input || !running) {
		return;
	}
	Viewport *vp = get_viewport();
	Vector2 size = (vp != NULL) ? vp->get_visible_rect().size : Vector2(1, 1);
	fs2host::push_input(event, size.x, size.y);
}

Dictionary Fs2Game::get_stats() const
{
	Dictionary d;
	d["calls"] = (int64_t)stat_calls;
	d["game_frames"] = frame_count;
	d["game_usec"] = (int64_t)stat_game_usec;
	d["last_step_usec"] = (int64_t)stat_last_step_usec;
	if (screen != nullptr) {
		d["sink_usec"] = (int64_t)static_cast<Fs2Screen *>(screen)->sink_core().stat_sink_usec;
		d["sink_frames"] = static_cast<Fs2Screen *>(screen)->get_frames_committed();
	}
	d["input_consumed"] = (int64_t)fs2host::consumed_count();
	d["input_consumed_usec"] = (int64_t)fs2host::consumed_usec();
	return d;
}
