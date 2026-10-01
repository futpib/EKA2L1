# Adjacent single-use pure IR values

Opt-in policy 15 uses policy 13 semantics and its 32-instruction segment bound.
It folds a pure value into its immediately following pure consumer when the
value has exactly one graph use and is not an architectural exit root.
Required final and memory-failure snapshots participate in use counting.
Cold recipes, reads, writes, guards, state reads, constants and host proofs are
not candidates. No memory effect, guard or state publication crosses the move.

Lowering recursively emits selected operands in their original operand slots,
including the third operand of a select. Shared expressions stay in locals.
Selected nodes receive no i32/i64 slot; the existing suffix fixups and parallel
snapshot publication remain in use for all other nodes. Cold recipes retain
their independent per-exit memoization. The source graph and ARM semantics do
not change.

The two captured busy-loop fixtures select 11 and 30 nodes. This establishes
coverage, not machine-code savings or game throughput. V8 may already eliminate
some local traffic; smaller WASM is insufficient evidence of a gain.

Acceptance adds policy 15 to the full conditional and long-graph differential
matrices and explicitly runs the native callback/fault matrix under policy 15.
Those cover flags, predicates, shared arithmetic, register swaps, wide results,
overwritten source memory, every partial budget, remapping and precise faults.
Full checked image/audio replay and same-application serial timing remain gates.
No default selection or deployment change is part of this experiment.
