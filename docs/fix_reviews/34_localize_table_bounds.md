# Fix review: string table loading writes past Xstr_table, and reads Ts_text[-1] on an empty string

**Recommendation: TAKE.** Two out-of-bounds accesses in the string table code, each reached only with an unusual table. Found with `/analyze` (C6385/C6386) on 2026-10-13.

## The bugs
**`lcl_xstr_init`** (`Localization/localize.cpp`) reads `strings.tbl`, one entry per line: an index, the text, and optional screen offsets. It stores the text only for an index in range, but the offsets for any index:
```cpp
if (index >= 0 && index < XSTR_SIZE) {
	...
	Xstr_table[index].str = strdup(buf);
}
...
Xstr_table[index].offset_x = offset_lo;
...
Xstr_table[index].offset_x_hi = offset_hi;   // or offset_lo
```
An index of `XSTR_SIZE` (1570) or more, or below 0, writes past the table into other globals.

**`lcl_ext_lookup_sub`** parses `tstrings.tbl` (translated mission text) one character at a time. At a closing quote it checks the character before it to skip escaped quotes:
```cpp
if((*p == '\"') /*&& (Ts_text_size > 0)*/ && (Ts_text[Ts_text_size - 1] != '\\')){
```
For an empty string (`""`) `Ts_text_size` is 0, so it reads `Ts_text[-1]`, the byte before the buffer. If that byte is a backslash the closing quote is taken as part of the string and the rest of the file is misread. The commented-out `Ts_text_size > 0` shows the problem was seen; that check alone would have stopped empty strings from ending at all.

## Proposed fix
- `lcl_xstr_init`: put the offset writes under the same range check as the string.
- `lcl_ext_lookup_sub`: a quote ends the string when the string is empty or the previous character is not a backslash.

## What was checked
- `XSTR`, which reads the strings, range-checks the index. `lcl_get_xstr_offset` does not (its one caller, in `UI/WINDOW.cpp`, passes a UI string's id); left as it is.
- `tstrings.tbl` is only parsed when the language is not English (`lcl_ext_setup_pointers` and `lcl_ext_lookup` return early for the default language), so the second bug never runs in English.

## Not changed
`lcl_ext_lookup_sub` also has no length check on `Ts_text` or `Ts_id_text`, so a string longer than `PARSE_TEXT_STRING_LEN` would overrun it. It can be its own fix if wanted.

## Effect on mission flow
None expected in English: only an out-of-range index in `strings.tbl` is affected, and its offsets are now ignored like its string already was. In other languages an empty translated string now always ends where it should.
