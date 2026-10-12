# Fix review: beam-fire reads Sexp_nodes[-1] when no target subsystem is given

**Recommendation: TAKE.** Low risk.

Found by AddressSanitizer in the campaign autotest: `CTEXT` reading before `Sexp_nodes[]`, and `ship_get_subsys` comparing the text found there against subsystem names.

## The bug
The target subsystem argument of the `beam-fire` SEXP is optional:
```cpp
fire_info.target_subsys = ship_get_subsys(&Ships[sindex], CTEXT(CDR(CDR(CDR(node)))));
```
When it is left out, `CDR(...)` is -1 and `CTEXT(-1)` reads `Sexp_nodes[-1]`: memory before the array. If that memory has the variable flag set, `CTEXT` also indexes `Sexp_variables[]` with whatever `atoi` makes of it. The text found is compared against every subsystem name of the target ship.

## Proposed fix
Only look the subsystem up when the argument is there:
```cpp
if (CDR(CDR(CDR(node))) >= 0) {
    fire_info.target_subsys = ship_get_subsys(...);
}
```
`target_subsys` stays `NULL` otherwise, which is what the code already set it to.

## Effect on mission flow
None in practice. The text read was never going to be a subsystem name, so the lookup returned `NULL`, as it does now.
