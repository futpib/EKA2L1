# Cheaper lowering of existing memory proofs

Adopted for **direct memory**, in both ARM and Thumb. Sky Force used 7.63% less
worker CPU in the paired screen; Snakes was effectively unchanged. The TLB
version reduced WASM instructions too but regressed CPU time, so its original
lowering is retained. Backend selection remains `EKA2L1_MEMORY_IMPL=0|2`.

This change optimizes existing proofs. It does not broaden which addresses or
blocks receive a proof, lower the ARM four-access threshold, or introduce a new
speculative entry check.

ARM's already-proved immediate word loads/stores use the saved host pointer and
a WASM memory offset directly. Their previous lowering computed a guest address,
constructed helper arguments and then discarded those values in favor of the
proved host address. Cached registers, immediate addressing, no writeback and
non-PC operands are required. The existing entry guard and private fallback
remain unchanged.

Thumb PUSH/POP/LDM/STM already had a whole-transfer proof for two or more words.
Direct mode now branches around the entire fast transfer and uses fixed memory
offsets. The original scalar sequence remains in the fallback arm. Previously,
each word tested the same proof result and reconstructed its host address.
Single-word/empty transfers and stores requiring mutation callbacks retain their
original lowering. Register ordering, PC/Thumb-state updates, writeback and
callback state publication are preserved.

## Executed instruction-count gate

Selection is structural at translation time: ARM removes work after an existing
proof, and Thumb replaces N tests of an existing proof result with one. No new
proof cost has to be amortized over an estimated number of accesses. Every
selected lowering removes operations on its successful path; ARM proof failure
keeps the original path, while Thumb proof failure also removes repeated tests.
No runtime instruction counter or cost-model branch is added to production.

The regression gate counts executed WASM operations, not byte length, guest ARM
instructions or predicted native machine instructions. The test tool inserts a
separate i64 global counter into exported compiler fixtures and their private
fallback functions. Counter instructions and structural block/loop/end/else
markers are excluded. Imported helper bodies are uninstrumented; helper call
arguments, order and counts must match. Known branch, loop and private-call
fixtures check the counter itself before it judges emulator output.

For the retained compiler, all 5,376 comparisons pass: **908 reduced, 4,468
unchanged, zero increased**. They include both backends, successful and failed
proofs, separate read/write permissions, absent mappings, valid tags with null
backing, endian fallback, unaligned and cross-page accesses, arena paths,
conditions and short budgets. Registers, memory, guest instruction progress
and helper traces match the baseline. Unexecuted or unselected optimizations
may be unchanged; every changed module must demonstrate a reduction.

Counts below cover complete invocations, including guards and state handling.

| # | Fixture | Backend/path | Before | Retained compiler |
|---|---|---|---:|---:|
| 1 | Four proved ARM loads | Direct, arena | 196 | 144 |
| 2 | Four proved ARM stores | Direct, arena | 196 | 136 |
| 3 | Four proved ARM loads | Direct, page directory | 217 | 165 |
| 4 | Eight-word Thumb LDM | Direct, arena | 257 | 159 |
| 5 | Eight-word Thumb STM | Direct, arena | 258 | 128 |
| 6 | Four ARM loads, entry span rejected | Direct, cross-page | 403 | 403 |
| 7 | Eight-word Thumb LDM, span rejected | Direct, cross-page | 521 | 505 |
| 8 | Eight-word Thumb LDM, helper fallback | Direct, null view | 878 | 862 |
| 9 | Four proved ARM loads | TLB, valid page | 211 | 211 |
| 10 | Eight-word Thumb LDM | TLB, valid page | 256 | 256 |

The initial combined candidate reduced 1,804 cases and left 3,572 unchanged,
with zero increases. Its TLB counts fell from 211 to 159 for the ARM example
and from 256 to 171 for the Thumb example, despite slower CPU measurements.
Fewer executed WASM operations therefore do not establish fewer native
instructions or less CPU time after V8 optimization. The CPU regression's
cause was not isolated in this experiment.

## Correctness

Focused checks passed for the combined candidate and the retained compiler:
2,688 Thumb transfer-span cases, 2,310 Thumb memory cases, 25,600 invariant-read
cases, 16,896 invariant-write cases, 816 backend comparisons, 21,146 ARM
short-block cases, 24,192 instruction-budget cases and the associated
mapping-lifetime, callback-state and block-transfer-PC checks.

Snakes and Sky Force each pass a 60-frame native-reference replay under TLB and
direct memory, including exact pixels, guest timestamps, instruction counts,
PCM and audio events. These checks were repeated on the final retained build.
The correctness replays use SwiftShader; CPU timings use the hardware GPU.
The normal browser build contains no counter injection. No fresh full
compiler-suite pass is claimed.

All 28 retained TLB probe modules are byte-identical to the original baseline;
all 28 retained direct probe modules are byte-identical to the timed combined
candidate. The later change only restricts the new lowering to direct mode.

## CPU measurements

Positive change means slower. Observations are worker CPU seconds. Each game
and backend uses baseline-candidate-candidate-baseline order with two fresh
browser observations per build. The candidate here is the **combined trial**;
the subsequent direct-only restriction and final replay verification are
recorded separately. No additional CPU campaign was run after that restriction.

| # | Game/backend | Baseline observations | Candidate observations | Mean CPU change | Pair changes | Decision |
|---|---|---|---|---:|---|---|
| 1 | Snakes / TLB | 2.119269, 2.000326 | 2.265383, 2.048322 | +4.71% | +6.89%, +2.40% | Retain original TLB |
| 2 | Snakes / direct | 1.840910, 1.891131 | 1.890775, 1.843885 | +0.07% | +2.71%, -2.50% | No established CPU change |
| 3 | Sky Force / TLB | 9.247182, 9.056908 | 9.314628, 10.230433 | +6.78% | +0.73%, +12.96% | Retain original TLB |
| 4 | Sky Force / direct | 8.758240, 8.938679 | 8.096139, 8.250910 | -7.63% | -7.56%, -7.69% | Retain direct lowering |

Sky Force's direct gain repeats in both orders. Snakes' opposing pair changes
and flat mean establish no gain there. These are short comparisons on a shared
host, not a precise estimate of small effects or a promise for other games.
The ARM and Thumb contributions were not timed separately.

Hardware GPU and shared audio are enabled; sampling, tracing and custom
diagnostics are off. No owned build, compiler test, replay or instrumented-module
execution overlaps timing. Worker scheduler CPU seconds are primary; renderer
CPU and wall time are retained too. All completed observations are kept.

Snakes uses guest time 21-25 seconds; Sky Force uses 42.000001-48 seconds. The
driver checks equal instruction endpoints and presentation journals across
builds: 644,728,231 and 2,171,043,925 instructions, respectively.

## Evidence and reproduction

[Complete evidence](SHARED_SPAN_LOWERING_RESULTS.json) records the combined
trial patch, baseline and candidate artifact hashes, full instruction-count
matrix, focused checks, native replay comparisons, all CPU observations and
commands. Its `adoption` entry records the final restriction, artifact hashes,
byte-identity checks, instruction-count summary and repeated correctness checks.
The combined matrix also describes the final direct counts; final TLB counts
are the matrix's baseline column.

Baseline source: `347f3973b`. Both test builds use the same added probe exporter
and focused selectors; the baseline uses the original ARM/Thumb CPU emitters.
Build `test_aot_wasm` for each revision, then run:

```sh
node BASELINE_TEST.js --emit-memory-probes > baseline-probes.log
node CANDIDATE_TEST.js --emit-memory-probes > candidate-probes.log
node src/tests/wasm/span-counts.mjs baseline-probes.log candidate-probes.log counts.json
node CANDIDATE_TEST.js --shared-spans-only
```

The other focused selectors are `--memory-implementations-only`,
`--arm-memory-only` and `--loop-budget-only`. Native replay comparisons use
`memory_implementations.py replays --modes 0 2 --frames 60`. Timing uses the
existing `compare_memory_builds.py --modes 0 2 --rounds 2` on frozen normal builds.
Local artifacts are in `/home/claude/.scratch/eka-span-counted/`.
