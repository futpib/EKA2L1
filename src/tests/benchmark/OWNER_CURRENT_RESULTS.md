# Owning-core reuse on the graduated runtime

Status: current-runtime validation, with the historical sweep paused at its
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
