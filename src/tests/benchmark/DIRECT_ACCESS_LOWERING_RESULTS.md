# Direct memory: consume scalar lookup results immediately

Adopted on `wasm-port`. Sky Force uses 3.07% less worker CPU in the paired
screen, and Snakes uses 3.56% less over the longer follow-up window.

This extends cheaper direct-memory lowering to ordinary ARM and Thumb loads
and stores, including byte/halfword/word widths, signed loads, register offsets,
writeback and the scalar fallback of multi-register transfers. Backend 2 remains
the WASM default; TLB lowering is unchanged.

Previously, a successful arena lookup initialized a host-pointer local to zero,
replaced it with the translated address, and then tested it again before the
memory operation. The new emitter performs the operation immediately inside
the successful arena branch. Outside the arena, it branches to the existing
slow path on an invalid view, page crossing or absent read/write permission,
then consumes the permitted page pointer directly. ARM retains its existing
choice between yielding before a restartable access and calling a helper.
Stores requiring code-write guards retain their existing guarded path.

Whole-span proofs also return their pointer on the WASM value stack and assign
the host local once. Successful proofs remove two executed operations; failed
proofs retain their previous cost. Proof eligibility and access thresholds are
unchanged. No new speculative check or runtime counter is introduced.

## Instruction-count gate

The expanded `span-counts.mjs` matrix runs 164 real generated modules: 82 per
backend, including both ARM helper and deferred-memory variants. Fifteen memory
scenarios, four budgets and both condition outcomes produce 19,680 comparisons.
**6,986 decrease, 12,694 are unchanged and none increase.** Every changed module
has at least one reduction. All 82 TLB modules are byte-identical to baseline.

Scenarios cover arena and page-directory success, separate read/write
permissions, denied pages, null views, zero host pointers, endian fallback,
unaligned and cross-page accesses, page zero and address wraparound. State,
memory, guest instruction progress and helper-call traces match exactly.
The counter includes branches and private fallback functions; it excludes
structural block/loop/end/else markers, its own operations and imported helper
bodies. Identical helper arguments, ordering and counts are required.

The savings per executed scalar access are six operations in ARM's arena path
and five in Thumb's; page-directory paths save four and three respectively.
Fallback paths save two to five depending on the ISA and exit strategy.
Successful whole-span proofs save two operations per proof.

These examples count complete generated invocations, including entry/exit and
state handling, with budget 16 and Z clear:

| # | Fixture | Path | Before | After |
|---|---|---|---:|---:|
| 1 | ARM word load, deferred fallback | Arena | 90 | 84 |
| 2 | ARM word load, deferred fallback | Page directory | 111 | 107 |
| 3 | ARM word load, deferred fallback | Null view | 74 | 70 |
| 4 | ARM word load, helper fallback | Null view | 105 | 100 |
| 5 | Thumb word load | Arena | 82 | 77 |
| 6 | Thumb word load | Page directory | 103 | 100 |
| 7 | Thumb word load | Permission denied | 141 | 139 |
| 8 | Four proved ARM loads | Arena | 144 | 142 |
| 9 | Eight-word Thumb LDM | Arena | 159 | 157 |
| 10 | Eight-word Thumb LDM, scalar fallback | Null view | 862 | 838 |

## Correctness and timing

Focused checks pass: 816 ARM/Thumb backend comparisons, direct arena edges and
mapping/callback lifetime checks, and the existing shared-span, ARM-memory and
loop-budget suites. The latter suites primarily guard the unchanged TLB path;
the expanded counted modules explicitly exercise both backends.

Both games pass 60-frame Chrome replays against the native reference, including
exact pixels, guest timestamps, guest instruction counts, PCM and audio events.
Snakes ends at 3,012,361,018 instructions / 23,827,651 guest microseconds;
Sky Force ends at 15,827,750,326 / 43,864,773. These correctness replays use
SwiftShader; CPU timings below use the hardware GPU and shared audio, with
sampling, tracing, diagnostics and count injection disabled.

Observations are DedicatedWorker scheduler CPU seconds. Each comparison uses
baseline-candidate-candidate-baseline order and identical guest work and
presentation journals. Positive change means slower. All observations are kept.

| # | Game / measured guest window | Baseline CPU seconds | Candidate CPU seconds | Mean CPU change | Pair changes |
|---|---|---|---|---:|---|
| 1 | Snakes / 21-25 s | 1.679663, 1.810370 | 1.755964, 1.803441 | +1.99% | +4.54%, -0.38% |
| 2 | Sky Force / 42.000001-48 s | 8.284111, 8.077065 | 7.952006, 7.907178 | -3.07% | -4.01%, -2.10% |
| 3 | Snakes follow-up / 21-33 s | 5.210500, 5.234368 | 5.042248, 5.030990 | -3.56% | -3.23%, -3.89% |

The short Snakes pairs disagree and establish no reliable change. That possible
regression triggered a follow-up with three times the measured guest window;
it improves in both orders. The longer segment includes additional gameplay,
so it does not prove every shorter section improves. Sky Force improves in both
orders in the original screen. These are small gains on a shared host, not
precise guarantees for other workloads or estimates of native instruction counts.
The scalar-access and span-result changes were measured together.

The original windows execute 644,728,231 Snakes instructions and 2,171,043,925
Sky Force instructions. The follow-up checks equal instruction endpoints and
presentation journals across all four longer Snakes observations.

[Complete evidence](DIRECT_ACCESS_LOWERING_RESULTS.json) preserves the source
patch, build hashes, full count matrix, focused check output, native comparisons,
all 12 CPU observations and the longer-window driver. One initial replay launch
failed before guest execution because the frozen build had not yet been copied;
its exact setup error is retained separately in the local artifacts.

## Live build

The LAN launcher serves the frozen candidate build. Fresh launches of both games
use default backend 2, exercise the direct arena, advance guest time and present
frames without page errors. The loaded module hash is
`32ac6857ddb9b2e4e98b207c0b22d25bf8714f6fbde6fe3e6296abde2d8b3d4c`,
matching the replayed and timed candidate. The initial browser check raced the
server restart; it passed on retry after the server began listening. This check
does not make an additional claim about physical speaker output.

## Reproduction

Baseline source: `4125fba6a`. Both probe builds use the same expanded fixtures;
only the candidate changes production emitters. Build `test_aot_wasm`, export
`--emit-memory-probes` from each build, then run:

```sh
node src/tests/wasm/span-counts.mjs baseline-probes.log candidate-probes.log counts.json
node build-wasm/src/tests/aot/test_aot_wasm.js --memory-implementations-only
node build-wasm/src/tests/aot/test_aot_wasm.js --shared-spans-only
node build-wasm/src/tests/aot/test_aot_wasm.js --arm-memory-only
node build-wasm/src/tests/aot/test_aot_wasm.js --loop-budget-only
```

Native comparisons use `memory_implementations.py replays --modes 2 --frames 60`.
CPU comparison uses `compare_memory_builds.py --modes 2 --rounds 2` on frozen
normal browser builds. No owned build, compiler test, replay or instrumented
module execution overlaps CPU timing. Raw local artifacts are in
`/home/claude/.scratch/eka-direct-lowering/`.
