# Fix review: cfopen copies a full path into a 128-byte buffer

**Recommendation: FIX (steward, D-20261107-101604).** `cfopen` copies the caller's path into `copy_file_path[MAX_PATH_LEN]`, and `MAX_PATH_LEN` is 128. A path of 128 characters or more overruns the stack. The tools pass full paths from their File > Open dialogs, so opening a model from a deep folder crashes Pofview. The buffer is now `_MAX_PATH` (260), the size of `longname`, which receives the same path. A path that still does not fit is refused, never cut short.

## The bug
`code/CFile/cfile.cpp`, `cfopen`, as released:

```
CFILE *cfopen(char *file_path, char *mode, int type, int dir_type, bool localize)
{
	char longname[_MAX_PATH];
...
	int offset, size;
	char copy_file_path[MAX_PATH_LEN];  // FIX change in memory from cf_find_file_location
	strcpy(copy_file_path, file_path);
```

`MAX_PATH_LEN` is 128 (`GlobalIncs/PsTypes.h`), including the terminator, so a path of 128 characters writes 129 bytes. The copy then goes to `cf_find_file_location`. For a path with `/`, `\` or `:` in it, that opens the path as given and copies it into `longname` with `strcpy`, so `longname` needs the same room.

The write path (`mode` with `w`) has the same exposure: a full path is copied into `longname[_MAX_PATH]` with `strcpy`, unchecked.

The game itself passes bare names or short relative paths, so it never reaches 128. The tools do: Pofview's `CPofViewDoc::Serialize` passes `GetFilePath()` straight to `model_load`, which calls `cfopen` with it. FRED and Nebedit call `cfopen` too.

## Fix
`cfopen`:
- `copy_file_path` is `_MAX_PATH` long instead of `MAX_PATH_LEN`, the size of `longname`.
- Just after the parameter asserts, a path of `_MAX_PATH` characters or more returns `NULL`, the same result as a file that is not found, which every caller already has to cope with. This covers the read and the write paths.

Both changes carry a `// Claude:` comment. A path under 128 characters behaves exactly as before.

## What was checked
Build: the whole solution, Release and Debug, from clean, into a scratch folder: 0 errors, and the same warning counts as before the change (2,956 Release, 3,054 Debug).

Pofview, File > Open, driven with window messages, run in `C:\dev\GameCopy\FS2_work` under its own name:

| Exe | 13 characters | 128 characters | 210 characters |
|---|---|---|---|
| VS2022 Release (unfixed) | opens | crashes, 0xC0000005 | not run |
| This branch, Release | opens | opens | opens |
| This branch, Debug | opens, then the known Tmapper assert | passes `cfopen`, then the `FILENAME_LEN` assert (see Not changed) | same as 128 |

The 128-character path is the model in the scratch folder: `...\scratchpad\extract\fighter01.pof`. The 210-character path is the same file two folders deeper. The Debug run of the VS2022 tools on the 128-character path reported "Run-Time Check Failure #2 - Stack around the variable 'copy_file_path' was corrupted". With the fix that report is gone.

A path of 260 characters or more, which takes the new refusal, was not run.

## Not changed
- `read_model_file` asserts `strlen(filename) < FILENAME_LEN` (32) in Debug, then `strncpy`s the name into `pm->filename`, which leaves it unterminated for a long name. That stops Debug Pofview on any full path longer than 31 characters. It is a separate original bug, and Debug tools are outside the tools standard (D-20261107-101605).
- `cf_find_file_location` and `lcl_add_dir_to_path_with_filename` use 128-byte temporaries for localisation. They only run on names without a path separator, which are short.
- `cf_create_default_path_string` builds `root + folder + name` into its caller's buffer without a length check. The game's names keep it short; it is outside this fix.

## Effect on mission flow
None. The game passes `cfopen` bare names and short relative paths, all well under 128 characters, so every call it makes behaves exactly as before. No recording can move.
