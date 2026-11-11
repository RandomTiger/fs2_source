# Tools standard (VS2022 and every layer above)

The solution builds the game and its tools: FRED2, Pofview, Nebedit, Fonttool, Cryptstring,
Scramble, Cfilearchiver and Ac. Each tool that links into the game install takes a per-layer
name from `build/layer.props`, for example `Fred2_VS2022r.exe` / `Fred2_VS2022d.exe`.

## What is checked

- **Every project compiles**, Release and Debug, x86 (D-20261017-224309).
- **The tools run in Release x86 against retail FreeSpace 2 data (`PATH_FS2`) only.** The FS2
  demo, FS1 and FS1 demo folders are out of scope; a tool failing there is not a break
  (D-20261107-101601).
- **Debug tools are compiled but not run-checked** (D-20261107-101605). Their known asserts
  are listed on issue #234.
- **Software views draw what the released source drew** (D-20261107-104005,
  D-20261018-022937). There is no textured or scaled-bitmap drawing, because the released
  source's software texture mapper was never linked. Textured preview is Tom's option on
  issue #13. A view that used to draw lines and now draws nothing is a regression.

## What each tool's view shows

Checked on 2026-11-07, Release x86, retail data:

| Tool | View |
|---|---|
| FRED2 | The grid, ship and waypoint icons and their labels draw. Ship models are not drawn textured. |
| Pofview | Opens a model from the command line. The model's textured polygons draw nothing, so the viewport is black by default. Its line and flat overlays draw (radius sphere, pivots, turned on from their menu commands). |
| Nebedit | The nebula grid and the status text draw. |
