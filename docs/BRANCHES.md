# Branches

The repo is a stack of layers. Each layer is a branch off the one below it, and changes only ever merge upward.

```
master                    original 1999 source, as provided by Volition. Never changed.
  VS2015                  builds with Visual Studio 2015
    VS2022                builds with Visual Studio 2022
      OriginalSourceBugFixes   bugs that were in the original game, one commit and review doc each (docs/fix_reviews)
        RecordPlayback    gameplay recorder and replayer, recordings, the test suite
          <features>      one branch per feature, side by side (PadControl, ShivanShipGlow, 32BitRendering, ...)
            Enhanced      the original game with all its features combined (Latest points here)
              PrePort     everything that can be done before the engine moves, nothing Godot-specific; its own recordings
                GodotPrep Godot-specific preparation, still the C++ game on Windows
                  Godot   the game running in Godot, with its own feature branches off it
                    GodotEnhanced   the Godot game with its features combined
```

`Latest` is not a layer. It is the default branch on GitHub and is moved forward to `Enhanced` each time `Enhanced` passes the full suite, so it is always the newest known-good build.

## Folders
Each branch in use has its own folder (a git worktree, like a second Perforce workspace) next to the others, named `fs2_<Branch>`: `fs2_RecordPlayback` (the main clone), `fs2_Enhanced`, `fs2_PrePort`, and later `fs2_GodotPrep`, `fs2_Godot`, `fs2_GodotEnhanced`. The prefix differs per machine (`fs2_source_<Branch>` on DESKTOP-CLAUDE). Build and run the tests in `fs2_Enhanced`, so every feature is covered; commit each change in the folder of the branch it belongs on.

## Rules
- **Fix a bug on the lowest layer that has it, then merge it upward one layer at a time.** An original-game bug goes on `OriginalSourceBugFixes`, a recorder bug on `RecordPlayback`, a feature bug on that feature's branch. Then: `OriginalSourceBugFixes` → `RecordPlayback` → `Enhanced` → `PrePort` → `GodotPrep` → `Godot` → `GodotEnhanced`.
- **Merges only flow up.** Never merge a higher branch into a lower one. One agreed exception: `CosmeticRandom` (a separate random stream for effects) was merged into `RecordPlayback`, because it makes the recordings independent of rendering and fixes nothing a player sees.
- **Feature branches branch off `RecordPlayback`** and only take merges from it, never from `Enhanced` or from each other. That keeps each feature separable. Features that need the port preparation (for example `Headless`) branch off `PrePort` instead and only take merges from it.
- **A feature goes into `Enhanced` with a merge commit** (`git merge --no-ff`), once Tom allows it in (finished, or a work in progress good enough to include). Later fixes to it are made on the feature branch and merged into `Enhanced` again.
- **Never rebase, squash or amend anything already pushed.** Merge instead. Before a push, local commits can still be tidied.
- **When `Enhanced` passes the full suite, move `Latest` to it and tag it** (`latest-<date>`), so there are known-good points to go back to.
- **Keep old branches.** A branch costs nothing; it is how a feature is fixed later.

## Recordings as the referee
The recordings check "original game plus `OriginalSourceBugFixes`", because the fixes sit below `RecordPlayback`. Re-recording is a gameplay deviation, never a repair: a change that invalidates recordings goes to the decision steward first, with the count of recordings affected, and its review doc lists them.

There are two recording sets. `RecordPlayback`'s set referees it, the features and `Enhanced`. `PrePort` has its own set (`recordings/` on `PrePort`), because separating logic from drawing changes the stream of OS reads; it is made in the reference configuration (1024x768, GR_1024, medium skill) with the AI pilot.

Recordings replay a stream of OS reads, so they can referee every layer up to `PrePort`. The Godot port will not make the same reads, so it will need the traces (game state) compared another way. When `Godot` replaces the engine, tag that point; after it, merging the C++ layers upward stops making sense and fixes are carried across by hand.

## In Perforce terms
A branch is a stream, a merge is an integrate, a push is a submit. Unlike Perforce, commits are local until pushed, and pushed history must never be rewritten.
