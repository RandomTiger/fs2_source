// Claude: InstallShield 5 cabinet reader (see CabReader.h, docs/FS1_DEMO.md).
//
// Layout, as fsdemo10.exe's data1.cab has it (all values little-endian):
//   0x00  "ISc(", then the version (0x01005100: major version 5 in bits 12-15), the volume info, and the
//         offset and size of the cabinet descriptor
//   cabinet descriptor + 0x0c  offset of the file table, from the descriptor
//                      + 0x1c  directory count
//                      + 0x28  file count
//   file table: one 32-bit offset per directory (to its name), then one per file (to its descriptor), all
//   from the start of the file table.  A file descriptor is 0x2a bytes: name offset, directory index,
//   16-bit flags, expanded size, compressed size, 0x14 bytes of attributes and dates, data offset.
//   Compressed data is a run of chunks, each a 16-bit length and that many bytes of raw deflate, inflated
//   by the zip reader's inflater (DataSetInstall.cpp); a chunk's output size is not stored anywhere.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "CabReader.h"
#include "DataSetInstall.h"

static unsigned int cab_get16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned int cab_get32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24); }

// the NUL-terminated string at off, if it lies inside the cabinet
static const char *cab_string(cab_archive *cab, unsigned int off)
{
	unsigned int i;

	for (i = off; i < cab->len; i++) {
		if (!cab->data[i]) {
			return (const char *)cab->data + off;
		}
	}
	return NULL;
}

int cab_open(cab_archive *cab, const unsigned char *data, unsigned int len, char *err, int err_len)
{
	unsigned int version, desc, desc_size, table, num_dirs, num_files, i, d, dir, name_off;
	const char *name, *dir_name;
	cab_entry *e;

	memset(cab, 0, sizeof(*cab));
	cab->data = data;
	cab->len = len;

	if ((len < 20) || memcmp(data, "ISc(", 4)) {
		_snprintf(err, err_len, "Not an InstallShield cabinet.");
		return 1;
	}
	version = cab_get32(data + 4);
	if (((version >> 12) & 0xf) != 5) {
		_snprintf(err, err_len, "InstallShield cabinet version %08x; only version 5 is read.", version);
		return 1;
	}
	desc = cab_get32(data + 12);
	desc_size = cab_get32(data + 16);
	if ((desc_size < 0x30) || (desc > len) || (desc_size > len - desc)) {
		_snprintf(err, err_len, "The cabinet's descriptor lies outside it.");
		return 1;
	}
	table = desc + cab_get32(data + desc + 0x0c);
	num_dirs = cab_get32(data + desc + 0x1c);
	num_files = cab_get32(data + desc + 0x28);
	if ((table > len) || (num_dirs > 0x10000) || (num_files > 0x10000) || ((num_dirs + num_files) * 4 > len - table)) {
		_snprintf(err, err_len, "The cabinet's file table lies outside it.");
		return 1;
	}

	cab->entries = (cab_entry *)malloc(num_files * sizeof(cab_entry) + 1);
	if (!cab->entries) {
		_snprintf(err, err_len, "Out of memory reading the cabinet's file list.");
		return 1;
	}
	for (i = 0; i < num_files; i++) {
		d = table + cab_get32(data + table + (num_dirs + i) * 4);
		if ((d < table) || (d > len) || (len - d < 0x2a)) {
			_snprintf(err, err_len, "File %u's descriptor lies outside the cabinet.", i);
			cab_close(cab);
			return 1;
		}
		name_off = cab_get32(data + d);
		dir = cab_get32(data + d + 4);
		e = &cab->entries[cab->num_entries];
		e->flags = cab_get16(data + d + 8);
		e->expanded = cab_get32(data + d + 10);
		e->compressed = cab_get32(data + d + 14);
		e->offset = cab_get32(data + d + 0x26);
		if ((e->flags & 0x8) || !name_off) {
			continue;			// an unused slot
		}
		name = cab_string(cab, table + name_off);
		dir_name = (dir < num_dirs) ? cab_string(cab, table + cab_get32(data + table + dir * 4)) : NULL;
		if (!name || !dir_name || (strlen(dir_name) + strlen(name) + 2 > CAB_MAX_PATH)) {
			_snprintf(err, err_len, "File %u's name or folder lies outside the cabinet.", i);
			cab_close(cab);
			return 1;
		}
		if (dir_name[0]) {
			_snprintf(e->path, CAB_MAX_PATH, "%s\\%s", dir_name, name);
		} else {
			_snprintf(e->path, CAB_MAX_PATH, "%s", name);
		}
		if ((e->offset > len) || (e->compressed > len - e->offset)) {
			_snprintf(err, err_len, "%s's data lies outside the cabinet.", e->path);
			cab_close(cab);
			return 1;
		}
		cab->num_entries++;
	}
	return 0;
}

void cab_close(cab_archive *cab)
{
	if (cab->entries) {
		free(cab->entries);
	}
	cab->entries = NULL;
	cab->num_entries = 0;
}

int cab_extract(cab_archive *cab, int index, unsigned char *dst, char *err, int err_len)
{
	cab_entry *e;
	const unsigned char *p, *end;
	unsigned int done = 0, chunk;
	int produced;

	if ((index < 0) || (index >= cab->num_entries)) {
		_snprintf(err, err_len, "No file %d in the cabinet.", index);
		return 1;
	}
	e = &cab->entries[index];
	if (e->flags & (CAB_FILE_SPLIT | CAB_FILE_OBFUSCATED)) {
		_snprintf(err, err_len, "%s is split across volumes or obfuscated, which is not read here.", e->path);
		return 1;
	}

	p = cab->data + e->offset;
	end = p + e->compressed;
	if (!(e->flags & CAB_FILE_COMPRESSED)) {
		if (e->compressed != e->expanded) {
			_snprintf(err, err_len, "%s is stored, but its two sizes differ.", e->path);
			return 1;
		}
		memcpy(dst, p, e->expanded);
		return 0;
	}

	while (p < end) {
		if (end - p < 2) {
			break;
		}
		chunk = cab_get16(p);
		p += 2;
		if ((chunk == 0) || (chunk > (unsigned int)(end - p))) {
			break;
		}
		if (!dataset_inflate(p, chunk, dst + done, e->expanded - done, &produced, NULL)) {
			_snprintf(err, err_len, "%s is damaged: a chunk at byte %u of its data does not decode.", e->path, (unsigned int)(p - 2 - (cab->data + e->offset)));
			return 1;
		}
		done += produced;
		p += chunk;
	}
	if ((p != end) || (done != e->expanded)) {
		_snprintf(err, err_len, "%s is damaged: %u of its %u bytes decoded.", e->path, done, e->expanded);
		return 1;
	}
	return 0;
}
