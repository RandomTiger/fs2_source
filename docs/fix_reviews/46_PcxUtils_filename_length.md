# Fix review: PCX functions overflow a 32-byte file name buffer

**Recommendation: TAKE.** Every file name that worked before behaves the same; only names that overflowed the stack are refused. Steward decision D-20261017-234807. GitHub issue #10.

## The bug
The six functions in `PcxUtils/pcxutils.cpp` (`pcx_read_header`, `pcx_read_bitmap_8bpp`, `pcx_read_bitmap_16bpp`, `pcx_read_bitmap_16bpp_aabitmap`, `pcx_read_bitmap_16bpp_nondark` and `pcx_write_bitmap`) start the same way:
```cpp
char filename[MAX_FILENAME_LEN];		// 32
strcpy( filename, real_filename );
char *p = strchr( filename, '.' );
if ( p ) *p = 0;
strcat( filename, ".pcx" );
```
Neither the copy nor the `.pcx` suffix is checked against the 32-byte buffer. A name of 32 characters or more, or one whose name without its extension is 28 or more, overwrites the stack. The Debug runtime caught it in Fonttool, given a full path: "Stack around the variable 'header' was corrupted".

## Developers' intent
`MAX_FILENAME_LEN` is commented "Length for filenames, ie "title.pcx"": the functions expect a bare file name, which is what the game passes. Tools that pass a path were never meant to reach the overflow.

## Proposed fix
A helper, `pcx_filename_fits`, returns true exactly when the original code would not overflow: the name is shorter than the buffer, and its part before the first `.` plus `.pcx` fits. Each function returns its usual failure code when it does not: `PCX_ERROR_OPENING` for the readers and `PCX_ERROR_WRITING` for `pcx_write_bitmap`. The buffers and stack frames are unchanged.

## What was checked
- For every name the original handled without overflow, the helper returns true and the function runs exactly as before.
- The game passes bare names from its tables and missions, far below 28 characters.
- Re-run per layer (the suite is the proof, since this is game code): see below.

## Not changed
Callers that pass paths (the tools) now get an error instead of a corrupted stack. Letting tools pass paths would be a feature, not this fix.

## Effect on mission flow
None. **No recording changed.** Per-layer re-run: in the merge chain that carries this fix (OriginalSourceBugFixes build; RecordPlayback and Enhanced replay_check 47/47; PrePort, PrePort64 and GodotPrep quick suite).
