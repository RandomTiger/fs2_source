# Fix review: FRED Release used 100 ships while the game code it links used 150

**Recommendation: TAKE.** Two different sizes for the same arrays inside one program. FRED only; the game is unchanged.

## The bug
`Ship/Ship.h` sets the ship limit by build:
```cpp
#ifdef NDEBUG
	#ifdef FRED
		#define	MAX_SHIPS					100
	#else
		#define	MAX_SHIPS					150
	#endif
#else
#define	MAX_SHIPS					200
#endif
```
FRED Release defines `FRED` (Volition's `Fred2.dsp` did too), so FRED's own files get 100. But FRED links `code.lib`, the game code, which is built without `FRED` and gets 150: `Ships[]`, mission loading and the rest of the game code allow 150 ships. So inside one FRED Release program:
- `Fred_alt_names[MAX_SHIPS]` (`FRED2/Management.cpp`) has 100 entries but is indexed by ship number (`Fred_alt_names[objp->instance]`, `FREDDoc.cpp`), which goes up to 149: a mission with more than 100 ships writes past it.
- FRED's own loops over ships stop at 100, missing ships 100 to 149 that the game code created.

Debug builds are unaffected: without `NDEBUG` both get 200.

## Proposed fix
Give FRED the game's limit, 150, in `Ship.h`. The `FRED` define stays, so the `#error` in `FRED2/FRED.cpp` that checks for it still works.

## What was checked
- `FRED` is only tested in `Ship.h` (this) and `FREESPACE2/FreeSpace.cpp`, which FRED doesn't build; inside FRED, only by that `#error`.
- The only header array sized by `MAX_SHIPS` besides `Ships[]` is `Fred_alt_names`.
- FRED Release and Debug build and start (an empty mission, title "Untitled - FRED2 - FreeSpace2 Mission Editor").

## Not changed
The other way to make them agree, a separate build of the game code with `FRED` defined for FRED (keeping FRED at 100), was considered and not taken: Tom chose the game's limit, so FRED can make any mission the game can play.

## Effect on mission flow
None in the game: it never defines `FRED`. FRED can now place up to 150 ships, the game's limit, and missions with more than 100 ships no longer overrun `Fred_alt_names`.
