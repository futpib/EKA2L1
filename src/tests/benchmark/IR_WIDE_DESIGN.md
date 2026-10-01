# Wide values across guarded IR memory

The opt-in mixed IR now accepts existing i64 long-multiply and accumulate nodes
inside a segment. A pack of the low and high halves of the same value resolves
to that original wide value, so compatible accumulators remain wide across
intervening integer and memory instructions. The original compiler still runs
short budgets and unsupported operations. IR options remain OFF by default.

Each segment reserves separate i32 and i64 value slots. Nonoverlapping segments
share their maximum slot counts. Architectural cache locals are allocated lazily
between the i32 values and the i64 suffix, so fixed-width local-index operands
are patched once the final cache size is known, before cache barriers alter byte
offsets. This leaves the generic emitter's reserved wide local independent.

All guard snapshots participate in liveness. Low/high extracts used only by
intermediate exits become cold reconstruction recipes: their wide source remains
live, and the extraction is emitted when that exit needs it. Ordinary consumers,
including memory addresses and stored values, still compute their inputs on the
successful path. No products or memory effects are recomputed by these recipes.
Snapshot assignment consumes all sources before writing any destination.

The existing mapping, endian, alignment, span, physical code-write and budget
checks remain. Failed guards publish the exact pre-instruction snapshot and
return the count of completed instructions. Generic fallback wide state is
materialized before its join with the optimized path.

Validation expands exact interpreter comparisons with signed/unsigned multiply
and accumulate, source/destination overlap, loops, supported inlined calls,
partial budgets, and failed memory accesses after several wide updates. A new
production-runner `--ir-wide` fault mode compares real callback state with native.
Long-multiply leaves themselves remain excluded by the existing leaf selector;
a rejected new fixture that assumed otherwise was corrected without expanding
that selector. No performance claim follows from wider IR coverage alone.
