// fs2_status_panel.h
// Text in the black column left of the game picture: the build (D-20261105-201403) and the devices found (D-20261105-201401).
// Live shown play only. The game never sees it and compared frames come from the SubViewport, so it touches no test.
#ifndef FS2_STATUS_PANEL_H
#define FS2_STATUS_PANEL_H

#include <windows.h>

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class Fs2Screen;

class Fs2StatusPanel {
	Label *label = nullptr;
	Fs2Screen *screen = nullptr;
	String version;
	unsigned long long next_refresh_usec = 0;

public:
	// Adds the text under the screen node. dll is the game DLL actually loaded.
	void create(Fs2Screen *on_screen, HMODULE dll);
	// Call each Godot frame: the devices are looked at about once a second, the column's width every time.
	void refresh();
};

} // namespace godot

#endif
