Formation ghost viewer (issue #1, throwaway branch FormationGhost; never merged)

WHAT YOU SEE
Each launcher replays a recording with the game exactly as retail plays it. The green wireframe ships are
"ghosts": the wing leaders as they fly with the formation fix, captured from a second run of the same
recording. The ghosts are drawn only; they do not touch the game (checked: with ghosts drawn, every
replay still matches its recording frame for frame).

The ghost of a ship stops when its captured run stopped. With the fix, the run diverges from the
recording's inputs, and a replay ends at its first desync.

Replays play at real speed (the HUD still shows x4; ignore it). The camera is the player's own view (Alpha 1).
Close the window to stop early.

HOW TO RUN
Double-click view_<mission>.cmd in this folder. If Windows Firewall asks about FS2_ghost_r.exe, Cancel is
fine: the replay does not use the network.

WHAT TO LOOK FOR (largest gaps first)
Alpha 1 appears only because the AI flies the player's ship in these recordings. When you fly Alpha 1
yourself, the formation code does not run for it. The leaders that matter in real play are the AI ones.

sm1-05   view_sm1-05.cmd     whole recording, 120 s
         Aries 1 (AI wing leader): ghost drifts 275 m by 31 s, up to 845 m around 108 s
         Alpha 1 (player's ship, AI-flown here): 235 m at 37 s, growing to 2.4 km by 102 s
         Iota 1, Kappa 1, Zeta 1: within 30 m; hard to see
sm2-10   view_sm2-10.cmd     ghosts until 28 s
         Aries 1: 500 m at 8 s, about 900 m at 15-21 s
         Scorpio 1: 300-400 m between 8 and 21 s
loop2-2  view_loop2-2.cmd    ghosts until 92 s
         Alpha 1 only (AI-flown here): up to 100 m around 23 s, then closing
         Same-wing collisions in this mission go from 15 to 33 with the fix (up to its desync)
sm2-02   view_sm2-02.cmd     Cancer 1: within 2 m; effectively no visible difference

The AI leaders are not always in the player's view; look ahead and to the sides around the times above.
Times are the mission clock, bottom right of the HUD.

NUMBERS
Issue #1 on GitHub has the full assessment (recordings moved, collisions, damage).
