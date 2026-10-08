# Owning-core reuse on the graduated runtime

Status: controlled two-game timing, with the historical sweep paused at its
73/109 review boundary. No runtime default has changed.

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

The completed Snakes comparison is +0.15% CPU throughput, +0.39% wall throughput,
and +0.46% native instructions. Three of four pairs are faster, ranging from
-0.34% to +0.49%; this does not establish a useful Snakes gain. All eight
observations are valid. Sky Force remains in progress.
