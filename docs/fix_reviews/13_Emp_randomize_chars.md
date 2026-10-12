# Fix review: emp_randomize_chars reads past the end of its character table

**Recommendation: TAKE.** Low risk.

Found because Debug replays of Release recordings of loop2-1 diverged. Logging the caller of every gameplay random draw in a Release recording and its Debug replay showed the first difference in `emp_randomize_chars`: the two builds made a different number of draws scrambling the same HUD directive text while an EMP was on the player.

## The bug
When an EMP hits the player, the HUD text is scrambled one character at a time:
```cpp
for(idx=0; idx<(int)(strlen(str)-1); idx++){
    if(frand_range(0.0f, 1.0f) < Emp_intensity){
        char_index = Emp_random_char[(int)frand_range(0.0f, (float)(NUM_RANDOM_CHARS - 1))];
        str[idx] = Emp_random_char[char_index];
    }
}
```
`char_index` is already a character from the 51-entry table, for example `'h'` (104). The second line uses that character code as an index into the same table, so it reads up to 75 bytes past its end. Whatever static data sits there becomes the new character.

That data depends on the build's memory layout, and some of it may change at run time. When the byte read is 0, the string is cut short at that point. `strlen` is evaluated on every pass, so the loop then stops early and makes fewer random draws. The gameplay random stream is shared, so this moves every later random number in the mission: AI, weapons, debris. Debug and Release lay out static data differently, which is why their replays of the same recording diverged.

## Proposed fix
Use the character that was picked:
```cpp
str[idx] = (char)char_index;
```
The scrambled text is now always taken from the table, as intended, and the number of draws depends only on the length of the text.

## Not changed
The table is declared with 51 entries but initialised with 50, so its last entry is 0. `frand_range(0.0f, 50.0f)` reaches 50 only when `frand` returns exactly 1.0, so a NUL can in principle still be picked. That is the original behaviour of the first lookup and is left alone.

## Effect on mission flow
None. EMP still scrambles the HUD text. Missions where an EMP hits the player (nebula lightning, EMP weapons) draw a different number of gameplay random numbers than before, so their recordings must be re-recorded.
