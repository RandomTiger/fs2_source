// host_hooks_godot.cpp
// G4: see host_hooks_godot.h.
#include "host_hooks_godot.h"

#include <deque>
#include <math.h>
#include <string.h>

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall ShowCursor(int show);	// user32; windows.h clashes with godot-cpp's names
#endif

using namespace godot;

namespace fs2host {

// the game's key codes (code/Io/Key.h; the DirectInput scan code, +0x80 for an extended key)
enum {
	K_ESC = 0x01, K_BACKSP = 0x0E, K_TAB = 0x0F, K_ENTER = 0x1C, K_SPACE = 0x39,
	K_LCTRL = 0x1D, K_RCTRL = 0x9D, K_LSHIFT = 0x2A, K_LALT = 0x38,
	K_MINUS = 0x0C, K_EQUAL = 0x0D, K_LBRACKET = 0x1A, K_RBRACKET = 0x1B, K_BACKSLASH = 0x2B, K_SEMICOL = 0x27,
	K_APOSTROPHE = 0x28, K_GRAVE = 0x29, K_COMMA = 0x33, K_PERIOD = 0x34, K_SLASH = 0x35,
	K_CAPSLOCK = 0x3A, K_NUMLOCK = 0x45, K_SCROLLOCK = 0x46, K_PAUSE = 0x45, K_PRINT_SCRN = 0xB7,
	K_F1 = 0x3B, K_F11 = 0x57, K_F12 = 0x58,
	K_PADMINUS = 0x4A, K_PADPLUS = 0x4E, K_PADPERIOD = 0x53, K_PADDIVIDE = 0xB5, K_PADMULTIPLY = 0x37, K_PADENTER = 0x9C,
	K_INSERT = 0xD2, K_HOME = 0xC7, K_PAGEUP = 0xC9, K_DELETE = 0xD3, K_END = 0xCF, K_PAGEDOWN = 0xD1,
	K_UP = 0xC8, K_DOWN = 0xD0, K_LEFT = 0xCB, K_RIGHT = 0xCD,
};

// KEY_A..KEY_Z to the game's codes, in alphabetical order
static const unsigned char Letter_codes[26] = {
	0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,	// A..M
	0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,	// N..Z
};
// KEY_0..KEY_9
static const unsigned char Digit_codes[10] = { 0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A };
// KEY_KP_0..KEY_KP_9
static const unsigned char Pad_codes[10] = { 0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49 };

unsigned int key_code(int k, int location)
{
	if ((k >= KEY_A) && (k <= KEY_Z)) {
		return Letter_codes[k - KEY_A];
	}
	if ((k >= KEY_0) && (k <= KEY_9)) {
		return Digit_codes[k - KEY_0];
	}
	if ((k >= KEY_KP_0) && (k <= KEY_KP_9)) {
		return Pad_codes[k - KEY_KP_0];
	}
	if ((k >= KEY_F1) && (k <= KEY_F10)) {
		return K_F1 + (k - KEY_F1);
	}
	switch (k) {
	case KEY_F11:		return K_F11;
	case KEY_F12:		return K_F12;
	case KEY_ESCAPE:	return K_ESC;
	case KEY_BACKSPACE:	return K_BACKSP;
	case KEY_TAB:		return K_TAB;
	case KEY_ENTER:		return K_ENTER;
	case KEY_SPACE:		return K_SPACE;
	// the game folds right shift and right alt into the left ones (the Windows handler does the same)
	case KEY_SHIFT:		return K_LSHIFT;
	case KEY_ALT:		return K_LALT;
	case KEY_CTRL:		return (location == KEY_LOCATION_RIGHT) ? K_RCTRL : K_LCTRL;
	case KEY_MINUS:		return K_MINUS;
	case KEY_EQUAL:		return K_EQUAL;
	case KEY_BRACKETLEFT:	return K_LBRACKET;
	case KEY_BRACKETRIGHT:	return K_RBRACKET;
	case KEY_BACKSLASH:	return K_BACKSLASH;
	case KEY_SEMICOLON:	return K_SEMICOL;
	case KEY_APOSTROPHE:	return K_APOSTROPHE;
	case KEY_QUOTELEFT:	return K_GRAVE;
	case KEY_COMMA:		return K_COMMA;
	case KEY_PERIOD:	return K_PERIOD;
	case KEY_SLASH:		return K_SLASH;
	case KEY_CAPSLOCK:	return K_CAPSLOCK;
	case KEY_NUMLOCK:	return K_NUMLOCK;
	case KEY_SCROLLLOCK:	return K_SCROLLOCK;
	case KEY_PAUSE:		return K_PAUSE;
	case KEY_PRINT:		return K_PRINT_SCRN;
	case KEY_KP_SUBTRACT:	return K_PADMINUS;
	case KEY_KP_ADD:	return K_PADPLUS;
	case KEY_KP_PERIOD:	return K_PADPERIOD;
	case KEY_KP_DIVIDE:	return K_PADDIVIDE;
	case KEY_KP_MULTIPLY:	return K_PADMULTIPLY;
	case KEY_KP_ENTER:	return K_PADENTER;
	case KEY_INSERT:	return K_INSERT;
	case KEY_HOME:		return K_HOME;
	case KEY_PAGEUP:	return K_PAGEUP;
	case KEY_DELETE:	return K_DELETE;
	case KEY_END:		return K_END;
	case KEY_PAGEDOWN:	return K_PAGEDOWN;
	case KEY_UP:		return K_UP;
	case KEY_DOWN:		return K_DOWN;
	case KEY_LEFT:		return K_LEFT;
	case KEY_RIGHT:		return K_RIGHT;
	}
	return 0;
}

// ---- the queue ----
enum EventType { EV_KEY, EV_MOUSE_BUTTON, EV_MOUSE_MOVE, EV_FOCUS, EV_CLOSE };

struct Event {
	EventType	type;
	unsigned int	a;		// key code | FS2_MOUSE_* bit
	int		b;		// state | set | got
	float		nx, ny;		// EV_MOUSE_MOVE: the position as a fraction of the window
	uint64_t	at_ms;		// when it was queued, for the key latency
};

static const size_t Queue_cap = 4096;	// nothing drains it before the game runs; the oldest go

static Fs2Api *Api = NULL;
static std::deque<Event> Queue;
static bool Foreground = true;
static int Mouse_x = 0, Mouse_y = 0;	// the cursor in the game's screen, as of the events the game has taken
static float View_w = 1.0f, View_h = 1.0f;	// the Godot viewport the last mouse event was relative to
static int Map_last[8];			// GodotOptions: the mapping the mouse was last mapped with (remap_mouse_on_change)
static bool Map_last_set = false;

static bool is_headless_display()
{
	DisplayServer *ds = DisplayServer::get_singleton();
	return (ds == NULL) || (ds->get_name() == String("headless"));
}

void attach(Fs2Api *api)
{
	Api = api;
	reset();
}

void reset()
{
	Queue.clear();
	Mouse_x = Mouse_y = 0;
	Map_last_set = false;
	// a headless Godot has no window to be in front, and the game must see itself in front as it did with the headless hooks
	Foreground = true;
	if (!is_headless_display()) {
		Foreground = DisplayServer::get_singleton()->window_is_focused(0);
		// retail hides the OS cursor (ShowCursor(0)); the game draws its own at the flip (gr_host h_flip)
		Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_HIDDEN);
	}
}

static void queue(const Event &e)
{
	if (Queue.size() >= Queue_cap) {
		Queue.pop_front();
	}
	Queue.push_back(e);
}

void push_input(const Ref<InputEvent> &ev, float vw, float vh)
{
	Event e;
	memset(&e, 0, sizeof(e));
	e.at_ms = Time::get_singleton()->get_ticks_msec();
	if (vw > 0 && vh > 0) {
		View_w = vw;
		View_h = vh;
	}

	InputEventKey *key = Object::cast_to<InputEventKey>(ev.ptr());
	if (key != NULL) {
		int k = (int)key->get_physical_keycode();
		if (k == 0) {
			k = (int)key->get_keycode();
		}
		unsigned int code = key_code(k, (int)key->get_location());
		if (code == 0) {
			return;
		}
		e.type = EV_KEY;
		e.a = code;
		e.b = key->is_pressed() ? 1 : 0;	// an auto-repeat is a key down again, as Windows sends it
		queue(e);
		return;
	}
	InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(ev.ptr());
	if (mb != NULL) {
		unsigned int bit = 0;
		switch (mb->get_button_index()) {
		case MOUSE_BUTTON_LEFT:		bit = FS2_MOUSE_LEFT; break;
		case MOUSE_BUTTON_RIGHT:	bit = FS2_MOUSE_RIGHT; break;
		case MOUSE_BUTTON_MIDDLE:	bit = FS2_MOUSE_MIDDLE; break;
		default: return;	// the wheel and the extra buttons: the game has none
		}
		// the button's own position first, so a click lands where the cursor is
		e.type = EV_MOUSE_MOVE;
		e.nx = mb->get_position().x / View_w;
		e.ny = mb->get_position().y / View_h;
		queue(e);
		e.type = EV_MOUSE_BUTTON;
		e.a = bit;
		e.b = mb->is_pressed() ? 1 : 0;
		queue(e);
		return;
	}
	InputEventMouseMotion *mm = Object::cast_to<InputEventMouseMotion>(ev.ptr());
	if (mm != NULL) {
		e.type = EV_MOUSE_MOVE;
		e.nx = mm->get_position().x / View_w;
		e.ny = mm->get_position().y / View_h;
		queue(e);
	}
}

void push_focus(bool got)
{
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EV_FOCUS;
	e.b = got ? 1 : 0;
	e.at_ms = Time::get_singleton()->get_ticks_msec();
	queue(e);
}

void push_close()
{
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EV_CLOSE;
	e.at_ms = Time::get_singleton()->get_ticks_msec();
	queue(e);
}

// ---- the hooks ----
// R1-feel: when the game last took an event from the queue (measurement only; nothing the game sees)
static unsigned long long Consumed_count = 0;
static long long Consumed_usec = 0;
unsigned long long consumed_count(void) { return Consumed_count; }
long long consumed_usec(void) { return Consumed_usec; }

static long long h_time_counter(void)
{
	return (long long)Time::get_singleton()->get_ticks_usec();
}

static long long h_time_frequency(void)
{
	return 1000000LL;
}

static void game_screen(int *w, int *h)
{
	Api->screen_size(w, h);
	if (*w <= 0 || *h <= 0) {	// before the game has a screen: the reference configuration
		*w = 1024;
		*h = 768;
	}
}

// GodotResolution: the canvas the game draws (fs2_canvas_info, v8) and the base screen's corner in it. The canvas is the base screen
// unless the display mode widens it to the window's aspect.
static void game_canvas(int *vw, int *vh, int *cx, int *cy)
{
	int gw, gh;
	game_screen(&gw, &gh);
	Api->canvas_info(vw, vh, cx, cy);
	if (*vw < gw || *vh < gh) {
		*vw = gw;
		*vh = gh;
		*cx = 0;
		*cy = 0;
	}
}

// The game's screen sits centred in the Godot viewport at its own aspect (Fs2Screen: TextureRect keep-aspect), so a viewport of another
// aspect (a 16:9 fullscreen, a resized window) has bars. fit_rect gives the picture's rectangle in viewport pixels.
static void fit_rect(int gw, int gh, float *ox, float *oy, float *scale)
{
	float s = View_w / (float)gw;
	const float sy = View_h / (float)gh;
	if (sy < s) {
		s = sy;
	}
	*scale = s;
	*ox = (View_w - gw * s) * 0.5f;
	*oy = (View_h - gh * s) * 0.5f;
}

// GodotResolution: window -> canvas pixels -> base screen pixels (the game's mouse, also its flight deltas, stays in base pixels).
// px, py: viewport pixels. A position over a bar lands on the picture's edge.
static void map_mouse(float px, float py, int gw, int gh, int cw, int ch, int cx, int cy)
{
	float ox, oy, sc;
	fit_rect(cw, ch, &ox, &oy, &sc);
	const int mx = (int)((px - ox) / sc) - cx;
	const int my = (int)((py - oy) / sc) - cy;
	Mouse_x = mx < 0 ? 0 : (mx > gw - 1 ? gw - 1 : mx);
	Mouse_y = my < 0 ? 0 : (my > gh - 1 ? gh - 1 : my);
}

// GodotOptions: the window -> base pixel mapping changes under a still mouse when the GR mode switches live (another base screen and
// canvas) or the window is resized (a Resolution pick). No mouse event follows, so the game's cursor stayed in the old mapping's
// pixels, drawn away from the pointer until the mouse moved. On a change, read the pointer again and map it the new way.
static void remap_mouse_on_change(int gw, int gh, int cw, int ch, int cx, int cy)
{
	if (is_headless_display()) {
		return;
	}
	DisplayServer *ds = DisplayServer::get_singleton();
	const Vector2i ws = ds->window_get_size(0);
	const int now[8] = { gw, gh, cw, ch, cx, cy, ws.x, ws.y };
	const bool changed = Map_last_set && (memcmp(now, Map_last, sizeof(now)) != 0);
	memcpy(Map_last, now, sizeof(now));
	Map_last_set = true;
	if (!changed || ws.x <= 0 || ws.y <= 0) {
		return;
	}
	View_w = (float)ws.x;
	View_h = (float)ws.y;
	const Vector2i p = ds->mouse_get_position() - ds->window_get_position(0);
	map_mouse((float)p.x, (float)p.y, gw, gh, cw, ch, cx, cy);
}

// ---- GodotOptions: the game's drop-down (fs2_set_dropdown_hooks, v9) as Godot's own OptionButton ----
// The game gives the rect in its base screen pixels; it is mapped through the canvas and the keep-aspect fit the picture uses
// (game_canvas, fit_rect), every poll, so the button sits on the game's row at any window size and in either GR mode.
static OptionButton *Dd_button = NULL;
static int Dd_x = 0, Dd_y = 0, Dd_w = 0, Dd_h = 0;
static int Dd_selected = -1;		// the entry the game has lit; a different selection on the button is the player's pick
static bool Console_cursor = false;	// the console is open
static bool Dd_cursor_shown = false;	// the OS cursor is shown over the button and its list (the game's own is drawn under them)
static Button *Side_button = NULL;	// GodotOptions (v11): the button in the black column, bottom left of the window
static bool Side_held = false;		// it was held down at the last poll

static Ref<StyleBoxFlat> dd_box(const Color &bg, const Color &border)
{
	Ref<StyleBoxFlat> b;
	b.instantiate();
	b->set_bg_color(bg);
	b->set_border_color(border);
	b->set_border_width_all(1);
	b->set_content_margin_all(4);
	return b;
}

// plain, in the game's colours: near-black boxes, a green edge, grey-white text that goes white (on dark green) under the cursor
static void dd_style_button(Button *b)
{
	const Color bg(0.02f, 0.03f, 0.02f), edge(0.18f, 0.45f, 0.18f), edge_lit(0.35f, 0.8f, 0.35f), lit(0.05f, 0.25f, 0.05f);
	const Color text(0.78f, 0.78f, 0.78f), text_lit(1.0f, 1.0f, 1.0f);
	b->add_theme_stylebox_override("normal", dd_box(bg, edge));
	b->add_theme_stylebox_override("hover", dd_box(bg, edge_lit));
	b->add_theme_stylebox_override("pressed", dd_box(lit, edge_lit));
	b->add_theme_stylebox_override("hover_pressed", dd_box(lit, edge_lit));
	b->add_theme_stylebox_override("focus", dd_box(Color(0, 0, 0, 0), Color(0, 0, 0, 0)));
	b->add_theme_color_override("font_color", text);
	b->add_theme_color_override("font_hover_color", text_lit);
	b->add_theme_color_override("font_pressed_color", text_lit);
	b->add_theme_color_override("font_hover_pressed_color", text_lit);
	b->add_theme_color_override("font_focus_color", text);
}

static void dd_style(OptionButton *b)
{
	const Color bg(0.02f, 0.03f, 0.02f), lit(0.05f, 0.25f, 0.05f), edge(0.18f, 0.45f, 0.18f);
	const Color text(0.78f, 0.78f, 0.78f), text_lit(1.0f, 1.0f, 1.0f);
	dd_style_button(b);
	PopupMenu *p = b->get_popup();
	p->add_theme_stylebox_override("panel", dd_box(bg, edge));
	p->add_theme_stylebox_override("hover", dd_box(lit, lit));
	p->add_theme_color_override("font_color", text);
	p->add_theme_color_override("font_hover_color", text_lit);
}

void attach_dropdown(Node *parent)
{
	if (parent == NULL || is_headless_display()) {
		return;
	}
	if (Dd_button == NULL) {
		Dd_button = memnew(OptionButton);
		Dd_button->set_focus_mode(Control::FOCUS_NONE);	// the game's keys never drive it; the open list takes its own
		dd_style(Dd_button);
		parent->add_child(Dd_button);	// after the screen's picture, so drawn over it
	}
	Dd_button->set_visible(false);
	Dd_selected = -1;
	if (Side_button == NULL) {
		Side_button = memnew(Button);
		Side_button->set_focus_mode(Control::FOCUS_NONE);	// the game's keys never drive it
		dd_style_button(Side_button);
		parent->add_child(Side_button);
	}
	Side_button->set_visible(false);
	Side_held = false;
}

static bool dd_shown(void)
{
	return Dd_button != NULL && Dd_button->is_visible();
}

static bool dd_list_open(void)
{
	return dd_shown() && Dd_button->get_popup()->is_visible();
}

// the button on the game's row, its text sized with it
static void dd_layout(void)
{
	if (!dd_shown()) {
		return;
	}
	Viewport *vp = Dd_button->get_viewport();
	if (vp == NULL) {
		return;
	}
	const Vector2 vs = vp->get_visible_rect().size;
	int cw, ch, cx, cy;
	game_canvas(&cw, &ch, &cx, &cy);
	if (cw <= 0 || ch <= 0) {
		return;
	}
	float s = vs.x / (float)cw;
	if (vs.y / (float)ch < s) {
		s = vs.y / (float)ch;
	}
	const float ox = (vs.x - (float)cw * s) * 0.5f, oy = (vs.y - (float)ch * s) * 0.5f;
	// the text follows the picture's height, not the art mode: the menu art fills the same height at 1024 and 640, while the row
	// (FONT1, the same pixel font in both modes) is a third shorter at 1024. 2% of the height reads like the menu labels; the box
	// grows round it where the row is shorter, centred on the row.
	int fs = (int)((float)ch * s * 0.02f + 0.5f);
	if (fs < 12) {
		fs = 12;
	}
	float bh = (float)Dd_h * s;
	const float need = (float)fs * 1.5f;
	const float by = oy + (float)(Dd_y + cy) * s - (bh < need ? (need - bh) * 0.5f : 0.0f);
	if (bh < need) {
		bh = need;
	}
	Dd_button->set_position(Vector2(ox + (float)(Dd_x + cx) * s, by));
	Dd_button->set_size(Vector2((float)Dd_w * s, bh));
	Dd_button->add_theme_font_size_override("font_size", fs);
	Dd_button->get_popup()->add_theme_font_size_override("font_size", fs);
}

// ---- GodotOptions: the side button (v11). The base screen's rectangle in viewport pixels: the column is what lies left of it. ----
static void base_rect(Viewport *vp, float *left, float *top, float *right, float *bottom)
{
	const Vector2 vs = vp->get_visible_rect().size;
	int gw, gh, cw, ch, cx, cy;
	game_screen(&gw, &gh);
	game_canvas(&cw, &ch, &cx, &cy);
	float s = 1.0f;
	if (cw > 0 && ch > 0) {
		s = vs.x / (float)cw;
		if (vs.y / (float)ch < s) {
			s = vs.y / (float)ch;
		}
	}
	const float ox = (vs.x - (float)cw * s) * 0.5f, oy = (vs.y - (float)ch * s) * 0.5f;
	*left = ox + (float)cx * s;
	*top = oy + (float)cy * s;
	*right = *left + (float)gw * s;
	*bottom = *top + (float)gh * s;
}

static bool side_shown(void)
{
	return Side_button != NULL && Side_button->is_visible();
}

// the word sized like the drop-down's, the margin from the window's corner. With no column to sit in (a picture that fills the
// window) it sits over the picture's corner, above the line a data set's copyright can reach down to.
static void side_layout(void)
{
	Viewport *vp = Side_button->get_viewport();
	if (vp == NULL) {
		return;
	}
	float left, top, right, bottom;
	base_rect(vp, &left, &top, &right, &bottom);
	const float margin = 16.0f;
	const float picture_h = bottom - top;
	int fs = (int)(picture_h * 0.02f + 0.5f);
	if (fs < 12) {
		fs = 12;
	}
	Side_button->add_theme_font_size_override("font_size", fs);
	Side_button->reset_size();
	const Vector2 size = Side_button->get_size();
	float y = vp->get_visible_rect().size.y - size.y - margin;
	if (left < size.x + margin * 2.0f) {
		y -= picture_h * 18.0f / 480.0f;
	}
	Side_button->set_position(Vector2(margin, y));
}

static void h_side_button(const char *text)
{
	if (Side_button == NULL) {
		return;
	}
	Side_held = false;
	if (text == NULL) {
		Side_button->set_visible(false);
		return;
	}
	Side_button->set_text(String::utf8(text));
	Side_button->set_visible(true);
	side_layout();
}

// once per poll while it shows: follow the window, hand over a click. True when the OS cursor should show: the game's own cannot
// leave the picture, so outside it the player needs this one to reach the button.
static bool side_poll(void)
{
	if (!side_shown()) {
		return false;
	}
	side_layout();
	const bool held = Side_button->is_pressed();
	if (Side_held && !held && Side_button->is_hovered() && Api->input_side_button != NULL) {
		Api->input_side_button();
	}
	Side_held = held;
	Viewport *vp = Side_button->get_viewport();
	if (vp == NULL) {
		return false;
	}
	float left, top, right, bottom;
	base_rect(vp, &left, &top, &right, &bottom);
	const Vector2 m = vp->get_mouse_position();
	return m.x < left || m.x >= right || m.y < top || m.y >= bottom || Side_button->is_hovered();
}

static void h_dd_show(int x, int y, int w, int h, const char *const *labels, int count, int selected)
{
	if (Dd_button == NULL) {
		return;
	}
	Dd_x = x;
	Dd_y = y;
	Dd_w = w;
	Dd_h = h;
	Dd_button->clear();
	for (int i = 0; i < count; i++) {
		Dd_button->add_item(String::utf8(labels[i]), i);
	}
	Dd_selected = (selected >= 0 && selected < count) ? selected : -1;
	Dd_button->select(Dd_selected);
	Dd_button->set_visible(true);
	dd_layout();
}

static void dd_cursor(bool shown)
{
	if (shown != Dd_cursor_shown && !is_headless_display()) {
		Dd_cursor_shown = shown;
#ifdef _WIN32
		// The game's os_init called ShowCursor(0) on this thread, so the display count hides the cursor whatever Godot's mode says.
		if (shown) {
			while (ShowCursor(1) < 0) {
			}
		} else {
			while (ShowCursor(0) >= 0) {
			}
		}
#endif
		Input::get_singleton()->set_mouse_mode(shown ? Input::MOUSE_MODE_VISIBLE : Input::MOUSE_MODE_HIDDEN);
	}
}

static void h_dd_hide(void)
{
	if (Dd_button != NULL) {
		Dd_button->get_popup()->hide();
		Dd_button->set_visible(false);
	}
	Dd_selected = -1;
	dd_cursor(false);
}

// once per poll: follow the window, hand over the player's pick, show the OS cursor where the game's is covered
static void dd_poll(void)
{
	if (!dd_shown()) {
		const bool side = side_poll();
		dd_cursor(side || Console_cursor);
		return;
	}
	dd_layout();
	const int sel = Dd_button->get_selected();
	if (sel >= 0 && sel != Dd_selected) {
		Dd_selected = sel;
		Api->input_dropdown(sel);
	}
	Viewport *vp = Dd_button->get_viewport();
	const bool over = vp != NULL && Dd_button->get_global_rect().has_point(vp->get_mouse_position());
	dd_cursor(over || dd_list_open() || Console_cursor);
}

void fill_dropdown(fs2_dropdown_hooks *h)
{
	h->show = h_dd_show;
	h->hide = h_dd_hide;
	h->side_button = h_side_button;
}

void console_cursor(bool on)
{
	Console_cursor = on;
}

void hide_dropdown(void)
{
	h_dd_hide();
	h_side_button(NULL);
}

bool dropdown_takes(const Ref<InputEvent> &ev)
{
	if (side_shown()) {
		InputEventMouseButton *side_mb = Object::cast_to<InputEventMouseButton>(ev.ptr());
		if (side_mb != NULL && (Side_held || Side_button->get_global_rect().has_point(side_mb->get_position()))) {
			return true;
		}
	}
	if (!dd_shown()) {
		return false;
	}
	if (dd_list_open()) {	// the open list owns every key and click (Godot hands most to it before this)
		return Object::cast_to<InputEventMouseMotion>(ev.ptr()) == NULL;
	}
	InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(ev.ptr());
	return mb != NULL && Dd_button->get_global_rect().has_point(mb->get_position());
}

// the only place the game takes the host's events
static void h_poll_events(void)
{
	uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (Api->input_dropdown != NULL) {
		dd_poll();
	}
	int gw, gh;
	game_screen(&gw, &gh);
	int cw, ch, cx, cy;
	game_canvas(&cw, &ch, &cx, &cy);
	remap_mouse_on_change(gw, gh, cw, ch, cx, cy);
	while (!Queue.empty()) {
		Event e = Queue.front();
		Queue.pop_front();
		Consumed_count++;
		Consumed_usec = (long long)Time::get_singleton()->get_ticks_usec();
		switch (e.type) {
		case EV_KEY: {
			unsigned int latency = (now > e.at_ms) ? (unsigned int)(now - e.at_ms) : 0;
			Api->input_key(e.a, e.b, latency);
			break;
		}
		case EV_MOUSE_BUTTON:
			Api->input_mouse_button(e.a, e.b);
			break;
		case EV_MOUSE_MOVE:
			map_mouse(e.nx * View_w, e.ny * View_h, gw, gh, cw, ch, cx, cy);
			break;
		case EV_FOCUS:
			Foreground = (e.b != 0);
			Api->input_focus(e.b);
			break;
		case EV_CLOSE:
			Api->input_close();
			break;
		}
	}
}

static int h_is_foreground(void)
{
	return Foreground ? 1 : 0;
}

static void h_mouse_get_pos(int *x, int *y)
{
	*x = Mouse_x;
	*y = Mouse_y;
}

// The game moves the cursor (it keeps it centred while it flies with the mouse). Move Godot's cursor too, and drop the motion
// still queued from before the move, which would pull the game's cursor back.
static void h_mouse_set_pos(int x, int y)
{
	int cw, ch, cx, cy;
	game_canvas(&cw, &ch, &cx, &cy);
	Mouse_x = x;
	Mouse_y = y;
	for (std::deque<Event>::iterator it = Queue.begin(); it != Queue.end();) {
		it = (it->type == EV_MOUSE_MOVE) ? Queue.erase(it) : it + 1;
	}
	if (!is_headless_display()) {
		float ox, oy, sc;
		fit_rect(cw, ch, &ox, &oy, &sc);
		Input::get_singleton()->warp_mouse(Vector2(ox + (float)(x + cx) * sc, oy + (float)(y + cy) * sc));
	}
}

// Joystick: the game's slot number is Godot's device id. The slot count stays at the 16 WinMM reports with nothing attached
// (a recording that held a stick still finds a driver, as with the headless hooks).
static int h_joy_count(void)
{
	return 16;
}

// The headless gate has no device, so the game's input test (-at_input joyhost / padhost, InputTest.h) feeds its script in as
// synthetic joypad events on device 0, and the hooks then read it back through Godot's Input like a real device's.
// 0 none, 1 a stick (an unknown device: the joystick path), 2 a pad (a known mapping: the pad path).
static int Synth_kind = 0;
static const int SYNTH_DEVICE = 0;

static bool joy_connected(int stick)
{
	if (stick < 0 || stick >= 16) {
		return false;
	}
	if (Synth_kind != 0 && stick == SYNTH_DEVICE) {
		return true;
	}
	return Input::get_singleton()->get_connected_joypads().has(stick);
}

static int h_joy_caps(int stick, fs2_joy_caps *caps)
{
	if (!joy_connected(stick)) {
		return 1;
	}
	CharString name = (Synth_kind != 0 && stick == SYNTH_DEVICE) ? String("synthetic").utf8() : Input::get_singleton()->get_joy_name(stick).utf8();
	memset(caps->name, 0, sizeof(caps->name));
	strncpy(caps->name, name.get_data(), sizeof(caps->name) - 1);
	// Godot does not report an axis count; a mapped pad has six
	caps->has_z = caps->has_r = caps->has_u = caps->has_v = 1;
	return 0;
}

// -1..1 to the WinMM range 0..65535, centre 32768 (the inverse of winmm_to_axis, so the input test's known answers round-trip
// exactly; the old (v+1)*32767.5 was one to two units off at the extremes and on the quarter points, which the test caught)
static unsigned int axis_to_winmm(float v)
{
	if (v < -1.0f) v = -1.0f;
	if (v > 1.0f) v = 1.0f;
	long r = lroundf(v * 32768.0f) + 32768;
	return (unsigned int)(r > 65535 ? 65535 : r);
}

// one read of the stick, once per frame from joy_poll (D-20261022-143926)
static int h_joy_read(int stick, fs2_joy_state *st)
{
	if (!joy_connected(stick)) {
		return 1;
	}
	Input *in = Input::get_singleton();
	for (int a = 0; a < 6; a++) {
		st->axes[a] = axis_to_winmm(in->get_joy_axis(stick, (JoyAxis)a));
	}
	// the buttons as bits; Godot's d-pad buttons (11..14) are the hat
	st->buttons = 0;
	for (int b = 0; b < 32; b++) {
		if (b >= JOY_BUTTON_DPAD_UP && b <= JOY_BUTTON_DPAD_RIGHT) {
			continue;
		}
		if (in->is_joy_button_pressed(stick, (JoyButton)b)) {
			st->buttons |= (1u << b);
		}
	}
	bool up = in->is_joy_button_pressed(stick, JOY_BUTTON_DPAD_UP);
	bool down = in->is_joy_button_pressed(stick, JOY_BUTTON_DPAD_DOWN);
	bool left = in->is_joy_button_pressed(stick, JOY_BUTTON_DPAD_LEFT);
	bool right = in->is_joy_button_pressed(stick, JOY_BUTTON_DPAD_RIGHT);
	// WinMM: hundredths of a degree clockwise from up, 0xFFFF when centred
	unsigned int pov = 0xFFFF;
	if (up && right) pov = 4500;
	else if (right && down) pov = 13500;
	else if (down && left) pov = 22500;
	else if (left && up) pov = 31500;
	else if (up) pov = 0;
	else if (right) pov = 9000;
	else if (down) pov = 18000;
	else if (left) pov = 27000;
	st->pov = pov;
	return 0;
}

// The pad: the first connected device Godot has a standard mapping for (a pad, as opposed to a stick or wheel), read in Godot's
// own button and axis order, which is PadState's. Sticks stay -1..1 with +y down, triggers 0..1, as Godot reports them.
static int h_pad_poll(fs2_pad_state *out)
{
	Input *in = Input::get_singleton();
	int dev = -1;

	memset(out, 0, sizeof(*out));
	if (Synth_kind == 2) {
		dev = SYNTH_DEVICE;
	} else {
		TypedArray<int> pads = in->get_connected_joypads();
		for (int i = 0; i < pads.size(); i++) {
			int d = pads[i];
			if (Synth_kind != 0 && d == SYNTH_DEVICE) {
				continue;
			}
			if (in->is_joy_known(d)) {
				dev = d;
				break;
			}
		}
	}
	if (dev < 0) {
		return 0;
	}
	out->connected = 1;
	for (int a = 0; a < FS2_PAD_AXES; a++) {
		float v = in->get_joy_axis(dev, (JoyAxis)a);
		float lo = (a >= JOY_AXIS_TRIGGER_LEFT) ? 0.0f : -1.0f;
		out->axis[a] = v < lo ? lo : (v > 1.0f ? 1.0f : v);
	}
	for (int b = 0; b < FS2_PAD_BUTTONS; b++) {
		out->button[b] = in->is_joy_button_pressed(dev, (JoyButton)b) ? 1 : 0;
	}
	return 1;
}

// The input test's script, as synthetic events. Only what changed is sent; events from outside the main loop reach the Input
// state only after a flush.
static float Synth_axis[6];
static bool Synth_button[32];
static bool Synth_fresh = true;

static void synth_axis(int a, float v)
{
	if (!Synth_fresh && Synth_axis[a] == v) {
		return;
	}
	Synth_axis[a] = v;
	Ref<InputEventJoypadMotion> e;
	e.instantiate();
	e->set_device(SYNTH_DEVICE);
	e->set_axis((JoyAxis)a);
	e->set_axis_value(v);
	Input::get_singleton()->parse_input_event(e);
}

static void synth_button(int b, bool down)
{
	if (!Synth_fresh && Synth_button[b] == down) {
		return;
	}
	Synth_button[b] = down;
	Ref<InputEventJoypadButton> e;
	e.instantiate();
	e->set_device(SYNTH_DEVICE);
	e->set_button_index((JoyButton)b);
	e->set_pressed(down);
	Input::get_singleton()->parse_input_event(e);
}

static float winmm_to_axis(unsigned int a)
{
	float v = ((float)a - 32768.0f) / 32768.0f;
	return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

static void h_input_script(const fs2_input_script *sc)
{
	if (Synth_kind != sc->kind) {
		Synth_kind = sc->kind;
		Synth_fresh = true;
	}
	if (sc->kind == 1) {
		for (int a = 0; a < 6; a++) {
			synth_axis(a, winmm_to_axis(sc->joy.axes[a]));
		}
		unsigned int pov = sc->joy.pov;
		bool up = false, right = false, down = false, left = false;
		if (pov != 0xFFFF) {
			up = (pov >= 31500 || pov <= 4500);
			right = (pov >= 4500 && pov <= 13500);
			down = (pov >= 13500 && pov <= 22500);
			left = (pov >= 22500 && pov <= 31500);
		}
		for (int b = 0; b < 32; b++) {
			if (b >= JOY_BUTTON_DPAD_UP && b <= JOY_BUTTON_DPAD_RIGHT) {
				continue;
			}
			synth_button(b, (sc->joy.buttons >> b) & 1);
		}
		synth_button(JOY_BUTTON_DPAD_UP, up);
		synth_button(JOY_BUTTON_DPAD_DOWN, down);
		synth_button(JOY_BUTTON_DPAD_LEFT, left);
		synth_button(JOY_BUTTON_DPAD_RIGHT, right);
	} else if (sc->kind == 2) {
		for (int a = 0; a < FS2_PAD_AXES; a++) {
			synth_axis(a, sc->pad.axis[a]);
		}
		for (int b = 0; b < FS2_PAD_BUTTONS; b++) {
			synth_button(b, sc->pad.button[b] != 0);
		}
	}
	Synth_fresh = false;
	Input::get_singleton()->flush_buffered_events();
}

void (*input_script_sink())(const fs2_input_script *)
{
	return h_input_script;
}

void fill(fs2_hooks *h)
{
	h->time_counter = h_time_counter;
	h->time_frequency = h_time_frequency;
	h->poll_events = h_poll_events;
	h->is_foreground = h_is_foreground;
	h->mouse_get_pos = h_mouse_get_pos;
	h->mouse_set_pos = h_mouse_set_pos;
	h->joy_count = h_joy_count;
	h->joy_caps = h_joy_caps;
	h->joy_read = h_joy_read;
	h->pad_poll = h_pad_poll;
}

} // namespace fs2host
