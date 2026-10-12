# Fix review: message_play_anim reads Personas[-1] for a message with no persona

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer: 25 reports across the campaign autotest.

## The bug
For built-in messages and talking heads, `message_play_anim` picks the head animation from the message's persona:
```cpp
persona_index = m->persona_index;
...
if ( Personas[persona_index].flags & (PERSONA_FLAG_WINGMAN | PERSONA_FLAG_SUPPORT) ) {
    ...
} else if ( Personas[persona_index].flags & (PERSONA_FLAG_COMMAND | PERSONA_FLAG_LARGE) ) {
    ...
}
if (!subhead_selected) {
    // choose between a and b
    ...
    mprintf(("message '%s' with invalid head.  Fix by assigning persona to the message.\n", m->name));
}
```
A message with no persona has `persona_index` -1, so both checks read `Personas[-1]`, the memory before the array. The fallback after them, with its "invalid head" message, is the case the code meant for this.

## Proposed fix
Add `(persona_index >= 0) &&` to both checks, so a message with no persona takes the fallback.

## Effect on mission flow
None. Only the head animation can change: a message with no persona now always gets head `a` or `b` from the fallback, where before the data read could pick the wingman or command head set.

Loading and playing a head animation reads the clock, and a different head can read it a different number of times. In the campaign autotest this changed sm2-02, whose recording desynced at frame 91 and had to be re-recorded. The other missions with persona-less messages replayed unchanged.
