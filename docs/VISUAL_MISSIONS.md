# Visual reference missions

Three missions that each open on a fixed view, used to compare how different builds draw the same scene. Examples are the original renderer, Enhanced (32-bit rendering, Shivan glow) and the Godot port.

They are **not gated**. No recording, no replay check and no pass/fail. Run them when Tom asks for a visual comparison at a particular point.

## The missions
All three keep everything still: the player sits at the origin facing +z, ships have zero velocity and no AI goals, and every ship is on the Friendly team, so nobody fires. The view on the first frame is the shot.

| Mission | Covers | In view |
|---|---|---|
| `VisualSpace` | Open space: background nebulas, planets, suns, glow maps | Bitmaps `dneb03` and `neb18`, planets `planeth` (upper right) and `planetc` (lower left), 2000 stars. A Ravana is broadside at about 3.3 km with its unlit side to the camera, so its glow points show. A Lilith is upper left and an Aeolus upper right. The sun is high and beyond the ships, out of view. |
| `VisualNebula` | Full nebula (`nbackpurp2`) and fog falloff | An Aeolus at 250 m, a Lilith (Shivan glow through fog) at 500 m, a Moloch at 800 m and an Orion at 1200 m. In the original renderer the two far ships are lost in the fog. |
| `VisualSubspace` | Subspace tunnel (mission flag 1) | An Aeolus on the left at 600 m (engine glows) and a Lilith on the right at 550 m. |

The missions are retail FS2 data only. The ship classes and bitmaps are not in the demos or FS1.

## Files
- Scripts: `tools/missions/VisualSpace.ps1`, `VisualNebula.ps1`, `VisualSubspace.ps1`. Edit a script and rerun it; never edit the `.fs2` by hand.
- Generated missions: `data/missions/Visual*.fs2`, on `RecordPlayback`, which merges upward.
- The generator, `tools/missions/MissionGen.ps1`, takes these background fields on the mission hashtable:
  - `$m.Stars`: star count. The default is 500.
  - `$m.Ambient`: ambient light level. The default is 0.
  - `$m.Suns`: a list of `@{ Name; Angles = @(p, b, h); Scale }`. Sun names include `SunWhite`, `SunGold`, `SunBlue`, `SunRed`, `SunGreen` and `SunViolet`.
  - `$m.Bitmaps`: a list of `@{ Name; Angles = @(p, b, h); ScaleX; ScaleY; DivX; DivY }`. Names include `dneb01`-`dneb18`, `neb01`-`neb18` and `planeta1`, `planetb`-`planeth`.
  - Angles are in degrees. The generator converts them to the radians the file stores.
  - Observed placement for bitmaps: positive p puts the bitmap higher on screen, and negative h puts it further right.
  - If none of these fields is set, the output is the old fixed background, so the existing missions regenerate unchanged.

## Running one
Copy the mission into the game folder, then run it from there (`PATH_FS2`, or the FS1 install for FS1 builds):
```
copy data\missions\VisualSpace.fs2 %PATH_FS2%\data\missions\
FS2vs2022r.exe -window -autotest -at_mission VisualSpace -at_time 60 -at_speed 1
```
- Pass `-at_speed 1`. Without it autotest runs at 4x speed, and 60 s of mission time ends after about 15 s.
- The mission notes contain `autotest: player stays still`, so `-playernpc` does not move the player.
- Only one game instance can run at a time. Check that no `FS2vs2022*` process is running first.
- The shots include the HUD. The targeting reticle partly covers the middle of each view.

## Capturing a shot
There is no in-mission screenshot switch yet: `-at_shots` covers the menu tour only. The shots so far were taken like this:
1. Launch the game as shown above.
2. Wait about 15 s.
3. Take the game process's `MainWindowHandle`.
4. Call `SetProcessDPIAware`, read the client rectangle (`GetClientRect` + `ClientToScreen`), and save it with `Graphics.CopyFromScreen` to a PNG.
5. Stop the process.

The window shot is 1006x721 on DESKTOP-MAIN.

For Godot builds, use `tools\perf_shots.ps1` (`FS2_SHOT_DIR`).

**Shots are game art.** Never commit them to fs2_source. Keep them outside the repo, or in the private fs2_reference repo if Tom wants them kept.

## Comparing
- Take each shot at the same mission time and the same window size on every build.
- The scenes are static, but some things animate:
  - the subspace tunnel
  - nebula lightning (none is set)
  - engine glow flicker

  So compare by eye, or with a tolerance or mask, not by exact hash.

## Changing the views
- Keep a scene static: no velocity, no AI goals, one team.
- Keep the sun out of frame unless the sun itself is under test.
- After moving anything, regenerate the mission, run it, look at a fresh shot, and update the table above.
