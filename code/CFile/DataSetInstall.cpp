// Claude: choosing the data set folder at start-up, and making it from a demo's original installer
// (D-20261018-221704, see DataSetInstall.h).
//
// The FreeSpace 2 demo's installer, fs2demo10.exe, is a WinZip self-extractor: a Windows program with a
// zip archive appended.  The game's files (root_fs2.vp, sparky_fs2.vp, sparky_hi_fs2.vp and Data\) are
// plain entries in that zip, stored with deflate, so extracting them needs a zip directory reader and
// an inflater (RFC 1951), both here.  The installer itself (a 16-bit InstallShield setup inside the
// zip) is never run.  The FreeSpace 1 demo keeps its files inside InstallShield's data1.cab instead; its
// reader belongs to the FS1 work and is not here.
//
// Extraction writes into "<set>.partial" and renames it to "<set>" only when every file is written
// and its CRC matches the zip's, so a half-made folder is never used.  An existing folder is never
// written to.

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "DataSetInstall.h"

static char *Data_set_names[NUM_DATA_SETS] = { "FS2 retail", "FS2 demo", "FS1 retail", "FS1 demo" };

char *data_set_name(int set)
{
	return ((set >= 0) && (set < NUM_DATA_SETS)) ? Data_set_names[set] : "unknown";
}

// the data sets that live in a subfolder made from an installer
typedef struct dataset_info {
	int	set;					// DATA_SET_*
	char	*name;				// subfolder, and the -dataset value
	char	*installer;			// original installer next to the exe
	int	can_extract;		// 0: the installer's format is not read here (FS1's cab)
} dataset_info;

static dataset_info Datasets[] = {
	{ DATA_SET_FS2_DEMO,	"fs2demo",	"fs2demo10.exe",	1 },
	{ DATA_SET_FS1_DEMO,	"fs1demo",	"fsdemo10.exe",	0 },		// the FS1 branch adds the InstallShield cab reader
};
#define NUM_DATASETS		(int)(sizeof(Datasets) / sizeof(Datasets[0]))
#define DATASET_DEFAULT	0						// fs2demo, when more than one set could run

// -----------------------------------------------------------------------------------------
// inflate (RFC 1951), after the structure of Joergen Ibsen's tinf
//

typedef struct inf_tree {
	unsigned short	counts[16];		// codes of each length
	unsigned short	symbols[288];	// symbols in canonical order
} inf_tree;

typedef struct inf_state {
	const unsigned char	*src, *src_end;
	unsigned int			tag;
	int						bitcount;
	unsigned char			*dst, *dst_start, *dst_end;
	int						bad;					// ran off the input, or a malformed stream
	void						(*progress)(int done);
	unsigned char			*progress_mark;
} inf_state;

static unsigned char Length_bits[30], Dist_bits[30];
static unsigned short Length_base[30], Dist_base[30];
static int Inf_tables_made = 0;

static void inf_bits_base(unsigned char *bits, unsigned short *base, int delta, int first)
{
	int i, sum;

	for (i = 0; i < delta; i++) {
		bits[i] = 0;
	}
	for (i = 0; i < 30 - delta; i++) {
		bits[i + delta] = (unsigned char)(i / delta);
	}
	for (sum = first, i = 0; i < 30; i++) {
		base[i] = (unsigned short)sum;
		sum += 1 << bits[i];
	}
}

static void inf_make_tables()
{
	if (Inf_tables_made) {
		return;
	}
	inf_bits_base(Length_bits, Length_base, 4, 3);
	Length_bits[28] = 0;		// length 258 is its own code
	Length_base[28] = 258;
	inf_bits_base(Dist_bits, Dist_base, 2, 1);
	Inf_tables_made = 1;
}

static void inf_build_tree(inf_tree *t, const unsigned char *lengths, int num)
{
	unsigned short offs[16];
	int i, sum;

	memset(t->counts, 0, sizeof(t->counts));
	for (i = 0; i < num; i++) {
		t->counts[lengths[i]]++;
	}
	t->counts[0] = 0;
	for (sum = 0, i = 0; i < 16; i++) {
		offs[i] = (unsigned short)sum;
		sum += t->counts[i];
	}
	for (i = 0; i < num; i++) {
		if (lengths[i]) {
			t->symbols[offs[lengths[i]]++] = (unsigned short)i;
		}
	}
}

static int inf_getbit(inf_state *s)
{
	int bit;

	if (!s->bitcount--) {
		if (s->src >= s->src_end) {
			s->bad = 1;
			s->tag = 0;
		} else {
			s->tag = *s->src++;
		}
		s->bitcount = 7;
	}
	bit = s->tag & 1;
	s->tag >>= 1;
	return bit;
}

static int inf_bits(inf_state *s, int num, int base)
{
	int val = 0, i;

	for (i = 0; i < num; i++) {
		if (inf_getbit(s)) {
			val += 1 << i;
		}
	}
	return val + base;
}

static int inf_symbol(inf_state *s, const inf_tree *t)
{
	int sum = 0, cur = 0, len = 0;

	do {
		cur = 2 * cur + inf_getbit(s);
		if (++len > 15) {
			s->bad = 1;
			return 0;
		}
		sum += t->counts[len];
		cur -= t->counts[len];
	} while (cur >= 0);
	return t->symbols[sum + cur];
}

static int inf_dynamic_trees(inf_state *s, inf_tree *lt, inf_tree *dt)
{
	static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	unsigned char lengths[288 + 32];
	inf_tree code_tree;
	int hlit, hdist, hclen, i, num, sym, prev, len;

	hlit = inf_bits(s, 5, 257);
	hdist = inf_bits(s, 5, 1);
	hclen = inf_bits(s, 4, 4);
	if ((hlit > 286) || (hdist > 30)) {
		return 0;
	}
	memset(lengths, 0, sizeof(lengths));
	for (i = 0; i < hclen; i++) {
		lengths[order[i]] = (unsigned char)inf_bits(s, 3, 0);
	}
	inf_build_tree(&code_tree, lengths, 19);

	for (num = 0; num < hlit + hdist; ) {
		sym = inf_symbol(s, &code_tree);
		if (s->bad) {
			return 0;
		}
		switch (sym) {
		case 16:
			if (num == 0) {
				return 0;
			}
			prev = lengths[num - 1];
			len = inf_bits(s, 2, 3);
			break;
		case 17:
			prev = 0;
			len = inf_bits(s, 3, 3);
			break;
		case 18:
			prev = 0;
			len = inf_bits(s, 7, 11);
			break;
		default:
			prev = sym;
			len = 1;
			break;
		}
		if (num + len > hlit + hdist) {
			return 0;
		}
		while (len--) {
			lengths[num++] = (unsigned char)prev;
		}
	}
	inf_build_tree(lt, lengths, hlit);
	inf_build_tree(dt, lengths + hlit, hdist);
	return 1;
}

static int inf_block(inf_state *s, const inf_tree *lt, const inf_tree *dt)
{
	int sym, len, dsym, offs, i;

	for (;;) {
		sym = inf_symbol(s, lt);
		if (s->bad) {
			return 0;
		}
		if (sym == 256) {
			return 1;
		}
		if (sym < 256) {
			if (s->dst >= s->dst_end) {
				return 0;
			}
			*s->dst++ = (unsigned char)sym;
		} else {
			sym -= 257;
			if (sym >= 29) {
				return 0;
			}
			len = inf_bits(s, Length_bits[sym], Length_base[sym]);
			dsym = inf_symbol(s, dt);
			if (dsym >= 30) {
				return 0;
			}
			offs = inf_bits(s, Dist_bits[dsym], Dist_base[dsym]);
			if (s->bad || (offs > s->dst - s->dst_start) || (len > s->dst_end - s->dst)) {
				return 0;
			}
			for (i = 0; i < len; i++) {
				s->dst[i] = s->dst[i - offs];
			}
			s->dst += len;
		}
		if (s->progress && (s->dst - s->progress_mark >= (1 << 20))) {
			s->progress((int)(s->dst - s->progress_mark));
			s->progress_mark = s->dst;
		}
	}
}

static int inf_stored(inf_state *s)
{
	unsigned int len, invlen;

	// the rest of the current byte is padding; src already points at the next byte
	s->bitcount = 0;
	if (s->src_end - s->src < 4) {
		return 0;
	}
	len = s->src[0] | (s->src[1] << 8);
	invlen = s->src[2] | (s->src[3] << 8);
	s->src += 4;
	if ((len != (~invlen & 0xffff)) || ((unsigned int)(s->src_end - s->src) < len) || ((unsigned int)(s->dst_end - s->dst) < len)) {
		return 0;
	}
	memcpy(s->dst, s->src, len);
	s->dst += len;
	s->src += len;
	return 1;
}

// inflates src into dst, up to dst_cap bytes, with the number written in *produced (DataSetInstall.h)
int dataset_inflate(const unsigned char *src, int src_len, unsigned char *dst, int dst_cap, int *produced, void (*progress)(int done))
{
	static unsigned char fixed_lengths[288 + 30];
	static inf_tree fixed_lt, fixed_dt;
	static int fixed_made = 0;
	inf_tree lt, dt;
	inf_state s;
	int final, type, ok, i;

	inf_make_tables();
	if (!fixed_made) {
		for (i = 0; i < 144; i++) fixed_lengths[i] = 8;
		for (; i < 256; i++) fixed_lengths[i] = 9;
		for (; i < 280; i++) fixed_lengths[i] = 7;
		for (; i < 288; i++) fixed_lengths[i] = 8;
		inf_build_tree(&fixed_lt, fixed_lengths, 288);
		for (i = 0; i < 30; i++) fixed_lengths[i] = 5;
		inf_build_tree(&fixed_dt, fixed_lengths, 30);
		fixed_made = 1;
	}

	*produced = 0;
	memset(&s, 0, sizeof(s));
	s.src = src;
	s.src_end = src + src_len;
	s.dst = s.dst_start = s.progress_mark = dst;
	s.dst_end = dst + dst_cap;
	s.progress = progress;

	do {
		final = inf_getbit(&s);
		type = inf_bits(&s, 2, 0);
		switch (type) {
		case 0:
			ok = inf_stored(&s);
			break;
		case 1:
			ok = inf_block(&s, &fixed_lt, &fixed_dt);
			break;
		case 2:
			ok = inf_dynamic_trees(&s, &lt, &dt) && inf_block(&s, &lt, &dt);
			break;
		default:
			ok = 0;
			break;
		}
		if (!ok || s.bad) {
			return 0;
		}
	} while (!final && (s.src < s.src_end));		// a sync-flushed stream (InstallShield's chunks) ends without a final block

	if (progress && (s.dst > s.progress_mark)) {
		progress((int)(s.dst - s.progress_mark));
	}
	*produced = (int)(s.dst - s.dst_start);
	return 1;
}

// inflates src into dst; 1 if the stream was whole and filled dst exactly
static int inflate_buffer(const unsigned char *src, int src_len, unsigned char *dst, int dst_len, void (*progress)(int done))
{
	int produced;

	return dataset_inflate(src, src_len, dst, dst_len, &produced, progress) && (produced == dst_len);
}

// -----------------------------------------------------------------------------------------
// CRC-32, as zip stores it
//

static unsigned int Crc_table[256];

static unsigned int crc32_buffer(const unsigned char *p, int len)
{
	unsigned int c;
	int i, k;

	if (!Crc_table[1]) {
		for (i = 0; i < 256; i++) {
			c = (unsigned int)i;
			for (k = 0; k < 8; k++) {
				c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
			}
			Crc_table[i] = c;
		}
	}
	c = 0xFFFFFFFF;
	for (i = 0; i < len; i++) {
		c = Crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFF;
}

unsigned int dataset_crc32(const unsigned char *p, int len)
{
	return crc32_buffer(p, len);
}

// -----------------------------------------------------------------------------------------
// progress window
//

static HWND Progress_wnd = NULL, Progress_text = NULL;
static char *Progress_title = "";
static double Progress_total = 1.0, Progress_done = 0.0;
static int Progress_shown = -1;

static void progress_pump()
{
	MSG msg;

	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static void progress_open(char *title, double total)
{
	WNDCLASS wc;
	int w = 420, h = 110;

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = DefWindowProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = "FS2DataSetProgress";
	RegisterClass(&wc);

	Progress_title = title;
	Progress_total = (total > 0) ? total : 1.0;
	Progress_done = 0.0;
	Progress_shown = -1;
	Progress_wnd = CreateWindow("FS2DataSetProgress", "FreeSpace 2", WS_POPUP | WS_CAPTION | WS_VISIBLE,
		(GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, NULL, NULL, wc.hInstance, NULL);
	if (Progress_wnd) {
		Progress_text = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 25, w - 20, 40, Progress_wnd, NULL, wc.hInstance, NULL);
	}
	progress_pump();
}

static void progress_add(int bytes)
{
	char buf[256];
	int pct;

	Progress_done += bytes;
	pct = (int)(100.0 * Progress_done / Progress_total);
	if (pct > 100) {
		pct = 100;
	}
	if (Progress_text && (pct != Progress_shown)) {
		sprintf(buf, "%s\n%d%%", Progress_title, pct);
		SetWindowText(Progress_text, buf);
		Progress_shown = pct;
	}
	progress_pump();
}

static void progress_close()
{
	if (Progress_wnd) {
		DestroyWindow(Progress_wnd);
	}
	Progress_wnd = Progress_text = NULL;
	progress_pump();
}

// -----------------------------------------------------------------------------------------
// zip
//

static unsigned int get16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned int get32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24); }

// the zip entries a data set folder takes: the VPs at the top, and the Data folder
static int zip_entry_wanted(const char *name)
{
	int len = (int)strlen(name);

	if (!_strnicmp(name, "Data/", 5)) {
		return 1;
	}
	return !strchr(name, '/') && (len > 3) && !_stricmp(name + len - 3, ".vp");
}

static unsigned char *read_whole_file(char *path, int *len)
{
	HANDLE f;
	DWORD size, got;
	unsigned char *buf;

	f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE) {
		return NULL;
	}
	size = GetFileSize(f, NULL);
	buf = (unsigned char *)VirtualAlloc(NULL, size ? size : 1, MEM_COMMIT, PAGE_READWRITE);
	if (!buf || !ReadFile(f, buf, size, &got, NULL) || (got != size)) {
		if (buf) {
			VirtualFree(buf, 0, MEM_RELEASE);
		}
		CloseHandle(f);
		return NULL;
	}
	CloseHandle(f);
	*len = (int)size;
	return buf;
}

static int write_whole_file(char *path, const unsigned char *data, int len)
{
	HANDLE f;
	DWORD put;
	int ok;

	f = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ok = WriteFile(f, data, (DWORD)len, &put, NULL) && (put == (DWORD)len);
	CloseHandle(f);
	return ok;
}

// creates every folder on the way to path (a file's path when is_file)
static void make_folders(char *path, int is_file)
{
	char buf[MAX_PATH];
	char *p;

	strncpy(buf, path, MAX_PATH - 1);
	buf[MAX_PATH - 1] = 0;
	for (p = buf + 3; *p; p++) {
		if ((*p == '\\') || (*p == '/')) {
			*p = 0;
			CreateDirectory(buf, NULL);
			*p = '\\';
		}
	}
	if (!is_file) {
		CreateDirectory(buf, NULL);
	}
}

// extracts the wanted entries of the zip appended to installer into folder; 0 and err on failure
static int zip_extract(char *installer, char *folder, char *title, char *err, int err_len)
{
	unsigned char *zip, *out;
	const unsigned char *eocd = NULL, *cd, *p, *lh, *data;
	int zip_len, i, entries, cd_size, cd_offset, base, n_len, x_len, c_len, method, csize, usize, ok = 1;
	unsigned int crc;
	double total = 0;
	char name[MAX_PATH], path[MAX_PATH], *q;

	zip = read_whole_file(installer, &zip_len);
	if (!zip) {
		_snprintf(err, err_len, "Could not read %s.", installer);
		return 0;
	}

	// the end of central directory record is in the last 64 KB
	for (i = zip_len - 22; (i >= 0) && (i >= zip_len - 22 - 65535); i--) {
		if (get32(zip + i) == 0x06054b50) {
			eocd = zip + i;
			break;
		}
	}
	if (!eocd) {
		_snprintf(err, err_len, "%s is not a zip self-extractor this game can read.", installer);
		VirtualFree(zip, 0, MEM_RELEASE);
		return 0;
	}
	entries = get16(eocd + 10);
	cd_size = get32(eocd + 12);
	cd_offset = get32(eocd + 16);
	// offsets count from the start of the zip, which follows the self-extractor's program
	base = (int)(eocd - zip) - cd_size - cd_offset;
	if ((base < 0) || (base + cd_offset + cd_size > zip_len)) {
		_snprintf(err, err_len, "%s has a damaged zip directory.", installer);
		VirtualFree(zip, 0, MEM_RELEASE);
		return 0;
	}
	cd = zip + base + cd_offset;

	// first pass: what to extract, for the progress total
	for (p = cd, i = 0; i < entries; i++) {
		if (get32(p) != 0x02014b50) {
			ok = 0;
			break;
		}
		n_len = get16(p + 28);
		_snprintf(name, MAX_PATH - 1, "%.*s", n_len, (const char *)(p + 46));
		name[MAX_PATH - 1] = 0;
		if (zip_entry_wanted(name)) {
			total += get32(p + 24);
		}
		p += 46 + n_len + get16(p + 30) + get16(p + 32);
	}
	if (!ok) {
		_snprintf(err, err_len, "%s has a damaged zip directory.", installer);
		VirtualFree(zip, 0, MEM_RELEASE);
		return 0;
	}

	progress_open(title, total);
	for (p = cd, i = 0; ok && (i < entries); i++) {
		method = get16(p + 10);
		crc = get32(p + 16);
		csize = get32(p + 20);
		usize = get32(p + 24);
		n_len = get16(p + 28);
		x_len = get16(p + 30);
		c_len = get16(p + 32);
		_snprintf(name, MAX_PATH - 1, "%.*s", n_len, (const char *)(p + 46));
		name[MAX_PATH - 1] = 0;
		lh = zip + base + get32(p + 42);
		p += 46 + n_len + x_len + c_len;
		if (!zip_entry_wanted(name)) {
			continue;
		}

		_snprintf(path, MAX_PATH - 1, "%s\\%s", folder, name);
		path[MAX_PATH - 1] = 0;
		for (q = path; *q; q++) {
			if (*q == '/') {
				*q = '\\';
			}
		}
		if (name[n_len - 1] == '/') {		// a folder
			make_folders(path, 0);
			continue;
		}
		make_folders(path, 1);

		if ((lh < zip) || (lh + 30 > zip + zip_len) || (get32(lh) != 0x04034b50)) {
			_snprintf(err, err_len, "%s: %s has a damaged zip header.", installer, name);
			ok = 0;
			break;
		}
		data = lh + 30 + get16(lh + 26) + get16(lh + 28);
		if (data + csize > zip + zip_len) {
			_snprintf(err, err_len, "%s: %s is cut short.", installer, name);
			ok = 0;
			break;
		}
		out = (unsigned char *)VirtualAlloc(NULL, usize ? usize : 1, MEM_COMMIT, PAGE_READWRITE);
		if (!out) {
			_snprintf(err, err_len, "Not enough memory to extract %s.", name);
			ok = 0;
			break;
		}
		if (method == 0) {
			if (csize != usize) {
				ok = 0;
			} else {
				memcpy(out, data, usize);
				progress_add(usize);
			}
		} else if (method == 8) {
			ok = inflate_buffer(data, csize, out, usize, progress_add);
		} else {
			ok = 0;
		}
		if (!ok) {
			_snprintf(err, err_len, "%s: %s could not be decompressed.", installer, name);
		} else if (crc32_buffer(out, usize) != crc) {
			_snprintf(err, err_len, "%s: %s does not match its checksum.", installer, name);
			ok = 0;
		} else if (!write_whole_file(path, out, usize)) {
			_snprintf(err, err_len, "Could not write %s.", path);
			ok = 0;
		}
		VirtualFree(out, 0, MEM_RELEASE);
	}
	progress_close();
	VirtualFree(zip, 0, MEM_RELEASE);
	return ok;
}

// reads one entry of the zip appended to installer into memory, checking its CRC (DataSetInstall.h)
int dataset_zip_read(char *installer, char *entry, unsigned char **data, int *len, char *err, int err_len)
{
	unsigned char *zip, *out = NULL;
	const unsigned char *eocd = NULL, *p, *lh, *src;
	int zip_len, i, entries, cd_size, cd_offset, base, n_len, method, csize, usize = 0, ok = 0;
	unsigned int crc;

	*data = NULL;
	*len = 0;
	zip = read_whole_file(installer, &zip_len);
	if (!zip) {
		_snprintf(err, err_len, "Could not read %s.", installer);
		return 1;
	}
	for (i = zip_len - 22; (i >= 0) && (i >= zip_len - 22 - 65535); i--) {
		if (get32(zip + i) == 0x06054b50) {
			eocd = zip + i;
			break;
		}
	}
	if (!eocd) {
		_snprintf(err, err_len, "%s is not a zip self-extractor this game can read.", installer);
		VirtualFree(zip, 0, MEM_RELEASE);
		return 1;
	}
	entries = get16(eocd + 10);
	cd_size = get32(eocd + 12);
	cd_offset = get32(eocd + 16);
	base = (int)(eocd - zip) - cd_size - cd_offset;
	if ((base < 0) || (base + cd_offset + cd_size > zip_len)) {
		_snprintf(err, err_len, "%s has a damaged zip directory.", installer);
		VirtualFree(zip, 0, MEM_RELEASE);
		return 1;
	}

	_snprintf(err, err_len, "%s has no %s in it.", installer, entry);
	for (p = zip + base + cd_offset, i = 0; i < entries; i++) {
		if ((p + 46 > eocd) || (get32(p) != 0x02014b50)) {
			_snprintf(err, err_len, "%s has a damaged zip directory.", installer);
			break;
		}
		n_len = get16(p + 28);
		if ((n_len == (int)strlen(entry)) && (p + 46 + n_len <= eocd) && !_strnicmp((const char *)(p + 46), entry, n_len)) {
			method = get16(p + 10);
			crc = get32(p + 16);
			csize = get32(p + 20);
			usize = get32(p + 24);
			lh = zip + base + get32(p + 42);
			if ((lh < zip) || (lh + 30 > zip + zip_len) || (get32(lh) != 0x04034b50)) {
				_snprintf(err, err_len, "%s: %s has a damaged zip header.", installer, entry);
				break;
			}
			src = lh + 30 + get16(lh + 26) + get16(lh + 28);
			if ((csize < 0) || (usize < 0) || (src + csize > zip + zip_len)) {
				_snprintf(err, err_len, "%s: %s is cut short.", installer, entry);
				break;
			}
			out = (unsigned char *)malloc(usize ? usize : 1);
			if (!out) {
				_snprintf(err, err_len, "Not enough memory to read %s.", entry);
				break;
			}
			if (method == 0) {
				ok = (csize == usize);
				if (ok) {
					memcpy(out, src, usize);
				}
			} else if (method == 8) {
				ok = inflate_buffer(src, csize, out, usize, NULL);
			}
			if (!ok) {
				_snprintf(err, err_len, "%s: %s could not be decompressed.", installer, entry);
			} else if (crc32_buffer(out, usize) != crc) {
				_snprintf(err, err_len, "%s: %s does not match its checksum.", installer, entry);
				ok = 0;
			}
			break;
		}
		p += 46 + n_len + get16(p + 30) + get16(p + 32);
	}
	VirtualFree(zip, 0, MEM_RELEASE);
	if (!ok) {
		if (out) {
			free(out);
		}
		return 1;
	}
	err[0] = 0;
	*data = out;
	*len = usize;
	return 0;
}

// -----------------------------------------------------------------------------------------
// choosing the folder
//

static int file_exists(char *path)
{
	DWORD a = GetFileAttributes(path);
	return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int folder_has_vps(char *folder)
{
	char pattern[MAX_PATH];
	WIN32_FIND_DATA fd;
	HANDLE h;

	_snprintf(pattern, MAX_PATH - 1, "%s\\*.vp", folder);
	pattern[MAX_PATH - 1] = 0;
	h = FindFirstFile(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		return 0;
	}
	FindClose(h);
	return 1;
}

static int use_dataset(char *exe_dir, int idx, char *dir, int dir_len, char *err, int err_len)
{
	dataset_info *ds = &Datasets[idx];
	char folder[MAX_PATH], partial[MAX_PATH], installer[MAX_PATH], title[128];

	_snprintf(folder, MAX_PATH - 1, "%s\\%s", exe_dir, ds->name);
	folder[MAX_PATH - 1] = 0;
	_snprintf(installer, MAX_PATH - 1, "%s\\%s", exe_dir, ds->installer);
	installer[MAX_PATH - 1] = 0;
	strncpy(dir, folder, dir_len - 1);
	dir[dir_len - 1] = 0;

	if (folder_has_vps(folder)) {
		return 0;
	}
	if (!file_exists(installer)) {
		_snprintf(err, err_len, "There is no %s folder, and no %s next to the game to make it from.", ds->name, ds->installer);
		return 1;
	}
	if (!ds->can_extract) {
		_snprintf(err, err_len, "This version of the game cannot extract %s yet.", ds->installer);
		return 1;
	}
	if (GetFileAttributes(folder) != INVALID_FILE_ATTRIBUTES) {
		// never write into a folder that is already there
		_snprintf(err, err_len, "%s exists but holds no game data.  Remove it, and the game will make it again from %s.", folder, ds->installer);
		return 1;
	}

	_snprintf(partial, MAX_PATH - 1, "%s.partial", folder);
	partial[MAX_PATH - 1] = 0;
	make_folders(partial, 0);
	_snprintf(title, sizeof(title) - 1, "Extracting the game files from %s (first start only)", ds->installer);
	title[sizeof(title) - 1] = 0;
	if (!zip_extract(installer, partial, title, err, err_len)) {
		return 1;
	}
	if (!MoveFile(partial, folder)) {
		_snprintf(err, err_len, "Could not rename %s to %s.", partial, folder);
		return 1;
	}
	return 0;
}

int dataset_select_folder(char *exe_dir, char *requested, int recorded, char *dir, int dir_len, char *err, int err_len)
{
	char folder[MAX_PATH], installer[MAX_PATH];
	int i, found = -1, count = 0;

	strncpy(dir, exe_dir, dir_len - 1);
	dir[dir_len - 1] = 0;
	err[0] = 0;

	// 1. -dataset
	if (requested && requested[0]) {
		if (!_stricmp(requested, "retail")) {
			return 0;
		}
		for (i = 0; i < NUM_DATASETS; i++) {
			if (!_stricmp(requested, Datasets[i].name)) {
				return use_dataset(exe_dir, i, dir, dir_len, err, err_len);
			}
		}
		_snprintf(err, err_len, "Unknown data set '%s' (-dataset takes fs2demo, fs1demo or retail).", requested);
		return 1;
	}

	// 2. a replay runs on the data set it was recorded on
	if (recorded == DATA_SET_FS2_RETAIL) {
		return 0;
	}
	if (recorded == DATA_SET_FS2_DEMO) {
		// a flat demo install (the VPs next to the exe) is fine too; the data set check after cfile_init
		// still refuses the wrong data
		_snprintf(folder, MAX_PATH - 1, "%s\\%s", exe_dir, Datasets[DATASET_DEFAULT].name);
		folder[MAX_PATH - 1] = 0;
		_snprintf(installer, MAX_PATH - 1, "%s\\%s", exe_dir, Datasets[DATASET_DEFAULT].installer);
		installer[MAX_PATH - 1] = 0;
		if (folder_has_vps(folder) || file_exists(installer) || !folder_has_vps(exe_dir)) {
			return use_dataset(exe_dir, DATASET_DEFAULT, dir, dir_len, err, err_len);
		}
		return 0;
	}

	// 3. retail data next to the exe, as always
	if (folder_has_vps(exe_dir)) {
		return 0;
	}

	// 4. the only set there is, or can be made; with more than one, the default
	for (i = 0; i < NUM_DATASETS; i++) {
		_snprintf(folder, MAX_PATH - 1, "%s\\%s", exe_dir, Datasets[i].name);
		folder[MAX_PATH - 1] = 0;
		_snprintf(installer, MAX_PATH - 1, "%s\\%s", exe_dir, Datasets[i].installer);
		installer[MAX_PATH - 1] = 0;
		if (folder_has_vps(folder) || (Datasets[i].can_extract && file_exists(installer))) {
			found = i;
			count++;
		}
	}
	if (count == 1) {
		return use_dataset(exe_dir, found, dir, dir_len, err, err_len);
	}
	if (count > 1) {
		return use_dataset(exe_dir, DATASET_DEFAULT, dir, dir_len, err, err_len);
	}

	// nothing found: run in the exe's folder, which reports the missing data as it always has
	return 0;
}
