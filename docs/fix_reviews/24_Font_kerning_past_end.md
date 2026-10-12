# Fix review: get_char_width reads past the end of the kerning table

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer during a hand tour of the menus: 6 reports, from the end-mission popup measuring its text (`popup` → `split_str` → `gr_get_string_size` → `get_char_width`).

## The bug
Kerning pairs are sorted by first letter. `get_char_width` starts at the first pair for the letter and walks forward:
```cpp
font_kernpair *k = &Current_font->kern_data[i];
while( (k->c1 == letter) && (k->c2<letter2) && (i<Current_font->num_kern_pairs) ) {
    i++;
    k++;
}
if ( k->c2 == letter2 ) {
    *spacing += k->offset;
}
```
The bounds check comes after the reads. For the letter that owns the last pairs in the table, the walk steps one past the end, reads `k->c1` there, and the `if` reads `k->c2` there too. If that memory happened to match `letter2`, the character got a random spacing offset.

## Proposed fix
- Check `i < num_kern_pairs` first in the loop.
- Only apply the offset when `i` is still inside the table.

## Effect on mission flow
None. Text spacing only. A pair of characters that read garbage before now gets no kerning, which is what it should get.

## Not changed
The `if` does not check `k->c1 == letter`. When the walk stops because it ran into the next letter's pairs, a pair there with the same second letter is applied to the wrong first letter. It is a spacing mistake inside the table, not a memory error, and fixing it would change text layout, so it is left alone.
