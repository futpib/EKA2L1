# Outlined short-budget recovery experiment

The controlled reassessment recovers a Snakes gain for the archived outlined
prototype: CPU throughput improves by 1.91%, wall throughput by 1.75%, and
native retired instructions fall by 3.04%. All four adjacent CPU pairs are
faster (+0.79% to +2.89%) in ABBA then BAAB order. All eight observations pass
the unchanged clock and host checks. The original failure to repeat a gain
does not hold for this comparison. This supports a current-runtime follow-up;
it is not a direct comparison with the other entry-budget prototype, and their
percentages are not additive. See [controlled results](CONTROLLED_RESULTS.md)
and [scope](CONTROLLED_REASSESSMENT.md). Original observations and correctness
evidence remain below; no runtime change is adopted by the reassessment.

This rejected candidate proves sufficient budget at entry for loop-free ARM
regions with no inlined dependencies. It removes redundant per-instruction
budget guards on that path while retaining memory, callback, code-write and
AOT_EXIT handling. Short budgets call a private copy of the original precise
function. This differs from ENTRY_BUDGET_RESULTS.md, where both bodies were
inside one function. No game/address whitelist is involved.

The module builder appends private callees after public functions, preserving
sibling indices, and relocates a fixed-width call operand. New regression checks
cover zero/six imports, two/128 public functions and invalid relocation metadata.
Only public functions are exported. The complete experimental patch, including
these feature-specific checks, is preserved as outlined_budget_experiment.patch;
all production changes and the feature-specific test are removed on rejection.

## Results

Shared audio and physical GPU enabled; no capture, profiling or detailed
counters. Runs and warmup are serial, with no owned build or other test/browser
job overlapping timing. Each measures guest seconds 78–96, executes
3,975,618,624 instructions and presents 676 frames. Every outlier is retained.

| Batch | Direct-loop baseline | Outlined candidate | Served archive |
| --- | ---: | ---: | ---: |
| A | 13.87520s | 14.07615s | 14.44275s |
| B | 14.16490s | 14.02345s | 13.67810s |

A order: direct/outlined/served/served/outlined/direct.
B order: outlined/served/direct/direct/served/outlined. Unlike the previous CMP
confirmation, this rotates the candidate from positions 2/5 to positions 1/6.
These are symmetric, equal-replication orders, not a claim of statistical power
or complete position balancing. A third rotation was not run after rejection.

Throughput changes versus the immediate baseline are -1.43%
and +1.01%; versus served they are +2.60%
and -2.46%. Candidate runs range from 13.2031 to 14.9492s.
The signs do not repeat against either control. Do not promote or claim a new
speedup. These results do not disprove all cold-path outlining or exact-budget
strategies; they reject this implementation on the measured workload. No V8
inlining or native-code-size conclusion was measured here.

## Correctness and artifacts

All 138 WASM tests pass, including the private-callee tests and existing 464,912
bounded comparisons, 229,376 conditional ALU cases, 64,512 compare cases,
92,160 long-multiply cases and 42 interrupt checks. Native three targets and
seven frontend checks pass. All four explicitly rebuilt 672-case fault modes
match native. The checked replay matches native across 1,600 images, guest
records and 4,919,249 stereo PCM frames. The native-identical gameplay heuristic
failure remains unchanged and is not described as passing.

The archive outlined-budget-candidate was built from f7ee55bbc plus the saved
patch. WASM SHA-256:
d80a9808603da911a45a1048913736d5e75d0a019faf78c0d04a5f31079aef11.
OUTLINED_BUDGET_EVIDENCE.json records all run data, archive/source hashes,
verification results and logs. Reproduce with serial_variants.py and
EKA2L1_SHARED_AUDIO=1 using the archived paths and orders recorded there.
The served build is unchanged; no new live/audio acceptance or deployment is
claimed. Independent CMP regression coverage remains in the restored compiler
suite; the outlining-only test lives with the rejected patch.
