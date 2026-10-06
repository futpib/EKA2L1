# Adjacent pure stack values: not promoted

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

Opt-in policy 15 preserves policy 13 semantics and its 32-instruction bound.
A pure result used once by the immediately following pure node is emitted in
that operand slot without a temporary local. Snapshot roots, shared expressions,
state reads, memory effects and cold recipes are excluded. No effect or guard
is crossed. See IR_STACK_VALUES_DESIGN.md.

All 160 compiler tests pass. Policy 15 adds 57,344 predicate/flag/budget
comparisons; the three-policy long-sequence matrix now covers 51,840 comparisons.
Both policies 13 and 15 pass 13,856 explicitly selected native fault comparisons
(27,712 total), including required matching policy markers. Invalid policy 16,
wrong markers and missing markers are rejected. Three native targets and nine
frontend checks pass. Both interpreter-checked 1,600-image/guest-record replays
match native, including all 4,919,249 stereo PCM frames. Tested cases are evidence,
not proof of universal correctness.

| Fixture | Policy | Selected stack values | Main body bytes including locals | Module bytes | i32 locals |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0x70013edc | 13 | 0 | 10650 | 32004 | 82 |
| 0x70013edc | 15 | 11 | 10606 | 31960 | 79 |
| 0x70014224 | 13 | 0 | 6163 | 19976 | 86 |
| 0x70014224 | 15 | 30 | 6043 | 19856 | 77 |

Each fixture retains five segments, maximum length 32, and one i64 local.
The selected guest instructions and memory guards are unchanged. These small
WASM reductions do not establish machine-code savings or gameplay throughput.

Archive: /home/claude/.scratch/eka-benchmark/ir-stack-candidate.
WASM SHA256: 21653dec5ec6c0c59af954e8ee3a32dd2f0860d2369749752d0b93a8c54eb1ff.
Base a0cf50a91 plus archived patch. A comparator-only valid-policy range correction
followed archiving; the final tool hash is recorded separately. Initial parser
failure and missing-browser frontend attempt remain recorded. Neither is counted
as a correctness pass. Successful complete retries use the unchanged archive.

Serial comparison selected policies 15, 13 and 7 within this identical
application and include the exact served policy-7 archive. All observations are
retained; no owned heavy work may overlap timing or warmup. No default, deployment
or push is part of this experiment. Raw checks, hashes and results are recorded
in IR_STACK_VALUES_EVIDENCE.json.

## Completed serial timing

Order: stack15, conditions13, combined7, served7, served7, combined7,
conditions13, stack15. First three use the identical archived application.
No other owned heavy workload ran during warmup or measurement.

| Policy | First seconds | Second seconds | Mean seconds |
| --- | ---: | ---: | ---: |
| stack | 15.4073 | 14.6218 | 15.0145 |
| conditions | 13.3259 | 13.1166 | 13.2213 |
| combined | 12.8674 | 12.8964 | 12.8819 |
| served | 12.7748 | 14.8990 | 13.8369 |

Every window covers 18 guest seconds, 3,975,618,624 instructions and 676
presentations. Both adjacent candidate/previous-IR pairs favor the previous IR.
The candidate also loses to matching original emission. The slow served closing
control stays in the record. These noisy observations do not justify promotion
or a precise causal slowdown percentage. All eight observations are retained.

A first launcher copy incorrectly mapped the stack label to policy 7. Its report
explicitly recorded 7; it was stopped after the first run during the next warmup.
That malformed experiment is retained as ir-stack-timing-a, with an explanatory
marker, and is not counted as policy-15 timing. The corrected complete experiment
is ir-stack-timing-v2-a; each report's policy, workload and hashes were checked.
No deployment or push. The implementation remains opt-in.
