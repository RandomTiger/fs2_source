# FreeSpace 2 source: RecordPlayback branch

## What this branch is
Branch `RecordPlayback` of `RandomTiger/fs2_source`, off `VS2022`. It holds the original 1999 FreeSpace 2 source, building with Visual Studio 2022, plus a gameplay recorder (`docs/REPLAY.md`).

The recorder records a person playing a mission. It replays the same inputs into any later build and compares a per-frame gameplay-state trace, so any change to how the game plays is detected and needs a decision, instead of slipping through. It is the referee for all later work, including any engine port.

## Rules
- **Minimal change.** Only change what the current task needs. Every change to game code, including bug fixes, must pass replay verification against the existing recordings.
- **Nothing from `fs2_source_ue` comes in unasked.** Not its headless mode and not its fixes. If a later task wants one, it goes in as its own change and is verified with replay.
- Surgical edits; no whitespace churn or refactors of code you only passed through.
- FSO (FreeSpace Open) is not a reference.
- Game assets are never committed. Recordings (`recordings/`) and traces are fine.

## Paths (this machine: DESKTOP-MAIN)
- Game install (`PATH_FS2`): `U:\SteamLibrary\steamapps\common\Freespace 2`
- Built exes go to `PATH_FS2`, named `FS2vs2022d.exe` (Debug) and `FS2vs2022r.exe` (Release). The fs2_source_ue repo builds the same names into the same folder, so whichever repo built last is the exe there.
- `TraceDiff.exe` goes to `tools\bin\`.
- MSBuild: `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`

## Build
```
MSBuild freespace2.sln /t:Freespace2 /p:Configuration=Debug /p:Platform=x86 /m
MSBuild freespace2.sln /t:TraceDiff /p:Configuration=Release /p:Platform=x86 /m
```

## Verify a change
For each recording in `recordings/`:
```
FS2vs2022d.exe -window -replay recordings\NAME.fs2rec -trace replays\NAME.trace
tools\bin\TraceDiff.exe recordings\NAME.trace replays\NAME.trace
```
Run with the working directory set to `PATH_FS2`, and pass absolute paths to the recording. `NAME.fs2rec.replay.log` must say `REPLAY OK`, and TraceDiff must exit with 0. On a divergence, report the frame and fields to Tom and let him decide. Never change thresholds or re-record to make a test pass.
