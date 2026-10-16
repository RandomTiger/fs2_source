// TraceDiff: compare two FS2 gameplay state traces (see code/Replay/TraceFormat.h).
//
//   TraceDiff <a.trace> <b.trace>                 exact: first frame whose state differs
//   TraceDiff <a.trace> <b.trace> -tol <file>     within per-kind tolerances (hard checks stay exact)
//   TraceDiff <a.trace> <b.trace> -curve          CSV of the largest difference per kind, per full frame
//
// Exit code: 0 = match, 1 = divergence, 2 = usage or file error.

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <map>
#include <vector>
#include <string>

typedef unsigned char ubyte;
typedef unsigned int uint;

#include "../../code/Replay/TraceFormat.h"

struct Frame {
	int head[8];				// frame, missiontime, frametime, timestamp_ticker, rand_count, hash_lo, hash_hi, size
	const ubyte *payload;	// NULL if only the hash was written
};

struct Trace {
	std::vector<ubyte> data;
	std::vector<Frame> frames;
};

struct Object {
	int type;
	std::vector<int> vals;
};

struct State {
	std::vector<int> controls, buttons, events, goals;
	std::map<int, Object> objects;	// by signature
};

static const char *Head_names[5] = { "frame", "missiontime", "frametime", "timestamp_ticker", "rand_count" };

static bool load_trace(const char *name, Trace &t)
{
	FILE *fp = fopen(name, "rb");
	if (!fp) {
		fprintf(stderr, "Cannot open %s\n", name);
		return false;
	}
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	t.data.resize(size);
	if (size && (fread(&t.data[0], 1, size, fp) != (size_t)size)) {
		fclose(fp);
		fprintf(stderr, "Cannot read %s\n", name);
		return false;
	}
	fclose(fp);

	if ((size < 12) || memcmp(&t.data[0], "FS2TRACE", 8)) {
		fprintf(stderr, "%s is not a trace\n", name);
		return false;
	}
	int version;
	memcpy(&version, &t.data[8], 4);
	if (version != TRACE_VERSION) {
		fprintf(stderr, "%s is trace version %d, this tool reads version %d\n", name, version, TRACE_VERSION);
		return false;
	}

	long pos = 12;
	while (pos + 32 <= size) {
		Frame f;
		memcpy(f.head, &t.data[pos], 32);
		pos += 32;
		f.payload = NULL;
		if (f.head[7]) {
			if (pos + f.head[7] > size) {
				break;		// the game stopped mid-write
			}
			f.payload = &t.data[pos];
			pos += f.head[7];
		}
		t.frames.push_back(f);
	}
	return true;
}

static int rd(const ubyte *&p)
{
	int v;
	memcpy(&v, p, 4);
	p += 4;
	return v;
}

static void parse(const Frame &f, State &s)
{
	const ubyte *p = f.payload;
	int i, n;

	n = rd(p);
	for (i = 0; i < n; i++) s.controls.push_back(rd(p));
	n = rd(p);
	for (i = 0; i < n; i++) s.buttons.push_back(rd(p));
	n = rd(p);
	for (i = 0; i < n * TRE_COUNT; i++) s.events.push_back(rd(p));
	n = rd(p);
	for (i = 0; i < n * TRG_COUNT; i++) s.goals.push_back(rd(p));
	n = rd(p);
	for (i = 0; i < n; i++) {
		int sig = rd(p);
		Object o;
		o.type = rd(p);
		int count = rd(p);
		for (int j = 0; j < count; j++) o.vals.push_back(rd(p));
		s.objects[sig] = o;
	}
}

static float as_float(int v)
{
	float f;
	memcpy(&f, &v, 4);
	return f;
}

// name and kind of value i of an object of the given type
static void object_field(int type, int i, std::string &name, int &kind)
{
	char buf[64];

	if (i < TRO_COMMON_COUNT) {
		name = Trace_common_fields[i].name;
		kind = Trace_common_fields[i].kind;
		return;
	}
	i -= TRO_COMMON_COUNT;
	if (type == 1) {		// OBJ_SHIP
		if (i < TRO_SHIP_COUNT) {
			name = Trace_ship_fields[i].name;
			kind = Trace_ship_fields[i].kind;
		} else {
			sprintf(buf, "subsys[%d].hits", i - TRO_SHIP_COUNT);
			name = buf;
			kind = TRK_HEALTH;
		}
		return;
	}
	if ((type == 2) && (i < TRO_WEAPON_COUNT)) {		// OBJ_WEAPON
		name = Trace_weapon_fields[i].name;
		kind = Trace_weapon_fields[i].kind;
		return;
	}
	sprintf(buf, "value[%d]", i + TRO_COMMON_COUNT);
	name = buf;
	kind = TRK_INT;
}

struct Diff {
	std::string what;
	int kind;
	double a, b, err;
};

// every difference between two states; float kinds are compared as floats
static void diff_states(const State &a, const State &b, std::vector<Diff> &out)
{
	char buf[256];
	size_t i;

	for (i = 0; i < a.controls.size() && i < b.controls.size(); i++) {
		int kind = (i < TRC_COUNT) ? Trace_control_fields[i].kind : TRK_INT;
		if (a.controls[i] != b.controls[i]) {
			Diff d;
			sprintf(buf, "player control %s", (i < TRC_COUNT) ? Trace_control_fields[i].name : "?");
			d.what = buf; d.kind = kind;
			d.a = (kind == TRK_INT) ? a.controls[i] : as_float(a.controls[i]);
			d.b = (kind == TRK_INT) ? b.controls[i] : as_float(b.controls[i]);
			d.err = fabs(d.a - d.b);
			out.push_back(d);
		}
	}
	for (i = 0; i < a.buttons.size() && i < b.buttons.size(); i++) {
		if (a.buttons[i] != b.buttons[i]) {
			Diff d;
			sprintf(buf, "player buttons[%d]", (int)i);
			d.what = buf; d.kind = TRK_INT; d.a = (uint)a.buttons[i]; d.b = (uint)b.buttons[i]; d.err = 1;
			out.push_back(d);
		}
	}
	if (a.events.size() != b.events.size()) {
		Diff d; d.what = "number of mission events"; d.kind = TRK_INT;
		d.a = (double)(a.events.size() / TRE_COUNT); d.b = (double)(b.events.size() / TRE_COUNT); d.err = 1;
		out.push_back(d);
	}
	for (i = 0; i < a.events.size() && i < b.events.size(); i++) {
		if (a.events[i] != b.events[i]) {
			Diff d;
			sprintf(buf, "mission event %d %s", (int)(i / TRE_COUNT), Trace_event_fields[i % TRE_COUNT].name);
			d.what = buf; d.kind = TRK_INT; d.a = a.events[i]; d.b = b.events[i]; d.err = 1;
			out.push_back(d);
		}
	}
	for (i = 0; i < a.goals.size() && i < b.goals.size(); i++) {
		if (a.goals[i] != b.goals[i]) {
			Diff d;
			sprintf(buf, "mission goal %d %s", (int)(i / TRG_COUNT), Trace_goal_fields[i % TRG_COUNT].name);
			d.what = buf; d.kind = TRK_INT; d.a = a.goals[i]; d.b = b.goals[i]; d.err = 1;
			out.push_back(d);
		}
	}

	std::map<int, Object>::const_iterator it;
	for (it = a.objects.begin(); it != a.objects.end(); ++it) {
		std::map<int, Object>::const_iterator jt = b.objects.find(it->first);
		if (jt == b.objects.end()) {
			Diff d;
			sprintf(buf, "object %d (type %d) exists only in A", it->first, it->second.type);
			d.what = buf; d.kind = TRK_INT; d.a = 1; d.b = 0; d.err = 1;
			out.push_back(d);
			continue;
		}
		const Object &oa = it->second, &ob = jt->second;
		if ((oa.type != ob.type) || (oa.vals.size() != ob.vals.size())) {
			Diff d;
			sprintf(buf, "object %d type/value count %d/%d vs %d/%d", it->first, oa.type, (int)oa.vals.size(), ob.type, (int)ob.vals.size());
			d.what = buf; d.kind = TRK_INT; d.a = 0; d.b = 0; d.err = 1;
			out.push_back(d);
			continue;
		}
		for (size_t k = 0; k < oa.vals.size(); k++) {
			if (oa.vals[k] == ob.vals[k]) {
				continue;
			}
			std::string name;
			int kind;
			object_field(oa.type, (int)k, name, kind);
			Diff d;
			if (oa.type == TRACE_EYE_TYPE) {
				sprintf(buf, "eye %s", name.c_str());
			} else {
				sprintf(buf, "object %d (type %d) %s", it->first, oa.type, name.c_str());
			}
			d.what = buf; d.kind = kind;
			d.a = (kind == TRK_INT) ? oa.vals[k] : as_float(oa.vals[k]);
			d.b = (kind == TRK_INT) ? ob.vals[k] : as_float(ob.vals[k]);
			d.err = (kind == TRK_INT) ? 1 : fabs(d.a - d.b);
			out.push_back(d);
		}
	}
	for (it = b.objects.begin(); it != b.objects.end(); ++it) {
		if (a.objects.find(it->first) == a.objects.end()) {
			Diff d;
			sprintf(buf, "object %d (type %d) exists only in B", it->first, it->second.type);
			d.what = buf; d.kind = TRK_INT; d.a = 0; d.b = 1; d.err = 1;
			out.push_back(d);
		}
	}
}

static void print_diffs(const std::vector<Diff> &diffs, int max)
{
	for (size_t i = 0; i < diffs.size() && (int)i < max; i++) {
		const Diff &d = diffs[i];
		if (d.kind == TRK_INT) {
			printf("    %-60s A=%.0f B=%.0f\n", d.what.c_str(), d.a, d.b);
		} else {
			printf("    %-60s A=%.9g B=%.9g (diff %.3g, %s)\n", d.what.c_str(), d.a, d.b, d.err, Trace_kind_names[d.kind]);
		}
	}
	if ((int)diffs.size() > max) {
		printf("    ... %d more\n", (int)diffs.size() - max);
	}
}

static bool load_tolerances(const char *name, double *tol)
{
	FILE *fp = fopen(name, "rt");
	char line[256], key[64];
	double val;

	if (!fp) {
		fprintf(stderr, "Cannot open %s\n", name);
		return false;
	}
	while (fgets(line, sizeof(line), fp)) {
		if ((line[0] == '#') || (sscanf(line, "%63s %lf", key, &val) != 2)) {
			continue;
		}
		int k;
		for (k = 1; k < TRK_NUM_KINDS; k++) {
			if (!strcmp(key, Trace_kind_names[k])) {
				tol[k] = val;
				break;
			}
		}
		if (k == TRK_NUM_KINDS) {
			fprintf(stderr, "Unknown tolerance '%s' in %s\n", key, name);
			fclose(fp);
			return false;
		}
	}
	fclose(fp);
	return true;
}

int main(int argc, char **argv)
{
	Trace a, b;
	double tol[TRK_NUM_KINDS];
	bool use_tol = false, curve = false;
	int i;

	if (argc < 3) {
		fprintf(stderr, "usage: TraceDiff <a.trace> <b.trace> [-tol <tolerances.txt>] [-curve]\n");
		return 2;
	}
	for (i = 0; i < TRK_NUM_KINDS; i++) {
		tol[i] = 0.0;
	}
	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "-tol") && (i + 1 < argc)) {
			if (!load_tolerances(argv[++i], tol)) {
				return 2;
			}
			use_tol = true;
		} else if (!strcmp(argv[i], "-curve")) {
			curve = true;
		} else {
			fprintf(stderr, "Unknown option %s\n", argv[i]);
			return 2;
		}
	}
	if (!load_trace(argv[1], a) || !load_trace(argv[2], b)) {
		return 2;
	}

	printf("A: %s, %d frames\nB: %s, %d frames\n", argv[1], (int)a.frames.size(), argv[2], (int)b.frames.size());

	size_t n = a.frames.size() < b.frames.size() ? a.frames.size() : b.frames.size();
	int result = 0;

	if (curve) {
		printf("frame,missiontime");
		for (i = 1; i < TRK_NUM_KINDS; i++) printf(",%s", Trace_kind_names[i]);
		printf(",hard\n");
	}

	// exact pass over every frame's header and hash
	if (!use_tol && !curve) {
		for (size_t f = 0; f < n; f++) {
			const Frame &fa = a.frames[f], &fb = b.frames[f];
			int h;
			for (h = 0; h < 7; h++) {
				if (fa.head[h] != fb.head[h]) break;
			}
			if (h == 7) continue;

			printf("\nDIVERGED at frame %d (mission time %.3f s): %s differs\n", fa.head[0], fa.head[1] / 65536.0,
				(h < 5) ? Head_names[h] : "state hash");
			if (h < 5) {
				printf("    A=%d B=%d\n", fa.head[h], fb.head[h]);
			}

			// the first frame at or after this one that both traces wrote in full
			size_t g;
			for (g = f; g < n; g++) {
				if (a.frames[g].payload && b.frames[g].payload) break;
			}
			if (g < n) {
				State sa, sb;
				std::vector<Diff> diffs;
				parse(a.frames[g], sa);
				parse(b.frames[g], sb);
				diff_states(sa, sb, diffs);
				printf("  state at frame %d (%d frames later):\n", a.frames[g].head[0], (int)(g - f));
				print_diffs(diffs, 40);
				if (g != f) {
					printf("  For the exact field at frame %d, replay both with -fulltrace.\n", fa.head[0]);
				}
			}
			return 1;
		}
		if (a.frames.size() != b.frames.size()) {
			printf("\nDIVERGED: the traces have different lengths (%d vs %d frames); identical up to frame %d\n",
				(int)a.frames.size(), (int)b.frames.size(), (int)n);
			return 1;
		}
		printf("\nMATCH: %d frames identical\n", (int)n);
		return 0;
	}

	// tolerance / curve pass over the frames both wrote in full
	double worst[TRK_NUM_KINDS];
	for (i = 0; i < TRK_NUM_KINDS; i++) worst[i] = 0.0;
	int checked = 0;

	for (size_t f = 0; f < n; f++) {
		const Frame &fa = a.frames[f], &fb = b.frames[f];
		if (!fa.payload || !fb.payload) continue;

		State sa, sb;
		std::vector<Diff> diffs, fails;
		double maxerr[TRK_NUM_KINDS];
		int hard = 0;

		for (i = 0; i < TRK_NUM_KINDS; i++) maxerr[i] = 0.0;
		parse(fa, sa);
		parse(fb, sb);
		diff_states(sa, sb, diffs);
		checked++;

		for (size_t k = 0; k < diffs.size(); k++) {
			const Diff &d = diffs[k];
			if (d.kind == TRK_INT) {
				hard++;
				fails.push_back(d);
			} else {
				if (d.err > maxerr[d.kind]) maxerr[d.kind] = d.err;
				if (d.err > tol[d.kind]) fails.push_back(d);
			}
		}
		for (i = 1; i < TRK_NUM_KINDS; i++) {
			if (maxerr[i] > worst[i]) worst[i] = maxerr[i];
		}

		if (curve) {
			printf("%d,%.3f", fa.head[0], fa.head[1] / 65536.0);
			for (i = 1; i < TRK_NUM_KINDS; i++) printf(",%.6g", maxerr[i]);
			printf(",%d\n", hard);
		} else if (!fails.empty() && (result == 0)) {
			printf("\nOUT OF TOLERANCE at frame %d (mission time %.3f s):\n", fa.head[0], fa.head[1] / 65536.0);
			print_diffs(fails, 40);
			result = 1;
			break;
		}
	}

	if (!curve) {
		if (result == 0 && (a.frames.size() != b.frames.size())) {
			printf("\nDIVERGED: the traces have different lengths (%d vs %d frames)\n", (int)a.frames.size(), (int)b.frames.size());
			result = 1;
		}
		printf("\n%d full frames compared. Largest differences:", checked);
		for (i = 1; i < TRK_NUM_KINDS; i++) printf(" %s=%.3g", Trace_kind_names[i], worst[i]);
		printf("\n%s\n", result ? "FAIL" : "MATCH within tolerance");
	}
	return result;
}
