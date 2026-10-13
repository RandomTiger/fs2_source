# Fix review: D3D textures default to a quarter of their size

**Recommendation: TAKE.** Low risk. Visual only.

## The bug
`d3d_tcache_init` (`code/Graphics/GrD3DTexture.cpp`) sets the texture size divider from the `D3DFast` registry value:
```cpp
uint tmp_val = os_config_read_uint( NULL, NOX("D3DFast"), 1 );
D3D_texture_divider = 1;
if(tmp_val){
    D3D_texture_divider = 4;
}
```
`d3d_create_texture` divides every texture's width and height by `D3D_texture_divider` (AA bitmaps excepted). So when the registry has no `D3DFast` value, every model, effect and background texture loads at a quarter of its width and height. The launcher and the GOG setup (`Registry\FS2.reg`) both write `D3DFast=0`, so a normal install never sees it. A hand-made setup or a cleaned registry does, with nothing to say why.

The earlier code, which picked the divider from free texture memory, is still there commented out. `D3DFast` replaced it for 1999 cards, where 1 was the safe default.

## Proposed fix
Default `D3DFast` to 0 when the value is missing. An explicit `D3DFast=1` still quarters the textures.

## What was checked
- `D3D_texture_divider` is only read in `d3d_create_texture`, and only for texture size.
- On the test PC the launcher key has `D3DFast=0`, so the default is not reached there. The replay check below shows the change has no side effect. It cannot show the size change itself.

## Not changed
- The texture detail setting (`Detail.hardware_textures`), which shrinks textures further at lower settings.
- The commented-out memory-based divider.

## Effect on mission flow
None. Texture size is not gameplay state, and texture loads are not recorded. Installs with `D3DFast` set behave exactly as before.
