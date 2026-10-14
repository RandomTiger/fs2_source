# Settings file

The game keeps its settings in `fs2.ini` in the game folder instead of the Windows registry. The code is in `code/OsApi/OsRegistry.cpp`: every `os_config_read_*` and `os_config_write_*` call goes to the ini.

## First run
If the file doesn't exist, the game creates it from the registry on start-up, taking each value from where a registry read would have found it: the current user's `Volition\FreeSpace2` key first, then the machine's `Volition\FreeSpace2`, then the launcher's `Volition\Free`. After that the registry is never read or written, so the original launcher no longer changes the game's settings. Delete `fs2.ini` to copy the registry again.

## Other files
`-ini <file>` uses another settings file, for example one per configuration in a test run. A file that doesn't exist is created from the registry in the same way.

## Layout
- `[Settings]` holds what the registry kept at the top level: `Soundcard`, `Gamma`, `LastPlayer`, `ComputerSpeed`, joystick settings and so on, with the registry's names.
- The launcher's `Videocard` string, for example `Direct 3D - Primary Display Driver (32 bit) (1024x768)`, is split into `Renderer` (`Direct3D` or `Glide`), `Device`, `ColourDepth` (`32` or `16`) and `Resolution` (`1024x768` or `640x480`). The game puts the string back together when it reads `Videocard`. A string in another form is kept as `Videocard`.
- `[Version]` and `[PXO]` are the registry's subkeys.
- `[Detail]` sets the detail levels for every pilot, replacing the ones saved in the pilot file. If the section is missing it is written with the highest preset. The options screen saves changes to it. Values are 0 (lowest) to 4 (highest); `TargetViewModel`, `PlanetsSuns` and `WeaponExtras` are 0 or 1; `Setting` is the preset (0 to 3) or -1 for custom.

## Recordings
A recording stores the screen size, sound settings and detail levels it was made with. A replay restores the detail levels and stops with an error if the screen size or sound settings differ, so a replay must use a settings file with the same resolution and sound as the recording.
