# Connected callees on the graduated runtime

Status: the narrow candidate was rejected; the full historical bundle passed focused validation and awaits timing. The historical sweep is
paused at 72/109 comparisons for this graduation cycle. No new default has been
committed or deployed yet.

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
