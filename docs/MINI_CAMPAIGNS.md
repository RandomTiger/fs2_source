# Mini campaigns

Branch `MiniCampaigns` (wip, off RecordPlayback). The retail multiplayer content, played as single-player campaigns.
No multiplayer code or screens come back.

## Campaigns

| File | Missions | Side |
|---|---|---|
| `data/missions/MiniTvT_FS2_Alpha.fc2` | MT-01 to MT-10 (team vs team) | Alpha |
| `data/missions/MiniTvT_FS2_Zeta.fc2` | MT-01 to MT-10 | Zeta |
| `data/missions/fs1/MiniTvT_FS1_Alpha.fsc` | M-Gate, M-GuardDuty, m-duel, m-pain | Alpha |
| `data/missions/fs1/MiniTvT_FS1_Zeta.fsc` | same | Zeta |
| `data/missions/MiniCoop_FS2_Templar.fc2` | Templar-01 to 04 (co-op) | — |
| `data/missions/fs1/MiniCoop_FS1_LastHope.fsc` | mm3-01a to 03a | — |
| `data/missions/fs1/MiniCoop_FS1_Altair.fsc` | mm3-04a to 06a | — |

- Team vs team: missions in retail order. Win your team's primary goals to go on; lose and you fly the mission again.
  The last mission ends the campaign on a win.
- Co-op: retail missions and branching (retail already repeats a lost mission). The other players' ships are flown by
  the AI: retail single-player never marks them as player ships.
- The missions themselves are the retail files from the install; only the campaign files are ours.
- To play: copy the `.fc2` files into the install's `data\missions`, then pick the campaign in the Campaign Room.
  The FS1 files need the FS1Compat code and an FS1 install, so they cannot run on this branch alone.

## Shipping

Ruling D-20261109-212152 (#296): the campaign files ship in the package's own `data\missions`, listed in
`data/package_files.txt`, and the game reads them from a second read-only search root (the folder of the exe or game
DLL). It never writes them into the player's install. Until that lands, copy the files into the install by hand as above.

## Tests

`-at_campaign <file>` with `-at_mission <m>` plays a mission inside a campaign file on the scratch pilot, so a
team-vs-team campaign's side applies (`code/Replay/Autotest.h`). Replays restore the pilot and campaign save from the
recording. `tools\record_autotest.ps1` and `tools\record_autoplay.ps1` take `-Campaign <file>` and name the files
`<mission>_<campaign>`: recordings/autotest and recordings/autoplay hold MT-01 to MT-10 on both sides and Templar-01 to 04.

## How the side works

Team-vs-team missions hold two copies of most things: player starts (Alpha, Zeta), briefings, debriefings, goals
(+Team), ship and weapon pools. Multiplayer picks the copy from the player's network team. Here the campaign file says:

```
+Player Team: 2        ; optional, read by this project only: 1 Alpha (default), 2 Zeta
```

`mission_single_tvt()` is true only in a single-player campaign playing a team-vs-team mission, and
`mission_player_team()` gives the team index (multiplayer: the network team, as retail). Every change uses these, so:

- multiplayer paths are unchanged;
- single-player missions and retail campaigns are unchanged (`mission_single_tvt()` is false);
- retail data files are not edited, and campaigns without `+Player Team:` load as before.

What follows the side: player start ship (first Zeta start when on Zeta), starting wing (orders, ship select, wingman
status), briefing, debriefing, ship/weapon pool, goals list, goal messages, HUD objective count. HUD IFF already follows
the player ship's team.

## Not done / to check in play

- Missions were tuned for multiplayer respawns; single-player has none.
- The Tech Room simulator does not list these missions (#295).
- Multiplayer's goals screen always shows team 0's goals (#294, retail bug, left as is).
