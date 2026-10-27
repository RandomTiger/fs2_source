# Intended gameplay changes

Changes that make the game play differently from the original on purpose. Bug fixes in the original code are not listed here; they have review docs in `docs/fix_reviews` on `OriginalSourceBugFixes`. Each entry names its branch and document, and the recordings it changed.

| Date | Branch | Change | Document |
|---|---|---|---|
| 2026-10-15 | `DebrisOffscreenFix` (off `PrePort`) | A capital ship that breaks up leaves its debris whether or not it is on screen; the debris slot pool is 128 instead of 64 | `docs/DEBRIS_OFFSCREEN_FIX.md` |
| 2026-10-27 | `P7` | The squad message menu (C key) runs its logic in `HUD_update_2d` and only draws in `HUD_render_2d`, so it works with `-nodraw`. A key that opens a menu built on its first frame now shows that menu one frame earlier (no empty menu frame). No change to what the keys do | P7-7 commit message |
