# Branches

The repo is a stack of layers. Each layer is a branch off the one below it, and changes only ever merge upward.

```
master                    original 1999 source, as provided by Volition. Never changed.
  VS2015                  builds with Visual Studio 2015
    VS2022                builds with Visual Studio 2022
      OriginalSourceBugFixes   bugs that were in the original game, one commit and review doc each (docs/fix_reviews)
        RecordPlayback    gameplay recorder and replayer, recordings, the test suite
          <features>      one branch per feature, side by side (FS2Demo, FS1Compat, PadControl, OgvMovies, ...)
            Enhanced      the original game with all its features combined (Latest points here)
              PrePort     everything that can be done before the engine moves to Godot
                Godot     the game running in Godot, with its own feature branches off it
                  GodotEnhanced   the Godot game with its features combined
```

`Latest` is not a layer. It is the default branch on GitHub and is moved forward to `Enhanced` each time `Enhanced` passes the full suite, so it is always the newest known-good build.

## Folders
Each branch in use has its own folder (a git worktree, like a second Perforce workspace) next to the others, named `fs2_<Branch>`: `fs2_RecordPlayback` (the main clone), `fs2_Enhanced`, and later `fs2_PrePort`, `fs2_Godot`, `fs2_GodotEnhanced`. Build and run the tests in `fs2_Enhanced`, so every feature is covered; commit each change in the folder of the branch it belongs on.

## Rules
- **Fix a bug on the lowest layer that has it, then merge it upward one layer at a time.** An original-game bug goes on `OriginalSourceBugFixes`, a recorder bug on `RecordPlayback`, a feature bug on that feature's branch. Then: `OriginalSourceBugFixes` → `RecordPlayback` → `Enhanced` → `PrePort` → `Godot` → `GodotEnhanced`.
- **Merges only flow up.** Never merge a higher branch into a lower one. One agreed exception: `CosmeticRandom` (a separate random stream for effects) was merged into `RecordPlayback`, because it makes the recordings independent of rendering and fixes nothing a player sees.
- **Feature branches branch off `RecordPlayback`** and only take merges from it, never from `Enhanced` or from each other. That keeps each feature separable. Features that need the port preparation (for example `Headless`) branch off `PrePort` instead and only take merges from it.
- **A feature goes into `Enhanced` with a merge commit** (`git merge --no-ff`), once Tom allows it in (finished, or a work in progress good enough to include). Later fixes to it are made on the feature branch and merged into `Enhanced` again.
- **Never rebase, squash or amend anything already pushed.** Merge instead. Before a push, local commits can still be tidied.
- **When `Enhanced` passes the full suite, move `Latest` to it and tag it** (`latest-<date>`), so there are known-good points to go back to.
- **Long-lived feature branches** (`FS2Demo`, the one exe for the demo; `FS1Compat`, FreeSpace 1 data): off `RecordPlayback`, into `Enhanced`, merged once per green batch. Each merge into `Enhanced` is carried up through `PrePort`, `PrePortCleanup`, `PrePort64` and `GodotPrep` in the same step, with the quick suite on each layer. No recording on any layer may move from it: a moved retail recording is a gating leak, to be fixed, never re-recorded.
- **Tags are never deleted, local or remote. Only the branch label is removed after a done tag is pushed and verified.** A finished feature branch (fully merged into its layer, that layer green and pushed) gets an annotated `<Name>-done` tag at its tip, naming the layer and the merge commit; the tag is pushed and checked on GitHub (`git ls-remote --tags`), and then the branch label is deleted, on GitHub first. The commits stay reachable through the tag, and a branch can be made from it again at any time. Unfinished work gets `<Name>-checkpoint-<YYYY-MM-DD>` tags only and keeps its branch. The `<Layer>-done` tags (`PrePort-done`, `Enhanced-done`, ...) mark layer freezes (D-059) and keep that meaning; new layer freezes are tagged `<Layer>-frozen-<date>`. Layers, `Latest` and live branches are never removed this way (D-20261020-063400, 063405; Tom, 2026-10-20).

## Tags for finished branches
| Tag | Merged into |
|---|---|
| `32BitRendering-done` | `Enhanced` |
| `CosmeticRandom-done` | `RecordPlayback` |
| `DebrisOffscreenFix-done` | `PrePort` |
| `LogicDrawSplit-done` | `PrePort` |
| `ShivanShipGlow-done` | `Enhanced` |
| `SoundOwnTimer-done` | `PrePort` |
| `Warnings-done` | `Enhanced` |

`PadControl-checkpoint-2026-10-13` marks the pad work, which is not finished; `PadControl` stays a branch.

## Recordings as the referee
The recordings check "original game plus `OriginalSourceBugFixes`", because the fixes sit below `RecordPlayback`. A fix that changes gameplay means re-recording the affected missions; its review doc says which.

Recordings replay a stream of OS reads, so they can referee every layer up to `PrePort`. The Godot port will not make the same reads, so it will need the traces (game state) compared another way. When `Godot` replaces the engine, tag that point; after it, merging the C++ layers upward stops making sense and fixes are carried across by hand.

## In Perforce terms
A branch is a stream, a merge is an integrate, a push is a submit. Unlike Perforce, commits are local until pushed, and pushed history must never be rewritten.
