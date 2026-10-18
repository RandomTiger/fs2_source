/*
 * FS1 compatibility (docs/FS1_COMPAT.md): default text for tables that FreeSpace 2 reads and
 * FreeSpace 1 data does not have.
 *
 * read_file_text() asks for it when a table cannot be opened.  Each default is the smallest text
 * the table's parser accepts, built from the names of files the FS1 data has.  Where FS1 behaved
 * differently, a later FS1 batch replaces the default with something closer.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "PsTypes.h"
#include "cfile.h"
#include "ContextHelp.h"
#include "Fs1Tables.h"

extern char *help_overlay_section_names[MAX_HELP_OVERLAYS];	// GameHelp/ContextHelp.cpp

#define FS1_MAX_SHIELD_ICONS		40		// MAX_SHIELD_ICONS in Hud/HUDshield.cpp
#define FS1_TABLE_TEXT_LEN			16384

static char Fs1_table_text[FS1_TABLE_TEXT_LEN];

static void fs1_line(char *format, ...)
{
	char line[512];
	va_list args;

	va_start(args, format);
	_vsnprintf(line, sizeof(line) - 2, format, args);
	va_end(args);
	line[sizeof(line) - 2] = 0;
	strcat(line, "\n");

	if (strlen(Fs1_table_text) + strlen(line) < FS1_TABLE_TEXT_LEN) {
		strcat(Fs1_table_text, line);
	}
}

// hud.tbl: the shield icons are the HUD's shield-*.ani files, which is the set FS1's ships.tbl names
static void fs1_hud_tbl()
{
	char names[FS1_MAX_SHIELD_ICONS][MAX_FILENAME_LEN];
	char *list[FS1_MAX_SHIELD_ICONS];
	int i, count;

	count = cf_get_file_list_preallocated(FS1_MAX_SHIELD_ICONS, names, list, CF_TYPE_HUD, "shield-*.ani", CF_SORT_NAME);
	fs1_line("#Shield Icons Begin");
	for (i = 0; i < count; i++) {
		fs1_line("$Shield: %s", list[i]);
	}
	fs1_line("#End");
}

// stars.tbl: no background bitmaps yet (FS1 missions name theirs; converting them is a later batch),
// FS1's one sun, and its four debris pieces.  FS1 has no full nebula, so its nebula debris slots,
// which the parser requires, reuse the normal debris.
static void fs1_stars_tbl()
{
	int i;

	fs1_line("#end");
	fs1_line("$Sun: Sun01");
	fs1_line("$Sunglow: SunGlow01");
	fs1_line("$SunRGBI: 1.0 1.0 1.0 1.0");
	fs1_line("#end");
	for (i = 1; i <= 4; i++) {
		fs1_line("$Debris: debris%02d", i);
	}
	fs1_line("#end");
	for (i = 1; i <= 4; i++) {
		fs1_line("$DebrisNeb: debris%02d", i);
	}
	fs1_line("#end");
}

// nebula.tbl: FS1 has no full nebula.  The parser expects six poofs; these are only loaded in a full
// nebula mission, which FS1 does not have.
static void fs1_nebula_tbl()
{
	int i;

	fs1_line("#end");
	for (i = 0; i < 6; i++) {
		fs1_line("+Poof: debris01");
	}
	fs1_line("#end");
}

// help.tbl: every help overlay section, empty
static void fs1_help_tbl()
{
	int i;

	for (i = 0; i < MAX_HELP_OVERLAYS; i++) {
		fs1_line("%s", help_overlay_section_names[i]);
		fs1_line("$end");
	}
}

// mainhall.tbl: FS1's two main halls, background and mask only, with FS1's main menu music.  The door
// and animation positions were built into the FS1 exe; they come with the interface batch.  FS1 has
// no 1024 art, so the 1024 entries use the 640 art.  The tooltip line sits near the bottom of the screen,
// at the same height as FS2's.
static void fs1_mainhall_tbl()
{
	int hall, res;

	for (hall = 1; hall <= 2; hall++) {
		for (res = 0; res < 2; res++) {
			fs1_line("$Main Hall");
			fs1_line("+Bitmap: MainHall%d", hall);
			fs1_line("+Mask: MainHall%d-m", hall);
			fs1_line("+Music: Choco Mousse");
			fs1_line("+Num Intercom Sounds: 0");
			fs1_line("+Num Misc Animations: 0");
			fs1_line("+Num Door Animations: 0");
			fs1_line("+Tooltip Y: %d", res ? 755 : 469);
		}
	}
	fs1_line("#end");
}

char *fs1_table_default(char *filename)
{
	if (!Cfile_fs1_data || !filename) {
		return NULL;
	}

	Fs1_table_text[0] = 0;
	if (!stricmp(filename, "pixels.tbl")) {
		fs1_line("#END");									// no non-darkening pixels
	} else if (!stricmp(filename, "hud.tbl")) {
		fs1_hud_tbl();
	} else if (!stricmp(filename, "stars.tbl")) {
		fs1_stars_tbl();
	} else if (!stricmp(filename, "nebula.tbl")) {
		fs1_nebula_tbl();
	} else if (!stricmp(filename, "lightning.tbl")) {
		fs1_line("#Bolts begin");						// no nebula lightning in FS1
		fs1_line("#Bolts end");
		fs1_line("#Storms begin");
		fs1_line("#Storms end");
	} else if (!stricmp(filename, "mflash.tbl")) {
		fs1_line("#Muzzle flash types");				// no muzzle flashes in FS1
		fs1_line("#end");
	} else if (!stricmp(filename, "weapon_expl.tbl")) {
		fs1_line("#Start");								// FS1 weapon explosions come from fireball.tbl
		fs1_line("#End");
	} else if (!stricmp(filename, "ssm.tbl")) {
		fs1_line("#end");									// no subspace missiles in FS1
	} else if (!stricmp(filename, "tips.tbl")) {
		fs1_line("#end");									// FS1 had no tips
	} else if (!stricmp(filename, "help.tbl")) {
		fs1_help_tbl();
	} else if (!stricmp(filename, "mainhall.tbl")) {
		fs1_mainhall_tbl();
	} else {
		return NULL;
	}

	mprintf(("FS1: built-in default for %s\n", filename));
	return Fs1_table_text;
}
