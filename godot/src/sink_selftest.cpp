#include "sink_selftest.h"

#include <cstdlib>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "selftest_scenes.h"

using namespace godot;

void Fs2SinkTest::_ready()
{
	out_dir = OS::get_singleton()->get_environment("FS2_SELFTEST_OUT");
	if (out_dir.is_empty()) {
		out_dir = ".";
	}
	measure = OS::get_singleton()->get_environment("FS2_SELFTEST_MEASURE") == "1";
	set_process(true);
}

void Fs2SinkTest::start_scene()
{
	const fs2sink::Scene &sc = *fs2sink::all_scenes[scene_index];
	screen = memnew(Fs2Screen);
	add_child(screen);
	suffix = "";
	if (OS::get_singleton()->get_environment("FS2_SELFTEST_PERSP") == "subdivide") {
		screen->sink_core().set_perspective_mode(fs2sink::PERSPECTIVE_SUBDIVIDE);
		suffix = "_subdivide";
	}
	if (measure) {
		RenderingServer::get_singleton()->viewport_set_measure_render_time(screen->get_viewport_rid(), true);
		gpu_sum = cpu_sum = 0.0;
		samples = 0;
		measure_left = 150;
	}
	const long tris0 = screen->sink_core().batch_stats().triangles_in;
	const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	sc.run(screen->get_sink());
	const uint64_t us = Time::get_singleton()->get_ticks_usec() - t0;
	const long tris = screen->sink_core().batch_stats().triangles_in - tris0;
	if (tris > 0) {
		UtilityFunctions::print("selftest ", sc.name, ": ", (int64_t)tris, " triangles in ", (int64_t)us, " us on the CPU = ", (double)us * 1000.0 / (double)tris, " ns per triangle");
	}
	wait = 3;	// let the viewport render the committed frame
	if (measure) { wait = 20; }	// then the sampling window below
}

void Fs2SinkTest::finish_scene()
{
	const fs2sink::Scene &sc = *fs2sink::all_scenes[scene_index];
	if (measure && samples > 0) {
		RenderingServer *rs = RenderingServer::get_singleton();
		UtilityFunctions::print("drawcost ", sc.name, suffix, ": GPU ", gpu_sum / samples, " ms, viewport CPU ", cpu_sum / samples, " ms per frame (mean of ", samples, " frames); draw calls ",
			(int64_t)rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME), ", primitives ",
			(int64_t)rs->get_rendering_info(RenderingServer::RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME));
	}
	Ref<Image> img = screen->get_image();
	if (img.is_null() || img->is_empty()) {
		UtilityFunctions::printerr("selftest ", sc.name, ": no image (a headless run has no renderer)");
		failures++;
	} else {
		if (img->get_width() != sc.w || img->get_height() != sc.h) {
			UtilityFunctions::printerr("selftest ", sc.name, ": viewport is ", img->get_width(), "x", img->get_height());
			failures++;
		}
		for (int i = 0; i < sc.nprobes; i++) {
			const fs2sink::Probe &p = sc.probes[i];
			Color c = img->get_pixel(p.x, p.y);
			const int r = (int)(c.r * 255.0f + 0.5f), g = (int)(c.g * 255.0f + 0.5f), b = (int)(c.b * 255.0f + 0.5f);
			const bool ok = std::abs(r - p.r) <= p.tol && std::abs(g - p.g) <= p.tol && std::abs(b - p.b) <= p.tol;
			if (!ok) {
				failures++;
				UtilityFunctions::printerr("selftest ", sc.name, " FAIL ", p.what, " at ", p.x, ",", p.y, ": got ", r, " ", g, " ", b,
					" want ", p.r, " ", p.g, " ", p.b);
			}
		}
		img->save_png(out_dir + "/selftest_" + sc.name + suffix + ".png");
		UtilityFunctions::print("selftest ", sc.name, ": ", sc.nprobes, " probes, PNG saved");
	}
	screen->queue_free();
	screen = nullptr;
}

void Fs2SinkTest::_process(double)
{
	if (wait > 0) {
		wait--;
		return;
	}
	if (measure_left > 0 && screen != nullptr) {
		RenderingServer *rs = RenderingServer::get_singleton();
		const double g = rs->viewport_get_measured_render_time_gpu(screen->get_viewport_rid());
		if (g > 0.0) {
			gpu_sum += g;
			cpu_sum += rs->viewport_get_measured_render_time_cpu(screen->get_viewport_rid());
			samples++;
		}
		measure_left--;
		return;
	}
	if (screen != nullptr) {
		finish_scene();
	}
	scene_index++;
	if (scene_index >= fs2sink::all_scenes_count) {
		UtilityFunctions::print(failures == 0 ? "selftest: PASS" : "selftest: FAIL");
		set_process(false);
		get_tree()->quit(failures == 0 ? 0 : 1);
		return;
	}
	start_scene();
}
