# FS2 Godot project

Code and project files only. No game data (read at run time through cfile).

## Pins
- Godot 4.7.2-stable win64, from github.com/godotengine/godot-builds, in `C:\dev\Tools\godot_4.7.2\` (not in git).
  Zip SHA512 starts `83decd58fd`. A bump is its own brief.
- godot-cpp 10.0.0-stable, commit `507ed9d840c01a3c5b2a39af8bb4000bfac30bf5`, in `C:\dev\Tools\godot-cpp` (not in git). API 4.7.

## Build the extension (needs fs2game.dll built; see fs2game/)
    cmake -S godot -B godot/build -G "Visual Studio 17 2022" -A x64 -DGODOT_CPP_DIR=C:/dev/Tools/godot-cpp
    cmake --build godot/build --config Debug --parallel
CMake is the one bundled with Visual Studio. Output: `godot/bin/fs2godot.dll` (template_debug, the flavour the stock binary loads).
The wrapper is /MD, fs2game.dll is /MT; only the plain C API (fs2game/fs2_api.h) crosses, so the two CRTs never share memory.
First use of a fresh checkout: `godot --headless --path godot --import` (tools/godot_check.ps1 does this itself).

## Run
`tools\godot_check.ps1 [-Config r|d] [-Missions a,b] [-Quick]`; runner.gd reads FS2_DLL, FS2_ROOT, FS2_CMD (and FS2_CMD2 for the restart check).

## Package (batch P)
`tools\godot_package.ps1 -Godot <4.7.2 editor exe> -GodotCpp <godot-cpp> [-Stage <data dir> -GameDir <dir with FS2game64r.dll>]`
builds the template_release wrapper (`bin/fs2godot_rel.dll`), exports preset "Windows Desktop" with the stock 4.7.2-stable
Windows release template (official, unmodified, installed per user in `%APPDATA%\Godot\export_templates\4.7.2.stable`, never in
git) and leaves `build/pkg/FS2Godot.exe` + `FS2Godot.pck` + `fs2godot_rel.dll`. Install = those three files plus `FS2game64r.dll`
in the game's data folder. Locations also read from FS2_GODOT, FS2_GODOT_CPP, PATH_FS2 (machine profile).
Launch (`launch.gd`, the main scene): no arguments and no environment variables; the data folder is the exe's folder, else the
current folder, first valid wins (`*.vp` there or in `Data\`); `--fs2-root <dir>` names it instead. None valid: the folders tried
are shown in a message box (stderr under --headless) and the exit code is 3 (4: fs2game64r.dll missing). Config, pilots and logs
are written under the data folder by the game, as before. FS2_CMD / FS2_DLL / FS2_ROOT still work for the gate harness.
Gate: `tools\godot_pkg_check.ps1 [-Level L1|L2] [-Config r|d] [-Set ...]` runs the same recordings through the packaged exe, copied
into each set's folder.
