# Direct RAM Thumb store continuation

The opt-in Thumb path can now continue past a RAM store when mode 3 is selected
and every memory access so far used the direct TLB path. No game or address
identity selects it. Mode 3 already accepts immutable executable bytes.

Each former store boundary retains the original 64-bit stop and pending unmasked
IRQ checks. Any memory callback, including a preceding read, marks the path so
it returns at the original store boundary. The runner then retains its normal
successor mapping/lifetime validation. Each direct access still checks its own
TLB permission, alignment and endian state; no span proof survives an instruction
or callback. Modes 0/1/2 keep their old store boundaries. Execution limits,
instruction budgets and guest scheduling are unchanged.

The candidate removes the unpromoted V6 prefix-publication stage and uses the
accepted V5 source behavior as its comparison baseline. V6 evidence is retained.
The frozen archive records the exact source patch from its build HEAD; subsequent
document-only commits do not change that source attribution.

All 180 compiler tests pass. The new 3,207-case test covers scalar and register
stores, full state and memory, both TLB layouts, all four byte policies,
permissions, endian modes, short budgets, 64-bit stops, IRQ masks, and read/store
callback changes. All 1,152 native call comparisons and 2,880 native memory-fault
comparisons pass. Normal control/candidate and checked stationary Sky Force,
both Snakes routes, and normal/checked moving-and-firing Sky Force match native
images, guest records and PCM exactly. Selected checked invocations force
callbacks; normal replays and unit/fault matrices independently cover direct
memory.

A diagnostic build outlines the compiled runner solely to separate its samples
from InterpreterMainLoop. That extra call is not a performance candidate. The
normal frozen V7 archive is compared with frozen V5 in the subsequent eight-run
serial screen, with opposite orders across four routes. Both use Thumb memory,
ROM regions off, original limits, shared audio and physical GPU rendering without
capture or detailed counters. One pair per route remains exploratory. The
realtime goal is still open; the option remains off by default and unserved.

## Runner diagnostic limitation

The temporary no-inline diagnostic was built and profiled after correctness and
before timing. Its measured guest work is unchanged (2,142,147,961 instructions,
192 presentations), but it did not expose an execute_chain_impl frame in the
CPU-worker profile; that name is also absent from the diagnostic WASM name bytes.
InterpreterMainLoop still owns 33.75% of self samples. This does not separate
compiled-runner cost from interpreter fallback. The result and temporary patch
are retained, and the patch was removed before committing the normal candidate.
Its sampled 16.62-second window is excluded from all speed comparisons.
