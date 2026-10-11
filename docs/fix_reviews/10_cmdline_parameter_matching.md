# Fix review: cmdline.cpp matches parameters inside other text and mangles values

**Recommendation: TAKE.** Low risk.

Found while adding autotest flags (`-at_mission sm1-03.fs2`, `-at_speed 4`).

## The bugs
1. **Parameters match inside other text.** `os_parse_parms` looks each parameter up with `strstr`:
   ```cpp
   cmdline_offset = strstr(cmdline, parmp->name);
   ```
   So a parameter is "found" inside a longer parameter or inside a value. `-cat` matches `-catfish`, and `-record c:\my-replay.fs2rec` also turns on `-replay`.
2. **A value ends at the first `-`.** `parm_stuff_args` copies the value up to any `-`:
   ```cpp
   while ((*cmdline != 0) && (*cmdline != '-')) {
   ```
   So `-at_mission sm1-03.fs2` gives `sm1`, and any path with a `-` in it is cut short. It also copies into a 1024-byte stack buffer with no bound.
3. **A one-character value is lost.** `drop_extra_chars` trims the spaces around a value but only moves the text when it is longer than one character:
   ```cpp
   if (e > s){
       memmove(str, str + s, e - s + 1);
   }
   ```
   For `" 3"`, `s == e == 1`, nothing moves and the string is cut to `" "`. So `-port 5` or `-timeout 9` reads as 0.

## Proposed fix
- `parm_find` replaces `strstr`. A parameter matches only as a whole token: at the start of the command line or after white space, followed by the end or white space, and not inside double quotes.
- `parm_stuff_args` stops at the next parameter, which is a `-` that starts a token outside quotes. It also stops before the buffer is full.
- `drop_extra_chars` moves the text when `e >= s`.

Checked with a standalone test of the three functions:

| Command line | Parameter | Before | After |
|---|---|---|---|
| `-catfish 3` | `-cat` | found, value `fish 3` | not found |
| `-cat 3 -catfish 4` | `-cat` | value `" "` | value `3` |
| `-record c:/my-replay.fs2rec` | `-replay` | found | not found |
| `-at_mission sm1-03.fs2 -at_time 60` | `-at_mission` | `sm1` | `sm1-03.fs2` |
| `-record "U:/Free space 2/a -b.rec" -window` | `-record` | `U:/Free space 2/a` | `U:/Free space 2/a -b.rec` |
| `-fulltrace` | `-trace` | not found | not found |

## Not changed
`os_validate_parms` still splits on white space without looking at quotes. A quoted value with a token starting with `-` inside it (`"a -b.rec"`) is still reported as an unknown parameter. Paths like that are rare. The fix would be a separate change.

## Effect on mission flow
None. This runs once at startup. Command lines that worked before give the same values, except where a value was cut short or a parameter was wrongly found.
