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
#include "vecmat.h"
#include "ContextHelp.h"
#include "Fs1Tables.h"

extern char *help_overlay_section_names[MAX_HELP_OVERLAYS];	// GameHelp/ContextHelp.cpp

#define FS1_MAX_SHIELD_ICONS		40		// MAX_SHIELD_ICONS in Hud/HUDshield.cpp
#define FS1_MAX_HUD_ANIS			512		// every .ani in data/hud, FS1's and Silent Threat's
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

// hud.tbl: the shield icons are the HUD's shield-*.ani files, which is the set FS1's ships.tbl names.  The file list
// filters by extension only (a "shield-*.ani" wildcard returned every HUD .ani, cut off at 40 before any shield icon,
// so no ship had one and the player's shield gauge was never drawn), so the names are filtered here.
static void fs1_hud_tbl()
{
	static char names[FS1_MAX_HUD_ANIS][MAX_FILENAME_LEN];
	static char *list[FS1_MAX_HUD_ANIS];
	int i, count, n = 0;

	count = cf_get_file_list_preallocated(FS1_MAX_HUD_ANIS, names, list, CF_TYPE_HUD, "*.ani", CF_SORT_NAME);
	fs1_line("#Shield Icons Begin");
	for (i = 0; (i < count) && (n < FS1_MAX_SHIELD_ICONS); i++) {
		if (!strnicmp(list[i], "shield-", 7)) {
			fs1_line("$Shield: %s", list[i]);
			n++;
		}
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
	fs1_line("$Sun: Sun01-unlit");					// FS1's sun with $Light: 0, which gives no light (cfile maps the name to Sun01)
	fs1_line("$Sunglow: SunGlow01");
	fs1_line("$SunRGBI: 1.0 1.0 1.0 0.0");
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

// mainhall.tbl: FS1's two main halls.  The door and animation positions were built into the FS1 exe;
// these were found by matching each anim's first frame against the hall's background (exact for all
// doors), and each door belongs to the mask region it covers.  The mouse jumps to the middle of a
// door's mask region, and its sound pans by that point's x.  Doors use FS1's main hall door sounds,
// and each hall its three intercom sounds (FS1's sounds.tbl).  The ambient anims play now and then,
// silent: which of FS1's lift, weld and crane sounds go with them, on which frames, is not known.
// FS1 has no 1024 art, so the 1024 entries use the 640 art.  The tooltip line sits near the bottom of
// the screen, at the same height as FS2's.
typedef struct fs1_hall_anim {
	char *name;
	int x, y;		// where the anim draws
	int cx, cy;		// a door: the middle of its mask region
} fs1_hall_anim;

static fs1_hall_anim Fs1_hall_doors[2][6] = {			// in region order: exit, barracks, ready room, tech room, options, campaign
	{ {"main1-d1", 68, 260, 103, 301}, {"main1-d6", 309, 34, 338, 67}, {"main1-d3", 312, 264, 386, 299},
	  {"main1-d4", 457, 34, 551, 101}, {"main1-d5", 530, 206, 574, 293}, {"main1-d2", 305, 133, 358, 170} },
	{ {"main2-d1", 199, 265, 264, 297}, {"main2-d6", 296, 278, 334, 309}, {"main2-d3", 423, 138, 531, 220},
	  {"main2-d4", 363, 187, 381, 209}, {"main2-d5", 47, 307, 107, 340}, {"main2-d2", 325, 311, 394, 372} },
};

static fs1_hall_anim Fs1_hall_misc[2][4] = {
	{ {"main1-m1", 14, 14}, {"main1-m2", 174, 198} },
	{ {"main2-m1", 0, 37}, {"main2-m2", 59, 0}, {"main2-m3", 80, 115}, {"main2-m4", 0, 182} },
};
static int Fs1_hall_num_misc[2] = { 2, 4 };
static int Fs1_hall_intercom[2] = { 38, 49 };			// the first of each hall's three

static void fs1_mainhall_tbl()
{
	int hall, res, i;

	for (hall = 0; hall < 2; hall++) {
		for (res = 0; res < 2; res++) {
			fs1_line("$Main Hall");
			fs1_line("+Bitmap: MainHall%d", hall + 1);
			fs1_line("+Mask: MainHall%d-m", hall + 1);
			fs1_line("+Music: Choco Mousse");

			fs1_line("+Num Intercom Sounds: 3");
			for (i = 0; i < 3; i++) {
				fs1_line("+Intercom delay: 8000 15000");
			}
			for (i = 0; i < 3; i++) {
				fs1_line("+Intercom sound: %d", Fs1_hall_intercom[hall] + i);
			}
			for (i = 0; i < 3; i++) {
				fs1_line("+Intercom pan: 0.0");
			}

			fs1_line("+Num Misc Animations: %d", Fs1_hall_num_misc[hall]);
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim: %s", Fs1_hall_misc[hall][i].name);
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim delay: -1 10000 20000");
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim coords: %d %d", Fs1_hall_misc[hall][i].x, Fs1_hall_misc[hall][i].y);
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim mode: 2");		// MISC_ANIM_MODE_TIMED
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim pan: 0.0");
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim sounds: 0");
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim trigger: 0");
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim handles: 0");
			}
			for (i = 0; i < Fs1_hall_num_misc[hall]; i++) {
				fs1_line("+Misc anim flags: 0");
			}

			fs1_line("+Num Door Animations: 6");
			for (i = 0; i < 6; i++) {
				fs1_line("+Door anim: %s", Fs1_hall_doors[hall][i].name);
			}
			for (i = 0; i < 6; i++) {
				fs1_hall_anim *d = &Fs1_hall_doors[hall][i];
				fs1_line("+Door coords: %d %d %d %d", d->x, d->y, d->cx, d->cy);
			}
			for (i = 0; i < 6; i++) {
				fs1_line("+Door sounds: 23 24");		// FS1's main hall door opens, closes
			}
			for (i = 0; i < 6; i++) {
				fs1_line("+Door pan: %.2f", (Fs1_hall_doors[hall][i].cx - 320) / 320.0f);
			}

			fs1_line("+Tooltip Y: %d", res ? 755 : 456);	// where retail FS1 draws its tooltips
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
// - FS1's $Player Weapon Precedence lists "Disruptor Missile", which its table does not define (FS2 warns and
//   skips it).  Names the table does not define are left out of the list.
#define FS1_MAX_SWARM_WEAPONS		32
#define FS1_MAX_WEAPONS				128

static int Fs1_in_trail;
static char Fs1_weapon[NAME_LENGTH];						// the weapon being read
static char Fs1_swarm[FS1_MAX_SWARM_WEAPONS][NAME_LENGTH];	// weapons with FS1's Swarm flag
static int Fs1_num_swarm;
static char Fs1_weapons[FS1_MAX_WEAPONS][NAME_LENGTH];	// every weapon the table defines
static int Fs1_num_weapons;
static int Fs1_in_precedence;

// removes each quoted name on a precedence list line that the table does not define
static void fs1_drop_unknown_weapons(char *line)
{
	char *open, *close, name[NAME_LENGTH];
	int i, n, known;

	for (open = strchr(line, '"'); open && ((close = strchr(open + 1, '"')) != NULL); open = strchr(open, '"')) {
		n = close - open - 1;
		if (n > NAME_LENGTH - 1) {
			n = NAME_LENGTH - 1;
		}
		memcpy(name, open + 1, n);
		name[n] = 0;
		known = 0;
		for (i = 0; i < Fs1_num_weapons; i++) {
			if (!stricmp(name, Fs1_weapons[i])) {
				known = 1;
			}
		}
		if (known) {
			open = close + 1;
		} else {
			memmove(open, close + 1, strlen(close + 1) + 1);
		}
	}
}

static void fs1_weapons_prepare(char *line)
{
	char *v;

	if ((v = fs1_key(line, "$Name:")) != NULL) {
		fs1_name(v, Fs1_weapon, NAME_LENGTH);
		if (Fs1_num_weapons < FS1_MAX_WEAPONS) {
			strcpy(Fs1_weapons[Fs1_num_weapons++], Fs1_weapon);
		}
	} else if (fs1_key(line, "$Flags:") && strstr(line, "\"Swarm\"") && (Fs1_num_swarm < FS1_MAX_SWARM_WEAPONS)) {
		strcpy(Fs1_swarm[Fs1_num_swarm++], Fs1_weapon);
	}
}

static void fs1_weapons_line(char *line)
{
	char *v;
	int i;

	if (fs1_key(line, "$Player Weapon Precedence:")) {
		Fs1_in_precedence = 1;
	}
	if (Fs1_in_precedence) {
		fs1_drop_unknown_weapons(line);
		if (strchr(line, ')')) {
			Fs1_in_precedence = 0;
		}
	}

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

// FS1 missions (.fsm), #Background bitmaps: FS1 places a bitmap with "$Bitmap:", "$Orientation:" (a matrix whose
// forward vector points at it), "$Rotation rate:", "$Distance:" and "$Light:".  FS2 reads "$Sun:" with angles and a
// scale.  FS2 puts a sun on the third column of its angles' matrix, so the angles come from the transposed FS1
// matrix.  FS1 drew the bitmap one unit either side of its centre at that distance (calculate_bitmap_points, kept
// commented out in Starfield/StarField.cpp), and FS2 draws a sun 0.05 * scale across at distance 1, so the
// scale is 20 / distance.  Every FS1 background bitmap is Sun01; with "$Light: 0" it becomes Sun01-unlit, which
// stars.tbl gives no light.
static int Fs1_in_background;
static char Fs1_bg_name[NAME_LENGTH];
static float Fs1_bg_matrix[9];
static int Fs1_bg_floats = -1;					// matrix numbers read so far, -1 outside $Orientation
static float Fs1_bg_distance;

static void fs1_mission_line(char *line)
{
	char *v, *end;
	matrix m;
	angles a;
	float f, scale;
	int light;

	if (line[0] == '#') {
		Fs1_in_background = (fs1_key(line, "#Background bitmaps") != NULL);
	}
	if (!Fs1_in_background) {
		fs1_out("%s", line);
		return;
	}

	if ((v = fs1_key(line, "$Bitmap:")) != NULL) {
		fs1_name(v, Fs1_bg_name, NAME_LENGTH);
		return;
	}
	if ((v = fs1_key(line, "$Orientation:")) != NULL) {
		Fs1_bg_floats = 0;
		line = v;
	}
	if ((Fs1_bg_floats >= 0) && (Fs1_bg_floats < 9)) {
		for (v = line; (Fs1_bg_floats < 9) && *v; v = end) {
			while ((*v == ',') || (*v == ' ') || (*v == '\t')) {
				v++;
			}
			f = (float)strtod(v, &end);
			if (end == v) {
				break;
			}
			Fs1_bg_matrix[Fs1_bg_floats++] = f;
		}
		return;
	}
	if (fs1_key(line, "$Rotation rate:")) {
		return;
	}
	if ((v = fs1_key(line, "$Distance:")) != NULL) {
		Fs1_bg_distance = (float)atof(v);
		return;
	}
	if ((v = fs1_key(line, "$Light:")) != NULL) {
		light = atoi(v);
		// the transpose: FS1's rows are FS2's columns
		m.rvec.x = Fs1_bg_matrix[0];	m.uvec.x = Fs1_bg_matrix[1];	m.fvec.x = Fs1_bg_matrix[2];
		m.rvec.y = Fs1_bg_matrix[3];	m.uvec.y = Fs1_bg_matrix[4];	m.fvec.y = Fs1_bg_matrix[5];
		m.rvec.z = Fs1_bg_matrix[6];	m.uvec.z = Fs1_bg_matrix[7];	m.fvec.z = Fs1_bg_matrix[8];
		vm_extract_angles_matrix(&a, &m);
		scale = (Fs1_bg_distance > 0.0f) ? (20.0f / Fs1_bg_distance) : 2.0f;
		fs1_out("$Sun: %s%s", Fs1_bg_name, light ? "" : "-unlit");
		fs1_out("+Angles: %f %f %f", a.p, a.b, a.h);
		fs1_out("+Scale: %f", scale);
		Fs1_bg_floats = -1;
		return;
	}
	fs1_out("%s", line);
}

// icons.tbl: FS1 gives one set of three animations (regular, fade, highlight) per briefing icon type, in the
// same type order as FS2's ICON_ defines.  FS2 reads three sets per type, one for each species.  Each FS1
// set is written out three times.
static char Fs1_icon_set[3][256];
static int Fs1_icon_count;

static void fs1_icons_line(char *line)
{
	int i;

	if (fs1_key(line, "$Name:")) {
		strncpy(Fs1_icon_set[Fs1_icon_count], line, 255);
		Fs1_icon_set[Fs1_icon_count][255] = 0;
		if (++Fs1_icon_count == 3) {
			for (i = 0; i < 9; i++) {
				fs1_out("%s", Fs1_icon_set[i % 3]);
			}
			Fs1_icon_count = 0;
		}
		return;
	}
	fs1_out("%s", line);
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
	{ "icons.tbl",			NULL,							fs1_icons_line },
	{ "*.fsm",				NULL,							fs1_mission_line },		// every FS1 mission
	{ "*.fs2",				NULL,							fs1_mission_line },		// the same, asked for by its FS2 name (cfile finds the .fsm)
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

// Some of FS1's missions start with a "//" line FRED wrote ("// Of 1800 total sexp nodes, ..."), before
// #Mission Info; FS2's parser only knows ';', so its mission-info read (the simulator's list) stopped there.
// A line starting "//" becomes a ';' comment, in place.
static void fs1_line_comments(char *text, int len)
{
	int i, line_start = 1;

	for (i = 0; i < len; i++) {
		if (line_start && (i + 1 < len) && (text[i] == '/') && (text[i + 1] == '/')) {
			text[i] = ';';
		}
		if (text[i] == '\n') {
			line_start = 1;
		} else if ((text[i] != ' ') && (text[i] != '\t') && (text[i] != '\r')) {
			line_start = 0;
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
	if ((i >= 4) && (!stricmp(filename + i - 4, ".fs2") || !stricmp(filename + i - 4, ".fsm"))) {
		fs1_line_comments(text, len);
	}
	for (i = 0; i < sizeof(Fs1_converters) / sizeof(Fs1_converters[0]); i++) {
		char *name = Fs1_converters[i].filename;
		int flen = strlen(filename), nlen = strlen(name);

		if (name[0] == '*') {
			// "*.ext": any file ending in .ext
			if ((flen >= nlen - 1) && !stricmp(filename + flen - (nlen - 1), name + 1)) {
				conv = &Fs1_converters[i];
			}
		} else if (!stricmp(filename, name)) {
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
	Fs1_in_background = 0;
	Fs1_bg_floats = -1;
	Fs1_bg_name[0] = 0;
	Fs1_bg_distance = 10.0f;
	Fs1_icon_count = 0;
	Fs1_num_weapons = 0;
	Fs1_in_precedence = 0;

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
