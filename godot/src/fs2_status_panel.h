// fs2_status_panel.h
// Text in the black column left of the game picture: the build (D-20261105-201403) and the devices found (D-20261105-201401).
// Live shown play only. The game never sees it and compared frames come from the SubViewport, so it touches no test.
#ifndef FS2_STATUS_PANEL_H
#define FS2_STATUS_PANEL_H

#include <windows.h>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class Fs2Screen;

class Fs2StatusPanel {
	TextureRect *text_rect = nullptr;
	Fs2Screen *screen = nullptr;
	String version;
	String shown_text;
	int (*font_text)(const char *, unsigned char *, int *, int *) = nullptr;
	int (*voice_enabled)(void) = nullptr;
	int (*column_shown)(void) = nullptr;
	int text_w = 0, text_h = 0;
	unsigned long long next_refresh_usec = 0;

public:
	// Adds the text under the screen node. dll is the game DLL actually loaded, text_in_font its fs2_font_text: the text is the game's own font.
	void create(Fs2Screen *on_screen, HMODULE dll, int (*text_in_font)(const char *, unsigned char *, int *, int *), int (*voice_is_enabled)(void), int (*left_column_shown)(void));
	// Call each Godot frame: the devices are looked at about once a second, the column's width every time.
	void refresh();

private:
	void set_text(const String &text);
};

} // namespace godot

#endif
