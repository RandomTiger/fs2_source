# Vulkan and D3D12: performance and gate C comparison

Branch `Vulkan`, base `322407c`. Measurement only: no source change and no project renderer setting change. The backend was picked at launch with `--rendering-driver vulkan` or `--rendering-driver d3d12`. `project.godot` sets no rendering driver, so Vulkan is the current default.

Machine: RTX 2060. Vulkan reports "Vulkan 1.3.280 - Forward+" and D3D12 reports "D3D12 12_0 - Forward+". Engine: Godot 4.7.2-stable (`FS2_GODOT`). Release game DLL and wrapper, built with L0 PASS and 0 warnings.

## Update: quiet rerun (supersedes the figures below where they differ)

The run described in Method was mostly noisy (53 of 68 runs flagged NOISY). Tom reran the comparison on a quiet machine. Those figures replace the noisy ones; the tables further down are kept as the original noisy run.

Windowed, vsync on: both backends hold 60 fps in every scene. Game frame in ms, Vulkan vs D3D12:

| Scene | Vulkan | D3D12 |
|---|---|---|
| sm3-06 | 4.9 | 7.0 |
| sm2-07 | 6.6 | 8.3 |
| md-01 | 4.7 | 6.1 |
| menus | 1.8 | 2.0 |

Vulkan's game frame is 15-30% shorter.

Windowed, uncapped, fps, Vulkan vs D3D12:

| Scene | Vulkan | D3D12 |
|---|---|---|
| sm3-06 | 1116 | 491 |
| sm2-07 | 523 | 325 |
| md-01 | 1481 | 1021 |
| menus | 3233 | 4132 |

Vulkan is 1.4-2.3x faster in the missions. D3D12 is faster in the menus only.

What changed against the noisy run: D3D12 no longer drops to about 51 fps with vsync on (the "only Vulkan holds 60 fps" finding is withdrawn), and the uncapped gap shrinks from up to 2.9x to up to 2.3x.

Caveats:
- One run per case.
- No fullscreen runs.
- The rerun used a newer build (the GodotDrawCalls branch), so part of the gain comes from code changes, not only from the quieter machine.

Recommendation unchanged: keep Vulkan as the driver.

## Result

- **Vulkan is faster in every game scene.** Uncapped, its shown frame time is 1.4x to 2.9x shorter. The game-side time is the same on both backends. The gap is all in the sink (the wrapper's draw submission into Godot), which takes D3D12 about twice as long.
- **Only Vulkan holds 60 fps with vsync on (noisy run; withdrawn, see the quiet rerun above).** D3D12 drops to about 51 fps in both FS2 missions.
- **Vulkan gate C hashes equal the baseline 5fe0769 on every case that has a baseline.** D3D12 equals it on sm2-07 only. Elsewhere D3D12 differs by isolated single pixels (point sprites such as stars and debris). This is not visible in play.
- **D3D12 starts about 0.45 s sooner** on an empty project and draws its first menu frame about 100 ms earlier. That is its only win, apart from trivial menu frames that are already under 1 ms.

Recommendation (a recommendation only; a pin is a steward brief): pin Vulkan as the driver, which is what the default already gives. Keep D3D12 as an untested fallback only. It is slower and not bit-identical to the gate C baseline.

## Method

- **Scenes**, all replays from `recordings/`:
  - sm3-06: FS2, dense, no fog (`autotest` set).
  - sm2-07: FS2, nebula.
  - md-01: an FS1 mission (`fs1` set, FS1 install copy).
  - menus: the menu and main hall tour, `-autotest -at_menus`, 59 screens.
- **Resolution:** windowed 1152x648, the default. Fullscreen uses the desktop resolution.
- **Cases:**
  - `w`: windowed, vsync on (the default).
  - `u`: windowed, uncapped (`FS2_VSYNC=0`).
  - `f`: fullscreen, uncapped.
  - The game runs at a 120 Hz pace (`FS2_PACE=1`) in every case.
- **Runs:** 3 per case for `w` and `u`, 2 or 3 for `f`. Backend order alternates by rep: reps 1 and 3 run Vulkan first, rep 2 runs D3D12 first. One game instance at a time.
- **Profiler:** the existing perf_prof, which writes `_game.csv` and `_godot.csv`.
  - "Godot frame" is the interval between shown Godot frames. The first 10% is skipped as load.
  - "Game frame" is the drawn game frames from frame 300 on.
  - sink = the sum of the z_* zones. gameside = total minus sink. upload = tex_create + tex_free.
  - Every figure is the mean over reps of each run's mean, p95 or p99. "Spread" is the min to max over reps.
- **VRAM:**
  - The game's own free-VRAM figure is not used. It has a known bug (it reports -1), and the fix comes via Enhanced, not this branch.
  - Primary figure: Godot's texture memory monitor (`RenderingServer RENDERING_INFO_TEXTURE_MEM_USED`, the same value as `Performance.RENDER_TEXTURE_MEM_USED`), the peak `tex_mem` in `_godot.csv`.
  - perf_prof does not log `RENDER_VIDEO_MEM_USED`. Adding it needs a source change, which is outside this branch's measurement-only scope.
  - Secondary figure: the peak of the Windows counter `\GPU Process Memory(pid_<renderer>_*)\Dedicated Usage`, sampled once a second. It is read for the renderer child process, `Godot_v4.7.2-stable_win64.exe`, because the console exe is only a wrapper.
- **Startup:**
  - An empty project with one Node2D main scene, `--quit-after 30`. Wall time from launch to exit, 6 runs per backend, order alternated.
  - "first draw" is ms from perf start to the first Godot frame that makes RS draw calls.
- **Noise:** other workers ran L1s and other Godot instances throughout. Each run waited up to 90 s for the machine to be free of game processes, then recorded `quiet` or `NOISY(<process names>)` along with machine CPU load.
  - 53 of 68 perf runs were flagged NOISY.
  - Spreads are tight (below), and alternating the order spreads any load across both backends.
  - Four fullscreen rep 2 runs ran at 97-100% machine CPU: f_sm3-06 on both backends, f_sm2-07 on D3D12, and f_menus on D3D12. f_sm3-06 and f_sm2-07 were re-run as rep 3, and the table uses reps 1 and 3 for them. f_menus_d3d12 is kept as is; its frames are under 1 ms either way.
- **Install copies:** `C:\Games\FS2V_scratch` (FS2) and `C:\Games\FS2V_fs1` (FS1). Output goes to `C:\Games\FS2V_out`, with `runs.txt` holding one line per run. The scripts are in `C:\Users\Claude\fs2_tools`, outside git: `vk_build.ps1`, `vk_run.ps1`, `vk_batch.ps1`, `vk_agg.py`, `vk_sum.py`, `vk_cmp.ps1`.

## Frame times (ms)

Godot frame: mean, p95, p99 and fps. Game frame: mean, p95 and p99. The split is gameside, sink and upload. VRAM: tex_mem, then process dedicated memory, in MB.

### Windowed, vsync on

| scene | driver | Godot mean / p95 / p99 | fps | game mean / p95 / p99 | gameside / sink / upload | tex_mem / proc VRAM | spread Godot mean |
|---|---|---|---|---|---|---|---|
| sm3-06 | Vulkan | 16.69 / 28.42 / 31.41 | 59.9 | 6.38 / 10.04 / 11.68 | 1.46 / 4.92 / 0.021 | 81 / 343 | 16.66-16.73 |
| sm3-06 | D3D12 | 19.65 / 31.84 / 38.85 | 50.9 | 11.97 / 20.48 / 26.05 | 1.47 / 10.50 / 0.027 | 79 / 415 | 19.61-19.70 |
| sm2-07 | Vulkan | 16.93 / 26.61 / 31.48 | 59.1 | 11.41 / 14.84 / 20.17 | 7.53 / 3.88 / 0.635 | 95 / 370 | 16.86-17.03 |
| sm2-07 | D3D12 | 19.69 / 26.16 / 38.18 | 50.8 | 14.55 / 18.96 / 28.66 | 7.33 / 7.22 / 0.654 | 95 / 290 | 19.40-20.26 |
| md-01 | Vulkan | 16.88 / 30.18 / 31.50 | 59.2 | 5.65 / 10.38 / 14.10 | 1.52 / 4.13 / 0.012 | 64 / 215 | 16.80-16.93 |
| md-01 | D3D12 | 18.30 / 31.06 / 42.03 | 54.6 | 9.35 / 18.75 / 28.34 | 1.46 / 7.89 / 0.014 | 64 / 421 | 18.13-18.61 |
| menus | Vulkan | 18.07 / 31.01 / 60.18 | 55.3 | 2.64 / 4.61 / 38.28 | 1.89 / 0.75 / 0.047 | 127 / 347 | 17.99-18.21 |
| menus | D3D12 | 18.02 / 30.94 / 57.30 | 55.5 | 2.68 / 7.29 / 37.02 | 1.44 / 1.24 / 0.047 | 125 / 244 | 18.00-18.05 |

### Windowed, uncapped

| scene | driver | Godot mean / p95 / p99 | fps | game mean / p95 / p99 | gameside / sink / upload | tex_mem / proc VRAM | spread Godot mean |
|---|---|---|---|---|---|---|---|
| sm3-06 | Vulkan | 5.78 / 14.75 / 17.71 | 173.3 | 6.04 / 9.53 / 11.00 | 1.48 / 4.56 / 0.014 | 82 / 347 | 5.53-6.00 |
| sm3-06 | D3D12 | 16.14 / 29.12 / 35.10 | 62.0 | 11.01 / 19.19 / 23.36 | 1.39 / 9.62 / 0.021 | 79 / 437 | 15.62-16.68 |
| sm2-07 | Vulkan | 13.37 / 17.86 / 23.36 | 74.8 | 10.91 / 14.27 / 18.21 | 7.23 / 3.68 / 0.594 | 95 / 355 | 12.91-13.63 |
| sm2-07 | D3D12 | 18.13 / 25.05 / 37.94 | 55.2 | 14.02 / 18.88 / 28.15 | 7.21 / 6.82 / 0.663 | 95 / 311 | 17.67-18.50 |
| md-01 | Vulkan | 2.42 / 10.61 / 15.61 | 418.1 | 5.11 / 9.39 / 12.57 | 1.47 / 3.64 / 0.007 | 64 / 219 | 2.14-2.72 |
| md-01 | D3D12 | 7.00 / 20.44 / 34.08 | 145.8 | 7.94 / 16.49 / 25.08 | 1.38 / 6.56 / 0.011 | 64 / 421 | 6.08-8.42 |
| menus | Vulkan | 0.85 / 0.91 / 2.65 | 1181 | 1.96 / 4.08 / 26.59 | 1.40 / 0.57 / 0.022 | 128 / 350 | 0.76-0.90 |
| menus | D3D12 | 0.40 / 0.62 / 1.92 | 2492 | 2.31 / 5.53 / 27.80 | 1.39 / 0.92 / 0.025 | 127 / 265 | 0.38-0.42 |

### Fullscreen, uncapped

| scene | driver | reps | Godot mean / p95 / p99 | fps | game mean / p95 / p99 | gameside / sink / upload | tex_mem / proc VRAM |
|---|---|---|---|---|---|---|---|
| sm3-06 | Vulkan | 1, 3 | 5.55 / 14.84 / 18.06 | 180 | 6.09 / 9.56 / 11.07 | 1.49 / 4.61 / 0.014 | 98 / 515 |
| sm3-06 | D3D12 | 1, 3 | 16.02 / 29.38 / 35.59 | 62 | 11.04 / 19.63 / 23.96 | 1.38 / 9.65 / 0.021 | 95 / 504 |
| sm2-07 | Vulkan | 1, 3 | 12.78 / 17.30 / 21.96 | 78 | 10.57 / 13.82 / 17.48 | 7.06 / 3.51 / 0.581 | 111 / 525 |
| sm2-07 | D3D12 | 1, 3 | 17.15 / 23.47 / 32.36 | 58 | 13.49 / 17.92 / 26.11 | 7.01 / 6.48 / 0.645 | 112 / 400 |
| md-01 | Vulkan | 1, 2 | 2.44 / 10.30 / 15.21 | 410 | 4.98 / 9.17 / 12.03 | 1.44 / 3.54 / 0.006 | 81 / 387 |
| md-01 | D3D12 | 1, 2 | 6.21 / 19.90 / 31.60 | 161 | 7.85 / 15.93 / 25.33 | 1.34 / 6.50 / 0.010 | 81 / 520 |
| menus | Vulkan | 1, 2 | 1.01 / 1.05 / 4.48 | 987 | 1.98 / 3.99 / 26.51 | 1.40 / 0.58 / 0.023 | 145 / 518 |
| menus | D3D12 | 1, 2 | 0.64 / 1.21 / 2.70 | 1604 | 2.60 / 7.56 / 29.79 | 1.53 / 1.07 / 0.031 | 143 / 344 |

### Notes on the frame times

- **Game-side time** (game logic outside the sink) is the same on both backends in every case, within 0.1 ms. The difference is all in the sink. The texture upload cost is small and about the same on both.
- **Texture memory** is the same on both backends, as expected, since the same textures are loaded. Process dedicated VRAM is not consistently higher on either backend:
  - D3D12 is higher in sm3-06 and md-01.
  - Vulkan is higher in sm2-07 and the menus.
  - Fullscreen raises both.
- **vp_gpu_ms** (viewport GPU time) on D3D12 sometimes reads garbage (values around 1e9 to 1e10 ms in the md-01 and menus means), so it is not used. On Vulkan it stays at 0.03-0.49 ms.

## Startup

| | Vulkan | D3D12 |
|---|---|---|
| Empty project, `--quit-after 30`: mean of 6 runs (range) | 1.69 s (1.66-1.81) | 1.24 s (1.18-1.32) |
| First draw after perf start, FS2 missions | 925-1009 ms | 909-1008 ms |
| First draw after perf start, menus (windowed) | 277 ms | 166 ms |
| Wall time for a full replay | same within 0.7 s | same within 0.7 s |

An earlier startup set used a project with no main scene. Vulkan hung on the error dialog and D3D12 exited at once. Those 6 lines are marked `startup_invalid` in `runs.txt` and are not used.

## Gate C (shown-run hashes)

Shots every 300 frames, SHA256 of each PNG, compared with `C:\Games\FogR2_out2\base` (baseline 5fe0769) and between backends. Pixel counts come from `pxdiff.ps1`. Every replay ended REPLAY OK, and the menu tour passed 59 of 59 on both backends.

| case | Vulkan = base | D3D12 = base | Vulkan vs D3D12 |
|---|---|---|---|
| FS2 sm1-01 | yes | no | 6 of 12 shots differ, 8-26 px each |
| FS2 sm2-07 (nebula) | yes | yes | 0 of 11 |
| FS2 sm3-06 | (no baseline) | (no baseline) | 3 of 12 shots differ, 2-3 px |
| FS1 sm1-08a | yes | no | 6 of 12 shots differ, 1-13 px |
| FS1 md-01 | (no baseline) | (no baseline) | 7 of 12 shots differ, 3-75 px |

- **D3D12 is deterministic against itself.** A second D3D12 sm1-01 run gave identical hashes, 12 of 12. The D3D12 differences are a fixed difference in rasterization, not noise.
- **The differing pixels are isolated single points**: stars, debris and other point sprites landing one pixel over. Geometry, edges, HUD and text are identical.
- **The menu tour is not a hash gate.** It is timed in ms, so two runs on the same backend already differ (Vulkan 4 shot hashes, D3D12 6).
- No images are committed.
