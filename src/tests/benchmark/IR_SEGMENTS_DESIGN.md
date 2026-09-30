# Integer IR segments inside existing regions

This is an unaccepted experiment, enabled only with
`-DEKA2L1_WASM_IR_SEGMENTS=ON` (default OFF). For its isolated comparison,
`EKA2L1_WASM_REGION_IR` remains OFF. The two options control distinct paths;
whole-region IR takes precedence where its stronger proof succeeds.

Captured gameplay modules at 0x70013edc and 0x70014224 contain loops, inlined
leaves, indexed/halfword accesses and flags. The first whole-region IR excluded
them. These addresses are research fixtures only: selection never checks a
game address. `eka_compiler_probe --region PC SIZE --inline-leaves` reproduces
the original captured bodies byte for byte with 512-byte windows.

The new pass selects contiguous unconditional i32 integer sequences of 3–32
instructions. It stops at branch destinations, changes between caller and
inlined leaf code, memory effects, flag changes, control transfers, wide
multiplies and unsupported encodings. It uses the existing typed graph,
constant folding, value numbering and final-snapshot liveness. Segments reuse
value-local slots, reserved before the architectural register cache. The
existing memory lowering, code validation and control-flow representation remain.

The first instruction uses the original budget/exit check and consumes one
instruction. This establishes COUNT <= budget. A fast path requires
`budget - COUNT >= length - 1`, so the subtraction cannot wrap. The graph
executes with no observable internal exit; the final parallel assignment updates
architectural cache locals and COUNT advances by the remainder. Otherwise the
original emitted instructions run, preserving every partial-budget exit.

Pending wide values are materialized before entering a segment. Snapshot
sources are all consumed before any destination write, preserving copy cycles.
Both paths join before the next instruction/label. The emitter's instruction
handlers often use `continue`, so the segment's fallback block closes at the
next lexical boundary rather than only at the bottom of an instruction handler.
Memory/helper/branch/IRQ boundaries outside segments retain their prior checks.

The new independent interpreter matrix requires actual segment selection and
covers swaps, CSE, constant propagation, i32 multiply/accumulate, wide-value
boundaries, guest PC reads, partial budgets, loops, interior branch targets and
inlined leaves. A separate production fault-probe mode `--ir-segments` requires
selection, then checks callback registers/flags after the optimized sequence for
all existing memory access/fault policy variants. These augment the full package
suite and exact native gameplay/audio replay; they do not replace them.

No performance result is established by this design document. Duplicating a
short-budget remainder increases code size, and guards/materialization can cost
more than the removed work. Promotion requires repeated serial whole-game gains
and the existing live/audio acceptance gates.
