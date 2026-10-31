# Branches

The repo is a stack of layers. Each layer is a branch off the one below it, and changes only ever merge upward.

```
master                    original 1999 source, as provided by Volition. Never changed.
    VS2022                builds with Visual Studio 2022
      OriginalSourceBugFixes   bugs that were in the original game, one commit and review doc each (docs/fix_reviews)
        RecordPlayback    gameplay recorder and replayer, recordings, the test suite
          <features>      one branch per feature, side by side (FS2Demo, FS1Compat, VoiceControl, OgvMovies, ...; FS1Demo off FS1Compat, PadControl off VoiceControl)
            Enhanced      the original game with all its features combined (Latest points here)
              PrePort32   port preparation that needs no engine, rendered and 32-bit; code removal (cleanup) lives here
                PrePort64 x64, headless, with the host seam: the last point before an engine is chosen
                  Godot   the game running in Godot, cut from PrePort64, with its own feature branches off it
                    Vulkan          Vulkan renderer work and measurements (docs/Vulkan.md), a Godot sub-branch
                      SteamIntegration   Steam integration, cut from Godot, now child of Vulkan (D-20261031-085751)
                    GodotEnhanced   the Godot game with its features combined (later)
```

The port stack is `Enhanced` -> `PrePort32` -> `PrePort64` -> `Godot` -> `GodotEnhanced` (Tom, D-20261021-200950, as corrected by D-20261022-173525). `PrePort32` is the old `PrePort`, renamed after `PrePortCleanup` was merged into it; it therefore also holds the D-057 x64 portability commits, which are inert in the x86 build. `PrePortCleanup` and `GodotPrep` are closed: tagged `PrePortCleanup-closed` and `GodotPrep-closed`, branch names deleted; their commits are in `PrePort32` and `PrePort64`.

`Latest` is not a layer. It is the default branch on GitHub and is moved forward to `Enhanced` each time `Enhanced` passes the full suite, so it is always the newest known-good build.

## Folders
Each branch in use has its own folder (a git worktree, like a second Perforce workspace) next to the others, named `fs2_<Branch>`: `fs2_RecordPlayback` (the main clone), `fs2_Enhanced`, `fs2_PrePort32`, `fs2_PrePort64`, and later `fs2_Godot`, `fs2_GodotEnhanced`. Build and run the tests in `fs2_Enhanced`, so every feature is covered; commit each change in the folder of the branch it belongs on.

## Rules
- **Fix a bug on the lowest layer that has it, then merge it upward one layer at a time.** An original-game bug goes on `OriginalSourceBugFixes`, a recorder bug on `RecordPlayback`, a feature bug on that feature's branch. Then: `OriginalSourceBugFixes` → `RecordPlayback` → `Enhanced` → `PrePort32` → `PrePort64` → `Godot` → `GodotEnhanced`.
- **`RecordPlayback` is strictly for changes that let record and playback run without sync error** (Tom, 2026-10-22, D-20261022-085602), ideally only within the recording systems. What stays on it: the test suite (autotest, the menu tour, the replay and check scripts); the audio listening scenes and their A/B script, which count as autotest; and the `fs2.ini` settings file that replaced the registry (registry settings caused start-up problems). Docs and process commits are acceptable there, since they cannot cause a sync error. Diagnostics, speed tools and features go elsewhere: the lowest layer whose job it is, `PrePort32` for diagnostics and speed work. Already-pushed work stays where it is.
- **Merges only flow up.** Never merge a higher branch into a lower one. One agreed exception: `CosmeticRandom` (a separate random stream for effects) was merged into `RecordPlayback`, because it makes the recordings independent of rendering and fixes nothing a player sees.
- **Feature branches branch off `RecordPlayback`** and only take merges from it, never from `Enhanced` or from each other. That keeps each feature separable. Features that need the port preparation (for example `Headless`) branch off `PrePort32` instead and only take merges from it.
- **A feature goes into `Enhanced` with a merge commit** (`git merge --no-ff`), once Tom allows it in (finished, or a work in progress good enough to include). Later fixes to it are made on the feature branch and merged into `Enhanced` again.
- **Never rebase, squash or amend anything already pushed.** Merge instead. Before a push, local commits can still be tidied.
- **When `Enhanced` passes the full suite, move `Latest` to it and tag it** (`latest-<date>`), so there are known-good points to go back to.
- **Long-lived feature branches** (`FS2Demo`, the one exe for the demo; `FS1Compat`, FreeSpace 1 data): off `RecordPlayback`, into `Enhanced`, merged once per green batch: green on the branch's own gate is the entry ticket. **Carry-up is batched per layer** (Tom, D-20261020-205311): at each layer everything ready below it (every lower layer's new commits, every feature batch green on its own branch) merges in together, the layer's gate runs once on the combined result, and the combined result moves up one layer through `PrePort32` and `PrePort64`, once per `Latest` move rather than per feature (D-20261021-200950 item 7); no change gets a chain of its own. A red gate is diagnosed from the failing test and the layer's diff against its parent; only when that does not name the cause is the batch split and the suspects gated alone against the failing test, logged in STATUS as a deviation with the cause found. Nothing from a red batch moves up until the cause is named and fixed or reverted. Docs-only changes wait on their layer for the next real change, or ride the daily full run. No recording on any layer may move from it: a moved retail recording is a gating leak, to be found by the per-layer diff or the split, never re-recorded.
- **`FS1Demo`** (D-20261020-183012, D-20261020-183013): a long-lived data-set branch off `FS1Compat`, not off `RecordPlayback`. Its only upstream is `FS1Compat`, merged down into it (that carries every lower layer's change); it never merges into `FS1Compat` (D-20261021-173031: a child's own change never enters its parent). It merges into `Enhanced` direct, not through `FS1Compat` (Tom, D-20261021-171829), with the carry rule: it carries only `FS1Compat` commits already in `Enhanced`, so before the merge it takes `FS1Compat` merged down to `FS1Compat`'s last `Enhanced`-merged point, and while `FS1Compat` has commits `Enhanced` lacks, `FS1Demo` waits for `FS1Compat`'s own `Enhanced` merge. Its gate for that merge: the FS1 demo set proven, the 48 identical r/d, and `Enhanced` re-proving all four data sets after it. The FS1 demo is extracted from its installer at run time (the machine-profile setting, D-20261019-204553); nothing extracted is committed. A feature branch carries another feature branch's commits only once `Enhanced` has them: before every `FS1Demo` into `Enhanced` merge, `git log origin/Enhanced..FS1Demo` filtered to commits reachable from `origin/FS2Demo` must be empty and is quoted in the STATUS line; later `FS2Demo` content is taken at a commit already in `Enhanced`, never its tip. The FS1 demo's code was split out of `FS1Compat` (rule 4 of D-20261021-173031, D-20261023-202744): `FS1Compat` reverted the merge `f2363f5` ("Merge FS1Demo into FS1Compat") and re-applied only its FS1 retail hunks; `FS1Demo` merged that down and reverted the revert, so the demo code lives on `FS1Demo` only. A later `FS1Demo` into `FS1Compat` merge must not bring the demo code back.
- **`PadControl`** (Tom, D-20261021-154901, D-20261021-171829, D-20261022-093601): a child of `VoiceControl`, not off `RecordPlayback`. Its only upstream is `VoiceControl`, merged down into it, so it lags `RecordPlayback` by `VoiceControl`'s lag. It merges into `Enhanced` direct, not through `VoiceControl`, and carries only `VoiceControl` commits already in `Enhanced`; while `VoiceControl` has commits `Enhanced` lacks, `PadControl` waits for `VoiceControl`'s own `Enhanced` merge, and its first merge that carries voice code is a brief.
- **Tags are never deleted, local or remote. Only the branch label is removed after a done tag is pushed and verified.** A finished feature branch (fully merged into its layer, that layer green and pushed) gets an annotated `<Name>-done` tag at its tip, naming the layer and the merge commit; the tag is pushed and checked on GitHub (`git ls-remote --tags`), and then the branch label is deleted, on GitHub first. The commits stay reachable through the tag, and a branch can be made from it again at any time. Unfinished work gets `<Name>-checkpoint-<YYYY-MM-DD>` tags only and keeps its branch. The `<Layer>-done` tags (`PrePort-done`, `Enhanced-done`, ...) mark layer freezes (D-059) and keep that meaning; new layer freezes are tagged `<Layer>-frozen-<date>`. Layers, `Latest` and live branches are never removed this way (D-20261020-063400, 063405; Tom, 2026-10-20).
- **`FS1Compat`** (`docs/FS1_COMPAT.md`, Tom, 2026-10-20): its work is done on `FS1*` branches off it, each merged in with `--no-ff`, then tagged `<Name>-done` and its label removed. `RecordPlayback` is merged down into it before each FS1 batch. After each green batch (the builds, and `recordings/fs1` identical in Release and Debug; the FS2 suites run at `Enhanced` on the merged batch, D-20261020-211001) it merges into `Enhanced`, with a merge brief to the steward first. Exception to the feature-branch rule above: it took `Enhanced` once, for the data set profile table, on Tom's order. `FS1Demo`, off it, belongs to the FS1 demo work.

## Tags for finished branches
| Tag | Merged into |
|---|---|
| `32BitRendering-done` | `Enhanced` |
| `CosmeticRandom-done` | `RecordPlayback` |
| `DebrisOffscreenFix-done` | `PrePort` |
| `GodotPrep-closed` | `PrePort64` (layer closed, D-20261021-200950) |
| `LogicDrawSplit-done` | `PrePort` |
| `PrePortCleanup-closed` | `PrePort32` (layer closed, D-20261021-200950, D-20261022-173525) |
| `ShivanShipGlow-done` | `Enhanced` |
| `SoundOwnTimer-done` | `PrePort` |
| `Warnings-done` | `Enhanced` |

`PadControl-checkpoint-2026-10-13` marks the pad work, which is not finished; `PadControl` stays a branch.

Exception, pre-rule: `PadControl-done` (a false "done", deleted 2026-10-20 before this rule; its commit is held by `PadControl-checkpoint-2026-10-13`). When the pad work is finished, `PadControl` gets a new `PadControl-done` at its tip. From now on a wrong tag is corrected by adding a tag beside it, never by deleting it (D-20261020-105347).

## Recordings as the referee
The recordings check "original game plus `OriginalSourceBugFixes`", because the fixes sit below `RecordPlayback`. A fix that changes gameplay means re-recording the affected missions; its review doc says which.

Recordings replay a stream of OS reads, so they can referee every layer up to `PrePort64`. The Godot port will not make the same reads, so it will need the traces (game state) compared another way. When `Godot` replaces the engine, tag that point; after it, merging the C++ layers upward stops making sense and fixes are carried across by hand.

## In Perforce terms
A branch is a stream, a merge is an integrate, a push is a submit. Unlike Perforce, commits are local until pushed, and pushed history must never be rewritten.
