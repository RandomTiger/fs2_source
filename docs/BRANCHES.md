# Branches

The repo is a stack of layers. Each layer is a branch off the one below it, and changes only ever merge upward.

```
master                    original 1999 source, as provided by Volition. Never changed.
  VS2015                  builds with Visual Studio 2015
    VS2022                builds with Visual Studio 2022
      OriginalSourceBugFixes   bugs that were in the original game, one commit and review doc each (docs/fix_reviews)
        RecordPlayback    gameplay recorder and replayer, recordings, the test suite
          <features>      one branch per feature, side by side (FS2Demo, FS1Compat, VoiceControl, OgvMovies, ...; FS1Demo off FS1Compat, PadControl off VoiceControl)
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
- **`RecordPlayback` is strictly for changes that let record and playback run without sync error** (Tom, 2026-10-22, D-20261022-085602), ideally only within the recording systems. What stays on it: the test suite (autotest, the menu tour, the replay and check scripts); the audio listening scenes and their A/B script, which count as autotest; and the `fs2.ini` settings file that replaced the registry (registry settings caused start-up problems). Docs and process commits are acceptable there, since they cannot cause a sync error. Diagnostics, speed tools and features go elsewhere: the lowest layer whose job it is, `PrePort` for diagnostics and speed work. Already-pushed work stays where it is.
- **Merges only flow up.** Never merge a higher branch into a lower one. One agreed exception: `CosmeticRandom` (a separate random stream for effects) was merged into `RecordPlayback`, because it makes the recordings independent of rendering and fixes nothing a player sees.
- **Feature branches branch off `RecordPlayback`** and only take merges from it, never from `Enhanced` or from each other. That keeps each feature separable. Features that need the port preparation (for example `Headless`) branch off `PrePort` instead and only take merges from it.
- **A feature goes into `Enhanced` with a merge commit** (`git merge --no-ff`), once Tom allows it in (finished, or a work in progress good enough to include). Later fixes to it are made on the feature branch and merged into `Enhanced` again.
- **Never rebase, squash or amend anything already pushed.** Merge instead. Before a push, local commits can still be tidied.
- **When `Enhanced` passes the full suite, move `Latest` to it and tag it** (`latest-<date>`), so there are known-good points to go back to.
- **Long-lived feature branches** (`FS2Demo`, the one exe for the demo; `FS1Compat`, FreeSpace 1 data): off `RecordPlayback`, into `Enhanced`, merged once per green batch: green on the branch's own gate is the entry ticket. **Carry-up is batched per layer** (Tom, D-20261020-205311): at each layer everything ready below it (every lower layer's new commits, every feature batch green on its own branch) merges in together, the layer's gate runs once on the combined result, and the combined result moves up one layer through `PrePort`, `PrePortCleanup`, `PrePort64` and `GodotPrep`; no change gets a chain of its own. A red gate is diagnosed from the failing test and the layer's diff against its parent; only when that does not name the cause is the batch split and the suspects gated alone against the failing test, logged in STATUS as a deviation with the cause found. Nothing from a red batch moves up until the cause is named and fixed or reverted. Docs-only changes wait on their layer for the next real change, or ride the daily full run. No recording on any layer may move from it: a moved retail recording is a gating leak, to be found by the per-layer diff or the split, never re-recorded.
- **`FS1Demo`** (D-20261020-183012, D-20261020-183013): a long-lived data-set branch off `FS1Compat`, not off `RecordPlayback`. Its only upstream is `FS1Compat`, merged down into it (that carries every lower layer's change); it merges into `FS1Compat` after each green batch, under `FS1Compat`'s gate plus `tools/fs1demo_test.ps1`, and reaches `Enhanced` only through `FS1Compat`'s own merge, whose brief lists the `FS1Demo` content it carries. The FS1 demo is extracted from its installer at run time (the machine-profile setting, D-20261019-204553); nothing extracted is committed. A feature branch carries another feature branch's commits only once `Enhanced` has them: before every `FS1Demo` into `FS1Compat` merge, `git log origin/Enhanced..FS1Demo` filtered to commits reachable from `origin/FS2Demo` must be empty and is quoted in the STATUS line; later `FS2Demo` content is taken at a commit already in `Enhanced`, never its tip.
- **`PadControl`** (Tom, D-20261021-154901, D-20261021-171829, D-20261022-093601): a child of `VoiceControl`, not off `RecordPlayback`. Its only upstream is `VoiceControl`, merged down into it, so it lags `RecordPlayback` by `VoiceControl`'s lag. It merges into `Enhanced` direct, not through `VoiceControl`, and carries only `VoiceControl` commits already in `Enhanced`; while `VoiceControl` has commits `Enhanced` lacks, `PadControl` waits for `VoiceControl`'s own `Enhanced` merge, and its first merge that carries voice code is a brief.
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

Exception, pre-rule: `PadControl-done` (a false "done", deleted 2026-10-20 before this rule; its commit is held by `PadControl-checkpoint-2026-10-13`). When the pad work is finished, `PadControl` gets a new `PadControl-done` at its tip. From now on a wrong tag is corrected by adding a tag beside it, never by deleting it (D-20261020-105347).

## Recordings as the referee
The recordings check "original game plus `OriginalSourceBugFixes`", because the fixes sit below `RecordPlayback`. A fix that changes gameplay means re-recording the affected missions; its review doc says which.

Recordings replay a stream of OS reads, so they can referee every layer up to `PrePort`. The Godot port will not make the same reads, so it will need the traces (game state) compared another way. When `Godot` replaces the engine, tag that point; after it, merging the C++ layers upward stops making sense and fixes are carried across by hand.

## In Perforce terms
A branch is a stream, a merge is an integrate, a push is a submit. Unlike Perforce, commits are local until pushed, and pushed history must never be rewritten.
