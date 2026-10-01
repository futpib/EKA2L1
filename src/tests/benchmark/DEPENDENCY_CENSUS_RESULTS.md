# Code dependency overlap census

A separate diagnostic counts primary and inlined-dependency spans before exact
validation on the established longer-snake route (guest seconds 42–60). Compiler
policy 7, folded TLB 1, scanner 2 and original lookup 0 are explicitly verified.

The 154,157,420 validation requests cover 10,133,999,836 primary bytes and
321,012,804 dependency bytes. Dependencies are 3.07% of requested byte coverage.
The union of backing spans within each version finds **zero duplicate bytes**
weighted by actual validation requests. The compiler already deduplicates leaf
dependencies with identical guest addresses. Merging overlapping spans therefore
has no measured duplicate work to remove in this scene.

The histogram reconciles with the total requests. Guest work remains exactly
2,987,830,398 instructions and 720 presentations. This is a request census, not
a fresh correctness replay or a timing comparison. It includes bytes that a
failed validation could avoid reading through early exit. All diagnostic wall
times are excluded from speedup evidence. No validation behavior was changed.

The diagnostic patch, archive hashes and raw report are retained. Its three
instrumented sources were restored before continuing. The served build remains
the verified grouped-scanner archive. No optimization was justified by this lead.
