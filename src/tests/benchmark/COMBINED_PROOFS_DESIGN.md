# Combined write-span and budget proofs

Experimental policy 7 combines policies 5 and 6: selected reads/writes through
unchanged address registers plus 4–32 instruction budget chunks. It uses original
integer and memory instruction emitters, not the general IR. Existing policies,
ordinary defaults and the served policy remain unchanged.

Write proofs still require permissions, non-wrapping physical spans and exclusion
of code aliases. Loads/stores execute in order. Helper exits prevent using a
proved pointer after callback remapping. Budget chunks omit only proved internal
budget comparisons; instruction counts and applicable exit/interrupt checks
remain. A short budget publishes exact state, invokes the private precise
compiler and returns from the whole region. Failed entry memory proofs use the
original compiler. See INVARIANT_WRITES_DESIGN.md and BUDGET_CHUNKS_DESIGN.md.

The combination is not assumed additive: it changes hot code, proof costs and
fallback/module size. Same-binary policies 6 and 7 plus the exact served archive
will be timed serially only after correctness passes. Samples are retained.

The complete write and budget matrices run again for policy 7 and assert that
both mechanisms are actually selected. This covers physical aliases, conditional
stores, loop boundaries, short budgets, permissions and exact state/progress.
Rebuilt production fault fixtures require three write proofs on callback-remap
cases. Full package tests, native/frontend checks and exact native image/audio
replay remain required. No performance or deployment claim follows from this
implementation alone.
