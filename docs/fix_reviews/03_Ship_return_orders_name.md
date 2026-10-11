# Fix review: Ship.cpp `ship_return_orders` uninitialised `name`

**Recommendation: TAKE.**

## The bug
In the `AI_GOAL_DESTROY_SUBSYSTEM` case:
```cpp
			char name[NAME_LENGTH];
			if ( aip->targeted_subsys != NULL ) {
				sprintf(outbuf, XSTR( "atk %s %s", 496), aigp->ship_name, hud_targetbox_truncate_subsys_name(...));
				strcat(outbuf, name);
```
`name` is never written. `strcat` appends stack bytes up to the first zero byte, and might overrun `outbuf`.

The `sprintf` already builds the whole string ("atk <ship> <subsystem>"). The `strcat` is a leftover from an older version.

## Proposed fix
```diff
 		case AI_GOAL_DESTROY_SUBSYSTEM: {
-			char name[NAME_LENGTH];
 			if ( aip->targeted_subsys != NULL ) {
 				sprintf(outbuf, XSTR( "atk %s %s", 496), aigp->ship_name, hud_targetbox_truncate_subsys_name(aip->targeted_subsys->system_info->name));
-				strcat(outbuf, name);
 			} else {
```
Removing the declaration as well avoids an unused-variable warning.

## Effect on mission flow
None. The only caller is `HUDtargetbox.cpp:677`, which shows the "orders" line when you target a friendly ship that has a destroy-subsystem order. It's display only.
- **Before the fix:** the line can have junk characters on the end.
- **After the fix:** the line ends cleanly.
- **Overrun risk:** with enough junk bytes, the `strcat` can write past `outbuf`. That is memory corruption which *could* change anything. Removing it can only make behaviour more stable.

## Effect on replays
None expected; this is text only. It removes a possible source of stack-dependent memory corruption.
