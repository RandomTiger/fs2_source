# Fix review: mission expressions read and write Sexp_nodes[-1] when an argument is a plain number

**Recommendation: TAKE.** Out-of-bounds read in four operators and an out-of-bounds write in one, on a normal mission path. Found with `/analyze` (C6385/C6386) on 2026-10-13.

## The bug
In a mission expression each argument is a list cell. When the argument is an operator, the cell's `first` (`CAR`) points to the operator's node; when it is a plain number, as in `( < 3 ( num-ships-in-battle ) )`, `CAR` is -1 and the number is the cell's own text (`eval_sexp` returns `atoi` of it, `case 0`).

Several operators check the first argument's node for the "not a number" markers without checking for -1:
```cpp
// sexp_gt, sexp_lt, sexp_equal
if ( ( Sexp_nodes[CAR(n)].value == SEXP_NAN ) || ( Sexp_nodes[CDR(n)].value == SEXP_NAN ) )
	return 0;
else if ( ( Sexp_nodes[CAR(n)].value == SEXP_NAN_FOREVER ) || ...
	return SEXP_KNOWN_FALSE;
```
`add_sexps` (`+`) does the same for its first argument, and inside its loop for the previous argument. With a plain number these read `Sexp_nodes[-1].value`, whatever global lies just before the array. If it happens to hold -4 or -5 the comparison returns false or "known false" (which also locks the expression false for good), and the sum returns a marker instead of a number.

`sexp_and_in_sequence` is worse: after evaluating the first argument it marks that argument's node true for good,
```cpp
if ( result )
	Sexp_nodes[Sexp_nodes[n].first].value = SEXP_KNOWN_TRUE;
```
outside the `first != -1` check above it, so a plain non-zero number as the first argument writes -2 into `Sexp_nodes[-1].value`, corrupting another global.

What lies before the array depends on how the build is laid out, so these differ between builds: a split between Release, Debug, x64 or Godot recordings, or a mission that behaves differently.

## Proposed fix
Check `CAR(n) != -1` before reading the node. A plain number is never NAN, so a number argument now always counts as a number, which is what the code assumes. In `sexp_and_in_sequence`, only mark the node when there is one; a plain number is a constant and needs no marking.

## What was checked
- `sexp_not`, `sexp_or` and `sexp_and` only read `CAR(n)` inside `if (Sexp_nodes[n].first != -1)`, so they are fine.
- The second argument's checks use `CDR(n)`, the argument's own cell, which always exists when there are two arguments.

## Not changed
- In `add_sexps`'s loop the NAN_FOREVER check reads `CAR(n)`, the previous argument, where the NAN check beside it reads `CDR(n)`, the current one. It looks like a copy slip, so a later argument that is NAN_FOREVER is not passed up. Correcting it would change what `+` returns in such missions; it is left as it is, guarded against -1 only, and can be its own fix.
- Plain numbers in other positions are already handled.

## Effect on mission flow
Only expressions whose first argument (or, for `+`, an earlier argument) is a plain number are affected, and only where the memory before `Sexp_nodes` held -4 or -5 (reads) or mattered (the write in `and-in-sequence`). In those, comparisons and sums now always use the number. Missions that do this are expected to play the same unless they were hitting the bad value; the replay check after the merge into `RecordPlayback` says whether any recording changes.
