# Bitmap sections: review evidence (stage 0, unmodified PrePort32 tip e790469 plus the logger)

Decisions: D-20261029-075125 (scope), D-20261029-075126 (pass criteria A-E). No section code is changed at this stage.
Images are local only (never committed). This file holds their SHA-256 and the frame list.

## Tooling

- Mission: `data/missions/AutotestHudJitter.fs2` (generator `tools/missions/AutotestHudJitter.ps1`): nebula, `s_emp` lightning storm, scripted EMP Adv. hits, fixed seed. Plays to its end under autotest (`-at_time 150`, goal Survive).
- Recording and baseline trace: `recordings/autotest/AutotestHudJitter.fs2rec` / `.trace`. Replays MATCH in Release and Debug (3587 frames).
- Logger (`code/Replay/BitmapLog.*`), off unless `-bmlog FILE`, `-bmshots DIR -bmframes LIST`. Lines: `B` (GR-layer 2D bitmap call: kind, handle, bitmap size, x, y, w, h, sx, sy, alphablend, bitblt, alpha), `J` / `G` (emp_hud_jitter in/out and emp_should_blit_gauge, logged while EMP is active), `F` (frame end, Framecount, cosmetic_rand calls on the logic and drawing streams).
- Baseline log of the mission replay (Release): 3600 GR frames, 259148 B lines (gr_bitmap 36, gr_aabitmap 105070, gr_aabitmap_ex 154042), 676 frames with a non-zero jitter, end counts cr=60532 crd=198763.

## Criterion E (logging on vs off, Release replay, trace SHA-256)

| Recording | trace off == on | trace on == recorded | cosmetic counts at end (logic/drawing) |
|---|---|---|---|
| AutotestHudJitter | yes | yes | 60532 / 198763 |
| Autotest | yes | yes | 55732 / 72787 |
| AutotestAI | yes | yes | 524179 / 264313 |
| sm1-01 | yes | yes | 186660 / 97699 |
| sm1-06 | yes | yes | 94283 / 74801 |
| sm2-07 | yes | yes | 131050 / 155761 |

The counter is two global increments that run with logging on or off; with logging off nothing prints it, so "count on == count off" is shown by the traces being equal (the draw stream never reaches a trace) and by the count being identical across Release runs and Debug (below).

## Log determinism (AutotestHudJitter replay)

- Release run to run: log files byte-identical (SHA-256 equal).
- Release vs Debug: identical after dropping `h=` (bitmap handles differ between builds, by +2 for some animation frames) and the load-screen lines before mission frame 1; the F counts (cr, crd, mf) are identical. A before/after comparison must therefore be same-config and may compare handles only within a config.

## Frame list and hashes (SHA-256 of the 24-bit BMP of the D3D back buffer, 32-bit, window mode)

Capture: `FS2vs2022{r,d}.exe -window -autotest -replay recordings\autotest\AutotestHudJitter.fs2rec -bmshots DIR -bmframes <list>`; frame numbers are GR frames of the log (`F n`). Frames: load screen 3, 5, 20; no-EMP 300, 1000, 2000; EMP jitter frames (log has a non-zero J) 566 709 1162 1223 1322 1568 1773 2008 2069 2804 2937 2998 (12), of which the jitter frames are marked.
Each hash was taken twice per config (R, R2; D, D2) and equal, so all 18 are exact references. Release and Debug differ from each other (not compared).

| Frame | EMP jitter | Release | Debug |
|---|---|---|---|
| 3 | no | B2065A8E75B96D45FB12D8B5076D2D7DAE88802CB25D6FA55A96168AD01BD163 | B2065A8E75B96D45FB12D8B5076D2D7DAE88802CB25D6FA55A96168AD01BD163 |
| 5 | no | C6A5D7249F9E6A87FF6557DE62B51AED5790AB715DC60AACDE62CD0B187BAFCA | 5E561EEB69C6C6226D019E29D1F0E40A6B88649936B09F0574D5997DB42E96AE |
| 20 | no | EC31D1CEA32B8D0E6DC9458082FE4FD897192F5388FF564669811BE82F3CC209 | 1BA2793C4A25DF3A4FF9112619E58A6B2B684A5710EF45B67A1EEBBDD3A92D86 |
| 300 | no | 0E657FF8A84911CBC9548732F60557271CDAC8B2253B7F3694E8161CCC8AEC01 | 562ABC723ADADB98BEFC172461A82C351C622822D03AF520BAB50356D40303A1 |
| 566 | yes | 5E7613C51AAB96718294403B003FDDD2E4099F1E63DA09AE5B3458986D8C2E6E | B9FCC652A042E5A072D4C52A8351773946BB55F10A5A7486AAA1ABE7029CE4FD |
| 709 | yes | 351E103A9F8749EA149FC7F85F029711B1F051C38E1D5137C3B0E85F21A3306D | C8F2580E9B9B6CDD4749454922A853FB65C9C0A04A7D600177D13349909A2D57 |
| 1000 | no | 92A72EC838FD3993D9A4CE78735246D2DA2D9D84812481E9D1959F8C3509FC63 | 41AD6D7629C50855E52723F02D92CCD6EEBFF76F010491FD5D369FD6061A03BE |
| 1162 | yes | 43D79D4461DC4F5D71AEC830D6A956CC4093D3748D607096F857D295B76363ED | 1F46B1C5BB9BC498A1C5D7FB5D393C6B87C62ECB3B0C8233F19001959B5C8A8E |
| 1223 | yes | EDC9B93BF5731A81F37A6508D2FF72AFD0EE77455E4EF259C1A0F191B79A1F9D | BEA13143BDB4BAB977CEF4BCB5FAB7552DF47D4EBF68001226F6A29177A17838 |
| 1322 | yes | D255A8A7837E95956D39E3851002100EFCB3A6475D1BF3AB4C4E05724F265D12 | 83C00BDB46B564EDF06EF2001CA946C771A3BDF05FDA2C44293B2347DB7C5C8F |
| 1568 | yes | A8EA1E73F3BE4C87F81ADD5C0106E9A9741C6C34E42F4FCA24F56E78C635AC73 | 85E4E1A4AD8A5D24539A8623102AFFA52D38310949B62342EEC67809A8D01E8D |
| 1773 | yes | 775165D05319D7F2E89310D968EA267CC9A7344473CE812704EBF8114B1F7815 | 774B33BA8AC3A545B5A91784AE9A309AB21B9CE7199DBF6B4ED886199643DDCA |
| 2000 | yes | A3018DA30F2D9EC7EE1B43B89F22DC1A3C716697A534BE186474AB4B5CC739C4 | D3DF02EF2CA4E8D0FE418C82B0C6C1FE221DF6B36801E9B1731F8FE72E9183BE |
| 2008 | yes | 0491BA30F08EF3020EED6F1AF955FE16D49191D12E7A3A598241CE3DBBD15535 | B37D97F6CAD832404E9CFD74E20FB10B113F8C7E7FA1984BF59153F306EB96F3 |
| 2069 | yes | DFF1D5886A1E5C75A1CBD388A55395BBF8CA939AB88E1C54848D1544291F1E38 | D872E7EBFCC4488559DD37A0272103184191B325CA970256D861448FEFE42D9F |
| 2804 | yes | 7BA227D67B5DD8503AEDDA2107D542E81E6356A9C32D770764DF75BAD8720C0E | B2BD93D6373472D2128CE6509537E93DEFFE9C38D138C30B999BEA684CF8393F |
| 2937 | yes | C3D1E36B89536252ABA800C9ED36E2E1D2C3E2515446F40A9C8E3CAAF084C8CB | 7585BA859C4816305F71DE22A87F74E0AA4AD4D0F7055DF1C219BFB59FB67D51 |
| 2998 | yes | 750A2CC5FD3AA5C48CEEC3326F6D4DED44F8DE3F8C25B6582E5065950AE7261D | A8D11D6B18C75812933C6C062E06F811F4C9A70283C18F1A4B2E94EEBBEFE75D |

### Menu tour (`-at_menus -at_shots DIR`, live, 59 screens)

Covers: main hall, barracks (the pilot pic row, Barracks.cpp), medals, HUD config, options, tech room, campaign room, cutscenes, credits, simulator, briefing / ship select / weapon loadout / command briefing, debriefing and stats, the in-mission screens (pause, log, help, hotkeys, HUD). Hashes per run: Release (two runs), Debug (two runs). Stable = identical in both runs of the config; these are the exact references. Unstable screens (animation, scrolling, clock) are marked `-` and must be compared by eye or against a tolerance, not by hash.

| Screen | Release | Debug |
|---|---|---|
| briefing/briefing.bmp | BCA288DFC408D906DF7B59BF669DE2010E9D75D8F6DA26A67FF4BC0E4DAEC793 | 2099E5B31D80C328E4154CB4C80CCB816DB028093D0CB9759816CA2F7A44BC15 |
| briefing/briefing_help.bmp | 0E14996FE95ABF84274F4B1AA095E2C29D8075EC07EA96328353F56257015890 | C5FC12DEBF5EF556E4094BEB2ECBE1F175B8CEFF5345923BEDB0014D8A772FCD |
| briefing/command_briefing.bmp | DE364C59385AE1EE45ECF06CAC80D9BAE100C899A162166B8D9874D25FBCB4D5 | DE364C59385AE1EE45ECF06CAC80D9BAE100C899A162166B8D9874D25FBCB4D5 |
| briefing/command_briefing_help.bmp | 2E3B4469B5BFE5B111619A1F29EF978207864E28C0108EA5317A741B36DDC87C | DA573844FE42C21D923041FA8990967CFA7718F421D243C6D92750C9D3044C8B |
| briefing/ship_select.bmp | 2587A5604E0038DD0AE310E2913D1CEF210E38A7C00BFF6F0CD4144E1C8C92DA | - |
| briefing/ship_select_help.bmp | 5FE770E566B26AB69EE603615E6FBE83416578548E6CEF181C1A18A97A4673F7 | - |
| briefing/weapon_loadout.bmp | - | E8E32B44381B4031742FA1128549BBA7FAEC7329A4F4554A6B5F10E992F32CEF |
| briefing/weapon_loadout_help.bmp | - | E525CB56B2646311C511FA7D1D0F1A128B62AF0A1E687A56717A9D0C90614206 |
| campaign_techroom/campaign_room.bmp | 1D3C6C8DC1206D17B5DB59E00F38F282BC0EE1276EA6DC344CC82757B7DB7515 | 1D3C6C8DC1206D17B5DB59E00F38F282BC0EE1276EA6DC344CC82757B7DB7515 |
| campaign_techroom/campaign_room_help.bmp | AADA1B9CBE90A61564836C1A3096DAFE07FC4A6C22A989E0498B3D0D8224DCE2 | AADA1B9CBE90A61564836C1A3096DAFE07FC4A6C22A989E0498B3D0D8224DCE2 |
| campaign_techroom/credits.bmp | - | - |
| campaign_techroom/cutscenes.bmp | 8B8F9A646276EBCC35D9D961EBAEF6BA1339B89324169BAE0353D78644F190C9 | 8B8F9A646276EBCC35D9D961EBAEF6BA1339B89324169BAE0353D78644F190C9 |
| campaign_techroom/simulator_single.bmp | A4D400F8F2B088CED48B42823BD87FCB88D7C2E30AA164FB31CB46D9912B50C6 | A4D400F8F2B088CED48B42823BD87FCB88D7C2E30AA164FB31CB46D9912B50C6 |
| campaign_techroom/simulator_single_help.bmp | A9BACCD383BC4547BD69BA34CB8325264E6816CA9000A2436292DFB10EE3B92F | A9BACCD383BC4547BD69BA34CB8325264E6816CA9000A2436292DFB10EE3B92F |
| campaign_techroom/techroom_ships.bmp | - | - |
| campaign_techroom/techroom_ships_help.bmp | - | - |
| campaign_techroom/techroom_species.bmp | 788B42D6F7619CE901E9343495FED3AA613C5726AF1DD378D074232B00DF20A4 | 788B42D6F7619CE901E9343495FED3AA613C5726AF1DD378D074232B00DF20A4 |
| campaign_techroom/techroom_weapons.bmp | 4B9E6B035E97262F134F9A8DD705E0A9781B2D6E2ADBDAC022ABB0A5A36C438E | 1606B5102A4B19B5B6E81E0EB8ADD03F26D63BD5967C6772DB1BD0BF5FF65CDF |
| campaign_techroom/techroom_weapons_help.bmp | 1770EE77D3209EF9934B2E56ED66B0455B6D812ACD5F1A9E10D5C386810DD7CC | 1770EE77D3209EF9934B2E56ED66B0455B6D812ACD5F1A9E10D5C386810DD7CC |
| controls/controls_misc.bmp | E6BBE433DE326C58552AC0937B3A45C0817826C071ABF9C2E0FA0F9361204272 | E6BBE433DE326C58552AC0937B3A45C0817826C071ABF9C2E0FA0F9361204272 |
| controls/controls_ship.bmp | A741DD360A535C3C3D979654C0FC3F4FCEE33012CCCCE90E936B66C832EA8462 | A741DD360A535C3C3D979654C0FC3F4FCEE33012CCCCE90E936B66C832EA8462 |
| controls/controls_targeting.bmp | 286ECAC65D4EB9A67F6EC73C7E3795A71C8DE4602DE44E253E18C3A5A1C7BB5D | 286ECAC65D4EB9A67F6EC73C7E3795A71C8DE4602DE44E253E18C3A5A1C7BB5D |
| controls/controls_targeting_help.bmp | 9F6A7163C547E772259155F385234ADD82A0B91285F3D5B7A000AE82AD7B91F3 | 9F6A7163C547E772259155F385234ADD82A0B91285F3D5B7A000AE82AD7B91F3 |
| controls/controls_weapons.bmp | C9BCB8F89E4360641AED42B2408CF373237F6B5CCE82978D76570F5F09E73935 | C9BCB8F89E4360641AED42B2408CF373237F6B5CCE82978D76570F5F09E73935 |
| debrief/debrief.bmp | 10DC3FEC9CE074E3BC71D633DF0AC2E305A0B966F31AE4A291CE3B1D2C17DE6E | 130C68D0001C0D35CFC2307C647C229D7D48148D1FA3E15395D427357238D4C9 |
| debrief/debrief_help.bmp | 765922D69928120BAD68E67A13829982BEB2E8A1B61A73C0AFCF3ACA3587F78E | 723C6E3296476DE424485CEEA5E10D765C81440955EB66440F16267F8766BC76 |
| debrief/debrief_stats.bmp | 680006A235395D896C8AEDF11FAD8B6AB350A26464102F16CF597C7DE903688B | A54F6B24A345448BFF66E4C597828380314550FE3FB2656BD3105DC086DDF233 |
| debrief/debrief_stats_alltime.bmp | 1B74FB9C5AEE172E3628693F5F7BCC1A98C37ACFB4C1230D6841C15B871C42D7 | 302F454455E80E46E84D4632B6A04C50CBCC1917411680CD51A13277431EE7C5 |
| debrief/debrief_stats_alltime_kills.bmp | 056763AE5167E2FF53A3315C0192668E3D973CD35BED51447411370C2136E7BE | 001285C8FDE27550949F4FD35F6A1B9BC4C6E1C2FD08DA426B2FBB73E137A60E |
| debrief/debrief_stats_help.bmp | 3149317FE7ACAC376B35A22C473DB07C989B15D980090FC016B18FEAFE7CBAC6 | 79D655BD0BA36A34930B77AE28C7E9A749847FDA109ACDA269BEB711C0076DD8 |
| debrief/debrief_stats_kills.bmp | C4FE83485E8C446CF68F514970A167BD0AD1B96B506443437EE46729677E03A4 | 9E47EA22C1064347FE1704A6AAF62476E0817E84B67912189B5807FB22B36D61 |
| main_hall_pilot/barracks.bmp | 8E0F34D997728F29317982BD4F0BAC6AB72A4935D365C5102B02F16A5F4C7EEF | 8E0F34D997728F29317982BD4F0BAC6AB72A4935D365C5102B02F16A5F4C7EEF |
| main_hall_pilot/barracks_help.bmp | 130CAFF89DAF21FE84ADF05BD1FB0980A241D09FF5603DFF2F92FF4A51B73E4A | 130CAFF89DAF21FE84ADF05BD1FB0980A241D09FF5603DFF2F92FF4A51B73E4A |
| main_hall_pilot/hud_config.bmp | 5A2FE908ACB18C0AB6726CC2A0BF8D50036444D6772B236FFB2CB372DB96E3EA | 5A2FE908ACB18C0AB6726CC2A0BF8D50036444D6772B236FFB2CB372DB96E3EA |
| main_hall_pilot/main_hall.bmp | 40AD31619D72F0C2CD0D478771A78BF0B3CC6AC52AE7DCDBEFD7B036722BAFBF | 40AD31619D72F0C2CD0D478771A78BF0B3CC6AC52AE7DCDBEFD7B036722BAFBF |
| main_hall_pilot/main_hall_help.bmp | 227CE667453834C2A5717133DE694C806932E9BDFC7584F0D20EBF4B6AC3812E | 227CE667453834C2A5717133DE694C806932E9BDFC7584F0D20EBF4B6AC3812E |
| main_hall_pilot/medals.bmp | 2904048E3F20967E8B7315F7FD06EF895420A458414BF486D5A642F67BED993C | 2904048E3F20967E8B7315F7FD06EF895420A458414BF486D5A642F67BED993C |
| mission/controls.bmp | E6BBE433DE326C58552AC0937B3A45C0817826C071ABF9C2E0FA0F9361204272 | E6BBE433DE326C58552AC0937B3A45C0817826C071ABF9C2E0FA0F9361204272 |
| mission/help_keys.bmp | 31182E0F2BDA8EE30FCF1999AE9CDEC7AD12CA01023D715791E5DF12DD8DF238 | 31182E0F2BDA8EE30FCF1999AE9CDEC7AD12CA01023D715791E5DF12DD8DF238 |
| mission/help_keys_2.bmp | 782F78DB02D24DBF45EA119661E4FE04FD2884CD7B7B9A45371AC06F154EB15B | 782F78DB02D24DBF45EA119661E4FE04FD2884CD7B7B9A45371AC06F154EB15B |
| mission/help_keys_3.bmp | BF8641435958E62BF3720D7F81E633A85E1E52F6806430CDDE2E2898E2A9B592 | BF8641435958E62BF3720D7F81E633A85E1E52F6806430CDDE2E2898E2A9B592 |
| mission/help_keys_4.bmp | A871AC76B5846F39A0B1AD13A5A18B0A8FAF7DDDFF2B6B86AE77E832ED458494 | A871AC76B5846F39A0B1AD13A5A18B0A8FAF7DDDFF2B6B86AE77E832ED458494 |
| mission/help_keys_5.bmp | 2BBBF7A523CBF387E1985C956AE770E9DC80E5E42E484608685241EA89589E87 | 2BBBF7A523CBF387E1985C956AE770E9DC80E5E42E484608685241EA89589E87 |
| mission/help_keys_6.bmp | A0ACEE172B5591690BFFFDE147CC42B4778E527336377903742BC66EB6261870 | A0ACEE172B5591690BFFFDE147CC42B4778E527336377903742BC66EB6261870 |
| mission/help_keys_7.bmp | FB0D6311B42149423110EAC5D47DE27FA647D8F868A24323B89A68EAD7CCB949 | FB0D6311B42149423110EAC5D47DE27FA647D8F868A24323B89A68EAD7CCB949 |
| mission/help_keys_8.bmp | 3D62847773E903D12B0E07C58BBC559FF00C528C2253BE7912E803FFE1FBE3B9 | 3D62847773E903D12B0E07C58BBC559FF00C528C2253BE7912E803FFE1FBE3B9 |
| mission/help_keys_9.bmp | 371C4C376A118A7484EC6A43F8B0B16DAEE5EFCA9CD68966FB5B64AF9A7F30A6 | 371C4C376A118A7484EC6A43F8B0B16DAEE5EFCA9CD68966FB5B64AF9A7F30A6 |
| mission/hotkeys.bmp | 10A4E949E46EACC1E978F74CA25823C6302C4CCE8D431ADCFF0719503A95B521 | 10A4E949E46EACC1E978F74CA25823C6302C4CCE8D431ADCFF0719503A95B521 |
| mission/hotkeys_help.bmp | 54D1A41196E7A9837E32043DBD6FFD7893195A7A482FA2C2E4A0B64AEC5E5379 | 54D1A41196E7A9837E32043DBD6FFD7893195A7A482FA2C2E4A0B64AEC5E5379 |
| mission/hud_config.bmp | 5A2FE908ACB18C0AB6726CC2A0BF8D50036444D6772B236FFB2CB372DB96E3EA | 5A2FE908ACB18C0AB6726CC2A0BF8D50036444D6772B236FFB2CB372DB96E3EA |
| mission/hud_in_flight.bmp | - | - |
| mission/mission_log_events.bmp | 8FA07FC4B7999DFF46F972DB238294465666844C83F2DE4B7CF9A2D9FBBA1E8C | 8FA07FC4B7999DFF46F972DB238294465666844C83F2DE4B7CF9A2D9FBBA1E8C |
| mission/mission_log_messages.bmp | 45267278A866DF3D56654D4F41480F9315C26BB4B3DF21044671E254F9B224EF | 8F696CF260DFB7168C1F1C79A8F04E7A8C9A6D1BE49B017003BF7BA949351DFF |
| mission/mission_log_objectives.bmp | 29F785FF0E47227FDFC25016294E18160FAFC9DE7C7659A16F700A2190E0C4E4 | 29F785FF0E47227FDFC25016294E18160FAFC9DE7C7659A16F700A2190E0C4E4 |
| mission/options.bmp | E5F23EE6165849D86F2E88D01FAED11A2CA6C3CCC0EE93515A700CABEBE1C74E | E5F23EE6165849D86F2E88D01FAED11A2CA6C3CCC0EE93515A700CABEBE1C74E |
| mission/pause.bmp | - | - |
| options/options_detail.bmp | E1C04CFEFD114E95339E1E56A40933605998AA3B71A3A1DCD167334AF1A28651 | E1C04CFEFD114E95339E1E56A40933605998AA3B71A3A1DCD167334AF1A28651 |
| options/options_general.bmp | E5F23EE6165849D86F2E88D01FAED11A2CA6C3CCC0EE93515A700CABEBE1C74E | E5F23EE6165849D86F2E88D01FAED11A2CA6C3CCC0EE93515A700CABEBE1C74E |
| options/options_multi.bmp | B930AD031B0C820DF2ED5D6F3FC2D2C0CFD3C2917560C036437711158BF6E0A1 | B930AD031B0C820DF2ED5D6F3FC2D2C0CFD3C2917560C036437711158BF6E0A1 |

Unstable: Release 7 of 59, Debug 7 of 59. Release and Debug tour hashes equal on 38 screens.

### Bitmap sizes drawn through gr_bitmap (from `-bmlog` of the tour and the mission)

Bigger than one 256 section in some dimension (all drawn in the tour or the mission): 1024x768 (backgrounds), 937x428, 824x43, 695x141, 629x455, 605x187, 560x368, 534x79, 468x362, 440x200, 332x304, 309x34, 308x44. Smaller ones in the same logs (24x24, 56x24, 128x128, 160x120 ...) take the one-section path.

## Section users (grep: bitmap_section_info, bm_get_section_size, TMAP_FLAG_BITMAP_SECTION, TCACHE_TYPE_BITMAP_SECTION, `sections.`)

| File | What |
|---|---|
| code/GlobalIncs/PsTypes.h:320,336 | `bitmap_section_info` and `bitmap::sections` |
| code/Bmpman/BmpMan.cpp:779-793 | section grid built on load (MAX_BMAP_SECTION_SIZE) |
| code/Bmpman/BmpMan.cpp:1223 | `bm_get_info` returns the sections pointer |
| code/Bmpman/BmpMan.cpp:2619, BmpMan.h:221,345 | `bm_get_section_size` |
| code/Graphics/2d.cpp:1018-1034 | `gr_bitmap` section loop (only when `Gr_bitmap_poly`, which is 32-bit D3D, the default) calls `gr_set_bitmap(.., s_idx, idx)` and `g3_draw_2d_poly_bitmap(.., TMAP_FLAG_BITMAP_SECTION)` |
| code/Render/3ddraw.cpp:1760 | `g3_draw_2d_poly_bitmap` uses the section size |
| code/MenuUI/Barracks.cpp:1296,1301 | `barracks_draw_pilot_pic_row`: section 0,0 with TMAP_FLAG_BITMAP_SECTION |
| code/Graphics/GrD3DRender.cpp:806-819,937-939 | poly path: section tcache type, section size for UVs |
| code/Graphics/GrD3DTexture.cpp:391-411,563,665,944-977,1031-1047 | section texture create, cache, size and release |
| code/Graphics/GrInternal.h:213 | `TCACHE_TYPE_BITMAP_SECTION` |
| code/Graphics/GrSoft.cpp | sets `current_bitmap_sx/sy` only |

Not a section user: `gr_aabitmap`, `gr_aabitmap_ex` (function-pointer macros, no loop; logged at the D3D backend entry), `gr_bitmap_ex` (D3D, no section loop). Callers of `gr_bitmap` are many (menus, loading screens, HUD gauges); the log lists them by call.

## Backends not compiled in the reference build

Glide, OpenGL and DirectDraw backends are already removed from the source (their cases in `gr_bitmap` and `gr_bitmap_ex` are comments). Compiled: GrD3D (the real one), GrDummy (headless), GrSoft (only sets `current_bitmap_sx/sy`; no section loop, so it is not a section path). Sections are live only with `Gr_bitmap_poly`, set from `D3D_32bit` at GrD3D.cpp:2717; `-force_16bit` is the other state.

## Stage 1 review (fresh reviewer, original code at ee9830c, read-only)

Reviewer did not write the logger, the mission or the stage-0 evidence. No code/ file changed. Scope: D-20261029-075125, criteria D-20261029-075126 as amended by D-20261029-094806.

### Verdict

Removing the section loop from the GR layer is safe and can be pixel-identical on D3D, but only if the whole-bitmap draw keeps the section path's texture handling (full-resolution upload, exact texel copy, no filtering, half-texel UV offset). The existing non-section texture path does not do that and must not be used for these bitmaps. Today's sections produce no visible seam and no tile-separate movement; nothing visible is lost by drawing whole.

### What sections do today (D3D 32-bit, the only live path)

- Live only when `Gr_bitmap_poly` (GrD3D.cpp:2717, = `D3D_32bit`); the D3D texture cache is built with sections at GrD3D.cpp:2716 (`d3d_tcache_init(D3D_32bit)`). A 16-bit screen takes `gr_d3d_bitmap` (LFB copy) and never touches sections; GrSoft and GrDummy only store `sx, sy`.
- `gr_bitmap` (2d.cpp:1014-1042): one (x, y) from the caller (already jittered, if at all); 256x256 tiles (last row/column smaller) drawn at `x + x_line, y + y_line`, where `x_line, y_line` are running sums of `bm_get_section_size`. The tiles always exactly cover `[x, x+w) x [y, y+h)` with no gap or overlap, and they cannot move apart: no tile offset depends on anything but the bitmap size. This is the analytic half of D2 (tile union == whole rect, same origin, by construction).
- Per tile, D3D (GrD3DRender.cpp:806-824, 937-943): cache type `TCACHE_TYPE_BITMAP_SECTION`, `TEXTURE_SOURCE_NO_FILTERING` (point sampling), UV `(u + 0.5/sw) * u_scale` with `D3d_rendition_uvs` = 1 (default through `D3DTextureOrigin`, GrD3D.cpp:2835). The upload (GrD3DTexture.cpp:665-683) is a straight row copy of the tile at full resolution into a texture padded to the adjusted size; `u_scale = src_w / tex_w`. Each screen pixel samples exactly one source texel. No filtering means no bleed across a tile edge, so there is no seam today.
- `Barracks.cpp:1296-1301` (pilot pic rows, a Claude addition): tile 0,0 with `TMAP_FLAG_BITMAP_SECTION`, used to get the same unfiltered, full-res, half-texel path for a small bitmap (pilot/squad pics are one tile). `gr_filter_set` is a no-op on D3D (GrD3D.cpp:2378), so the flag is what makes the row unfiltered.
- `g3_draw_2d_poly_bitmap` (3ddraw.cpp:1760): reads `bm_get_section_size` into `bw, bh`, which only the commented-out block below uses. Dead read.
- `bm_get_info(..., &sections)` callers: 2d.cpp:1021, GrD3DTexture.cpp:394, BmpMan.cpp:2631 only. `bm_calc_sections` is called at BmpMan.cpp:852, 1022, 1210. `MAX_BMAP_SECTIONS_X/Y` = 4x3 caps a sectioned bitmap at 1024x768; nothing checks this on load (a wider bitmap would write past `sections.sx[4]`), a latent fault the removal also clears.
- Not section users: `gr_aabitmap`, `gr_aabitmap_ex`, `gr_bitmap_ex`, `gr_d3d_bitmap` (LFB), all 3D texture maps; `b_info.sections` in Weapons.cpp/Beam.cpp is beam sections, unrelated; Network/multi_pinfo.cpp:574,621 are commented-out calls.

### EMP and lightning jitter

- Both sources take one path: nebula bolts call `emp_apply` (NebLightning.cpp:715) exactly as EMP weapons do, so lightning jitter is `emp_hud_jitter` / `emp_should_blit_gauge` at a different intensity. All random draws happen in the caller before the gr call (Emp.cpp:504, 664-665); the section loop draws none. Drawing whole cannot change the cosmetic_rand count or order.
- The jittered HUD is almost all `gr_aabitmap` / `gr_aabitmap_ex` (stage 0: 259112 of 259148 B lines), which never used sections. `gr_bitmap` is 36 lines in the whole mission log; whether any falls in a J frame should be read off the local log. If none does, sections cannot affect HUD jitter in this mission at all, and the jitter criterion reduces to "the aabitmap path is untouched".
- No tile-separate movement exists today (above), so "jitter looks the same" is expected by construction, not hoped for.

### What would change if each bitmap were drawn whole

- Through the existing normal texture path (`d3d_create_texture`, GrD3DTexture.cpp:600-650, default upload 685-716): visibly wrong. That path (1) divides the size by `D3D_texture_divider` and by `16 >> Detail.hardware_textures`, (2) clamps to `D3D_max_texture_width/height`, which is 256 unless the registry value `D3DUseLargeTextures` is set (GrD3DTexture.cpp:1240-1246), and is also capped to the screen width when the driver lacks wide surfaces (1217-1233), and (3) resamples with `du = (bmap_w-1)/tex_w`, which drops or repeats columns even at equal size. A 1024x768 background would become 256x192 nearest-resampled. It also uses `TEXTURE_SOURCE_DECAL` (filtered) and the `flCAP` UV clamp instead of the half-texel offset.
- Through one full-size texture with the section path's upload and UVs: pixel-identical to today, if the device accepts a texture of the padded size (1024x768 needs 1024x1024 with pow2) and the 256 clamp is not applied to it.
- Hence the decision's "D3D may tile internally": with the default 256 clamp, tiling inside GrD3D is still required for every bitmap over 256, i.e. all 13 large sizes in the stage-0 list.

### Risks

1. Max texture dimension (main risk): the 256 default clamp and the wide-surface cap mean a whole-bitmap texture is not available by default. Tiling must stay, inside GrD3D, with the same grid (256, origin 0,0) to stay pixel-identical. Another tile size would probably also be exact under point sampling, but keeping 256 removes the question.
2. Filtering at tile edges: none today (point sampling). If the new path used `TEXTURE_SOURCE_DECAL`, edges and the whole image would blur; keep `NO_FILTERING` and the `0.5/sw` UV offset (sw = tile width, not bitmap width).
3. Texture cache: the tile key is (bitmap, type, sx, sy) via `D3D_last_section_x/y` and `t->data_sections[][]`; children are marked used together (GrD3DTexture.cpp:1012-1024). Keep that mechanism inside GrD3D so eviction order and VRAM use stay the same.
4. RNG draw counts: none in the loop; the logger only counts (below). Trace: the loop is draw-only, no gameplay state, so TraceDiff 0 is expected on all 48; a move is a fault.
5. Build breaks: `bitmap::sections` is in `PsTypes.h` (`struct bitmap`), so every includer recompiles; no `sizeof(bitmap)` use found. `gr_set_bitmap`'s `sx, sy` feed `gf_set_bitmap` in GrD3D, GrSoft and GrDummy; only 2d.cpp:1028 and Barracks.cpp:1296 pass them.
6. Handles: removing sections changes no bitmap loading, so `h=` within a build should not move; if it does, that is itself a finding.

### Logger inertness (D-20261029-094806 item 3)

Read code/Replay/BitmapLog.cpp/.h and the diff e790469..ee9830c. The logger calls only `fprintf`, `bm_get_info` (read), `gr_get_region` (back-buffer read for shots) and file I/O; it makes no `rand` / `frand` / `cosmetic_rand` draw. The only RNG-adjacent edits are (a) a counter increment in `cosmetic_rand` (SystemVars.cpp:288), which runs with logging on or off and does not touch the seeds, and (b) Emp.cpp:504-506 and 663-666, which keep the original single `cosmetic_frand_range` call per site and only log its result. Confirmed inert.

### Criteria checks requested by D-20261029-094806

1. Unstable tour screens (7 per build, 9 names over both builds, excluded from hashing):

| Screen | Unstable in | Cause | Former section user only here? | Substitute coverage |
|---|---|---|---|---|
| briefing/ship_select, _help | Debug | rotating ship model, timed by frame time | background stable only in Release, so none within Debug | Debug: eye check by a non-author |
| briefing/weapon_loadout, _help | Release | weapon animation, timed by frame time | background stable only in Debug, so none within Release | Release: eye check by a non-author |
| campaign_techroom/credits | both | scrolling text, timed artwork change | yes: credits background and artwork at (233,5) (Credits.cpp:663, 720) | eye check by a non-author |
| campaign_techroom/techroom_ships, _help | both | rotating 3D ship | no: same background `TechShipData` as techroom_species / techroom_weapons (stable) | covered by those |
| mission/hud_in_flight | both | live 3D scene | no: HUD draws covered by the 18 mission frames | mission frames |
| mission/pause | both | saved live frame under the overlay | yes: `PleaseWait` overlay (MissionPause.cpp:173) | eye check by a non-author |

   Whether each listed bitmap is over 256 (a true multi-tile user) should be read from the tour `-bmlog` before stage 2; a one-tile bitmap still goes through the changed path, so it still needs the check.
2. Lightning frames: the 18 frames are classified only as "non-zero J"; the log cannot tell lightning from EMP-missile jitter, since both reach `emp_hud_jitter` the same way. By the mission timeline, EMP waves fire in about 25-50 s and 55-85 s, and 85-120 s is storm only, so the late J frames (2804, 2937, 2998) are probably lightning, but that is not shown. Before stage 2: map each J frame to mission time (from `mf` and the replay frame times) or to its `emp_apply` source; if none of the 12 is in the storm-only window, add frames from it to the capture list and take their baselines on ee9830c.
3. Logger: confirmed inert (above).
4. Handles: within-build before/after comparisons keep `h=`; drop it only for Release vs Debug. If handles differ within a build after the change, compare by bitmap filename and say so in this doc.

### Implementation plan (smallest edit, stage 2)

Principle: the GR layer and bmpman stop knowing about sections; the same 256 tile loop moves into GrD3D as a private detail, so D3D draws the same polygons with the same textures and pixels match by construction.

1. GrD3DInternal.h: add D3D-private `D3D_TILE_SIZE 256`, `D3D_MAX_TILES_X 4`, `D3D_MAX_TILES_Y 3`, helpers `d3d_tile_grid(w, h, &nx, &ny)` and `d3d_tile_size(w, h, tx, ty, &tw, &th)` (the `bm_get_section_size` formula), and `void gr_d3d_bitmap_poly(int x, int y)`.
2. GrD3DRender.cpp: implement `gr_d3d_bitmap_poly` with the loop now at 2d.cpp:1016-1041, setting a D3D-private current tile (`D3D_tile_x/y`, replacing `gr_screen.current_bitmap_sx/sy`) and calling `g3_draw_2d_poly_bitmap(..., flag)` as now (3d.h is already included). In the tmapper (806-824, 937-943) use the private tile and `d3d_tile_size` instead of `bm_get_section_size`.
3. 2d.cpp `gr_bitmap`: replace the poly branch body with `gr_d3d_bitmap_poly(x, y); return;` and drop the unused locals. One call per bitmap at the same (x, y) the logger already records.
4. GrD3DTexture.cpp: `bm_get_section_size` (394-411, 958) -> `d3d_tile_size`; `bmp->sections.sx[sx] / sy[sy]` (977) -> `sx * D3D_TILE_SIZE`; `bmp->sections.num_x/num_y` (1046-1047) and `s->num_x/num_y` -> `d3d_tile_grid`; `MAX_BMAP_SECTIONS_X/Y` -> the D3D constants. Keep `TCACHE_TYPE_BITMAP_SECTION` (move it to GrD3DInternal.h) and the `data_sections` cache unchanged. Add an Assert that the grid fits 4x3 (today's silent overflow).
5. `TMAP_FLAG_BITMAP_SECTION`: it means "2D bitmap tile: unfiltered, full-res, half-texel". Smallest: keep the bit, rename it `TMAP_FLAG_BITMAP_2D` in TMAPPER.H; Barracks.cpp:1301 passes it, and its `gr_set_bitmap(..., 0, 0)` drops the last two arguments (tile 0,0 is the default inside D3D; Assert the pic is one tile).
6. Render/3ddraw.cpp:1752,1760: delete `bw, bh` and the `bm_get_section_size` call (used only by commented code).
7. bmpman: delete `bm_calc_sections` and its 3 calls, `bm_get_section_size` (BmpMan.cpp/.h), the `sections` parameter of `bm_get_info` (BmpMan.h:221, BmpMan.cpp:1223,1263), and `bitmap_section_info`, `bitmap::sections`, `MAX_BMAP_SECTION*` from PsTypes.h.
8. `gr_set_bitmap` sx/sy: remove from the inline in 2d.h:675, `gf_set_bitmap`, `gr_d3d_set_bitmap`, GrSoft.cpp:1212-1213, GrDummy.cpp:30, and `current_bitmap_sx/sy` from 2d.h:388-389. (Smaller alternative: keep the parameters and ignore them; stated so the choice is explicit.)
9. Leave the logger as is (the B line at `gr_bitmap` entry is unchanged). Run A-E as amended: within-build frame hashes, D1/D2 logs with `h=` kept, lightning frames added if item 2 finds none.

Rejected: drawing whole through `TCACHE_TYPE_NORMAL` / `XPARENT` (downsampling, resampling and filtering, above); raising the texture clamp to fit whole bitmaps (changes 3D texture sizes everywhere and depends on the driver).
