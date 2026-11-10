# OriginalSourceBugFixes: what belongs on this layer

Branch `OriginalSourceBugFixes` sits between `VS2022` and `RecordPlayback`. It fixes bugs in the
original FreeSpace 2 source as released. Every other branch is built on top of it, so a change
here changes every layer above.

## Criteria (Tom, 2026-11-08)

A change belongs here only if it fixes a bug in the released source that:

- affects **stability** (crash, hang, memory corruption, out-of-bounds or uninitialised read or
  write, leak that grows over a session), or
- is a **major game quality issue** (something a player sees or hears that is plainly wrong, not
  a matter of taste).

The scope is the **retail release and its shipped tools**: the game, FRED2, Pofview, Nebedit,
Fonttool and the rest of the solution. A tool crash counts (ruled 2026-11-08, during the layer
review).

Out of scope here: new features, behaviour changes that are preferences rather than bugs,
performance work, and anything about the recorder, autotest or the Godot port. Those go on
`RecordPlayback` or a feature branch.

Debug-only changes (asserts, warnings, Int3 suppression) are allowed when they make a Debug
build usable without changing Release behaviour; the review must say Release is unchanged.

## What every fix needs

1. A **GitHub issue** describing the bug, labelled `finding`, `original-code`, then `fixed` and
   `branch: OriginalSourceBugFixes` once it lands (or `kept-retail` if the review says skip).
2. A **review doc** in this folder, `NN_<File>_<topic>.md` with the next free number, citing the
   issue on its second line and covering: recommendation, the bug with code, the fix, what was
   checked, what was not changed, and the effect on mission flow (which recordings change).
3. A `// Claude:` comment on every changed line of original code, and a minimal diff.
4. A gameplay change is legitimate only if the recordings it moves are re-recorded and the
   review says which.

## Index

Reviews 01 to 42 correspond to issues 28 to 69 in order (review N is issue N+27). Later reviews
cite their issue in their second line.
