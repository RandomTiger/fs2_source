#include "fs2_node.h"
#include "fs2_screen.h"

#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
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
		int r = api.frame();
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
	return true;
}

void Fs2Game::_process(double)
{
	if (running) {
		step();
	}
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
