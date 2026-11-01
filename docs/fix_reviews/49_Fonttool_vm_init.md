# Fix review: Fonttool never sets up the memory heap

**Recommendation: TAKE.** Tools only; the game is not touched. GitHub issue #22. Same cause and fix as review 45 (#11).

## The bug
Fonttool links the game's `code.lib`, where `malloc` is a macro for `vm_malloc`, which in Release allocates from the private Win32 heap that `vm_init` creates (review 45 has the detail). Fonttool's `main` (`Fonttool/FontTool.cpp`) never calls `vm_init`, so its first allocation calls `HeapAlloc(NULL, ...)` and faults. Review 45 fixed Pofview and Nebedit; Fonttool was not checked then because the audit's only test input for it, the template `FontTool.pcx`, stops with an error before allocating anything.

## Developers' intent
Every program that links `code.lib` is meant to set up its allocator first, as the game, FRED and Ac do.

## Proposed fix
Call `vm_init(16*1024*1024)` (Ac's size) at the top of `main`, before the arguments are acted on.

## What was checked
- Found by the tools build-and-run check: Release Fonttool copying kerning between two game fonts (`font01.vf`, `font02.vf`, taken from the VPs) exits with 0xC0000005.
- With the fix, the same run prints "Copying kerning data from a.vf to b.vf", exits 0 and rewrites the target font.
- The template `FontTool.pcx` still stops with "Box at (270,6) is less than 1 pixel wide": it is a layout template, not a font sheet, so it is not a test input.
- On `PrePortCleanup`, `vm_malloc` uses the CRT allocator (D-075), so the crash cannot happen there. The call stays harmless as the seam's init.

## Not changed
The game, FRED, Ac, Pofview and Nebedit.

## Effect on mission flow
None: no game code changes. **No recording changed.**
