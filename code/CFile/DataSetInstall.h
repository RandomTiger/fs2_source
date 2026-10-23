// Claude: the data sets the game knows, choosing the data set folder at start-up, and making it from a
// demo's original installer (D-20261018-221704).  An original demo installer next to the exe
// (fs2demo10.exe) is extracted once, on the first start, into a subfolder named after its data set
// (.\fs2demo\), and the game then runs in that folder: its VPs, fs2.ini and pilots all live there.
// Retail data next to the exe runs there, as before.
#ifndef _DATASETINSTALL_H
#define _DATASETINSTALL_H

// The data sets, an enumeration rather than a demo flag (Tom, 2026-10-18), so code, logs and recordings
// say which one.  Only the FreeSpace 2 pair is detected and played so far.
#define DATA_SET_UNKNOWN		-1
#define DATA_SET_FS2_RETAIL	0
#define DATA_SET_FS2_DEMO		1
#define DATA_SET_FS1_RETAIL	2
#define DATA_SET_FS1_DEMO		3
#define NUM_DATA_SETS			4

char *data_set_name(int set);		// "FS2 retail", "FS2 demo", ...; "unknown" for anything else

// Picks the data set folder and extracts its installer if the folder is not there yet.
//   exe_dir    the exe's folder, no trailing backslash
//   requested  -dataset's value (fs2demo, fs1demo or retail), or NULL
//   recorded   a replay's data set (DATA_SET_*), or DATA_SET_UNKNOWN for none or not known
// On success returns 0 and puts the folder to run in, in dir.  On failure returns non-zero and puts
// the reason in err; the caller stops with it.
int dataset_select_folder(char *exe_dir, char *requested, int recorded, char *dir, int dir_len, char *err, int err_len);

#endif
