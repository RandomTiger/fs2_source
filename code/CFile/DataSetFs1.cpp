// Claude: the FreeSpace 1 demo data set: recognising it, and making it from its installer (see DataSetFs1.h,
// docs/FS1_DEMO.md).
//
// fsdemo10.exe is a Windows program with a zip appended.  The zip holds the 16-bit InstallShield setup and
// its cabinet, data1.cab; the cabinet holds the game: Data\freespace.vp (every table, mission, model and
// sound the demo has) and the programs, which are not taken.  So extraction is: find data1.cab in the zip,
// read it with the zip reader (DataSetInstall.cpp, which checks its CRC), then take the cabinet's Data\ files.

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CabReader.h"
#include "DataSetInstall.h"
#include "DataSetFs1.h"

static unsigned int fs1_get32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24); }

// creates every folder on the way to the file at path
static void fs1_make_folders_for(char *path)
{
	char tmp[MAX_PATH];
	char *p;

	strncpy(tmp, path, MAX_PATH - 1);
	tmp[MAX_PATH - 1] = 0;
	for (p = tmp; *p; p++) {
		if ((*p == '\\') && (p > tmp) && (p[-1] != ':')) {
			*p = 0;
			CreateDirectory(tmp, NULL);
			*p = '\\';
		}
	}
}

// ---------------------------------------------------------------------------------------------
// detection and extraction
//

int dataset_fs1_detect(char *dir)
{
	char path[MAX_PATH];
	unsigned char header[16], entry[44];
	unsigned int i, num, diroff;
	int found = 0;
	FILE *fp;

	_snprintf(path, MAX_PATH - 1, "%s\\Data\\freespace.vp", dir);
	path[MAX_PATH - 1] = 0;
	fp = fopen(path, "rb");
	if (!fp) {
		return 0;
	}
	if ((fread(header, 1, 16, fp) == 16) && !memcmp(header, "VPVP", 4)) {
		diroff = fs1_get32(header + 8);
		num = fs1_get32(header + 12);
		if ((num < 0x10000) && !fseek(fp, diroff, SEEK_SET)) {
			// each directory entry: offset, size, 32-byte name, time stamp
			for (i = 0; i < num; i++) {
				if (fread(entry, 1, 44, fp) != 44) {
					break;
				}
				entry[8 + 31] = 0;
				if (!_stricmp((char *)entry + 8, "demo.fsc")) {
					found = 1;
					break;
				}
			}
		}
	}
	fclose(fp);
	return found;
}

// the cabinet entries the data set folder takes: the Data folder, and any VP at the top
static int fs1_entry_wanted(const char *path)
{
	int len = (int)strlen(path);

	if (!_strnicmp(path, "Data\\", 5)) {
		return 1;
	}
	return !strchr(path, '\\') && (len > 3) && !_stricmp(path + len - 3, ".vp");
}

int dataset_fs1_extract(char *exe_dir, char *err, int err_len)
{
	char installer[MAX_PATH], folder[MAX_PATH], partial[MAX_PATH], path[MAX_PATH];
	unsigned char *data, *out;
	int len;
	cab_archive cab;
	FILE *fp;
	int i, ok, taken = 0;

	_snprintf(installer, MAX_PATH - 1, "%s\\%s", exe_dir, FS1_DEMO_INSTALLER);
	installer[MAX_PATH - 1] = 0;
	_snprintf(folder, MAX_PATH - 1, "%s\\%s", exe_dir, FS1_DEMO_FOLDER);
	folder[MAX_PATH - 1] = 0;
	_snprintf(partial, MAX_PATH - 1, "%s.partial", folder);
	partial[MAX_PATH - 1] = 0;

	if (GetFileAttributes(folder) != INVALID_FILE_ATTRIBUTES) {
		// never write into a folder that is already there
		_snprintf(err, err_len, "%s is already there; it is never written to.", folder);
		return 1;
	}
	if (dataset_zip_read(installer, "data1.cab", &data, &len, err, err_len)) {
		return 1;
	}
	if (cab_open(&cab, data, len, err, err_len)) {
		free(data);
		return 1;
	}

	CreateDirectory(partial, NULL);
	for (i = 0; i < cab.num_entries; i++) {
		if (!fs1_entry_wanted(cab.entries[i].path)) {
			continue;
		}
		out = (unsigned char *)malloc(cab.entries[i].expanded + 1);
		if (!out) {
			_snprintf(err, err_len, "Out of memory extracting %s.", cab.entries[i].path);
			break;
		}
		if (cab_extract(&cab, i, out, err, err_len)) {
			free(out);
			break;
		}
		_snprintf(path, MAX_PATH - 1, "%s\\%s", partial, cab.entries[i].path);
		path[MAX_PATH - 1] = 0;
		fs1_make_folders_for(path);
		fp = fopen(path, "wb");
		ok = fp && (fwrite(out, 1, cab.entries[i].expanded, fp) == cab.entries[i].expanded);
		if (fp && fclose(fp)) {
			ok = 0;
		}
		free(out);
		if (!ok) {
			_snprintf(err, err_len, "Could not write %s.", path);
			break;
		}
		taken++;
	}
	ok = (i == cab.num_entries);
	cab_close(&cab);
	free(data);
	if (!ok) {
		return 1;
	}
	if (!taken) {
		_snprintf(err, err_len, "%s holds no game files.", installer);
		return 1;
	}
	if (!MoveFile(partial, folder)) {
		_snprintf(err, err_len, "Could not rename %s to %s.", partial, folder);
		return 1;
	}
	return 0;
}
