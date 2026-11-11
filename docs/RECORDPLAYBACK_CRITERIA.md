# RecordPlayback: what belongs on this layer

Branch `RecordPlayback` sits on `OriginalSourceBugFixes` and under every feature branch and
`Enhanced`. It holds the gameplay recorder and replayer, the autotest, autoplay, the menu tour,
the recordings and the scripts that check them. Its recordings are the retail reference: every
layer above is judged against them. GitHub issue #279.

## Purpose (Tom, 2026-11-08)

Deterministic playback of missions and menus, through automated recording and playback, **without
changing the retail gameplay experience**. Any change to a random number technically changes the
experience, so each one is judged by the weight of the variable: a small change in logic varies
the result far less than a player's own choices (a different route, target, speed, weapon or ship).
FreeSpace 2 was never deterministic by value, only by the behaviour of the mission, which is a much
broader scope. The layer is crucial because any wrong logic or assumption here is baked into what
every other change is tested against; changes are held to a high standard, but the retail flaws,
above all in memory management, must not be allowed to stop the autotest.

## Rules

- Only changes that let record and playback run without sync error, ideally inside the recording
  systems, plus the test suite and its tooling (`docs/BRANCHES.md`; Tom, 2026-10-22,
  D-20261022-085602). Diagnostics, speed work and features go to their own layer.
- **Cosmetic means it reaches neither simulation state nor a recorded OS call.** A draw on the
  cosmetic random stream may never decide object creation or lifetime, AI, physics, damage,
  weapons, the timing of game events, nor whether or when the game plays or queries a sound, reads
  the clock or reads input. The second half was learned on 2026-11-08 (#276): the recorder replays
  OS reads in order, so a cosmetic value that steers one breaks replay with gameplay unchanged.
  Presentation may draw from the gameplay stream only inside the logic step (D-036).
- Every game-code change replays against the existing recordings in Release and Debug, with the
  switch on and off, and the two builds' traces must be identical. Thresholds are never changed and
  nothing is re-recorded to make a check pass (`CLAUDE.md`).
- A re-record of the reference set needs a what-changed note naming the cause. Hand-played
  recordings (`recordings/played`) are never re-made by an automated re-record: nobody can play
  them again the same way.

## Accepted changes to retail behaviour

These are the only places this layer changes how the game plays with no flag set. Each is a
deliberate ruling and is not a precedent for more.

1. **Cosmetic random stream** (`CosmeticRandom`, merged 2026-10-13 by exception to the merge-up
   rule; `docs/random_call_sites.md`). Effects draw from their own generator, so rendering no
   longer shifts the gameplay sequence. The gameplay sequence from a given seed is therefore a
   different one from retail's, within retail's own envelope: it already depended on how long the
   player sat in the briefing.
2. **Reseed on entering the mission.** Record and replay both reseed from the level seed when
   gameplay starts, discarding the briefing's draws. Same envelope argument.
3. **Settings in `fs2.ini`, detail levels per machine** (`docs/SETTINGS.md`, D-016). The registry
   and the launcher are no longer read; a fresh install starts at the highest detail preset for
   every pilot. A preference change, not a gameplay one; needed so a test machine's settings are a
   file the suite controls.

Everything else that changes play (autotest rules, the AI flying the player, autoplay, the menu
tour) is behind a command-line switch that defaults off and is stored in the recording.

## What this layer does not try to do

- **Independence from resolution, detail and sound.** A replay must use the recording's screen
  size, detail levels and sound settings, because some render code still draws from the gameplay
  stream and the sound device's answers are recorded (`docs/REPLAY.md`, known limits). The ideal
  of a recording that never needs re-recording belongs to `PrePort32` and `PrePort64`, where
  drawing's reads leave the stream and the game plays headless (D-015, D-026, D-027, D-034, D-049).
  Tom chose not to take that risk at this layer (2026-11-08): the recordings here must stay
  retail.
- **Running under a Windows service.** The game needs a Direct3D 7 window and a DirectSound
  device, so a build agent gating this layer must run on an interactive desktop (an auto-logon
  user, a display or virtual display, an audio device). A service-mode agent gates `PrePort64`.

## The gate

- **Quick gate:** the `recordings/autotest` set, every mission for two minutes of mission time at
  4x with an invulnerable, unflown or AI-flown player, plus the menu tour. Proves start-up, the
  opening events and that nothing crashes. Runs on every commit.
- **Deep gate (once its results are stable):** the `recordings/autoplay` set, the AI flying the
  player to win, full missions. Covers arrivals, break-ups, departures, the debriefing path and an
  armed player under fire. Replay sync is the gate; win or lose stays a tracked baseline
  (`docs/AUTOTEST.md`). Autoplay's own order logic and timing constants are part of what these
  recordings depend on, so a change to them re-records this set.
- **Hand-played recordings:** a few, for the human-input path, not one per mission.
- What a gate contains is the autotest planner's call, not this file's.

## Proof of isolation

A mutation test on 2026-11-08 against AutotestWeapons: one extra gameplay draw at weapon creation
desynced at frame 24; one extra gameplay draw in ship rendering desynced at frame 57 (the render
limit above); one extra cosmetic draw at particle creation desynced at frame 159 through the
debris arc sound (#276). Re-run it when a change touches either random stream: the gameplay
mutation must fail and the cosmetic one must pass.
