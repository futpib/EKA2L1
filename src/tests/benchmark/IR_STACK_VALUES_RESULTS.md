# Adjacent pure stack values: correctness accepted, timing pending

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

Serial comparison will select policies 15, 13 and 7 within this identical
application and include the exact served policy-7 archive. All observations are
retained; no owned heavy work may overlap timing or warmup. No default, deployment
or push is part of this experiment. Raw checks, hashes and results are recorded
in IR_STACK_VALUES_EVIDENCE.json.
