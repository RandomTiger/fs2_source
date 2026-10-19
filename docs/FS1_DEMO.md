# FreeSpace 1 demo data set

The FS1 demo as a data set (`DATA_SET_FS1_DEMO`, `-dataset fs1demo`): getting its files out of the original
installer, recognising them, and what the engine lacks to play them. Branch `FS1Demo`, off `FS1Compat`, with
`origin/FS2Demo` merged in once for the data-set code (`docs/FS2_DEMO.md`).

Nothing from the demo is committed. Its `readme.txt` licence forbids redistributing it, as the FS2 demo's
does; the test extracts from the installer at run time into a temporary folder.

---

## 1. The installer

`fsdemo10.exe` (26,742,784 bytes) is a WinZip self-extractor, like the FS2 demo's: a Windows program with a
zip appended. The zip holds a 16-bit InstallShield 5 setup (`SETUP.EXE`, `_INST32I.EX_`, `_SETUP.DLL`,
`setup.ins`, `_sys1.cab`, `_user1.cab`, and so on) and the game, in one cabinet:

| zip entry | size | holds |
|---|---|---|
| `data1.cab` | 25,974,157 | the game, as an InstallShield 5 cabinet |
| the rest (15 entries) | under 0.6 MB together | the setup program and its scripts and pictures |

`data1.cab` is version 5 (`ISc(`, `0x01005100`) with its header inside the cabinet (no `data1.hdr`). It holds
6 files, all compressed:

| file | size | taken |
|---|---|---|
| `Data\freespace.vp` | 37,933,752 | yes: every table, mission, model, sound and picture the demo has |
| `FS.exe` | 1,541,120 | no: the original demo game |
| `FreeSpace.exe` | 956,416 | no: the original launcher |
| `fsuninst.dll` | 135,168 | no |
| `readme.txt` | 17,916 | no |
| `refcard.rtf` | 6,633 | no |

A file's data is a run of chunks, each a 16-bit length and that many bytes of raw deflate, ended by a sync
flush rather than a final block, and no chunk records its output size. The cabinet keeps no checksum per file
(per-file MD5s came with later versions), so a file is accepted when every chunk decodes, the chunks use up
exactly its compressed size, and the output is exactly its expanded size. The cabinet itself is covered by
the zip's CRC.

The 16-bit setup cannot run on 64-bit Windows, so there is no "what the real installer produces" to compare
with. The test compares against an independent decoder instead (section 3).

## 2. Code

| file | what |
|---|---|
| `code/CFile/CabReader.h/.cpp` | InstallShield 5 cabinet reader: file list (`cab_open`), one file (`cab_extract`). Refuses split, obfuscated and multi-volume files rather than guessing |
| `code/CFile/DataSetFs1.h/.cpp` | `dataset_fs1_detect(dir)` and `dataset_fs1_extract(exe_dir, err, err_len)` |
| `code/CFile/DataSetInstall.h/.cpp` | shared with the FS2 demo's extraction (Tom's OK, 2026-10-19): `dataset_inflate` (the inflater, now reporting how much it wrote and accepting a sync-flushed stream), `dataset_crc32`, and `dataset_zip_read` (one zip entry into memory). The FS2 path is unchanged: `inflate_buffer` is a wrapper with its old behaviour, and `zip_extract` is untouched |
| `tools/fs1demo_test.ps1` | the test (section 3) |

**`int dataset_fs1_detect(char *dir)`**: 1 if `dir\Data\freespace.vp` exists and its directory lists
`demo.fsc`. FS1 retail has a `Data\freespace.vp` too, without `demo.fsc`.

**`int dataset_fs1_extract(char *exe_dir, char *err, int err_len)`**: reads `data1.cab` out of
`exe_dir\fsdemo10.exe` (CRC checked), takes the cabinet's `Data\` files and any top-level VP into
`exe_dir\fs1demo.partial`, and renames that to `fs1demo` only when every file is whole. An existing `fs1demo`
is never written to. 0 on success; otherwise non-zero and the reason in `err`.

**For the two hooks in `DataSetInstall.cpp`** (fs2-work-manager's): `use_dataset()` checks `folder_has_vps()`,
which looks for VPs at the top of the folder. The FS1 demo keeps its only VP in `Data\`, so for
`DATA_SET_FS1_DEMO` that check should be `dataset_fs1_detect()`, and `zip_extract()` should be
`dataset_fs1_extract()` (with `can_extract` set to 1).

## 3. Test

`tools\fs1demo_test.ps1 -Installer <fsdemo10.exe> [-Fs2Installer <fs2demo10.exe>]` builds a small program from
the files above, then:

1. lists and extracts every file in `data1.cab` with the game's reader;
2. extracts them again with an independent reader (.NET's zip and deflate, with the cabinet layout read in
   C#) and compares the two, file by file, by size and SHA-256;
3. runs `dataset_fs1_extract` into a fresh folder: `fs1demo` must hold exactly `Data\freespace.vp`, identical
   to the reference, with no `fs1demo.partial` left; `dataset_fs1_detect` must accept it and refuse FS1
   retail and an empty folder; a second extract must refuse the existing folder;
4. with `-Fs2Installer`, makes the FS2 demo's folder through `dataset_select_folder` and compares every file
   with .NET's extraction of the same zip entries, to show the shared inflater left the FS2 path working.

Result on 2026-10-19: all 20 checks pass.

## 4. What the demo contains

One archive, `Data\freespace.vp` (947 entries, 905 files). FS1 retail splits its data over `root.vp` and
`Data\freespace.vp` (3,065 files); the demo is a cut-down copy of the same game, with a few files of its own.

| folder | files | notes |
|---|---|---|
| `effects` | 41 | |
| `fonts` | 4 | `font01.vf` differs from retail; `font01.pcx`, `font02.pcx` (the fonts' source art) are demo-only |
| `hud` | 60 | 4 shield anims differ from retail |
| `interface` | 354 | 348 of retail's 982 (below); demo-only: `DemoTitle1`, `DemoUpsell1`-`4`, `OptionsMainPalette` |
| `maps` | 61 | 17 differ from retail |
| `missions` | 5 | `demo.fsc`, `btmdemo.fsm` (training), `demo01.fsm`, `demo02a.fsm`, `demo02b.fsm` |
| `models` | 42 | `cruiser01.pof` differs from retail |
| `music` | 16 | 4 differ from retail |
| `players\images` | 8 | pilot pictures |
| `sounds\8b22k`, `16b11k` | 59, 35 | 13 differ from retail |
| `tables` | 15 | below |
| `voice` | 205 | briefings, debriefings, training, special, personas; 31 briefing and debriefing files are demo-only |

**Tables** (all but `menu.tbl` encrypted with FS1's old scheme, `0xDEADBEEF`, which `FS1Compat` already reads):
`ai`, `asteroid`, `credits`, `fireball`, `icons`, `medals`, `menu`, `messages`, `music`, `rank`, `ships`,
`sounds`, `tooltips`, `traitor`, `weapons`. Retail's `cutscenes.tbl` and `species.tbl` are missing. Nine
differ from retail's: `ships.tbl` has 13 ships (retail 67), `weapons.tbl` 15 weapons (retail 45), `medals.tbl`
16 medals as in retail; `credits.tbl` has no `#end`, like retail's.

**Missions**: FS1's mission format (`$Version: 0.10`), encrypted like the tables. The campaign `demo.fsc`
("Freespace Demo Campaign", single player, starting ship GTF Apollo) runs `btmdemo` (until its "Flight
Training" goal is met), then `demo01`, then `demo02a` or `demo02b`. Its mission names have upper-case
extensions (`btmdemo.FSM`).

**Pilots**: none ship; the demo makes them when played. The original demo's `.plr` files are FS1's format,
which this engine does not read (FS1 retail is the same: `FS1Compat` plays with an FS2 pilot).

## 5. Gaps against the engine

What the engine reads that the FS1 demo lacks or has in another form, assuming `FS1Compat`'s FS1 support.
No game code is changed for these on this branch.

1. **Data set identification.** `cfile` sets `Cfile_fs1_data` from a `Data\freespace.vp` pack, so the demo
   takes FS1's loading paths (table conversion, old decryption, `.fsm`/`.fsc`, missing-table stand-ins). Code
   that must tell FS1 demo from FS1 retail needs the data set itself (the profile table's FS1 rows).
2. **Folder check in `use_dataset()`**: see section 2; `folder_has_vps()` does not see `Data\freespace.vp`.
3. **Screens with no art.** Of retail's interface pictures, the demo lacks the tech room (`tech`, `techdata`,
   `techshipdata`), campaign room (`campaign`), simulator (`readyroom`), load mission (`loadmission`),
   credits (`credits`, `creditswin*`, `creditsart*`), medals (`medal*`, `medalsdisplayempty`), hotkeys
   (`hotkeys`), red alert (`redalert`), training menu (`training`), the second main hall (`mainhall2`), the
   multiplayer screens (`multi*`, `mp*`, `pxo*`), the help overlays (`*_help*`), the debriefing's award, medal, rank and badge pictures
   (`debrief*`), and the loading pictures (`load*`). The demo must not reach those screens, as the FS2 demo's
   restrictions keep the FS2 demo from its missing ones; FS1's `DEMO` blocks (held under D-064) are where FS1
   did this.
4. **Upsell and title screens.** FS2's demo code shows `UpSell01`, `UpSell02` and FS2's title screen; the FS1
   demo's are `DemoUpsell1`-`4` and `DemoTitle1`.
5. **Missing tables.** `cutscenes.tbl` (the cutscene screen, which the demo lacks) and `species.tbl` (only the
   tech room reads FS1's). Neither screen is reachable in the demo (item 3).
6. **Smaller tables.** 13 ships and 15 weapons. FS1 retail's table conversion applies; whether anything the
   engine looks up by name (a default ship, a weapon in `sounds.tbl` or `icons.tbl`) is absent is not checked
   yet.
7. **Missions and campaign.** Same format as FS1 retail, so `FS1Compat`'s mission loading applies; none of the
   four missions has been loaded yet.
8. **Pilots.** None shipped; the engine makes its own.

## 6. Not done

- The hooks in `DataSetInstall.cpp`'s data-set table and `use_dataset()` (fs2-work-manager's).
- Loading or playing anything from the demo (items 3 to 8 above).
- `cab_open` reads only what this cabinet uses; other InstallShield versions are refused.
