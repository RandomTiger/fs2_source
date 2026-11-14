// Claude: GodotOptions. The player's on/off options, one line each (Tom, 2026-11-04: every option has a command line, a row on the
// Options screen and a console command, from one piece of logic). This line is all an option needs: see GameOptions.h.
//
// GO_OPTION(id, name, key, label, default, host only, choices)
//   name       the console command, and the command line with a - in front
//   key        the fs2.ini key
//   label      the Options screen row
//   host only  no row on a backend that cannot do it
//   choices    NULL: an on/off option (default 0 or 1). Else the choices' screen labels, split by |, and the value is the index of one
//              (default an index); the console and command line take a label in lower case without spaces, or its index.
//
// No include guard: included once per use, with GO_OPTION defined.

GO_OPTION(GO_OPT_SUBTITLES, "subtitles", "Subtitles", "Subtitles", 0, 0, NULL)
GO_OPTION(GO_OPT_BIG_HUD, "big_hud", "BigHud", "Big HUD", 0, 1, NULL)
// TextureFiltering (#315): Original is the game's own bilinear with no mipmaps; the others build mipmaps for the 3D textures
GO_OPTION(GO_OPT_TEXTURE_FILTER, "texture_filter", "TextureFilter", "Texture Filter", 0, 1, "Original|Trilinear|Aniso 2x|Aniso 4x|Aniso 8x|Aniso 16x")
GO_OPTION(GO_OPT_MIP_BIAS, "mip_bias", "MipBias", "Texture Sharpness", 2, 1, "Sharpest|Sharper|Normal|Softer|Softest")
// multisampling of the 3D views; the game itself never antialiased (Off)
GO_OPTION(GO_OPT_ANTIALIAS, "antialiasing", "Antialiasing", "Antialiasing", 0, 1, "Off|MSAA 2x|MSAA 4x|MSAA 8x")
