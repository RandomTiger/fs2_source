// Claude: the FreeSpace 1 demo data set (DATA_SET_FS1_DEMO; docs/FS1_DEMO.md).  Its installer, fsdemo10.exe,
// is a WinZip self-extractor like the FreeSpace 2 demo's, but the game files are inside an InstallShield 5
// cabinet in that zip (data1.cab), read by CabReader.cpp.  The installer itself is never run.
#ifndef _DATASETFS1_H
#define _DATASETFS1_H

#define FS1_DEMO_FOLDER			"fs1demo"
#define FS1_DEMO_INSTALLER		"fsdemo10.exe"

// 1 if dir holds the FreeSpace 1 demo's data: Data\freespace.vp, with the demo's campaign (demo.fsc) in it.
// FS1 retail has a Data\freespace.vp too, without demo.fsc.
int dataset_fs1_detect(char *dir);

// Extracts the game files from exe_dir\fsdemo10.exe into exe_dir\fs1demo, once: everything is written into
// fs1demo.partial and renamed only when every file is whole, and an existing fs1demo is never written to.
// 0 on success; otherwise non-zero and the reason in err.
int dataset_fs1_extract(char *exe_dir, char *err, int err_len);

#endif
