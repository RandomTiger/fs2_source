# Fix review: long install paths overrun the path buffers

Issue #269. **Recommendation: FIX (Tom, 2026-11-11: "make it as long as windows could need").** The game crashes when it is installed in a folder whose path is 78 characters or longer. Every buffer that holds a full path now holds the longest path Windows allows, 32767 characters plus the terminator, and the exe declares itself long-path aware.

## The bug
The file system code builds full paths (install folder plus a sub-folder plus a file name) into buffers of `MAX_PATH_LEN` (128) or 128 bytes:

```
		char longname[MAX_PATH_LEN];

		cf_create_default_path_string( longname, search_order[i], filespec, localize );
```

`cf_create_default_path_string` copies the install root and the sub-path in with `strcpy`/`strcat` and no length check. With an install path of about 77 characters or more, `data\missions\` plus a file name runs past the end, and the stack cookie stops the process (0xC0000409; at 110 characters it was 0xC0000005).

Also wrong:
- `Cfile_stack` was declared `[128][CFILE_STACK_MAX]`, the dimensions swapped, so each pushed directory had 8 bytes and spilled into the next entries.
- `CF_MAX_PATHNAME_LENGTH` (256) bounded the roots, so a 230-character install plus a pack file name did not fit either.
- `_getcwd`, `GetCurrentDirectory` and `GetModuleFileName` calls in `cfile.cpp`, `FreeSpace.cpp` and `OsApi.cpp` read into 128, 1024 or `MAX_PATH` buffers and cut longer paths short.

`tools/gates/longpath_check.ps1` (RecordPlayback) found it: install paths of 70 to 76 replayed fine, 78 and longer all crashed.

## Fix
- `CF_MAX_PATHNAME_LENGTH` is 32768 (32767 is Windows' longest path, `UNICODE_STRING` length).
- Every full-path buffer in `cfile.cpp`, `CfileSystem.cpp`, `AudioStr.cpp`, `FreeSpace.cpp` and `OsApi.cpp` uses it, and the reads into them pass its size.
- `Cfile_stack` is `[CFILE_STACK_MAX][CF_MAX_PATHNAME_LENGTH]`.
- `longPathAware.manifest` is merged into the exe manifest (`AdditionalManifestFiles`). When Windows long paths are switched on (`LongPathsEnabled`, Windows 10 1607 or later), file calls past 260 characters then work. Without that setting Windows itself still stops at 260, and the game fails to open such a file instead of crashing.

The buffers live on the stack in short-lived functions; the deepest file-open chain holds about four at once (128 KB of the 1 MB stack). The root list grows by 32 KB per root, about 1-2 MB in all.

Added 2026-11-12 (#305): the launcher update check (`game_maybe_update_launcher`) built the exe folder plus `\update\freespace.exe` into `MAX_PATH` buffers (`src_filename`, `dest_filename`, `update_dir`), which overflowed for an exe folder of about 238 characters or more, and the version check read the exe name into `_MAX_PATH`. Both use the new size too.

Buffers that hold only a file name (`name_no_extension`, `cf_add_ext`, the localize temp, pilot and UI file names) are unchanged. Every changed line carries a `// Claude:` comment.

## What was checked
Release and Debug build clean. Both exes carry `longPathAware`. The long-path replay check and the autotest replays were run on RecordPlayback after the merge (results in the commit message there).

## Not changed
- The exe itself cannot start from a path past 260 characters (a Windows `CreateProcess` limit), so in practice the install path stays under about 240.
- The crash handler and debug module-name buffers (`ExceptionHandler.cpp`, `WinDebug.cpp`) only name modules in reports.
- FRED and the other tools.

## Effect on mission flow
None: only buffer sizes change. No game state reads them and no saved file stores them. Recordings do not move.
