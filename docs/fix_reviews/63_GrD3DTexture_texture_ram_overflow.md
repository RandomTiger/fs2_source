# Fix review: 2 GB or more of free video memory wraps the texture budget negative

**Recommendation: TAKE (Tom, 2026-10-29, OriginalSourceBugFixes for the overflow; removing eviction and the glow-map cap goes on Enhanced).** `d3d_tcache_init` stores DirectDraw's free texture memory, a `DWORD`, in `int D3D_texture_ram`. Any card with 2 GB or more free wraps it negative, so texture preloading is switched off and the out-of-memory eviction would free every texture. Found while looking at #104.

## The bug
`code/Graphics/GrD3DTexture.cpp`, `d3d_tcache_init`:

```
		HRESULT ddrval = lpDD->GetAvailableVidMem(&ddsCaps, &dwTotal, &dwFree);
		...
		D3D_texture_ram = dwFree;
		...
			if ( D3D_texture_ram >= 6*1024*1024  )	{
				D3D_should_preload = 1;
```

On DESKTOP-MAIN a 32-bit probe making the same call (`DDSCAPS_TEXTURE`, IDirectDraw2) gets `free=4275087360` (4077 MB), which as an `int` is -19879936. With `D3DPreloadTextures` unset (the default, 255) the 6 MB test fails and `D3D_should_preload` is 0.

`d3d_free_some_texture_ram` caps its goal at `D3D_texture_ram*3/4`, which is negative too, so on `DDERR_OUTOFVIDEOMEMORY` it frees every texture.

## Fix
Cap the stored value at 512 MB (`0x20000000`) before the cast. 512 MB still passes the 6 MB preload test, and `D3D_texture_ram*3` stays inside an `int`, so the eviction goal cannot overflow either (a 2 GB cap would). The change carries a `// Claude:` comment.

## What was checked
- The probe, above. `Videocard` in the registry is Direct3D, so every test run takes this path.
- Builds: Release and Debug, on RecordPlayback after the merge.
- `rp_gate.ps1` and all 49 recordings in Release and Debug on RecordPlayback.

## Not changed
- The `TEXTURE RAM` mprintf still prints `dwFree` with `%d`; it only goes to the debug log.
- The eviction code and its whole-cache flush stay here; Enhanced removes them.

## Effect on mission flow
Textures are preloaded at level load again, as on the cards of the time, instead of uploading on first draw. Gameplay is unchanged: the recordings replay identically.
