// fs2_console.cpp
// See fs2_console.h.
#include "fs2_console.h"
#include "host_hooks_godot.h"

#include <vector>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/system_font.hpp>

using namespace godot;

namespace fs2console {

static const double Slide_seconds = 0.15;
static const int Answer_size = 2048;

static Fs2Api *Api = NULL;
static bool Enabled = false;
static Panel *Box = NULL;
static RichTextLabel *Output = NULL;
static LineEdit *Edit = NULL;
static Label *Fps = NULL;
static Ref<SystemFont> Mono;
static bool Open = false;
static bool Fps_on = false;
static double Slide = 0.0;	// 0 up and out of sight, 1 all the way down
static int Font_size = 0;
static std::vector<String> Pending;
static std::vector<String> History;
static int History_at = 0;

static void print_line(const String &text)
{
	Output->add_text(text + String("\n"));
}

static void show_help(void)
{
	print_line("help, usage           these commands");
	print_line("fps                   FPS display on or off");
	print_line("resolution            list the resolutions");
	print_line("resolution <letter>   use one from the list");
	print_line("resolution <height>   use one of that height");
	print_line("resolution <w>x<h>    try that size");
	print_line("unlock missions       unlock every mission in the current campaign");
	print_line("subtitles [on|off]    subtitles");
	print_line("big_hud [on|off]      the big HUD");
	print_line("show                  list what can be switched off, for testing");
	print_line("show <name> [on|off]  switch it; show all on or off");
}

static void submit(const String &raw)
{
	const String line = raw.strip_edges();
	if (line.is_empty()) {
		return;
	}
	History.push_back(line);
	History_at = (int)History.size();
	print_line("> " + line);

	const String low = line.to_lower();
	if (low == "help" || low == "usage") {
		show_help();
	} else if (low == "fps") {
		Fps_on = !Fps_on;
		Fps->set_visible(Fps_on);
		print_line(Fps_on ? "FPS on" : "FPS off");
	} else {
		Pending.push_back(line);	// the game's: run between frames
	}
}

static void set_open(bool on)
{
	Open = on;
	fs2host::console_cursor(on);
	if (on) {
		Box->set_visible(true);
		Edit->grab_focus();
	} else {
		Edit->release_focus();
	}
}

static void history_step(int by)
{
	const int n = (int)History.size();
	if (n == 0) {
		return;
	}
	int at = History_at + by;
	if (at < 0) {
		at = 0;
	}
	if (at >= n) {
		History_at = n;
		Edit->clear();
		return;
	}
	History_at = at;
	Edit->set_text(History[at]);
	Edit->set_caret_column(Edit->get_text().length());
}

void attach(Node *parent, Fs2Api *api, bool enabled)
{
	Api = api;
	Enabled = enabled && parent != NULL;
	if (!Enabled || Box != NULL) {
		return;
	}
	Mono.instantiate();
	PackedStringArray names;
	names.push_back("Consolas");
	names.push_back("Courier New");
	names.push_back("monospace");
	Mono->set_font_names(names);

	Ref<StyleBoxFlat> back;
	back.instantiate();
	back->set_bg_color(Color(0.0f, 0.03f, 0.08f, 0.92f));
	back->set_border_color(Color(0.45f, 0.6f, 0.8f, 1.0f));
	back->set_border_width(SIDE_BOTTOM, 2);

	Box = memnew(Panel);
	Box->add_theme_stylebox_override("panel", back);
	Box->set_z_index(100);
	Box->set_visible(false);
	parent->add_child(Box);

	Output = memnew(RichTextLabel);
	Output->set_use_bbcode(false);
	Output->set_scroll_follow(true);
	Output->set_focus_mode(Control::FOCUS_NONE);
	Output->add_theme_font_override("normal_font", Mono);
	Box->add_child(Output);

	Edit = memnew(LineEdit);
	Edit->set_placeholder("help for the commands");
	Edit->add_theme_font_override("font", Mono);
	Edit->set_keep_editing_on_text_submit(true);
	Box->add_child(Edit);

	Fps = memnew(Label);
	Fps->set_z_index(99);
	Fps->set_visible(false);
	Fps->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	Fps->add_theme_font_override("font", Mono);
	Fps->add_theme_color_override("font_color", Color(0.5f, 1.0f, 0.5f, 1.0f));
	Fps->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f, 1.0f));
	Fps->add_theme_constant_override("outline_size", 4);
	Fps->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	parent->add_child(Fps);
}

bool input(const Ref<InputEvent> &event, Viewport *viewport)
{
	if (!Enabled || Box == NULL) {
		return false;
	}
	InputEventKey *key = Object::cast_to<InputEventKey>(event.ptr());
	if (key != NULL && key->get_physical_keycode() == KEY_QUOTELEFT) {	// the key left of 1, whatever it prints on this keyboard
		if (key->is_pressed() && !key->is_echo()) {
			set_open(!Open);
		}
		if (viewport != NULL) {
			viewport->set_input_as_handled();
		}
		return true;
	}
	if (!Open) {
		return false;
	}
	if (key != NULL) {
		if (!key->is_pressed()) {
			return false;
		}
		bool ours = true;
		switch (key->get_keycode()) {
		case KEY_ENTER:
		case KEY_KP_ENTER:
			submit(Edit->get_text());
			Edit->clear();
			break;
		case KEY_UP:
			history_step(-1);
			break;
		case KEY_DOWN:
			history_step(1);
			break;
		case KEY_ESCAPE:
			set_open(false);
			break;
		default:
			ours = false;	// the line edit's, which Godot's GUI gives it after this
			break;
		}
		if (ours && viewport != NULL) {
			viewport->set_input_as_handled();
		}
		return true;
	}
	InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(event.ptr());
	return mb != NULL && mb->is_pressed();
}

void tick(double delta)
{
	if (!Enabled || Box == NULL) {
		return;
	}
	const Viewport *vp = Box->get_viewport();
	if (vp == NULL) {
		return;
	}
	const Vector2 view = vp->get_visible_rect().size;

	int fs = (int)(view.y * 0.022f + 0.5f);
	if (fs < 14) {
		fs = 14;
	}
	if (fs != Font_size) {
		Font_size = fs;
		Output->add_theme_font_size_override("normal_font_size", fs);
		Edit->add_theme_font_size_override("font_size", fs);
		Fps->add_theme_font_size_override("font_size", fs);
	}

	const double step = delta / Slide_seconds;
	Slide += Open ? step : -step;
	Slide = Slide < 0.0 ? 0.0 : (Slide > 1.0 ? 1.0 : Slide);
	if (!Open && Slide <= 0.0 && Box->is_visible()) {
		Box->set_visible(false);
	}

	const float h = view.y * 0.4f;
	const float margin = (float)fs * 0.5f;
	const float edit_h = (float)fs * 1.8f;
	Box->set_position(Vector2(0.0f, -h * (float)(1.0 - Slide)));
	Box->set_size(Vector2(view.x, h));
	Output->set_position(Vector2(margin, margin));
	Output->set_size(Vector2(view.x - margin * 2.0f, h - edit_h - margin * 3.0f));
	Edit->set_position(Vector2(margin, h - edit_h - margin));
	Edit->set_size(Vector2(view.x - margin * 2.0f, edit_h));
	if (Open && !Edit->has_focus()) {
		Edit->grab_focus();	// a click elsewhere took it
	}

	if (Fps_on) {
		Fps->set_position(Vector2(view.x - (float)fs * 8.0f - margin, margin));
		Fps->set_size(Vector2((float)fs * 8.0f, (float)fs * 1.5f));
		Fps->set_text(String::num_int64((int64_t)Engine::get_singleton()->get_frames_per_second()) + " FPS");
	}
}

void run_pending(void)
{
	if (Pending.empty()) {
		return;
	}
	for (size_t i = 0; i < Pending.size(); i++) {
		char answer[Answer_size];
		answer[0] = 0;
		const CharString text = Pending[i].utf8();
		if (Api != NULL && Api->console_command != NULL && Api->console_command(text.get_data(), answer, Answer_size)) {
			print_line(String::utf8(answer).strip_edges());
		} else {
			print_line("unknown command: help lists them");
		}
	}
	Pending.clear();
}

} // namespace fs2console
