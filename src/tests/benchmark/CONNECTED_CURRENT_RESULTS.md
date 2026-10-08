# Connected callees on the graduated runtime

Status: no connected-callee variant earns promotion on the current two-game
runtime. The budget-preserving variants improve Snakes but regress Sky Force.
The source has been restored to the graduated default; every candidate patch,
artifact, correctness check and timing observation is preserved.

The historical connected-callee bundle recovered +6.87% Snakes CPU throughput,
with all four pairs faster. That bundle also expanded the leaf bound from 16 to
64 instructions and accepted more predicates. Its percentage is not a gain on
the current runtime. See [historical evidence](MEMORY_AND_CONNECTED_RESULTS.md).

The current adaptation first isolates internal callee branches at the existing
16-instruction bound. Each inlined call receives its own compiler label namespace,
including repeated calls to the same guest function. Forward joins and backedges
retain precise state, exit checks and interrupt handling. Eligible single callee
loops retain the existing whole-iteration budget proof. Nested calls, external
branches and modifications of LR remain excluded. The direct-memory and full
state-pruning defaults remain active.

## Narrow-candidate validation

- Focused interpreter comparisons: 51,200 exact state, memory and budget checks
  across four compiler policies, repeated calls and single calls, forward branches,
  loops, helper exits and code aliases.
- Full WASM suite: 172 reported passes, zero failures; two diagnostic-only cases
  skipped by the diagnostics-free build. The suite retains its documented XFAIL.
- Snakes and Sky Force: 60-frame replays each match the established frame images,
  frame records and PCM references exactly. These deterministic software-rendered
  replays establish matching output; hardware timing and live LAN checks are separate.

Evidence: `/home/claude/.scratch/eka-connected-current/correctness.json`; candidate WASM SHA-256:
`38fd78e08cd6e7024c042795aa20a79a9873b48ad7ec8edf87f93ef416e7e491`.

## Narrow-candidate timing

The frozen plan is `/home/claude/.scratch/eka-connected-current/current-plan.json`. The control is the already graduated
`eka-promote-recovered/final-build`, SHA-256
`90a592686582bbe11ddae12296ba9c793c2fc63660ac0cba9c1354356ccbc102`.
Both variants use one frozen harness, identical guest work, ordinary V8 tiering,
the hardware GPU, reserved CPU 7 and sibling 15, a measured fixed 3.6 GHz request,
and ABBA followed by BAAB. Sampling, tracing, verification and guest diagnostics
are disabled during timing. Every valid and invalid observation is retained.

The first comparison keeps the leaf bound at 16 for both variants. If it earns
adoption, graduate that artifact immediately. Otherwise the prepared 64-instruction
candidate receives its own exact replays and timing. The historical sweep resumes
from its saved rows after the current-runtime decision.

## Narrow-candidate decision

| # | Game | CPU throughput | Wall throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | -0.57% | -0.24% | +0.023% | 1/4 |
| 2 | Sky Force | -1.25% | -1.04% | +0.039% | 2/4 |

All 16 observations passed the frozen validity rules, with no invalid attempts.
All 88 live host-restoration checks passed. Neither game establishes a gain, so
this narrow candidate is not promoted. Every observation, including the slower
Sky Force candidate, remains in the evidence.

The unrun branch-only 64-instruction variant is superseded by a separate plan
restoring the full historical predicate eligibility as well as the larger bound.
The original plan and all completed rows remain unchanged. The historical bundle
accepted conditional scalar memory; the narrow isolation above did not. The full
follow-up also tests conditional memory within callee loops and both taken and
untaken predicates. Its evidence directory is `eka-connected-full`.

## Full-bundle validation

The full historical eligibility bundle passes 113,280 focused exact interpreter
comparisons, including conditional scalar loads/stores in callee loops, predicates,
short budgets, helper exits and physical code aliases. Snakes and Sky Force each
match their established 60-frame images, frame records and PCM references exactly
at the 64-instruction bound. The earlier 172-test full-suite result belongs to the
narrow implementation; the final full suite remains a graduation check if this
broader candidate earns adoption.

Full-bundle evidence: `eka-connected-full/correctness.json`; candidate WASM
SHA-256: `7ac40fb7bc91312760771084a0ffdd239b4a10941180a9cead9d9b89d49e1284`.

The new plan `eka-connected-full/current-plan.json` compares this complete bundle
with the unchanged graduated runtime at bound 16. It retains the original
fixed-frequency, reserved-core, hardware-GPU and ABBA/BAAB controls.

## Full-bundle decision and budget-proof follow-up

| # | Game | CPU throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes | -0.44% | -0.282% | 1/4 |
| 2 | Sky Force | -1.17% | +0.029% | 1/4 |

The full bundle also fails current-runtime adoption. All 16 observations are
valid, none required a retry, and all 88 live restoration checks pass. Complete
evidence is retained in [the full-bundle results](CONNECTED_FULL_RESULTS.json).

Source inspection found a specific interaction with existing defaults: straight
budget chunks exclude inlined instructions, and loop proofs require the entire
region to have a single direct loop. Repeated inlined loops therefore lose those
proofs. This establishes compiler behavior, not how much of the measured game
regression it causes.

The follow-up in `eka-connected-proofs` admits unchanged contiguous callee spans
to the existing straight-chunk rule and proves each single-entry loop separately
from global loop dispatch. It retains the existing minimum length of four and
maximum of 32 instructions, excludes interior entries and namespace crossings,
and uses the same precise short-budget recovery. No runtime accounting is added.
Focused tests assert that both repeated call sites retain their loop proofs and
compare every budget/state/memory outcome with the interpreter. This candidate
must still earn adoption in the controlled two-game comparison.

The budget-preserving candidate passes the focused predicate, connected-callee,
loop-budget and batched-accounting suites, including the new repeated-call proof
assertions. Both 60-frame game replays match images, frame records and PCM exactly.
Its candidate WASM hash is `421626b01133ceb9171666b8e1e763a28b44a46f13953ec016ac131e15df7e4b`; full evidence is
`eka-connected-proofs/correctness.json`. Controlled timing is running under the same frozen validity rules.

The completed budget-preserving Snakes comparison improves CPU throughput
2.25%, wall throughput 2.15%, and reduces native instructions 3.67%. All four
adjacent pairs improve (1.48% to 2.93%); all eight observations pass the frozen
validity checks. Sky Force remains in progress, so graduation is pending.

## Budget preservation at bound 64: completed decision

| # | Game | CPU throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes | +2.25% | -3.67% | 4/4 |
| 2 | Sky Force | -1.87% | -0.02% | 1/4 |

All 16 observations are valid, with no retries. All 88 live restoration checks
pass. This tradeoff is not adopted. The same candidate artifact is now compared
at the existing 16-instruction bound in `eka-connected-proofs16`; no source
change or rebuild is required. The previous results remain separate and complete.

## Original-bound correctness

The unchanged budget-preserving artifact passes the full WASM suite: 172 reported
passes, zero failures, two existing diagnostic-only skips and the documented
XFAIL. At bound 16, both games match their 60-frame images, frame records and PCM
references exactly. Evidence is `eka-connected-proofs16/correctness.json`. The
controlled comparison uses bound 16 for both control and candidate, retaining all
previous defaults and validity rules; no rebuild was needed.

The original-bound Snakes comparison completes at +2.05% CPU throughput,
+1.63% wall throughput and -3.31% native instructions, with all four pairs
faster (1.62% to 2.44%). All eight observations are valid. Sky Force is pending.

## Original-bound decision

| # | Game | CPU throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes | +2.05% | -3.31% | 4/4 |
| 2 | Sky Force | -1.79% | -0.03% | 1/4 |

All 16 observations pass the frozen validity rules. The nine observations
before the idle-boundary controller update and seven after it are all retained.
Both sets of 88 host-restoration checks pass. The startup update removes cooling
delays; it changes no clock, throttling or measurement-validity threshold.

The smaller bound also fails the two-game tradeoff. No runtime default or LAN
artifact changes. The tested patch remains in `eka-connected-proofs16/rejected-source.patch`
and the immutable candidate artifacts remain available for later investigation.
The historical sweep resumes from its saved 72/109 comparisons. Owner-core reuse
and remaining expanded-leaf eligibility stay recorded as reassessment candidates;
this failed recovery is not grounds to promote their historical percentages.
