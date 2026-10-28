// host_hooks_godot.cpp
// G4: see host_hooks_godot.h.
#include "host_hooks_godot.h"

#include <deque>
#include <math.h>
#include <string.h>

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/time.hpp>

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
	// a headless Godot has no window to be in front, and the game must see itself in front as it did with the headless hooks
	Foreground = true;
	if (!is_headless_display()) {
		Foreground = DisplayServer::get_singleton()->window_is_focused(0);
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

// the only place the game takes the host's events
static void h_poll_events(void)
{
	uint64_t now = Time::get_singleton()->get_ticks_msec();
	int gw, gh;
	game_screen(&gw, &gh);
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
			Mouse_x = (int)(e.nx * gw);
			Mouse_y = (int)(e.ny * gh);
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
	int gw, gh;
	game_screen(&gw, &gh);
	Mouse_x = x;
	Mouse_y = y;
	for (std::deque<Event>::iterator it = Queue.begin(); it != Queue.end();) {
		it = (it->type == EV_MOUSE_MOVE) ? Queue.erase(it) : it + 1;
	}
	if (!is_headless_display()) {
		Input::get_singleton()->warp_mouse(Vector2((float)x / gw * View_w, (float)y / gh * View_h));
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
