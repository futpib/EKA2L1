# Recurrent literal-load fallback: data-TLB collision observed

The bounded diagnostic zero_literal_diagnostic.patch records 64 sampled literal
loads after zero-progress compiled returns. Every sample attempts a read in
0x70000000, while the selected TLB slot holds read/write page 0x00400000. Both
map to slot zero under the current 512-entry low-page-bit index. The host pointer
and AOT TLB are present, little-endian mode is active, exit flag is zero, and
remaining budgets are nonzero. The dominant site is 0x700002b8.

This identifies an actual mapping-cache conflict at the observed fallback sites;
it does not establish that every zero-count return has this cause. The 64 records
are bounded deterministic samples rather than a uniform sample of all accesses.
Inspection reads code through resolve_code and TLB metadata only, without reading
guest data or repairing a miss. Profiling perturbs execution. The diagnostic long
route retains the exact 2,987,830,398 guest instructions and 720 presentations.
Its elapsed time is not promotion evidence, and the diagnostic is not deployed.

A general next experiment folds higher virtual-page bits into the fixed-size
TLB index, consistently in insertion, lookup, invalidation and generated guards.
It must retain permission tags, callback/remap behavior and existing native-JIT
index contracts. No game PC or page address is to be special-cased. A collision
can move elsewhere, and extra hash operations can cost more than saved misses;
correctness and whole-game timings are required.
