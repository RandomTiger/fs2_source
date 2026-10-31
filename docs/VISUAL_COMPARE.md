# Visual compare: pictures of chosen mission frames

A replay plays the same mission frames every time, so the picture of one frame number can be saved from two builds or two
renderers and compared pixel by pixel.

## Saving frames: `-shot_frames`
```
FS2vs2022r.exe -window -autotest -replay <file.fs2rec> -shot_frames 300,1500 [-shot_dir <dir>]
```
- Each listed frame (the trace's frame number) is saved as `<dir>\frame_<F>.bmp` (six digits), 24-bit, at the game's
  screen size. `<dir>` defaults to `<recording>.shots`. Up to 64 frames.
- The replay log says `Shot of frame F: <file> (WxH)` for each, and `Shot of frame F not saved: ...` for a frame the replay
  never drew (past its end).
- The picture is read back after frame F was drawn and shown, at the end of frame F+1's simulation, before anything of
  F+1 is drawn. Nothing the game reads changes: the trace is the same with and without the switch.
- Only in a replay; ignored when recording or playing.
- In the Godot runner the game's read-back goes through the render sink, which returns the last frame the Godot viewport drew,
  so the same switch saves the Godot picture of the same frame. Run Godot shown (not `--headless`), with the host renderer
  (`FS2_GR_HOST=1`) and every frame drawn (`FS2_SKIP_STALE=0`); the Godot branch's `tools\godot_frame_runner.ps1` does this.

## The case library: `tools\frame_cases.json`
Each case is a recording and frame numbers; a case may override the defaults `threshold`, `pct_over`, `min_psnr`, and add
`masks` (`{ "rect": [x, y, w, h], "why": "..." }`, each with its reason). The `recordings\visual` recordings play the three
`Visual*` missions (docs/VISUAL_MISSIONS.md; retail FS2 data only) for 60 s at speed 1; they are not part of the replay
gate, but a capture checks each replay (REPLAY OK, trace MATCH) and marks a stale one BAD.

## `tools\frame_compare.ps1`
```
tools\frame_compare.ps1 -Capture <dirA> [-Config r|d] [-Cases a,b] [-Runner <script>]
tools\frame_compare.ps1 -A <dirA> -B <dirB> -Out <dir> [-NameA native] [-NameB godot]
```
Capture replays every case and saves its frames under `<dir>\<case>\`; `capture.txt` has one OK/BAD line per case.
Compare grades each frame (`tools\frame_compare.cs`) and writes `<Out>\report.md` and a diff PNG per differing frame
(A, B, and the difference: black same, grey within the threshold, red above it, blue masked):

| metric | meaning |
|---|---|
| max delta | largest difference of one colour channel, 0-255 |
| % differing | pixels with any channel different |
| % over threshold | pixels with a channel differing by more than `threshold` |
| PSNR | dB over RGB; inf when identical |
| SSIM | mean SSIM of luminance over 8x8 blocks; 1 when identical |

Grades: **identical** (every pixel the same), **within** (% over threshold <= `pct_over` and PSNR >= `min_psnr`),
**changed** (otherwise), **missing** (a side has no picture, or the sizes differ: pictures are never scaled).

Captures and diffs are game pictures: keep them under `replays\` (ignored by git) or the private reference repo. Never commit them.

## Noise floor
Two captures of the same frames from the same recording and the same native build must be identical.
On 2026-10-31 (FrameShots, Release): 19 of 19 frames identical over two runs (the 7 cases of `frame_cases.json`: the three `Visual*` missions, weapons, damage, nebula fighting and a campaign dogfight), with every replay REPLAY OK and its trace matching. Effects use their own random stream and a replay feeds the recorded clock, so nothing drawn depends on the PC: no masks are needed.

Compare captures of the same configuration: a Debug build draws debug-only text (for example `Skill level is set to Easy` at the start), so Release against Debug differs there.
