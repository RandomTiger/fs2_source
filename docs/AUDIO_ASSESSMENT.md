# Audio assessment brief

Decides D-062 item 2, the sound backend collapse. Both backends stay until Tom has heard them side by side
(D-20261017-231124). This brief is the method: scripted listening scenes, each played once per backend, at normal
speed, with identical gameplay in both runs.

## The two backends

`Soundcard` in the settings file (`docs/SETTINGS.md`) chooses between them.

| | `DirectSound` (default) | `EAX` |
|---|---|---|
| Positional sounds | "Fake 3D" (`snd_play_3d`, `Sound.cpp`): volume falls with distance, left/right pan from the angle to the camera's right vector | DirectSound3D buffers (`ds3d_play`): position and velocity per sound, so front/back, above/below and doppler (factor 1.0, `ds3d.cpp`) |
| Looping object sounds (engines, turrets) | Same fake 3D, updated each frame (`ObjectSnd.cpp`) | DirectSound3D, updated each frame |
| Reverb | None | EAX environment. Volition commented out the per-mission choice (subspace, asteroid fields), so it is always `Game_default_sound_env` |
| Own ship, interface, voices, music | Plain stereo | Plain stereo, same as DirectSound |

On Windows since Vista, DirectSound runs in software. DirectSound3D still works in software, but EAX needs hardware
or a wrapper, so `ds_eax_init` is expected to fail and the `EAX` setting probably means DirectSound3D without
reverb. Step 1 confirms that from the log.

## Method

1. **Confirm what EAX gives on this PC.** Start the game with `Soundcard=EAX`, read the sound lines in the log
   (DirectSound3D on or off, EAX on or off). One run, no code.
2. **Listening missions** (`tools/missions/AudioScenes.ps1` with the mission generator, on `RecordPlayback`). The
   player sits still, no AI flies it, and its own weapons and engine stay quiet, so only the scene's sounds play:
   - **A. Side flyby:** a fighter passes left to right, 50 m in front. Pan and doppler.
   - **B. Overhead flyby:** a fighter passes from in front to behind, 30 m above. Front/back and above/below, which
     fake 3D cannot place.
   - **C. Behind:** a freighter explodes 300 m directly behind the player.
   - **D. Approach:** a bomber flies straight in from 3 km to 100 m, then turns away. Distance falloff.
   - **E. Capital ship:** the player drifts past a cruiser's engines and firing turrets. Looping object sounds.
   - **F. Crowd:** a dogfight of 12 fighters around the player. Many sounds at once, channel limits.
   - **G. Control:** the player fires its own guns and afterburner. Must sound the same in both; if it doesn't, the
     setup is wrong.
3. **Identical runs.** Each scene is recorded once, then replayed under each backend with
   `FS2_REPLAY_SOUND_MODEL=1`. The model already makes gameplay independent of the sound device; the sound model
   check proves that on every recording. So both runs show and play the same events at the same moments.
4. **Normal speed.** A replay currently runs as fast as the PC can draw, about 4 to 8 times real speed. A new
   `-replay_pace` switch (on `RecordPlayback`, off by default) waits each frame until the recorded mission time has
   passed. It changes no recording or trace.
5. **Listening session.** `tools/audio_assessment.ps1` sets the backend, plays each scene twice as "A" and "B" in a
   random order per scene, and writes the order to a file Tom opens afterwards. About 15 minutes. For each scene
   Tom answers: A better, B better, or no difference, and anything that sounds wrong.

Session rules (D-20261019-110414):
- The listening exe is `PrePortCleanup` x86 windowed Release, the layer the collapse lands on.
- The script sets music and master volume the same for both runs and writes them to the order file.
- One unannounced pair plays the same backend twice, as a check on the listening; the order file logs it.
- #17 (shockwave impacts at full volume whatever the volume setting, D-20261018-043124) is on the agenda: the
  retail behaviour is shown with scene C and the master volume turned down. A version that respects the volume is
  heard only from an uncommitted trial build.
- If step 1 shows EAX failing, reverb is not assessed and the Godot reverb question stays Tom's. Either way the
  scenes and their recordings become the port's audio reference for the Godot layer's own listening pass.

## What the result decides

- Which backend the game keeps (D-062 item 2). The other is removed on `PrePortCleanup`, which must leave every
  recording unchanged; sound no longer feeds gameplay (D-034, D-035), so either removal is safe for the recordings.
- What the Godot port must reproduce. Godot's `AudioStreamPlayer3D` gives position, doppler and reverb buses, so
  either result can be matched; the answer says how much of it to switch on.

## Batches and gates

Placement and order: D-20261019-110415 and D-20261019-110416. Nothing here merges above `RecordPlayback`, and no
recording is made, until the warnings carry-up has reached `GodotPrep` and been pushed.

1. Step 1 log check, reported to the steward. No commit.
2. `-replay_pace` on `RecordPlayback`, quick suite, merged up (warning_check from `Enhanced` up). The report shows
   one recording replayed paced and unpaced with identical traces.
3. The scene missions and their recordings on `RecordPlayback` (`recordings/audio`, not part of the suites),
   recorded again on `PrePort` in format 4 as the demo set is (D-20261018-221705). Each is replayed in Release and
   Debug with a matching trace at the layer that made it, and must stay replayable.
4. `tools/audio_assessment.ps1` on `RecordPlayback`, merged up.
5. Tom's listening session, then the steward records the decision and the collapse is scheduled.

Not in scope: recording the sound output to files, measuring it, or changing any sound code before the decision.
