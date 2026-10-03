# Generic instruction-count batching

`EKA2L1_AOT_IR_MODE=18` extends policy 17 with deferred instruction accounting
through ARM regions. It has no game, DLL, address, loop-size or minimum-length
selection rule. Budget proofs retain their existing eligibility rules; batching
also applies outside those proofs and in their precise fallback functions.
Policy 17 remains available as the direct comparison.

The compiler represents progress as a runtime base plus a compile-time offset.
An instruction advances the offset without emitting a counter update. Budget
guards and returns read the exact sum. A cold memory rejection or unsupported
instruction still rolls back the current instruction before returning.

Internal branch edges materialize their own count. A conditional branch does
not discard the compiler offset needed by its fallthrough path. Fallthrough
materializes before closing a target label, so incoming branches cannot inherit
instructions they skipped. Backedges and loop exits use the same mechanism.
Predicated instructions, flattened inline calls and sequences spanning several
budget chunks retain their exact dynamic counts. Short ARM and Thumb blocks
already use constant counts and need no dynamic-counter batching.

Budgets, interrupt checks, callback state publication, memory permissions,
code-write handling and guest scheduling are unchanged. This optimization does
not reserve an entire batch unconditionally or charge instructions past a fault.

The focused tests compare counts, all state fields, memory and helper calls with
policy 17 across two-instruction blocks, joins, loops with interior entries,
conditional exits, unsupported instructions, several budget chunks and memory
fallbacks. Existing interpreter comparisons also run policy 18 through loop,
chunk, inline-call, predication, callback, syscall and interrupt matrices.

In the captured EUser queue traversal, seven increment-by-one assignments become
one increment-by-seven on the backedge or the fallthrough path. Its cold memory
exit keeps the precise rollback. This is emitted-WASM evidence, not a count of
V8 machine instructions or a speedup claim. V8 may already simplify some of the
previous increments, so game throughput must decide adoption.
