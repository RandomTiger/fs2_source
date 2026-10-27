#include "sink_selftest.h"

#include <cstdlib>

#include <godot_cpp/classes/os.hpp>
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
	set_process(true);
}

void Fs2SinkTest::start_scene()
{
	const fs2sink::Scene &sc = *fs2sink::all_scenes[scene_index];
	screen = memnew(Fs2Screen);
	add_child(screen);
	sc.run(screen->get_sink());
	wait = 3;	// let the viewport render the committed frame
}

void Fs2SinkTest::finish_scene()
{
	const fs2sink::Scene &sc = *fs2sink::all_scenes[scene_index];
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
		img->save_png(out_dir + "/selftest_" + sc.name + ".png");
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
