// Claude: GodotOptions. The player's on/off options, one line each (Tom, 2026-11-04: every option has a command line, a row on the
// Options screen and a console command, from one piece of logic). This line is all an option needs: see GameOptions.h.
//
// GO_OPTION(id, name, key, label, default, host only)
//   name       the console command, and the command line with a - in front
//   key        the fs2.ini key
//   label      the Options screen row
//   host only  no row on a backend that cannot do it
//
// No include guard: included once per use, with GO_OPTION defined.

GO_OPTION(GO_OPT_SUBTITLES, "subtitles", "Subtitles", "Subtitles", 0, 0)
GO_OPTION(GO_OPT_HUD_DOUBLE, "hud_double", "HudDouble", "Double size HUD", 0, 1)
