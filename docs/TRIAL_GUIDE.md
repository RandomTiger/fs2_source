# Audio trial: running the listening test on another PC

Throwaway branch `AudioTrial` (off `PrePortCleanup`, never merged). For Claude on a new PC, to guide Tom
through the test. Tom starts it with: *"Read docs/TRIAL_GUIDE.md on branch AudioTrial of RandomTiger/fs2_source
and guide me through it."*

## Rules for this branch
- Throwaway: nothing here merges anywhere. Do not run the autotest suites, gates or `replay_check` on it (Tom,
  2026-10-22). Build and listen only.
- Never download game data. Use the FreeSpace 2 install already on the PC (its machine profile names it; if
  there is no profile, ask Tom for the path).
- One game instance at a time.
- Guide one step at a time, in plain language; wait for Tom before the next step.

## What is being tested
Issue #81: the game's sound buffers had no pan control, so every sound was mono, and DirectSound3D
(`Soundcard=EAX`) fell back to plain DirectSound. The question is which sound path the game keeps; the other
is removed later (docs/AUDIO_ASSESSMENT.md, "What the result decides").

- Commit `f3c51bb`: trial 1. Pan control added; DirectSound3D allowed to run.
- Commit `8472a7d` (branch tip): trial 2. As trial 1, and 3D sounds keep the game's own volume falloff
  (DirectSound3D only positions them). **Listen to the tip** unless Tom asks for trial 1.

Seven scenes, AudioA to AudioG (`tools/missions/AudioScenes.ps1` says what each one is), are replayed from
`recordings/audio`. Each is captured twice, once per `Soundcard` setting, and Tom compares them blind.

## Setup (once per PC)
1. Tools: Visual Studio 2022 with "Desktop development with C++" (MSBuild, v143, Win32), Git, Python 3.13
   (python.org installer, "py" launcher).
2. Clone and check out the branch in its own folder (CamelCase worktree name, as on the main PC):
   ```
   git clone https://github.com/RandomTiger/fs2_source.git fs2_source
   cd fs2_source
   git config core.autocrlf false
   git worktree add ..\fs2_source_AudioTrial AudioTrial
   ```
3. Build Release. The linker writes `FS2vs2022r.exe` straight into `PATH_FS2`, which must be the FreeSpace 2
   install (it replaces any `FS2vs2022r.exe` already there; copy that aside first if it matters). In PowerShell,
   in `fs2_source_AudioTrial`:
   ```
   $env:PATH_FS2 = '<FreeSpace 2 install>'
   $env:CL=$null; $env:_CL_=$null; $env:_LINK_=$null
   $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
   & "$vs\MSBuild\Current\Bin\MSBuild.exe" Freespace2.vcxproj /p:Configuration=Release /p:Platform=Win32 /m /v:m /nologo > "$env:TEMP\audiotrial.log" 2>&1
   $LASTEXITCODE
   ```
   Exit 0 is required. Never pipe MSBuild into `Select-Object -First` (it orphans `CL.exe`).
4. First run: start `FS2vs2022r.exe -window` once from the install folder, so it writes `fs2.ini` (the game
   keeps its settings there, not in the registry). If it asks for a pilot, create one, then quit.
   `capture.py` copies `fs2.ini` per take and changes only the `Soundcard=` line.
5. Python environment, in `fs2_source_AudioTrial`:
   ```
   py -3 -m venv %USERPROFILE%\fs2_audio_venv
   %USERPROFILE%\fs2_audio_venv\Scripts\pip install -r tools\audio_ab\requirements.txt
   ```

## The test
1. **Capture** (about 15 minutes; Claude runs it). The capture records the PC's own speaker output (loopback),
   so Tom leaves the PC alone and silent while it runs; any other sound ends up in the takes. The default
   playback device must be the one Tom will listen on.
   ```
   powershell -ExecutionPolicy Bypass -File tools\audio_ab\capture_all.ps1 -Out <capture dir> -Python %USERPROFILE%\fs2_audio_venv\Scripts\python.exe -GameDir '<FreeSpace 2 install>'
   ```
   Each take prints `exit 0 OK` and a peak above 0. A silent take is retried up to three times; report any
   `FAILED` line to Tom rather than carrying on.
2. **Match loudness**, so the comparison is about placement, not volume:
   ```
   %USERPROFILE%\fs2_audio_venv\Scripts\python tools\audio_ab\match_loudness.py <capture dir> <capture dir>_matched
   ```
3. **Listen** (Tom, about 15 minutes, headphones). Tom runs this himself in a console window, because it reads
   keys:
   ```
   %USERPROFILE%\fs2_audio_venv\Scripts\python tools\audio_ab\player.py <capture dir>_matched
   ```
   Per scene: 1 = A, 2 = B, Space = A then B, T = swap every 3 s, S = stop, Enter = answer A, B or S (same),
   then an optional note. A and B are random per scene, and one scene is secretly the same take twice as a
   check on the listening.
4. **Results.** Only after Tom has finished, open `order_<time>.txt` next to `answers_<time>.txt` and tell
   Tom, scene by scene, which setting he preferred and whether the check scene came out as S. Post both files'
   contents as a comment on issue #81. The decision itself goes to the manager on the main PC, not made here.

## Not on this branch
DSOAL (OpenAL behind DirectSound, for EAX reverb) is optional and not in git; without it there is no reverb,
which is expected. `tools/audio_assessment.ps1` is the older live-replay version of the same session; use the
capture and player above.
