# Formation ghost: watching the wing leader test on another PC

Throwaway branch `FormationGhost` (off `PrePortCleanup`, never merged). For Claude on a new PC, to guide Tom
through it. Tom starts it with: *"Read docs/TRIAL_GUIDE.md on branch FormationGhost of RandomTiger/fs2_source
and guide me through it."*

## Rules for this branch
- Throwaway: nothing here merges anywhere. Do not run the autotest suites, gates or `replay_check` on it (Tom,
  2026-10-22). Build and watch only.
- Never download game data. Use the FreeSpace 2 install already on the PC (its machine profile names it; if
  there is no profile, ask Tom for the path).
- One game instance at a time.
- Guide one step at a time, in plain language; wait for Tom before the next step.

## What is being tested
Issue #1: the original formation code steers AI wing leaders oddly. The branch can switch a fix on, record the
leaders' paths with the fix, and draw those paths as green wireframe "ghosts" over a normal replay. Tom watches
how far the fixed leaders drift from the original ones and decides fix or keep. `ghost/README.txt` says what
to look for in each mission and when.

## Setup (once per PC)
1. Tools: Visual Studio 2022 with "Desktop development with C++" (MSBuild, v143, Win32) and Git.
2. Clone and check out the branch in its own folder:
   ```
   git clone https://github.com/RandomTiger/fs2_source.git fs2_source
   cd fs2_source
   git config core.autocrlf false
   git worktree add ..\fs2_source_FormationGhost FormationGhost
   ```
3. Build Release into a scratch folder, so the game's own exe is not touched. In PowerShell, in
   `fs2_source_FormationGhost`:
   ```
   $env:PATH_FS2 = "$env:TEMP\fs2_ghost_build"
   New-Item -ItemType Directory -Force $env:PATH_FS2 | Out-Null
   $env:CL=$null; $env:_CL_=$null; $env:_LINK_=$null
   $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
   & "$vs\MSBuild\Current\Bin\MSBuild.exe" Freespace2.vcxproj /p:Configuration=Release /p:Platform=Win32 /m /v:m /nologo > "$env:TEMP\ghost.log" 2>&1
   $LASTEXITCODE
   ```
   Exit 0 is required. Never pipe MSBuild into `Select-Object -First` (it orphans `CL.exe`).
4. Install into the game folder under its own name, so the normal exe stays as it is:
   ```
   $game = '<FreeSpace 2 install>'
   Copy-Item "$env:PATH_FS2\FS2vs2022r.exe" "$game\FS2_ghost_r.exe"
   Copy-Item -Recurse -Force ghost "$game\ghost"
   ```
   The launchers find the game folder as the folder above `ghost`, so the folder must sit directly inside the
   install.
5. If the game has never run on this PC, start `FS2_ghost_r.exe -window` once from the install folder and
   create a pilot, so `fs2.ini` exists; then quit.

## The test (Tom, about 10 minutes)
1. Open `<install>\ghost\README.txt` with Tom; it lists each mission's moments worth watching.
2. Tom double-clicks the launchers in this order: `view_sm1-05.cmd`, `view_sm2-10.cmd`, `view_loop2-2.cmd`,
   `view_sm2-02.cmd` (largest drift first). Only one at a time; close the window to stop early. A Windows
   Firewall prompt can be cancelled.
3. If a launcher closes at once or shows no green ships, check that `FS2_ghost_r.exe` and the `ghost` folder are
   where step 4 put them, then try again.
4. Afterwards ask Tom: fix the formation code, keep the original, or look again. Post his answer and any notes
   as a comment on issue #1. The decision is carried out by the manager on the main PC, not here.

## Cleaning up
Delete `<install>\FS2_ghost_r.exe` and `<install>\ghost` when done. The game's normal exe was never changed.
