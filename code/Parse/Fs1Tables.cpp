/*
 * FS1 compatibility (docs/FS1_COMPAT.md): default text for tables that FreeSpace 2 reads and
 * FreeSpace 1 data does not have, and converters from FS1's table formats to FS2's.
 *
 * read_file_text() asks for a default when a table cannot be opened, and passes every table it
 * reads through fs1_table_convert().  Each default is the smallest text the table's parser
 * accepts, built from the names of files the FS1 data has.  Where FS1 behaved differently, a later
 * FS1 batch replaces the default with something closer.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "PsTypes.h"
#include "cfile.h"
#include "parselo.h"
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

// ------------------------------------------------------------------------------------------------
// Converters: FS1 table text rewritten line by line into the form FS2's parser reads.  Each
// converter sees one line at a time (without its line end) and writes zero or more lines.
//

static char *Fs1_out;					// converted text
static int Fs1_out_len, Fs1_out_max;

static void fs1_out(char *format, ...)
{
	char line[1024];
	va_list args;
	int len;

	va_start(args, format);
	_vsnprintf(line, sizeof(line) - 3, format, args);
	va_end(args);
	line[sizeof(line) - 3] = 0;
	strcat(line, "\r\n");

	len = strlen(line);
	if (Fs1_out_len + len < Fs1_out_max) {
		memcpy(Fs1_out + Fs1_out_len, line, len);
		Fs1_out_len += len;
	}
}

// the value after "key" if the line (leading white space skipped) starts with it, else NULL
static char *fs1_key(char *line, char *key)
{
	int len = strlen(key);

	while ((*line == ' ') || (*line == '\t')) {
		line++;
	}
	if (strnicmp(line, key, len)) {
		return NULL;
	}
	line += len;
	while ((*line == ' ') || (*line == '\t')) {
		line++;
	}
	return line;
}

// medals.tbl: FS1 names a badge's two voice files, "$Wavefile 1: 1_badge_a.wav" and "$Wavefile 2: 3_badge_a.wav".
// FS2 names one base, "$Wavefile Base: badge_a", and puts the persona number in front itself.
static void fs1_medals_line(char *line)
{
	char base[MAX_FILENAME_LEN], *v, *p;

	if ((v = fs1_key(line, "$Wavefile 1:")) != NULL) {
		strncpy(base, v, MAX_FILENAME_LEN - 1);
		base[MAX_FILENAME_LEN - 1] = 0;
		if ((p = strpbrk(base, " \t;")) != NULL) {
			*p = 0;
		}
		if ((p = strrchr(base, '.')) != NULL) {
			*p = 0;
		}
		p = strchr(base, '_');
		fs1_out("$Wavefile Base: %s", p ? p + 1 : base);
	} else if (fs1_key(line, "$Wavefile 2:") == NULL) {
		fs1_out("%s", line);
	}
}

// sounds.tbl: FS1 keeps its flyby sounds in game sound slots 44 (Terran and Vasudan) and 71 (Shivan).
// FS2 reads flyby sounds from a #Flyby Sounds section, which FS1 lacks, and plays slot 44 as the
// engine wash loop and 71 as the large-engine loop.  So the two slots move into a flyby section and
// are left empty.  Every other slot keeps its FS1 number: the slots the engine names mean the same
// in both games, and the ones that differ are weapon sounds that only FS1's own weapons.tbl uses.
#define FS1_SND_FLYBY				44
#define FS1_SND_FLYBY_SHIVAN		71

static char Fs1_flyby[2][256];		// what follows the slot number on those two lines
static int Fs1_in_game_sounds;

static void fs1_sounds_line(char *line)
{
	char *v, *p;
	int slot, i;

	if (fs1_key(line, "#Game Sounds Start")) {
		Fs1_in_game_sounds = 1;
		Fs1_flyby[0][0] = Fs1_flyby[1][0] = 0;
	} else if (fs1_key(line, "#Game Sounds End")) {
		Fs1_in_game_sounds = 0;
	} else if (Fs1_in_game_sounds && ((v = fs1_key(line, "$Name:")) != NULL)) {
		slot = strtol(v, &p, 10);
		if ((p != v) && ((slot == FS1_SND_FLYBY) || (slot == FS1_SND_FLYBY_SHIVAN))) {
			while ((*p == ' ') || (*p == '\t')) {
				p++;
			}
			strncpy(Fs1_flyby[slot == FS1_SND_FLYBY_SHIVAN], p, 255);
			fs1_out("$Name: %d empty", slot);
			return;
		}
	}

	fs1_out("%s", line);

	if (fs1_key(line, "#Interface Sounds End")) {
		fs1_out("#Flyby Sounds Start");
		for (i = 0; i < 2; i++) {
			fs1_out("$Terran: %d %s", FS1_SND_FLYBY, Fs1_flyby[0]);
		}
		for (i = 0; i < 2; i++) {
			fs1_out("$Vasudan: %d %s", FS1_SND_FLYBY, Fs1_flyby[0]);
		}
		for (i = 0; i < 2; i++) {
			fs1_out("$Shivan: %d %s", FS1_SND_FLYBY_SHIVAN, Fs1_flyby[1]);
		}
		fs1_out("#Flyby Sounds End");
	}
}

// music.tbl: an FS1 soundtrack has 14 patterns, starting with three "normal" patterns that play in turn.
// FS2 has 12 slots with one normal pattern (SONG_NRML_1) and asserts beyond that.  Until FS1's chained
// normal patterns get their own code path, the second and third are left out, so the rest line up
// with FS2's slots: arrivals, battles, victories, goal failed, death.
static int Fs1_pattern = -1;		// $Name: lines seen in the current soundtrack, -1 outside one

static void fs1_music_line(char *line)
{
	if (fs1_key(line, "#SoundTrack Start")) {
		Fs1_pattern = 0;
	} else if (fs1_key(line, "#SoundTrack End")) {
		Fs1_pattern = -1;
	} else if ((Fs1_pattern >= 0) && fs1_key(line, "$Name:")) {
		Fs1_pattern++;
		if ((Fs1_pattern == 2) || (Fs1_pattern == 3)) {
			return;
		}
	}
	fs1_out("%s", line);
}

// the name on a "$Name:" line, without FS1's leading '@' (which the parsers drop too) or a comment
static void fs1_name(char *v, char *name, int size)
{
	char *p;

	if (*v == '@') {
		v++;
	}
	strncpy(name, v, size - 1);
	name[size - 1] = 0;
	if ((p = strchr(name, ';')) != NULL) {
		*p = 0;
	}
	for (p = name + strlen(name); (p > name) && ((p[-1] == ' ') || (p[-1] == '\t')); p--) {
		p[-1] = 0;
	}
}

// removes "flag" (quotes included, any case) from a $Flags: line; 1 if it was there
static int fs1_drop_flag(char *line, char *flag)
{
	char quoted[64], *p;
	int len;

	sprintf(quoted, "\"%s\"", flag);
	len = strlen(quoted);
	for (p = line; *p; p++) {
		if (!strnicmp(p, quoted, len)) {
			memmove(p, p + len, strlen(p + len) + 1);
			return 1;
		}
	}
	return 0;
}

// weapons.tbl:
// - Trails.  FS1 writes "$Trail: YES" or "$Trail: NO", then +Head Width, +Tail Width, +Life and +Bitmap.
//   FS2 takes any "$Trail:" as a trail and then requires +Start Width, +End Width, +Start Alpha,
//   +End Alpha, +Max Life and +Bitmap.  FS1 gives no alpha; its trails fade from solid to clear, so
//   alpha runs 1.0 to 0.0.
// - Swarm missiles.  FS1 marks them with a "Swarm" flag; FS2 reads "$Swarm: <count>" before $LaunchSnd.
//   FS1 always fired SWARM_DEFAULT_NUM_MISSILES_FIRED (4), so that is the count.
// - FS1's "No Ship" flag has no FS2 meaning and is dropped.
#define FS1_MAX_SWARM_WEAPONS		32

static int Fs1_in_trail;
static char Fs1_weapon[NAME_LENGTH];						// the weapon being read
static char Fs1_swarm[FS1_MAX_SWARM_WEAPONS][NAME_LENGTH];	// weapons with FS1's Swarm flag
static int Fs1_num_swarm;

static void fs1_weapons_prepare(char *line)
{
	char *v;

	if ((v = fs1_key(line, "$Name:")) != NULL) {
		fs1_name(v, Fs1_weapon, NAME_LENGTH);
	} else if (fs1_key(line, "$Flags:") && strstr(line, "\"Swarm\"") && (Fs1_num_swarm < FS1_MAX_SWARM_WEAPONS)) {
		strcpy(Fs1_swarm[Fs1_num_swarm++], Fs1_weapon);
	}
}

static void fs1_weapons_line(char *line)
{
	char *v;
	int i;

	if ((v = fs1_key(line, "$Name:")) != NULL) {
		fs1_name(v, Fs1_weapon, NAME_LENGTH);
	} else if (fs1_key(line, "$LaunchSnd:")) {
		for (i = 0; i < Fs1_num_swarm; i++) {
			if (!stricmp(Fs1_weapon, Fs1_swarm[i])) {
				fs1_out("$Swarm: 4");
			}
		}
	} else if (fs1_key(line, "$Flags:")) {
		fs1_drop_flag(line, "Swarm");
		fs1_drop_flag(line, "No Ship");
	}

	if ((v = fs1_key(line, "$Trail:")) != NULL) {
		Fs1_in_trail = !strnicmp(v, "YES", 3);
		if (Fs1_in_trail) {
			fs1_out("$Trail:");
		}
		return;
	}
	if (Fs1_in_trail) {
		if ((v = fs1_key(line, "+Head Width:")) != NULL) {
			fs1_out("+Start Width: %s", v);
			return;
		}
		if ((v = fs1_key(line, "+Tail Width:")) != NULL) {
			fs1_out("+End Width: %s", v);
			fs1_out("+Start Alpha: 1.0");
			fs1_out("+End Alpha: 0.0");
			return;
		}
		if ((v = fs1_key(line, "+Life:")) != NULL) {
			fs1_out("+Max Life: %s", v);
			return;
		}
		if (fs1_key(line, "+Bitmap:")) {
			Fs1_in_trail = 0;
		}
	}
	fs1_out("%s", line);
}

// ships.tbl: FS2 requires a #Default Player Ship section and an #Engine Wash Info section before
// #Ship Classes.  FS1 has neither: its default player ship is the first class flagged
// "default_player_ship" (FS2 still reads that flag too), and it has no engine wash.
static char Fs1_ship[NAME_LENGTH];					// the ship class being read
static char Fs1_default_ship[NAME_LENGTH];

static void fs1_ships_prepare(char *line)
{
	char *v;

	if ((v = fs1_key(line, "$Name:")) != NULL) {
		fs1_name(v, Fs1_ship, NAME_LENGTH);
	} else if (fs1_key(line, "$Flags:") && !Fs1_default_ship[0]) {
		for (v = line; *v; v++) {
			if (!strnicmp(v, "\"default_player_ship\"", 21)) {
				strcpy(Fs1_default_ship, Fs1_ship);
				break;
			}
		}
	}
}

static void fs1_ships_line(char *line)
{
	if (fs1_key(line, "#Ship Classes")) {
		fs1_out("#Default Player Ship");
		fs1_out("$Name: %s", Fs1_default_ship);
		fs1_out("#End");
		fs1_out("#Engine Wash Info");
		fs1_out("#End");
	}
	fs1_out("%s", line);
}

// asteroid.tbl: FS2 wants three models for each asteroid ("$POF file3:" when the name has "Asteroid");
// FS1 has two.  The second is used again as the third.
static void fs1_asteroid_line(char *line)
{
	char *v;

	fs1_out("%s", line);
	if ((v = fs1_key(line, "$Name:")) != NULL) {
		fs1_name(v, Fs1_ship, NAME_LENGTH);
	} else if (((v = fs1_key(line, "$POF file2:")) != NULL) && strstr(Fs1_ship, "steroid")) {
		fs1_out("$POF file3: %s", v);
	}
}

typedef struct fs1_converter {
	char *filename;
	void (*prepare_fn)(char *line);		// optional first pass, for what a line needs from later in the file
	void (*line_fn)(char *line);			// second pass, writes the converted lines
} fs1_converter;

static fs1_converter Fs1_converters[] = {
	{ "medals.tbl",		NULL,							fs1_medals_line },
	{ "sounds.tbl",		NULL,							fs1_sounds_line },
	{ "music.tbl",			NULL,							fs1_music_line },
	{ "weapons.tbl",		fs1_weapons_prepare,		fs1_weapons_line },
	{ "ships.tbl",			fs1_ships_prepare,		fs1_ships_line },
	{ "asteroid.tbl",		NULL,							fs1_asteroid_line },
};

// calls fn on each line of text, without its line end
static void fs1_each_line(char *text, int len, void (*fn)(char *line))
{
	char line[1024];
	int start, end, n;

	for (start = 0; start < len; start = end + 1) {
		for (end = start; (end < len) && (text[end] != '\n'); end++) {
		}
		n = end - start;
		if ((n > 0) && (text[start + n - 1] == '\r')) {
			n--;
		}
		if (n > (int)sizeof(line) - 1) {
			n = sizeof(line) - 1;
		}
		memcpy(line, text + start, n);
		line[n] = 0;
		fn(line);
	}
}

// FS1's parser took /* ... */ as a comment (its ships.tbl and messages.tbl have one); FS2's only knows ';'.
// The block is blanked out in place, keeping its line ends so error line numbers still match the file.
static void fs1_blank_block_comments(char *text, int len)
{
	int i, in_block = 0;

	for (i = 0; i < len; i++) {
		if (!in_block && (i + 1 < len) && (text[i] == '/') && (text[i + 1] == '*')) {
			in_block = 1;
		}
		if (in_block) {
			if ((i + 1 < len) && (text[i] == '*') && (text[i + 1] == '/')) {
				text[i] = text[i + 1] = ' ';
				i++;
				in_block = 0;
			} else if ((text[i] != '\r') && (text[i] != '\n')) {
				text[i] = ' ';
			}
		}
	}
}

int fs1_table_convert(char *filename, char *text, int len, int max_len)
{
	fs1_converter *conv = NULL;
	int i;

	if (!Cfile_fs1_data || !filename) {
		return len;
	}
	i = strlen(filename);
	if ((i >= 4) && !stricmp(filename + i - 4, ".tbl")) {
		fs1_blank_block_comments(text, len);
	}
	for (i = 0; i < sizeof(Fs1_converters) / sizeof(Fs1_converters[0]); i++) {
		if (!stricmp(filename, Fs1_converters[i].filename)) {
			conv = &Fs1_converters[i];
		}
	}
	if (!conv) {
		return len;
	}

	Fs1_out_max = max_len;
	Fs1_out = (char *)malloc(Fs1_out_max);
	if (!Fs1_out) {
		return len;
	}
	Fs1_out_len = 0;

	// converter state starts clean for each file
	Fs1_in_trail = 0;
	Fs1_weapon[0] = Fs1_ship[0] = Fs1_default_ship[0] = 0;
	Fs1_num_swarm = 0;
	Fs1_pattern = -1;

	if (conv->prepare_fn) {
		fs1_each_line(text, len, conv->prepare_fn);
	}
	fs1_each_line(text, len, conv->line_fn);

	memcpy(text, Fs1_out, Fs1_out_len);
	free(Fs1_out);
	Fs1_out = NULL;
	mprintf(("FS1: converted %s\n", filename));
	return Fs1_out_len;
}
