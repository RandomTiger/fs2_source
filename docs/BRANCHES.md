# Branches

The repo is a stack of layers. Each layer is a branch off the one below it, and changes only ever merge upward.

```
master                    original 1999 source, as provided by Volition. Never changed.
  VS2015                  builds with Visual Studio 2015
    VS2022                builds with Visual Studio 2022
      OriginalSourceBugFixes   bugs that were in the original game, one commit and review doc each (docs/fix_reviews)
        RecordPlayback    gameplay recorder and replayer, recordings, the test suite
          <features>      one branch per feature, side by side (PadControl, ShivanShipGlow, 32BitRendering, ...)
            Latest        all finished features combined; the default branch on GitHub
              GodotPrep   everything that can be done before the engine moves to Godot
                Godot     the game running in Godot, with its own feature branches off it
```

## Rules
- **Fix a bug on the lowest layer that has it, then merge it upward one layer at a time.** An original-game bug goes on `OriginalSourceBugFixes`, a recorder bug on `RecordPlayback`, a feature bug on that feature's branch. Then: `OriginalSourceBugFixes` → `RecordPlayback` → `Latest` → `GodotPrep` → `Godot`.
- **Merges only flow up.** Never merge a higher branch into a lower one.
- **Feature branches branch off `RecordPlayback`** and only take merges from it, never from `Latest` or from each other. That keeps each feature separable.
- **A feature goes into `Latest` with a merge commit** (`git merge --no-ff`), once it is finished and passes the recording suite. Later fixes to it are made on the feature branch and merged into `Latest` again.
- **Never rebase, squash or amend anything already pushed.** Merge instead. Before a push, local commits can still be tidied.
- **Tag `Latest` when it passes the full suite**, so there are known-good points to go back to.
- **Keep old branches.** A branch costs nothing; it is how a feature is fixed later.

## Recordings as the referee
The recordings check "original game plus `OriginalSourceBugFixes`", because the fixes sit below `RecordPlayback`. A fix that changes gameplay means re-recording the affected missions; its review doc says which.

Recordings replay a stream of OS reads, so they can referee every layer up to `GodotPrep`. The Godot port will not make the same reads, so it will need the traces (game state) compared another way. When `Godot` replaces the engine, tag that point; after it, merging the C++ layers upward stops making sense and fixes are carried across by hand.

## In Perforce terms
A branch is a stream, a merge is an integrate, a push is a submit. Unlike Perforce, commits are local until pushed, and pushed history must never be rewritten.
