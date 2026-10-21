# Branches

The repo is a stack of layers. Each layer is a branch off the one below it, and changes only ever merge upward.

```
master                    original 1999 source, as provided by Volition. Never changed.
  VS2022                builds with Visual Studio 2022
    OriginalSourceBugFixes   bugs that were in the original game, one commit and review doc each (docs/fix_reviews)
      RecordPlayback    gameplay recorder and replayer, recordings, the test suite
        <features>      one branch per feature, side by side (FS2Demo, FS1Compat, PadControl, OgvMovies, ...; FS1Demo off FS1Compat)
          Enhanced      the original game with all its features combined (Latest points here)
            PrePort     everything that can be done before the engine moves, nothing Godot-specific; its own recordings
              PrePortCleanup  dead code, memory guards, backend collapses (D-059 to D-062); no change may alter a recording
                PrePort64 the 64-bit build (D-057, D-058), proven 2026-10-17 (D-067); the last C++ layer before GodotPrep
                  GodotPrep   Godot-specific preparation, still the C++ game on Windows; x64
                    Godot     the game running in Godot (x64), with its own feature branches off it
                      GodotEnhanced   the Godot game with its features combined
```

`Latest` is not a layer. It is the default branch on GitHub and is moved forward to `Enhanced` each time `Enhanced` passes the full suite, so it is always the newest known-good build.

## Folders
Each branch in use has its own folder (a git worktree, like a second Perforce workspace) next to the others, named `fs2_<Branch>`: `fs2_RecordPlayback` (the main clone), `fs2_Enhanced`, `fs2_PrePort`, and later `fs2_GodotPrep`, `fs2_Godot`, `fs2_GodotEnhanced`. The prefix differs per machine (`fs2_source_<Branch>` on DESKTOP-CLAUDE). Build and run the tests in `fs2_Enhanced`, so every feature is covered; commit each change in the folder of the branch it belongs on.

## Rules
- **Fix a bug on the lowest layer that has it, then merge it upward one layer at a time.** An original-game bug goes on `OriginalSourceBugFixes`, a recorder bug on `RecordPlayback`, a feature bug on that feature's branch. Then: `OriginalSourceBugFixes` → `RecordPlayback` → `Enhanced` → `PrePort` → `PrePortCleanup` → `PrePort64` → `GodotPrep` → `Godot` → `GodotEnhanced`.
- **The VS2015 layer was removed** (D-20261017-223830, Tom). The stack starts `master`, `VS2022`. VS2015's commits stay in VS2022's history; nothing was rewritten. The branch and its `VS2015-done` tag are deleted locally and on origin.
- **Merges only flow up.** Never merge a higher branch into a lower one. One agreed exception: `CosmeticRandom` (a separate random stream for effects) was merged into `RecordPlayback`, because it makes the recordings independent of rendering and fixes nothing a player sees.
- **Every layer but `master` is open again** (D-20261017-223742, amending D-059). `master` stays closed as the untouched original. Open means the lowest layer whose job it is, not anywhere: original-source bugs go to `OriginalSourceBugFixes` with a review doc (the D-021 bar, and a D-079 assessment where a fix would move recordings); compiler fixes go to `VS2022`; recorder and suite work goes to `RecordPlayback`. A change low in the stack runs the suite at every layer above it; if it moves recordings, the re-record is a logged deviation at each layer. `PrePort64` and `PrePortCleanup` keep their own rules (D-058, D-067; the cleanup invariant still holds). The `<Branch>-done` tags stay as the record of where each layer stood when D-059 closed it (`PrePort-done` where `PrePort` was declared done, D-049; `Enhanced-done` at its last full-suite pass, where `Latest` points); a layer gets a new tag when it closes again.
- **The VS2022 standard** (steward with Tom, 2026-10-17): every project in the solution compiles and runs on VS2022 and every layer above. `tools/tools_check.ps1` (from `RecordPlayback`, `-RepoDir` for a lower checkout) builds all nine tool projects and smoke-runs them; it is a required PASS on every layer from `VS2022` up and part of the quick suite. Tom (2026-10-18, overriding D-20261018-120739): the tools must work on `VS2022` as well as they did in the release, so the start-up heap fixes for Fonttool, Pofview and Nebedit (#11, #22) are on `VS2022` too, as well as on `OriginalSourceBugFixes` with their reviews. The script keeps a per-layer known-failure list, now empty.
- **`PrePortCleanup`** (D-059) holds the cleanup: dead code (D-060), the memory wrapper trim and Debug guards (D-061), backend collapses and other simplifications (D-062). Its rule has no exceptions: no change may alter a recording. A change that moves a recording is reverted and reported, never re-recorded. Layer order (Tom, D-20261018-130305): `PrePort` → `PrePortCleanup` → `PrePort64` → `GodotPrep`. It began as a child of `PrePort64`, and `GodotPrep` had only ever merged `PrePort64`, so the cleanup never reached `GodotPrep`. `PrePort64` merged `PrePortCleanup` once, 2026-10-18; since then cleanup work flows up through `PrePort64` to `GodotPrep`. Nothing was rewritten; the tag `PrePort64-proven` marks 64-bit proven on its own.
- **64-bit is the final C++ layer before the port** (D-067). `PrePort64` was proven 2026-10-17 (tag `PrePort64-proven`): every recording replays identically in the 64-bit headless build, Release and Debug, and the headless restart check passes (D-057). It was then merged into `GodotPrep`, which had been its sibling off `PrePort` (D-058).
- **Builds per layer.** Below `PrePortCleanup`: x86 only. `PrePortCleanup` and `PrePort64`: x86 and x64. `GodotPrep` and `Godot` are x64 builds; `GodotPrep` keeps x86 only while the suite's windowed checks need it. 64-bit Windows has no Direct3D 7 or earlier, so the original renderer cannot run on x64: an x64 build runs headless or with the null renderer until Godot draws, refuses a windowed start with a clear error, and the windowed game is x86.
- **Suite per layer.** x86: every check. x64 (from `PrePort64` up): headless replay Release and Debug, and the headless restart check. `-nodraw` and the sound model need a renderer or device and run on x86 only; equal traces mean they cover the same logic.
- **Feature branches branch off `RecordPlayback`** and only take merges from it, never from `Enhanced` or from each other. That keeps each feature separable. Features that need the port preparation (for example `Headless`) branch off `PrePort` instead and only take merges from it. Demo mode by data detection (D-063) and FS1 compatibility (D-064) are such features: each gets its own branch off `RecordPlayback`, merged into `Enhanced` when green, then merged up (D-20261017-223742).
- **A feature goes into `Enhanced` with a merge commit** (`git merge --no-ff`), once Tom allows it in (finished, or a work in progress good enough to include). Later fixes to it are made on the feature branch and merged into `Enhanced` again.
- **Never rebase, squash or amend anything already pushed.** Merge instead. Before a push, local commits can still be tidied.
- **When `Enhanced` passes the full suite, move `Latest` to it and tag it** (`latest-<date>`), so there are known-good points to go back to.
- **Long-lived feature branches** (`FS2Demo`, the one exe for the demo; `FS1Compat`, FreeSpace 1 data): off `RecordPlayback`, into `Enhanced`, merged once per green batch: green on the branch's own gate is the entry ticket. **Carry-up is batched per layer** (Tom, D-20261020-205311): at each layer everything ready below it (every lower layer's new commits, every feature batch green on its own branch) merges in together, the layer's gate runs once on the combined result, and the combined result moves up one layer through `PrePort`, `PrePortCleanup`, `PrePort64` and `GodotPrep`; no change gets a chain of its own. A red gate is diagnosed from the failing test and the layer's diff against its parent; only when that does not name the cause is the batch split and the suspects gated alone against the failing test, logged in STATUS as a deviation with the cause found. Nothing from a red batch moves up until the cause is named and fixed or reverted. Docs-only changes wait on their layer for the next real change, or ride the daily full run. No recording on any layer may move from it: a moved retail recording is a gating leak, to be found by the per-layer diff or the split, never re-recorded.
- **`FS1Demo`** (D-20261020-183012, D-20261020-183013): a long-lived data-set branch off `FS1Compat`, not off `RecordPlayback`. Its only upstream is `FS1Compat`, merged down into it (that carries every lower layer's change); it merges into `FS1Compat` after each green batch, under `FS1Compat`'s gate plus `tools/fs1demo_test.ps1`, and reaches `Enhanced` only through `FS1Compat`'s own merge, whose brief lists the `FS1Demo` content it carries. The FS1 demo is extracted from its installer at run time (the machine-profile setting, D-20261019-204553); nothing extracted is committed. A feature branch carries another feature branch's commits only once `Enhanced` has them: before every `FS1Demo` into `FS1Compat` merge, `git log origin/Enhanced..FS1Demo` filtered to commits reachable from `origin/FS2Demo` must be empty and is quoted in the STATUS line; later `FS2Demo` content is taken at a commit already in `Enhanced`, never its tip.
- **Tags are never deleted, local or remote. Only the branch label is removed after a done tag is pushed and verified.** A finished feature branch (fully merged into its layer, that layer green and pushed) gets an annotated `<Name>-done` tag at its tip, naming the layer and the merge commit; the tag is pushed and checked on GitHub (`git ls-remote --tags`), and then the branch label is deleted, on GitHub first. The commits stay reachable through the tag, and a branch can be made from it again at any time. Unfinished work gets `<Name>-checkpoint-<YYYY-MM-DD>` tags only and keeps its branch. The `<Layer>-done` tags (`PrePort-done`, `Enhanced-done`, ...) mark layer freezes (D-059) and keep that meaning; new layer freezes are tagged `<Layer>-frozen-<date>`. Layers, `Latest` and live branches are never removed this way (D-20261020-063400, 063405; Tom, 2026-10-20).
- **`FS1Compat`** (`docs/FS1_COMPAT.md`, Tom, 2026-10-20): its work is done on `FS1*` branches off it, each merged in with `--no-ff`, then tagged `<Name>-done` and its label removed. `RecordPlayback` is merged down into it before each FS1 batch. After each green batch (the full 48 in Release and Debug, `recordings/fs1` identical, the retail quick suite matching `RecordPlayback`) it merges into `Enhanced`, with a merge brief to the steward first. Exception to the feature-branch rule above: it took `Enhanced` once, for the data set profile table, on Tom's order. `FS1Demo`, off it, belongs to the FS1 demo work.

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
The recordings check "original game plus `OriginalSourceBugFixes`", because the fixes sit below `RecordPlayback`. Re-recording is a gameplay deviation, never a repair: a change that invalidates recordings goes to the decision steward first, with the count of recordings affected, and its review doc lists them.

There are two recording sets. `RecordPlayback`'s set referees it, the features and `Enhanced`. `PrePort` has its own set (`recordings/` on `PrePort`), because separating logic from drawing changes the stream of OS reads; it is made in the reference configuration (1024x768, GR_1024, medium skill) with the AI pilot.

Recordings replay a stream of OS reads, so they can referee every layer up to `PrePort`. The Godot port will not make the same reads, so it will need the traces (game state) compared another way. When `Godot` replaces the engine, tag that point; after it, merging the C++ layers upward stops making sense and fixes are carried across by hand.

## In Perforce terms
A branch is a stream, a merge is an integrate, a push is a submit. Unlike Perforce, commits are local until pushed, and pushed history must never be rewritten.
