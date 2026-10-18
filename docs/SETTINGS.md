# Settings file

The game keeps its settings in `fs2.ini` in the game folder instead of the Windows registry. The code is in `code/OsApi/OsRegistry.cpp`: every `os_config_read_*` and `os_config_write_*` call goes to the ini.

## First run
The game never reads or writes the registry, and does not need the launcher. If the file doesn't exist, the game creates it on start-up. On every start, any of these the file lacks is written with a default: `Renderer=Direct3D`, `Device=Primary Display Driver`, `ColourDepth=32`, `Resolution=1024x768`, `Soundcard=DirectSound`, `Language=English`. Everything else has a default in the code. Settings the launcher or an earlier game wrote to the registry are not used: put them in the file by hand if they are wanted.

## Other files
`-ini <file>` uses another settings file, for example one per configuration in a test run. A file that doesn't exist is created in the same way.

## Layout
- `[Settings]` holds what the registry used to keep at the top level: `Soundcard`, `Gamma`, `LastPlayer`, `ComputerSpeed`, joystick settings and so on, with the registry's names. `Soundcard` takes `DirectSound` (the default), `EAX` (DirectSound3D with EAX reverb) or `no sound`; any other value, including the launcher's old `Aureal A3D`, gives plain DirectSound (D-071).
- The launcher's `Videocard` string, for example `Direct 3D - Primary Display Driver (32 bit) (1024x768)`, is kept as `Renderer` (`Direct3D` or `Glide`), `Device`, `ColourDepth` (`32` or `16`) and `Resolution` (`1024x768` or `640x480`). The game puts the string together when it reads `Videocard`. A `Videocard` key in the file is used as it is instead.
- `[Version]` and `[PXO]` were the registry's subkeys.
- `[Detail]` sets the detail levels for every pilot, replacing the ones saved in the pilot file. If the section is missing it is written with the highest preset. The options screen saves changes to it. Values are 0 (lowest) to 4 (highest); `TargetViewModel`, `PlanetsSuns` and `WeaponExtras` are 0 or 1; `Setting` is the preset (0 to 3) or -1 for custom.

## Recordings
A recording stores the screen size, sound settings and detail levels it was made with. A replay restores the detail levels and stops with an error if the screen size or sound settings differ, so a replay must use a settings file with the same resolution and sound as the recording.
