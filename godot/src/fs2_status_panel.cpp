#include "fs2_status_panel.h"
#include "fs2_screen.h"
#include "host_hooks_godot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

static const float PANEL_MARGIN = 16.0f;

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

void Fs2StatusPanel::create(Fs2Screen *on_screen, HMODULE dll)
{
	wchar_t path[MAX_PATH];
	const DWORD len = GetModuleFileNameW(dll, path, MAX_PATH);

	if (len == 0 || len >= MAX_PATH) {
		version = String("unknown");
	} else {
		const wchar_t *slash = wcsrchr(path, L'\\');
		version = panel_branch(slash != NULL ? slash + 1 : path) + "\n" + panel_build_time(path);
	}
	screen = on_screen;
	label = memnew(Label);
	label->set_position(Vector2(PANEL_MARGIN, PANEL_MARGIN));
	label->add_theme_color_override("font_color", Color(0.75f, 0.75f, 0.75f));
	label->set_visible(false);
	screen->add_child(label);
}

void Fs2StatusPanel::refresh()
{
	if (label == nullptr) {
		return;
	}
	const unsigned long long now = Time::get_singleton()->get_ticks_usec();
	if (now >= next_refresh_usec) {
		int pads = 0, sticks = 0;
		next_refresh_usec = now + 1000000;
		fs2host::device_counts(&pads, &sticks);
		label->set_text(String("Version:\n") + version + "\n\nStatus:\nMouse: " + (panel_mouse_found() ? "Detected" : "Undetected") +
			"\nJoystick: " + (sticks > 0 ? "Detected" : "Undetected") +
			"\nJoypad: " + (pads > 0 ? "Detected" : "Undetected"));
	}
	const Vector2 window = screen->get_window_size();
	const int font_size = (int)(window.y / 60.0f);
	label->add_theme_font_size_override("font_size", font_size < 12 ? 12 : font_size);
	// a picture wide enough to leave no room for the text hides it rather than covering the game
	label->set_visible(label->get_minimum_size().x + (PANEL_MARGIN * 2.0f) <= screen->get_left_column_width());
}
