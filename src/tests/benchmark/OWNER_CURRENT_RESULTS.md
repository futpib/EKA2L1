# Owning-core reuse on the graduated runtime

Status: not selected on the current runtime. The measured change does not establish a repeatable two-game gain. Source is restored; patch and artifacts remain preserved.

The historical owning-core experiment improves Snakes CPU throughput 0.81%,
reduces native instructions 0.24%, and has four faster adjacent pairs, including
one near tie. This small result needs reassessment on the current runtime.

The candidate reuses the stable owning-core reference within one compiled chain.
Address-space state, mapping generations and cache validity remain live reads.
The translated guest code and all current runtime configuration remain unchanged.
The control is the graduated `eka-promote-recovered/final-build`; evidence is in
`/home/claude/.scratch/eka-owner-current`.

Registry/syscall checks and exact two-game replays precede the frozen ABBA/BAAB
comparison. Measured clock, throttling and isolation rules are retained; there
is no temperature or post-build cooldown.

The sparse ROM and registry lifecycle checks pass, as do the syscall-boundary
checks. Both 60-frame game replays match images, frame records and PCM exactly.
This change touches the C++ compiled runner; guest translation is unchanged.
Its candidate WASM hash is `cf7f22347780f35280d248ef1314eadc534185f77ace275194f40b3744e0392f`.
Full evidence is in `eka-owner-current/correctness.json`.

## Completed current-runtime comparison

| # | Game | CPU throughput | Native instructions | Faster pairs |
| ---: | --- | ---: | ---: | ---: |
| 1 | Snakes | +0.15% | +0.46% | 3/4 |
| 2 | Sky Force | -0.82% | +0.53% | 2/4 |

Current Snakes +0.15% CPU throughput with mixed pairs and +0.46% native instructions; Sky Force -0.82% with 2/4 faster pairs and +0.53% native instructions. No repeatable two-game gain; retain the graduated baseline.

All 16 observations pass the frozen validity rules. All 88 host restoration checks pass. Exact replay, registry lifecycle and syscall evidence remains in `eka-owner-current/correctness.json`. No runtime default or LAN artifact changes. The rejected patch is preserved in `eka-owner-current/rejected-source.patch`.
