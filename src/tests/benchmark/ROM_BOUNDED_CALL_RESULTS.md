# Bounded immutable-ROM calls

The fresh V8 census (af2cc71e1) selected call/return dispatch as the next target.
This opt-in stage links a Thumb BL to one existing, unlinked Thumb base function
in the same immutable-ROM WASM module. Only callers with no preceding emitted
memory callback qualify. No game or address selects this optimization.

Original base functions never link; appended caller clones only call bases.
Thus guest self/cyclic calls cannot recurse on the host stack. Both long-call
halfword exits and the removed outer boundary retain budgets, stops and IRQs.
The callee receives the remaining budget, with complete state publication and
reload; its count is added and the outer budget restored before returning.
RAM successors still go through ordinary mapping/lifetime checks. Caller
prefixes with memory callbacks keep the original synchronization boundary.
BLX and indirect calls retain ordinary dispatch. This first stage links 313
sites among 5,614 Thumb bases in the reference ROM. Generated ROM module size
increases from 12,620,800 to 12,979,779 bytes (2.84%). That is static coverage,
not a claim about dynamic dispatch reduction or speed.

Validation: all 184 compiler tests pass, including 3,645 new full-state,
memory, budget, stop, IRQ, callback and self-call comparisons. Production
native/WASM probes pass 1,536 new ROM-call cases across modes 0/3 and both TLB
layouts, 4,032 ARM fault cases, 2,880 Thumb memory-fault cases and 1,152 long-call
cases. All three native CTest targets pass. Both Sky Force normal/checked
routes, the matching Sky control, and both Snakes normal routes match native images, frame records and
PCM exactly. Runtime option readback is 1 and post-initialization changes are
rejected. Test artifact hashes match the frozen production source archive.

Two probe orchestration errors are retained: argument order initially left an
option for the IR parser, then the comparator did not yet allow the new 384-case
fixture. Corrected argument order and the explicit allowed count resolved these;
comparison semantics and production binaries were unchanged. The new linked-call
probe was subsequently expanded to print every modified byte in addition to its
memory hash; all 1,536 cases were freshly rerun from the separately frozen
v17-exact-memory-probes archive. The compiler suite, game and other probe
artifacts were unchanged and their checks are reused. Existing completed
probe outputs were reused rather than rerun for the comparator-only correction.

The fixed sixteen-observation same-binary panel reverses orders for both Sky
Force and both Snakes routes. All original limits remain. No speed result is
claimed here. The option defaults off, live is unchanged, and the realtime goal
is open. No push or deployment.
