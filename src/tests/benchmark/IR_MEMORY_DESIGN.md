# Precise intermediate exits in IR segments

Experimental `EKA2L1_WASM_IR_MEMORY=ON` extends integer IR segments with ordinary
word loads/stores and LDM/STM. It requires IR_SEGMENTS, deferred memory and no
code-version instrumentation. Both IR options remain OFF by default. This is a
research path, not a promoted optimization.

The existing control-flow graph, branch labels and short-budget compiler remain.
A full-budget segment represents guest values across memory operations. Each
ordered guarded-host node proves an aligned, little-endian, permitted ordinary
TLB span on one page. A failed guard returns before the current instruction's
effects. Its pre-instruction snapshot supplies registers, flags and PC, and the
returned count includes exactly the prior completed instructions. The normal
runner performs its existing precise interpreter fallback when no compiled
progress is possible. A fallback can repair memory or change guest state; a new
entry reloads that state.

LDM/STM guard the whole span before any load/store. Failed page-crossing,
permission, alignment and endian cases retain the original partial-transfer and
callback behavior through fallback. Stores also reject physical overlap with
the current compiled-code interval, including guest aliases, before effects.
The original code validation remains at entry. No helper runs inside a successful
graph, so there is no invisible mapping or CPU-state mutation between guards.

Guards and memory effects stay ordered and are never CSE'd or removed. Every
possible guard-exit snapshot and the final snapshot participate in liveness.
Snapshot assignment first consumes all source values, then writes destinations,
so swaps and values overwritten later remain precise. The guard address itself
may depend on values produced by earlier instructions. This first extension
still stops at flags, control flow, wide multiplies and unsupported memory forms.

The full-budget guard charged one instruction before graph entry. A memory exit
at snapshot count N adjusts the runtime count by N-1; N=0 deliberately cancels
the first charge. A successful graph charges its remaining length as before.
An unsuccessful append is discarded transactionally, preventing effects from an
unsupported instruction from leaking into a selected prefix.

Validation adds raw generated-module comparisons for exact intermediate state,
no partial writes on guard failure, all four block-transfer address modes,
copy cycles and code aliases. Production-runner fault modes `--ir-memory` and
`--ir-memory-chain` require generated guard selection and compare real callbacks
with native. They augment the complete existing package tests and checked replay.
No performance claim follows from the architecture or emitted byte count.
