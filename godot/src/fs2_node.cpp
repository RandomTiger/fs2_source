#include "fs2_node.h"

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
	ADD_SIGNAL(MethodInfo("finished", PropertyInfo(Variant::INT, "exit_code")));
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
	for (int i = 0; i < frames_per_process; i++) {
		if (!api.frame()) {
			running = false;
			int code = api.shutdown();
			emit_signal("finished", code);
			return false;
		}
		frame_count++;
	}
	return true;
}

void Fs2Game::_process(double)
{
	if (running) {
		step();
	}
}
