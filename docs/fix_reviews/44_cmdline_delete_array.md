# Fix review: command-line argument text freed with `delete` instead of `delete []`

**Recommendation: TAKE.** No gameplay or recording can change. Steward decision D-20261017-223659 (under Tom's delegation, D-20261017-223433). GitHub issue #7.

## The bug
`Cmdline/cmdline.cpp`, `cmdline_parm::parse`: the argument text of a command-line parameter is allocated as an array,
```cpp
parm->args = new char[size];
```
When the same parameter appears again and the newer one replaces it, the old text is freed with scalar `delete`:
```cpp
if ( parm->args != NULL ) {
    delete( parm->args );
    parm->args = NULL;
}
```
The destructor `cmdline_parm::~cmdline_parm` frees the same member correctly with `delete [] args`.

## Developers' intent
Free the old text before replacing it. The destructor shows the intended form.

## Proposed fix
`delete [] parm->args;`, matching the allocation and the destructor.

## What was checked
- Mismatched `new[]`/`delete` is undefined behaviour. With MSVC and a `char` array both free the same block, so the original build did nothing visibly wrong. It only runs when a parameter is repeated on the command line, which no recording does.
- The change is confined to command-line parsing at startup. It reads no game state, draws no random numbers and makes no recorded reads.
- Found by the new/delete survey for routing C++ allocation through the memory wrapper (D-20261017-223659), which will make Debug builds see every `new`/`delete`.

## Effect on mission flow
None. **No recording changed.**
