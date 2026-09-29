# Guarded value IR prototype

This is a production-runner experiment, not an accepted performance change.
Enable it explicitly with `-DEKA2L1_WASM_REGION_IR=ON`; the default is OFF.
The current compiler remains the precise fallback. It follows the structural
compiler direction discussed on 2026-09-29; measured results and limitations
are recorded in `REGION_IR_RESULTS.md` and `REGION_IR_EVIDENCE.json`.

## Values, effects and exits

`region_ir.h` represents typed i32/i64 values, architectural state inputs,
constants, integer operations, wide packing/extraction and ordered word memory
effects. Every guest instruction boundary records all registers, NZCV/T flags,
PC and completed instruction count. Value numbering and constant propagation
operate only on pure nodes. Pack/extract identities can keep an accumulator
wide across several instructions. Dead-value analysis treats selected exit
snapshots and every memory effect as roots. Loads are deliberately retained;
there is no load CSE or alias-based memory reordering in this prototype.
Snapshot reconstruction is a parallel assignment: every source is evaluated
before any architectural destination is overwritten. This is required for
copy cycles and entry values forwarded directly into a final snapshot.

The first lowering proves all ordinary memory spans at entry and requires
budget >= the region's exact instruction count with no pending AOT exit.
Consequently the successful straight-line path has no intermediate exit or
callback. Only its final snapshot must be materialized. Other snapshots remain
in the graph, but their values are not made artificially live after the entry
proof establishes they cannot be observed on this path. Short budgets and
failed proofs call the original compiler before any guest effect; they do not
return zero indiscriminately, overshoot a quantum or report a future fault early.

This is a restricted form of snapshot-based compilation. It does not yet lower
internal snapshot exits, flag-setting instructions, arbitrary control flow,
helper continuations or speculative traces. Extending those requires active
snapshot consumers and explicit state reload after a mutating callback. The
liveness test verifies that an overwritten value survives when an intermediate
snapshot is selected, and can be removed when only the final exit is possible.

Memory proof groups track entry-relative addresses through simple moves and
pointer arithmetic. Each read/write permission span is aligned, within one
mapped page and in little-endian ordinary memory. Store spans must not overlap
the current physical compiled-code interval; wrapping physical ends cannot
prove non-alias. Memory effects retain their guest order, including aliases.
Code-version instrumentation keeps its existing compiler path until separately
validated. There are no game-address whitelists.

Only AL-condition, non-flag-setting integer ALU/immediate shifts, multiplies,
word LDR/STR, ordinary LDM/STM and supported final PC writes are candidates.
At least four memory instructions are required. Unsupported regions use the
existing compiler, including its exact-budget and memory/helper handling.
The rejected memory-only optimization is not retained for non-IR regions.

## Why a small custom IR for this discriminator

The local Dynarmic source already provides typed SSA, register/flag operations
and ARM semantics (`src/external/dynarmic/docs/Design.md`). Its frontend remains
a credible alternative to maintaining another broad opcode implementation.
Our existing restricted Dynarmic-to-WASM research emitter (`ir_probe.py`) does
not implement this runtime's callback, code-write and partial-budget contracts;
its kernel timings cannot establish production compatibility.

This deliberately small custom graph reuses the current proof and fallback
contracts to test cross-instruction optimization in the actual runner first.
It has a type checker, ordered effects and explicit snapshot roots. The first
subset is sufficient for the captured integer kernels without committing to a
complete second ARM frontend. `eka_compiler_probe` emits the corresponding
Dynarmic pre/post-optimization IR beside the actual WASM artifact for inspection.
Those Dynarmic dumps are semantic/design references, not an equivalent accepted
backend. Compare implementation cost and measured results before expanding the
custom decoder; do not interpret its existence as rejection of Dynarmic reuse.

## Required acceptance

The full compiler suite includes independent interpreter comparisons at every
partial budget, arithmetic extremes, carry inputs, overlapping multiply
registers, shifts, aliases and memory ordering. The IR-specific production fault
mode requires the original entry to contain the IR fallback metadata, preventing
a test from silently exercising only an unselected compiler. Exact native
callback/state/memory comparisons, checked 1,600-image/audio replay and serial
whole-game timing remain mandatory. Smaller WASM bodies are not proof of speed.
