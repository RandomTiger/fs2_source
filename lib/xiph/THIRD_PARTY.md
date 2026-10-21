<!-- Claude: written for the OgvMovies branch (D-20261020-222203) -->
# Third-party code in lib/xiph

Unmodified Xiph.Org Foundation sources, used by `code/Movie/Movie.cpp` to play `.ogv` cutscenes. Built by `Xiph.vcxproj` into `xiph.lib`.

| Library | Version | Licence |
|---|---|---|
| libogg | 1.3.6 | BSD-3-Clause, `ogg/COPYING` |
| libtheora | 1.2.0 | BSD-3-Clause, `theora/COPYING` |
| libvorbis | 1.3.7 | BSD-3-Clause, `vorbis/COPYING` |

Rules
- Nothing in these folders is edited; an upgrade replaces the folder whole and updates this table.
- Their headers are included as external (`/external:I`, `/external:W0`) by the `Movie.cpp` entry in `code/code.vcxproj`, so their warnings do not count against the project's warning check; `Xiph.vcxproj` itself is excluded from that check by name.
- `Xiph.vcxproj` is excluded from the x64 warning pass (D-20261020-222202).
