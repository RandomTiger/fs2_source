# Fix review: ai_fire_secondary_weapon writes before the secondary stamp array with no bank

**Recommendation: TAKE.** Changes gameplay slightly: re-run the recordings and re-record any that legitimately change.

Found by the compiler's code analysis (`/analyze`, warning C6386). AddressSanitizer cannot see it: the write stays inside the ship struct.

## The bug
```cpp
current_bank = swp->current_secondary_bank;
if (current_bank == -1) {
    swp->next_secondary_fire_stamp[current_bank] = timestamp(500);
    return rval;
}
```
With no secondary bank selected (for example a ship with no missiles), `current_bank` is -1, so the write lands on the field before `next_secondary_fire_stamp` in `ship_weapon`. That is `next_primary_fire_stamp[MAX_PRIMARY_BANKS-1]`, the fire timer of the third primary bank. Each time the AI tries to fire a missile with no bank, that gun is held back for half a second.

## Proposed fix
Remove the write. There is no bank to delay, and nothing reads a stamp for bank -1.

## Not changed
`ai_select_secondary_weapon` and the callers, which decide when the AI tries to fire.

## Effect on mission flow
Ships with three primary banks whose AI tries to fire a missile with no bank selected can now fire their third primary bank on time. Ships with one or two primary banks are unaffected. No mission logic, events or goals change, but combat with such ships can play out differently, so recordings that include them may need re-recording.
