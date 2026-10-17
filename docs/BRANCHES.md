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
                PrePort64 the 64-bit build (D-057, D-058), proven 2026-10-17: 48/48 x64 headless identical to x86 (D-064)
                  PrePortCleanup  dead code, memory guards, backend collapses (D-059 to D-062); no change may alter a recording
                    GodotPrep   Godot-specific preparation, still the C++ game on Windows; x64
                      Godot     the game running in Godot (x64), with its own feature branches off it
                        GodotEnhanced   the Godot game with its features combined
```

`Latest` is not a layer. It is the default branch on GitHub and is moved forward to `Enhanced` each time `Enhanced` passes the full suite, so it is always the newest known-good build.

## Folders
Each branch in use has its own folder (a git worktree, like a second Perforce workspace) next to the others, named `fs2_<Branch>`: `fs2_RecordPlayback` (the main clone), `fs2_Enhanced`, `fs2_PrePort`, and later `fs2_GodotPrep`, `fs2_Godot`, `fs2_GodotEnhanced`. The prefix differs per machine (`fs2_source_<Branch>` on DESKTOP-CLAUDE). Build and run the tests in `fs2_Enhanced`, so every feature is covered; commit each change in the folder of the branch it belongs on.

## Rules
- **Fix a bug on the lowest layer that has it, then merge it upward one layer at a time.** An original-game bug goes on `OriginalSourceBugFixes`, a recorder bug on `RecordPlayback`, a feature bug on that feature's branch. Then: `OriginalSourceBugFixes` → `RecordPlayback` → `Enhanced` → `PrePort` → `PrePort64` → `PrePortCleanup` → `GodotPrep` → `Godot` → `GodotEnhanced`.
- **Merges only flow up.** Never merge a higher branch into a lower one. One agreed exception: `CosmeticRandom` (a separate random stream for effects) was merged into `RecordPlayback`, because it makes the recordings independent of rendering and fixes nothing a player sees.
- **Every layer below `GodotPrep` is closed** (D-059). It takes a change only for a clear developer mistake a recording found, each through the steward. `PrePort` was open for generic fixes the 64-bit work showed up (D-058) and closed when `PrePort64` was proven (D-064). Each closed layer's frozen tip has an annotated tag, `<Branch>-done` (the P4 label: a fixed name for the state that passed, to diff a regression against): `PrePort-done` where `PrePort` was declared done (D-049), `Enhanced-done` at its last full-suite pass (where `Latest` points), and the lower layers and the feature branches at their tips when they closed.
- **`PrePortCleanup`** (D-059), a child of `PrePort64`, holds the cleanup: dead code (D-060), the memory wrapper trim and Debug guards (D-061), backend collapses and other simplifications (D-062). Its rule has no exceptions: no change may alter a recording. A change that moves a recording is reverted and reported, never re-recorded. `PrePort64` fixes merge into it as they land. It merges into `GodotPrep` once `PrePort64` is proven and it is green on the full suite; the stack is then `PrePort` → `PrePort64` → `PrePortCleanup` → `GodotPrep`.
- **64-bit is the final C++ layer before the port** (D-064). `PrePort64` was proven 2026-10-17 (tag `PrePort64-proven`): every recording replays identically in the 64-bit headless build, Release and Debug, and the headless restart check passes (D-057). It was then merged into `GodotPrep`, which had been its sibling off `PrePort` (D-058).
- **Builds per layer.** Below `PrePort64`: x86 only. `PrePort64` and `PrePortCleanup`: x86 and x64. `GodotPrep` and `Godot` are x64 builds; `GodotPrep` keeps x86 only while the suite's windowed checks need it. 64-bit Windows has no Direct3D 7 or earlier, so the original renderer cannot run on x64: an x64 build runs headless or with the null renderer until Godot draws, refuses a windowed start with a clear error, and the windowed game is x86.
- **Suite per layer.** x86: every check. x64 (from `PrePort64` up): headless replay Release and Debug, and the headless restart check. `-nodraw` and the sound model need a renderer or device and run on x86 only; equal traces mean they cover the same logic.
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
