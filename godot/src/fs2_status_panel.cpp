#include "fs2_status_panel.h"
#include "fs2_screen.h"
#include "host_hooks_godot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

static const float PANEL_MARGIN = 16.0f;
static const float PANEL_PIXEL_SCALE_HEIGHT = 480.0f;	// one font pixel per this much window height, whole steps so the font stays sharp

// The name of the game DLL gives the branch, FS2game_<Layer>_64r.dll or _64d.dll. Any other name is shown whole, never guessed at.
static String panel_branch(const wchar_t *file_name)
{
	static const wchar_t PREFIX[] = L"FS2game_";
	static const size_t PREFIX_LEN = (sizeof(PREFIX) / sizeof(PREFIX[0])) - 1;
	static const size_t SUFFIX_LEN = 8;	// _64r.dll
	const size_t len = wcslen(file_name);

	if (len > PREFIX_LEN + SUFFIX_LEN && wcsncmp(file_name, PREFIX, PREFIX_LEN) == 0) {
		const wchar_t *suffix = file_name + len - SUFFIX_LEN;
		if (wcscmp(suffix, L"_64r.dll") == 0 || wcscmp(suffix, L"_64d.dll") == 0) {
			wchar_t layer[MAX_PATH];
			const size_t layer_len = len - PREFIX_LEN - SUFFIX_LEN;
			wmemcpy(layer, file_name + PREFIX_LEN, layer_len);
			layer[layer_len] = 0;
			return String(layer);
		}
	}
	return String(file_name);
}

// The DLL file's modified time is the build's, in local time.
static String panel_build_time(const wchar_t *path)
{
	WIN32_FILE_ATTRIBUTE_DATA info;
	FILETIME local;
	SYSTEMTIME st;
	char text[32];

	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) || !FileTimeToLocalFileTime(&info.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &st)) {
		return String("unknown");
	}
	snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
	return String(text);
}

// The port's own version the build stamped into the DLL's ProductVersion (#311): "<milestone>.<commits> <layer> <hash>".
// Shown as the number on one line and the layer and hash on the next, so it fits the column. Empty for a DLL without one.
static String panel_port_version(const wchar_t *path)
{
	DWORD unused = 0;
	const DWORD size = GetFileVersionInfoSizeW(path, &unused);
	if (size == 0) {
		return String();
	}
	std::vector<unsigned char> block(size);
	wchar_t *value = NULL;
	UINT value_len = 0;
	if (!GetFileVersionInfoW(path, 0, size, block.data()) || !VerQueryValueW(block.data(), L"\\StringFileInfo\\040904b0\\ProductVersion", (void **)&value, &value_len) || value_len == 0) {
		return String();
	}
	String v = String(value).strip_edges();
	const int space = v.find(" ");
	return space > 0 ? v.substr(0, space) + "\n" + v.substr(space + 1) : v;
}

// A remote desktop session always lists a stand-in mouse, which is not a mouse here.
static bool panel_mouse_found()
{
	UINT count = 0;
	bool found = false;

	if (GetRawInputDeviceList(NULL, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) {
		return false;
	}
	RAWINPUTDEVICELIST *list = (RAWINPUTDEVICELIST *)malloc(count * sizeof(RAWINPUTDEVICELIST));
	if (list == NULL) {
		return false;
	}
	const UINT got = GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST));
	for (UINT i = 0; got != (UINT)-1 && i < got && !found; i++) {
		if (list[i].dwType == RIM_TYPEMOUSE) {
			char name[512];
			UINT name_len = sizeof(name);
			const UINT copied = GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, name, &name_len);
			found = (copied == (UINT)-1) || (strstr(name, "RDP_MOU") == NULL);
		}
	}
	free(list);
	return found;
}

void Fs2StatusPanel::create(Fs2Screen *on_screen, HMODULE dll, int (*text_in_font)(const char *, unsigned char *, int *, int *), int (*voice_is_enabled)(void), int (*left_column_shown)(void), void (*game_canvas_info)(int *, int *, int *, int *))
{
	wchar_t path[MAX_PATH];
	const DWORD len = GetModuleFileNameW(dll, path, MAX_PATH);

	if (len == 0 || len >= MAX_PATH) {
		version = String("unknown");
	} else {
		const wchar_t *slash = wcsrchr(path, L'\\');
		const String port_version = panel_port_version(path);	// #311; a DLL built before the stamp shows the layer from its name
		version = (port_version.is_empty() ? panel_branch(slash != NULL ? slash + 1 : path) : port_version) + "\n" + panel_build_time(path);
	}
	screen = on_screen;
	font_text = text_in_font;
	voice_enabled = voice_is_enabled;
	column_shown = left_column_shown;
	canvas_info = game_canvas_info;
	text_rect = memnew(TextureRect);
	text_rect->set_position(Vector2(PANEL_MARGIN, PANEL_MARGIN));
	text_rect->set_self_modulate(Color(0.75f, 0.75f, 0.75f));
	text_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	text_rect->set_stretch_mode(TextureRect::STRETCH_SCALE);
	text_rect->set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
	text_rect->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	text_rect->set_visible(false);
	screen->add_child(text_rect);
}

// The picture of the text, from the game's font. The fonts load during the game's start-up, so until then there is nothing yet and the next look tries again.
void Fs2StatusPanel::set_text(const String &text)
{
	const CharString chars = text.ascii();
	int w = 0, h = 0;

	if (text == shown_text || !font_text(chars.get_data(), NULL, &w, &h) || w <= 0 || h <= 0) {
		return;
	}
	PackedByteArray coverage;
	coverage.resize((int64_t)w * h);
	font_text(chars.get_data(), coverage.ptrw(), &w, &h);

	PackedByteArray rgba;
	rgba.resize((int64_t)w * h * 4);
	uint8_t *out = rgba.ptrw();
	const uint8_t *in = coverage.ptr();
	for (int64_t i = 0; i < (int64_t)w * h; i++) {
		out[(i * 4) + 0] = 255;
		out[(i * 4) + 1] = 255;
		out[(i * 4) + 2] = 255;
		out[(i * 4) + 3] = in[i];
	}
	text_rect->set_texture(ImageTexture::create_from_image(Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, rgba)));
	text_w = w;
	text_h = h;
	shown_text = text;
}

void Fs2StatusPanel::refresh()
{
	if (text_rect == nullptr) {
		return;
	}
	const unsigned long long now = Time::get_singleton()->get_ticks_usec();
	if (now >= next_refresh_usec) {
		int pads = 0, sticks = 0;
		next_refresh_usec = now + 1000000;
		fs2host::device_counts(&pads, &sticks);
		set_text(String("Version:\n") + version + "\n\nStatus:\nMouse: " + (panel_mouse_found() ? "Detected" : "Undetected") +
			"\nJoystick: " + (sticks > 0 ? "Detected" : "Undetected") +
			"\nJoypad: " + (pads > 0 ? "Detected" : "Undetected") +
			"\nVoice Control: " + (voice_enabled() ? "Enabled" : "Disabled"));
	}
	const Vector2 window = screen->get_window_size();
	int scale = (int)((window.y / PANEL_PIXEL_SCALE_HEIGHT) + 0.5f);
	scale = scale < 1 ? 1 : scale;
	text_rect->set_size(Vector2((float)(text_w * scale), (float)(text_h * scale)));
	// only on the game's two column screens; a picture wide enough to leave no room for the text hides it rather than covering the game
	// a wide DisplayResolution makes the canvas the whole window and the game draws the bars itself: the base screen's corner gives the column (#310)
	int vw = 0, vh = 0, cx = 0, cy = 0;
	if (canvas_info != nullptr) {
		canvas_info(&vw, &vh, &cx, &cy);
	}
	// EnhancedOptions: made only with -status, and then shown on every screen, top left of the window, over the game
	(void)vw; (void)cx;
	text_rect->set_visible(text_w > 0);
}
