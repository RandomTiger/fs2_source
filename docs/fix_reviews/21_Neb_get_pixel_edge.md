# Fix review: neb2_get_pixel reads past the end of the nebula pre-render

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer: 45 reports across the campaign autotest, in nebula missions, reading just past the end of `tpixels`.

## The bug
The nebula background is pre-rendered into a 32x32 image, `tpixels`. Polygons are fogged by averaging the pixel under a screen point and its neighbours:
```cpp
xs = (int)(ex_scale * x);
ys = (int)(ey_scale * y);

// sometimes goes over by 1 in direct3d
if(ys >= (this_esize - 1)){
    ys--;
}

bm_get_components(&tpixels[PIXEL_INDEX_SMALL(xs, ys)], &rv, &gv, &bv, NULL);
```
`ys` is pulled back once when it goes over, but `xs` never is. A point at the right edge of the screen (x equal to the screen width) gives `xs` 32, one past the last column, and at the bottom-right corner that is past the end of the image. Points off the screen can be further out, or negative.

## Proposed fix
After the existing `ys` adjustment, clamp `xs` and `ys` to 0 to 31. The neighbour reads already check against the edges, so they stay inside the image.

## Effect on mission flow
None. Only the fog colour of polygons at the edge of the screen can change.
