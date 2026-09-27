# Rejected LTO experiment

The 2026-09-27 experiment enabled CMake interprocedural optimization for the
Emscripten build (`-flto=thin`) at source `cb1c86820` plus the archived option
patch. The option was removed and the active build returned to non-LTO.

All 127 WASM CPU tests and the sampled compiled-block checker passed. However,
the exact 85-frame native comparison **failed**: every pixel, presentation and
guest timestamp matched, but all 85 instruction counts differed. The first
capture was 2,378,163,924 instructions versus 2,378,095,060 in the reference.
The cause of this startup offset is unresolved; matching pixels is insufficient
to accept the build.

Four-second no-capture physical-GPU timing trials were 4.83759/4.66730 seconds
without LTO and 4.57864/4.66843 with LTO. The first control may briefly overlap a
frame-comparison process after its gate was released, so it is not an isolated
control. Ranges overlap; no established speed gain is claimed. No further
controlled trials were warranted after the correctness gate failed.

Preserved local artifacts under `/home/claude/.scratch/eka-benchmark`:
`lto-build` (binaries, source patch and hashes), `lto-smoke/comparison.json`,
`lto-timings`, and `LTO_REJECTED.patch`. Unit log:
`/home/claude/.scratch/eka-lto-tests.log`.
