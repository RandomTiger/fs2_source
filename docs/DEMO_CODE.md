# Demo-conditional code (D-063)

Every preprocessor block whose condition names a demo or other restricted build, in game code (FRED, the
tools and `Network` excluded). None of it may be cut. The dead-code inventory treats these blocks as live
and no cleanup change touches them. Regenerate the table with `python tools\demo_blocks.py .`: one row per block,
outermost condition shown, line numbers as of `PrePortCleanup` after the first dead-macro cut and the `PrePort64` merge.

| Macro | Blocks | What it is |
|---|---|---|
| `FS2_DEMO` | 85 | The FreeSpace 2 demo. The data set the project can redistribute; basis of the demo suite. |
| `DEMO` | 37 | The FreeSpace 1 demo, never defined in FS2 ("no change for FS2_DEMO" in `PsTypes.h`). |
| `OEM_BUILD` | 35 | The OEM bundle build. |
| `E3_BUILD` | 15 | The 3dfx E3 show build. |
| `PRESS_TOUR_BUILD` | 12 | The press tour build. |
| `PD_BUILD` | 11 | The FRED documentation/evaluation build. |

A block naming several macros counts once per macro. 149 blocks in 54 files; 78 have an `#else`.

## Sizing the single-exe demo/retail refactor (D-063 item 3)

Scope is `FS2_DEMO` only: about 85 blocks in about 45 files, roughly 3,500 lines, 2,000 of them the
whole of `Asteroid/Asteroid.cpp` (the demo has no asteroids). By kind:

- **Pool limits (about 15 blocks, headers mostly).** The demo has smaller fixed pools: objects 300 (retail 1000),
  weapons 100 (350), particles 500 (2000), sexp nodes 1600 (2200), wings 15 (25), ship arrivals 45 (90),
  wing arrivals 10 (20), collision pairs 3000 (8000), ship subobjects 360 (700), subsystem status 125 (250),
  debris 48 (128 on `Enhanced`), wingman/command heads 1/1 (2/3). One exe keeps the retail array sizes and
  adds a run-time cap set from the detected data, so the demo still refuses what it refused. Each cap is
  one variable and one compare at the allocation site; this is where demo fidelity is decided (D-063 item 4).
- **Data layout (2 blocks).** `NUM_MEDALS` 16 vs 18 changes the pilot file; `MAX_BITMAPS` is the same in
  both. The pilot reader and writer take the medal count from the detected data.
- **Names and versions (2 blocks).** Built-in campaign (`"demo"`) and version number become run-time strings.
- **Whole-file and large gates (Asteroid, title screen, demo upsell screens in `FreeSpace.cpp`, about 400
  lines).** Compiled in always, entered on the demo flag.
- **Menu, mission-UI and flow differences (the rest, about 60 blocks, MenuUI and MissionUI mostly).**
  Each `#ifdef` becomes `if (Game_is_demo)`; `#else` branches become the `else`.

Order: the demo suite is recorded first with a compile-time `FS2_DEMO` build, so the refactor is proved by
that suite replaying unchanged and the retail suite replaying unchanged. Estimate: one batch per subsystem,
about eight batches, after the D-062 order (backend collapses first).

## Blocks

| File | Lines | Condition | Has else | Body lines | #defines |
|---|---|---|---|---|---|
| `Asteroid/Asteroid.cpp` | 277-2266 | `#ifndef FS2_DEMO` | yes | 1987 | 8 |
| `Bmpman/BmpMan.h` | 154-158 | `#ifdef FS2_DEMO` | yes | 2 | 2 |
| `ControlConfig/ControlsConfig.cpp` | 687-693 | `#ifdef FS2_DEMO` |  | 5 | 0 |
| `Cutscene/Cutscenes.cpp` | 214-226 | `#if defined(OEM_BUILD)` | yes | 10 | 0 |
| `Cutscene/Cutscenes.cpp` | 362-366 | `#if defined(OEM_BUILD)` | yes | 2 | 0 |
| `Debris/Debris.h` | 177-181 | `#ifdef FS2_DEMO` | yes | 2 | 2 |
| `FREESPACE2/FreeSpace.cpp` | 723-902 | `#ifdef FS2_DEMO` | yes | 174 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 934-944 | `#if defined(FS2_DEMO)` | yes | 8 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 1665-1671 | `#ifndef FS2_DEMO` |  | 5 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 1859-1861 | `#ifdef FS2_DEMO` |  | 1 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 2051-2053 | `#ifndef FS2_DEMO` |  | 1 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 2117-2131 | `#ifdef E3_BUILD` | yes | 9 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 2133-2152 | `#ifdef E3_BUILD` | yes | 14 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 2193-2198 | `#if defined(FS2_DEMO) \|\| defined(OEM_BUILD)` |  | 4 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 4460-4514 | `#ifdef FS2_DEMO` |  | 53 | 1 |
| `FREESPACE2/FreeSpace.cpp` | 4540-4542 | `#ifdef FS2_DEMO` |  | 1 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 4627-4632 | `#ifndef FS2_DEMO` |  | 4 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 4799-4805 | `#if defined(FS2_DEMO)` | yes | 4 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 5191-5202 | `#if defined(FS2_DEMO) \|\| defined(OEM_BUILD)` | yes | 9 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 5361-5365 | `#if defined(PRESS_TOUR_BUILD) \|\| defined(PD_BUILD)` | yes | 2 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 5623-5631 | `#if defined(PRESS_TOUR_BUILD) \|\| defined(PD_BUILD)` | yes | 6 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 6105-6109 | `#if defined(PRESS_TOUR_BUILD) \|\| defined(PD_BUILD)` | yes | 2 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 6539-6541 | `#if !defined(PRESS_TOUR_BUILD) && !defined(PD_BUILD)` |  | 1 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 7162-7166 | `#if defined (FS2_DEMO)` | yes | 2 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 7190-7194 | `#ifdef DEMO` | yes | 2 | 0 |
| `FREESPACE2/FreeSpace.cpp` | 7208-7379 | `#if defined(OEM_BUILD)` |  | 170 | 3 |
| `FREESPACE2/FreeSpace.cpp` | 7394-7530 | `#ifdef FS2_DEMO` |  | 133 | 2 |
| `FREESPACE2/FreeSpace.cpp` | 7727-7756 | `#if defined(FS2_DEMO) \|\| defined(OEM_BUILD)` |  | 28 | 0 |
| `FREESPACE2/FreeSpace.h` | 160-179 | `#ifdef MULTIPLAYER_BETA_BUILD` | yes | 15 | 10 |
| `FREESPACE2/FreeSpaceWin32.cpp` | 229-233 | `#if defined(OEM_BUILD)` | yes | 2 | 0 |
| `FREESPACE2/FreeSpaceWin32.cpp` | 224-261 | `#ifndef DEMO` |  | 26 | 0 |
| `FREESPACE2/FreeSpaceWin32.cpp` | 279-286 | `#ifdef FS2_DEMO` | yes | 5 | 0 |
| `FREESPACE2/FreeSpaceWin32.cpp` | 483-490 | `#if !defined(OEM_BUILD)` |  | 6 | 0 |
| `FREESPACE2/FreeSpaceWin32.cpp` | 536-548 | `#ifndef DEMO` |  | 11 | 0 |
| `GameHelp/ContextHelp.cpp` | 294-303 | `#if !defined(PRESS_TOUR_BUILD) && !defined(PD_BUILD)` |  | 8 | 0 |
| `GameHelp/GameplayHelp.cpp` | 485-488 | `#ifndef FS2_DEMO` |  | 2 | 0 |
| `GameHelp/GameplayHelp.cpp` | 493-496 | `#ifndef FS2_DEMO` |  | 2 | 0 |
| `GameHelp/GameplayHelp.cpp` | 649-660 | `#ifndef FS2_DEMO` |  | 10 | 0 |
| `GameHelp/GameplayHelp.cpp` | 704-707 | `#ifndef FS2_DEMO` |  | 2 | 0 |
| `GlobalIncs/PsTypes.h` | 193-195 | `#if defined(MULTIPLAYER_BETA_BUILD) \|\| defined(E3_BUILD) \|\| defined(RELEASE_REAL)` |  | 1 | 1 |
| `GlobalIncs/PsTypes.h` | 229-233 | `#if defined(DEMO) \|\| defined(OEM_BUILD) // no change for FS2_DEMO` | yes | 2 | 2 |
| `GlobalIncs/version.h` | 114-126 | `#if defined (FS2_DEMO)` | yes | 9 | 9 |
| `GlobalIncs/version.h` | 131-140 | `#ifdef MULTIPLAYER_BETA_BUILD` | yes | 6 | 6 |
| `Hud/HUDtarget.cpp` | 2491-2495 | `#ifndef FS2_DEMO` |  | 3 | 0 |
| `Hud/HUDtarget.cpp` | 3446-3455 | `#ifndef FS2_DEMO` |  | 8 | 0 |
| `Hud/HUDtargetbox.cpp` | 775-855 | `#ifndef FS2_DEMO` |  | 79 | 0 |
| `Io/Joy.cpp` | 380-385 | `#ifdef FS2_DEMO` |  | 4 | 0 |
| `Io/KeyControl.cpp` | 354-363 | `#ifdef FS2_DEMO` | yes | 7 | 0 |
| `Io/KeyControl.cpp` | 1064-1079 | `#ifndef FS2_DEMO` |  | 14 | 0 |
| `Io/KeyControl.cpp` | 1352-1437 | `#ifndef FS2_DEMO` |  | 82 | 0 |
| `Io/KeyControl.cpp` | 1621-1711 | `#ifdef FS2_DEMO` | yes | 88 | 0 |
| `Io/Mouse.cpp` | 253-258 | `#ifdef FS2_DEMO` |  | 4 | 0 |
| `MenuUI/Barracks.cpp` | 1020-1064 | `#if defined(DEMO) \|\| defined(OEM_BUILD)` | yes | 42 | 0 |
| `MenuUI/Barracks.cpp` | 1084-1089 | `#ifdef FS2_DEMO` | yes | 3 | 0 |
| `MenuUI/Barracks.cpp` | 1104-1111 | `#if defined(DEMO) \|\| defined(OEM_BUILD) // not for FS2_DEMO` | yes | 5 | 0 |
| `MenuUI/Barracks.cpp` | 1423-1428 | `#if defined(MULTIPLAYER_BETA_BUILD) \|\| defined(E3_BUILD) \|\| defined(PRESS_TOUR_BUILD)` |  | 4 | 0 |
| `MenuUI/Barracks.cpp` | 1437-1440 | `#if defined(E3_BUILD) \|\| defined(PRESS_TOUR_BUILD)` |  | 2 | 0 |
| `MenuUI/Barracks.cpp` | 1443-1449 | `#ifdef MULTIPLAYER_BETA_BUILD` | yes | 3 | 0 |
| `MenuUI/Barracks.cpp` | 1514-1524 | `#if defined(DEMO) \|\| defined(OEM_BUILD) // not for FS2_DEMO` | yes | 8 | 0 |
| `MenuUI/MainHallMenu.cpp` | 1043-1061 | `#ifdef MULTIPLAYER_BETA_BUILD` | yes | 15 | 0 |
| `MenuUI/MainHallMenu.cpp` | 1066-1072 | `#if defined(FS2_DEMO)` | yes | 4 | 0 |
| `MenuUI/MainHallMenu.cpp` | 1085-1105 | `#ifdef FS2_DEMO` | yes | 18 | 0 |
| `MenuUI/MainHallMenu.cpp` | 1083-1107 | `#if !defined(MULTIPLAYER_BETA_BUILD) && !defined(E3_BUILD) && !defined(PRESS_TOUR_BUILD)` |  | 20 | 0 |
| `MenuUI/MainHallMenu.cpp` | 1112-1121 | `#if defined(DEMO) \|\| defined(OEM_BUILD) // not for FS2_DEMO` | yes | 7 | 0 |
| `MenuUI/MainHallMenu.cpp` | 1128-1143 | `#if !(defined(MULTIPLAYER_BETA_BUILD) \|\| defined(FS2_DEMO))` |  | 12 | 0 |
| `MenuUI/MainHallMenu.cpp` | 1149-1164 | `#if !defined(NDEBUG) && !defined(FS2_DEMO)` |  | 14 | 0 |
| `MenuUI/OptionsMenu.cpp` | 755-761 | `#if !defined(DEMO) && !defined(OEM_BUILD) // not for FS2_DEMO` |  | 5 | 0 |
| `MenuUI/OptionsMenu.cpp` | 775-777 | `#if !defined(DEMO) && !defined(OEM_BUILD) // not for FS2_DEMO` |  | 1 | 0 |
| `MenuUI/OptionsMenu.cpp` | 790-795 | `#if defined(DEMO) \|\| defined(OEM_BUILD) // not for FS2_DEMO` |  | 4 | 0 |
| `MenuUI/OptionsMenu.cpp` | 801-804 | `#if !defined(DEMO) && !defined(OEM_BUILD) // not for FS2_DEMO` |  | 2 | 0 |
| `MenuUI/OptionsMenu.cpp` | 938-950 | `#ifdef FS2_DEMO` | yes | 10 | 0 |
| `MenuUI/OptionsMenu.cpp` | 1100-1102 | `#if !defined(DEMO) && !defined(OEM_BUILD) // not for FS2_DEMO` |  | 1 | 0 |
| `MenuUI/OptionsMenu.cpp` | 1258-1260 | `#if !defined(DEMO) && !defined(OEM_BUILD) // not for FS2_DEMO` |  | 1 | 0 |
| `MenuUI/OptionsMenu.cpp` | 1445-1447 | `#if !defined(DEMO) && !defined(OEM_BUILD) // not for FS2_DEMO` |  | 1 | 0 |
| `MenuUI/OptionsMenuMulti.cpp` | 901-904 | `#ifdef FS2_DEMO` |  | 2 | 0 |
| `MenuUI/OptionsMenuMulti.cpp` | 1307-1310 | `#ifndef FS2_DEMO` |  | 2 | 0 |
| `MenuUI/PlayerMenu.cpp` | 195-201 | `#ifdef FS2_DEMO` |  | 5 | 0 |
| `MenuUI/PlayerMenu.cpp` | 398-412 | `#ifdef FS2_DEMO` |  | 11 | 0 |
| `MenuUI/PlayerMenu.cpp` | 472-478 | `#ifdef MULTIPLAYER_BETA_BUILD` | yes | 4 | 0 |
| `MenuUI/PlayerMenu.cpp` | 499-515 | `#if defined(DEMO) \|\| defined(OEM_BUILD) \|\| defined(E3_BUILD) \|\| defined(PRESS_TOUR_BUILD) // not for FS2_DEMO` | yes | 13 | 0 |
| `MenuUI/PlayerMenu.cpp` | 522-559 | `#ifdef FS2_DEMO` |  | 36 | 0 |
| `MenuUI/PlayerMenu.cpp` | 565-570 | `#ifdef FS2_DEMO` |  | 4 | 0 |
| `MenuUI/PlayerMenu.cpp` | 594-616 | `#if defined(DEMO) \|\| defined(OEM_BUILD) // not for FS2_DEMO` | yes | 20 | 0 |
| `MenuUI/PlayerMenu.cpp` | 876-891 | `#if defined(DEMO) \|\| defined(OEM_BUILD) // not for FS2_DEMO` | yes | 13 | 0 |
| `MenuUI/PlayerMenu.cpp` | 908-912 | `#ifdef FS2_DEMO` |  | 3 | 0 |
| `MenuUI/ReadyRoom.cpp` | 449-454 | `#ifdef OEM_BUILD` | yes | 3 | 0 |
| `MenuUI/ReadyRoom.cpp` | 876-890 | `#ifdef FS2_DEMO` | yes | 12 | 0 |
| `MenuUI/ReadyRoom.cpp` | 936-946 | `#ifdef OEM_BUILD` | yes | 8 | 0 |
| `MenuUI/ReadyRoom.cpp` | 1450-1460 | `#ifdef PD_BUILD` | yes | 8 | 0 |
| `MenuUI/TechMenu.cpp` | 1140-1146 | `#if !defined(E3_BUILD) && !defined(PD_BUILD)` | yes | 4 | 0 |
| `MenuUI/TechMenu.cpp` | 1149-1155 | `#if !defined(E3_BUILD) && !defined(PD_BUILD)` | yes | 4 | 0 |
| `MenuUI/TechMenu.cpp` | 1158-1164 | `#if !defined(E3_BUILD) && !defined(PD_BUILD)` | yes | 4 | 0 |
| `Mission/MissionCampaign.cpp` | 1768-1772 | `#ifdef FS2_DEMO` | yes | 2 | 0 |
| `Mission/MissionCampaign.h` | 265-277 | `#if defined(FS2_DEMO)` | yes | 8 | 8 |
| `Mission/MissionLoad.cpp` | 512-523 | `#ifdef PD_BUILD` | yes | 9 | 0 |
| `Mission/MissionMessage.cpp` | 452-458 | `#ifdef FS2_DEMO` | yes | 4 | 4 |
| `Mission/MissionMessage.cpp` | 1526-1528 | `#ifndef DEMO // do we want this for FS2_DEMO` |  | 1 | 0 |
| `Mission/MissionParse.cpp` | 3305-3400 | `#ifndef FS2_DEMO` |  | 94 | 0 |
| `Mission/MissionParse.h` | 296-300 | `#ifdef FS2_DEMO` | yes | 2 | 2 |
| `Mission/MissionParse.h` | 455-461 | `#ifdef FS2_DEMO` | yes | 4 | 4 |
| `MissionUI/MissionBrief.cpp` | 627-629 | `#ifdef FS2_DEMO` |  | 1 | 0 |
| `MissionUI/MissionBrief.cpp` | 643-645 | `#ifdef FS2_DEMO` |  | 1 | 0 |
| `MissionUI/MissionBrief.cpp` | 1108-1110 | `#ifdef FS2_DEMO` |  | 1 | 0 |
| `MissionUI/MissionBrief.cpp` | 1166-1169 | `#ifndef FS2_DEMO` |  | 2 | 0 |
| `MissionUI/MissionBrief.cpp` | 1529-1534 | `#ifndef FS2_DEMO` |  | 4 | 0 |
| `MissionUI/MissionBrief.cpp` | 1704-1713 | `#ifdef FS2_DEMO` | yes | 7 | 0 |
| `MissionUI/MissionBrief.cpp` | 2099-2101 | `#ifndef FS2_DEMO` |  | 1 | 0 |
| `MissionUI/MissionBrief.cpp` | 2157-2203 | `#ifdef FS2_DEMO` | yes | 44 | 0 |
| `MissionUI/MissionBrief.cpp` | 2210-2239 | `#ifdef FS2_DEMO` | yes | 27 | 0 |
| `MissionUI/MissionDebrief.cpp` | 1644-1648 | `#if defined(FS2_DEMO) \|\| defined(OEM_BUILD)` | yes | 2 | 0 |
| `MissionUI/MissionScreenCommon.cpp` | 508-511 | `#ifdef DEMO // allow for FS2_DEMO` |  | 2 | 0 |
| `MissionUI/MissionScreenCommon.cpp` | 656-684 | `#ifndef DEMO // not for FS2_DEMO` |  | 27 | 0 |
| `MissionUI/MissionScreenCommon.cpp` | 981-991 | `#ifndef DEMO // not for FS2_DEMO` | yes | 8 | 0 |
| `MissionUI/MissionScreenCommon.cpp` | 1002-1008 | `#ifndef DEMO // not for FS2_DEMO` | yes | 4 | 0 |
| `MissionUI/MissionScreenCommon.cpp` | 1019-1044 | `#ifndef DEMO // not for FS2_DEMO` | yes | 23 | 0 |
| `MissionUI/MissionScreenCommon.cpp` | 1055-1080 | `#ifndef DEMO // not for FS2_DEMO` | yes | 23 | 0 |
| `MissionUI/MissionShipChoice.cpp` | 495-499 | `#ifdef FS2_DEMO` | yes | 2 | 2 |
| `MissionUI/MissionShipChoice.cpp` | 2800-2818 | `#ifndef DEMO // not for FS2_DEMO` |  | 17 | 0 |
| `MissionUI/MissionShipChoice.cpp` | 2841-2851 | `#ifndef DEMO // not for FS2_DEMO` |  | 9 | 0 |
| `MissionUI/MissionWeaponChoice.cpp` | 506-510 | `#ifdef FS2_DEMO` | yes | 2 | 2 |
| `MissionUI/MissionWeaponChoice.cpp` | 1405-1428 | `#ifndef DEMO // not for FS2_DEMO` |  | 22 | 0 |
| `MissionUI/MissionWeaponChoice.cpp` | 1476-1493 | `#ifndef DEMO // not for FS2_DEMO` |  | 16 | 0 |
| `MissionUI/MissionWeaponChoice.cpp` | 1639-1687 | `#ifndef DEMO // not for FS2_DEMO` |  | 47 | 0 |
| `Object/CollideDebrisShip.cpp` | 304-448 | `#ifndef FS2_DEMO` | yes | 142 | 0 |
| `Object/CollideDebrisWeapon.cpp` | 108-138 | `#ifndef FS2_DEMO` | yes | 28 | 0 |
| `Object/CollideShipShip.cpp` | 1051-1091 | `#ifndef FS2_DEMO` |  | 39 | 0 |
| `Object/ObjCollide.cpp` | 235-240 | `#ifdef FS2_DEMO` | yes | 3 | 2 |
| `Object/Object.h` | 322-326 | `#ifdef FS2_DEMO` | yes | 2 | 2 |
| `OsApi/OsRegistry.cpp` | 27-36 | `#if defined(FS2_DEMO)` | yes | 6 | 0 |
| `Parse/SEXP.H` | 292-299 | `#ifdef FS2_DEMO` | yes | 5 | 2 |
| `Particle/Particle.cpp` | 191-198 | `#ifdef FS2_DEMO` | yes | 5 | 2 |
| `Playerman/ManagePilot.cpp` | 1157-1161 | `#ifdef FS2_DEMO` | yes | 2 | 0 |
| `Ship/AiCode.cpp` | 2848-2855 | `#ifndef FS2_DEMO` |  | 6 | 0 |
| `Ship/Shield.cpp` | 255-270 | `#ifndef DEMO // not for FS2_DEMO` |  | 14 | 0 |
| `Ship/Shield.cpp` | 178-1228 | `#ifndef DEMO // not for FS2_DEMO` | yes | 1044 | 11 |
| `Ship/Ship.cpp` | 718-726 | `#ifdef FS2_DEMO` | yes | 6 | 2 |
| `Ship/Ship.cpp` | 1042-1050 | `#ifdef DEMO // not needed FS2_DEMO (using separate table file)` |  | 7 | 0 |
| `Ship/Ship.cpp` | 3527-3529 | `#ifdef DEMO // N/A FS2_DEMO` |  | 1 | 0 |
| `Ship/Ship.cpp` | 3544-3546 | `#ifdef DEMO // N/A FS2_DEMO` |  | 1 | 0 |
| `Ship/Ship.cpp` | 9287-9385 | `#if defined(MULTIPLAYER_BETA_BUILD) \|\| defined(FS2_DEMO)` | yes | 96 | 0 |
| `Ship/Ship.h` | 884-888 | `#ifdef FS2_DEMO` | yes | 2 | 2 |
| `Ship/ShipFX.cpp` | 2222-2226 | `#ifdef FS2_DEMO` | yes | 2 | 0 |
| `Ship/ShipFX.cpp` | 2228-2234 | `#ifdef FS2_DEMO` | yes | 4 | 0 |
| `Ship/ShipFX.cpp` | 2240-2244 | `#ifdef FS2_DEMO` | yes | 2 | 0 |
| `Stats/Scoring.h` | 179-185 | `#ifdef FS2_DEMO` | yes | 4 | 4 |
| `UI/WINDOW.cpp` | 920-922 | `#ifndef DEMO` |  | 1 | 0 |
| `Weapon/Beam.cpp` | 1841-1849 | `#ifndef FS2_DEMO` |  | 7 | 0 |
| `Weapon/Weapon.h` | 399-404 | `#ifdef FS2_DEMO` | yes | 3 | 2 |
| `Weapon/Weapons.cpp` | 761-770 | `#ifdef DEMO // not needed FS2_DEMO (separate table file)` |  | 8 | 0 |
