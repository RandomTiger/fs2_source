# FS1 reference recordings

Hand-played FreeSpace 1 recordings kept as reference material, not tests. No gate or `replay_check.ps1` replays this folder.

To replay one, run from the FS1 install (`PATH_FS1`) without `-autotest`; a hand-played replay exits 1 on success (D-033):
```
FS2vs2022r.exe -window -replay <abs path>\NAME.fs2rec -trace <out>\NAME.trace
tools\bin\TraceDiff.exe recordings\reference\fs1\NAME.trace <out>\NAME.trace
```

| Recording | Mission | Recorded | Why it is kept |
|---|---|---|---|
| `sm1-02a_friendly_ram` | sm1-02a | 2026-10-29, Tom, FS1Compat `ee5bdb3`, Release | A friendly ship crashes into Alpha 1 and does a fair bit of damage. Reference for improving the AI after the Godot port. Replayed identically in Release and Debug (29136 frames) on the day. |
