# Fix review: format strings that don't match their arguments

**Recommendation: TAKE.** Text output only; no recording can move. GitHub issue #71. Decided by the steward (D-20261019-024004, group B), found by the build warnings sweep.

## The bug
The VS2022 compiler checks `printf`-style calls against their format strings (warnings C4473, C4474, C4477). Nine calls in the original code don't match:

- **Fonttool, `FontCreate.cpp` 442, 452, 462:** when a font's kerning, character or pixel data can't be allocated,
  ```
  printf( "Out of memory reading %d bytes of font data from %s\n", tmp_name );
  ```
  The size argument is missing, so the file name goes to `%d` and `%s` reads whatever follows on the stack: undefined behaviour, on a path that is already failing.
- **Fonttool, `FontCreate.cpp` 534:** the same, with no argument at all for `%d`.
- **Multiplayer, `MultiUI.cpp` 5452:** the hacked-tables chat message has two branches. The one taken when `Net_players[idx].player` is NULL still formats `Net_players[idx].player->callsign`, a NULL dereference, plus an extra argument.
- **FRED, `wing_editor.cpp` 920:** "Number of waves illegal.  Has been set to 1." takes no argument but is passed one (harmless).
- **FRED `CreateWingDlg.cpp` 77, Pofview `ObjectTree.cpp` 123 and 153:** a `CString` passed straight to `sprintf`'s `%s`. It works only because a CString object holds just its string pointer.

## Developers' intent
Each message says what it means to print: the size in bytes and the font file, the wing name, the document title. The multiplayer message's NULL branch was meant to print the message without a callsign.

## Proposed fix
- Fonttool: pass the size (`fnt->kern_data_size`, `char_data_size`, `pixel_data_size`; `src->kern_data_size` for the copy).
- Multiplayer: the NULL branch prints only the message.
- FRED: drop the extra argument.
- CString arguments: cast to `(LPCSTR)`.

## What was checked
- Each site was read in full; the arguments added are the values the surrounding code allocates.
- The build no longer gives C4473, C4474 or C4477 in any project.
- Retail: quick suite and tools check, below.

## Not changed
Anything else at these sites. FRED's wave-count message is shown even when `bypass_errors` leaves `buf` unset; that is a separate oddity and left as it is.

## Effect on mission flow
None. Fonttool and FRED are tools, the multiplayer message runs only in a network game, and only the text of messages changes. **No recording changed.**
