# Fix review: Pofview and Nebedit never set up the memory heap

**Recommendation: TAKE.** Tools only; the game is not touched. Steward decision D-20261017-234807. GitHub issue #11.

## The bug
The tools link the game's `code.lib`, where `malloc`, `free` and `strdup` are macros for `vm_malloc`, `vm_free` and `vm_strdup` (`GlobalIncs/PsTypes.h`, `GlobalIncs/WinDebug.cpp`). In Release, `vm_malloc` allocates from a private Win32 heap that `vm_init` creates:
```cpp
void *ptr = HeapAlloc(Heap, HEAP_FLAG, size );
```
The game (`FreeSpace.cpp`), FRED (`Management.cpp`) and Ac (`ac.cpp`) call `vm_init` at start-up. Pofview (`PofView.cpp`, `CPofViewApp::InitInstance`) and Nebedit (`NebEdit.cpp`, `WinMain`) never do. So their first allocation calls `HeapAlloc(NULL, ...)`, and today's Windows faults inside ntdll. Debug builds fall back to the CRT's `_malloc_dbg` when there is no heap, which is why Debug got further and the bug went unnoticed.

## Developers' intent
Every program that links `code.lib` is meant to set up its allocator first, as the game, FRED and Ac do.

## Proposed fix
Call `vm_init(16*1024*1024)` (Ac's size) just before `cfile_init` in both tools.

## What was checked
- Found by the VS2022 tools audit: Release Pofview and Nebedit crashed on start with the same ntdll fault, and `vm_init` is called only by the game, FRED and Ac.
- After the fix, both tools start in Release (re-audit, OriginalSourceBugFixes).
- On `PrePortCleanup`, `vm_malloc` uses the CRT allocator (D-075), so the crash cannot happen there. The call stays harmless as the seam's init.

## Not changed
The game, FRED and Ac. Any further tool problems the start-up crash hid (#12, #13) are separate.

## Effect on mission flow
None: no game code changes. **No recording changed.**
