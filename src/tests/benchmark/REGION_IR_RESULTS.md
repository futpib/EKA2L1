# Guarded value IR: real kernel gain, no whole-game promotion

The first typed-IR prototype passes its correctness gates, but its production
Snakes timings do not establish a gain. Preserve it as an experimental compiler
path, disabled by default; do not replace the served build.

| Symmetric serial batch | Corrected baseline | IR | Served |
| --- | ---: | ---: | ---: |
| A: fixed/IR/served/served/IR/fixed | 14.20470s | 14.62445s | 14.94820s |
| B: IR/served/fixed/fixed/served/IR | 13.77705s | 14.51215s | 14.31000s |

Both batches lose to the immediate baseline. Against the served build, changes
are +2.2% then -1.4% throughput. All runs/outliers are retained; no statistical
significance claim. These are hardware-GPU/shared-audio runs over guest seconds
78–96, 3,975,618,624 instructions and 676 presentations. Warmups and timing were
serial with no owned build/test overlap. No deployment or live acceptance was
attempted for this version.

## What was actually implemented

A small typed value graph, ordered memory effects and explicit architectural
snapshots at every instruction boundary. Pure values undergo constant folding,
value numbering and dead-value analysis rooted in observable snapshots/effects.
Whole-region memory and budget guards prove there can be no internal exit on
the fast path; it materializes only its final snapshot. The original compiler
handles every short budget/failed proof before guest effects. This does not yet
lower internal snapshot exits, general control flow or flag-setting operations.
See REGION_IR_DESIGN.md for the restricted semantics and Dynarmic comparison.

The full suite passes 140 tests, including 19,392 new IR state/memory/budget
comparisons and snapshot-liveness/type checks. All 4,368 explicitly rebuilt
native fault comparisons pass (4,272 prior cases plus 96 fixtures requiring an
IR-eligible original entry), as do three native targets and seven frontend checks.
Checked gameplay matches all 1,600 images, guest records and 4,919,249 stereo PCM
frames. The known native-identical movement-heuristic failure remains separate.

## Kernel result

Two fresh-browser experiments alternate baseline/IR order for eight rounds each,
using five million calls per timing. The baseline is the candidate's exact
private original-compiler fallback, extracted as one public function without
rewriting its instructions. Both implementations match native fixtures for all
budgets and 16 seeds: 2,112 comparisons per browser (4,224 total).

| Fixture | Browser A baseline / IR | Browser B baseline / IR |
| --- | ---: | ---: |
| 57-instruction integer math | 321.77 / 159.35ms | 322.71 / 160.41ms |
| 7-instruction memory prefix | 114.16 / 103.02ms | 115.02 / 102.85ms |

This is roughly 2.0x math throughput and 11–12% prefix throughput. It is an
isolated fixture result, not a measured gameplay gain. Driver overhead is
retained, never subtracted. The fixture includes guards and exact short-budget
behavior, but omits real dispatch/validation and whole-game memory/cache context.
Static hot bodies are 1,674 and 697 WASM bytes; with fallbacks, modules are still
larger than the original. Smaller bodies alone were not the acceptance criterion.

## Coverage and attribution

A separately instrumented heavy window counts 11,239,245 eligible entries,
129,704 guard fallbacks, 11,109,541 fast entries and 516,259,803 fast-path guest
instructions. That is 12.986% of guest instructions, 1.154% fallbacks and 46.47
instructions per successful region. It is not wall-time coverage. Counters are
read only at paused window boundaries; u32 modular deltas are valid because the
window contains fewer than 2^32 guest instructions. The instrumented binary also
passes all 140 compiler tests and exact checked native replay.

Serial CPU profiles show the named math region at about 0.115s self samples in
the IR run and 0.375s in the fixed run, under 1% and about 2.3% of their sampled
worker time. Other major costs remain interpreter execution, code-cache lookup,
exact code-byte comparison and other generated regions. Overall run speeds
vary, so these profiles do not establish an independent throughput improvement.
The short-prefix isolated fixture is not assumed to be the entire runtime
region at that entry; module capture is checking its actual extent and selection.

## Artifacts and reproduction

Production candidate archive: /home/claude/.scratch/eka-benchmark/region-ir-candidate.
WASM SHA-256: 12cd2a590da55632b57e86e299ebecc9681aa92cdf3474e0bf1f51a060da5005.
Base 9aeaf98b7 plus region_ir_initial_experiment.patch (includes tests/probe changes).
REGION_IR_EVIDENCE.json embeds source hashes, every timing, exact comparisons,
profiles, counter results and kernel provenance. Run serial_variants.py with
shared audio and the recorded archive paths. Run region-ir-kernels.ts against
region-ir-kernel-comparison for the two native-checked kernel experiments.
The census-only instrumentation is preserved in region_ir_census.patch and was
removed from current production source. Its elapsed time is not a performance
result. No game-PC whitelist or relaxed memory/code/budget guard was introduced.
