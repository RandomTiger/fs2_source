# Fix review: FRED Debug used the DLL C runtime while the game code it links used the static one

**Recommendation: TAKE.** Two C runtimes in one program. FRED Debug only.

## The bug
FRED Debug was built with MFC as a DLL, which needs the DLL C runtime (`/MDd`), and linked `code.lib`, the game code, which is built with the static C runtime (`/MTd`). To make that link, the project told the linker to ignore the static runtime libraries (`libcmtd.lib`, `nafxcwd.lib`), so `code.lib`'s calls were resolved against the DLL runtime. Volition's `Fred2.dsp` did the same.

The game code was compiled for the static runtime, and the two runtimes keep separate state (heap, file handles, `errno`, locale). Memory allocated on one side and freed on the other, or a `FILE*` passed across, goes to the wrong runtime. It mostly works because the symbols resolve, which is why it went unnoticed; the linker warns about the conflict (LNK4098) when the libraries aren't ignored.

FRED Release already uses static MFC and the static runtime (`/MT`), matching `code.lib`.

## Proposed fix
Build FRED Debug like FRED Release: static MFC and the static debug runtime (`/MTd`), and stop ignoring `libcmtd.lib` and `nafxcwd.lib`, which are now the right libraries.

## What was checked
- FRED Debug rebuilds and links with no LNK4098 warning, and starts with an empty mission.
- FRED Release is unchanged and still builds and starts.

## Not changed
Release settings; `code.lib`.

## Effect on mission flow
None in the game. FRED Debug now runs one C runtime, the one the game code was built for.
