# Flycast SH4 browser CPU adapter, 2026-09-30

All four equivalent algorithms pass, without a guest OS or BIOS. Actual upstream
SH4 decoding/SSA, patched WASM code generation, and production C dispatch are
used. This adapter eagerly compiles all reachable blocks at setup; adaptive
hot-block/chain promotion and console scheduling are excluded. It is not the
release console frontend. Different SH4 instruction streams prevent pooling
with the ARM instruction table.

Intel Core i7-10875H, Linux 6.12.108, Chromium153, Emscripten4.0.10. One million
algorithm iterations; median ms over ten measured observations per cell in each
batch, with forward/reverse workload order. All 208 initial, small-count, warmup
and measured outputs match the independent reference, including full memory
hashes. No observations discarded.

| Batch | Arithmetic | Indexed RAM | Conditions | Dependent 64 KiB reads |
|---|---:|---:|---:|---:|
| a | 13.60 | 16.85 | 41.20 | 15.65 |
| b | 12.43 | 15.05 | 40.74 | 14.81 |

Batch A used the ordinary page timer (~0.1ms granularity). Batch B uses COOP/COEP
and the finer timer used in the direct ARM harness (~0.005ms). Both batches
remain in the evidence. Their difference is not attributed solely to timer
granularity; browser/host execution variation remains.

Cold cost is explicit: module initialization is recorded separately. First
setup includes RAM allocation and decoding/module compilation; subsequent
workload setup compiles its previously unseen blocks. First execution and all
warmups remain separate from measured medians. For batch B, first setup of the
page takes about 28–31ms; later new-workload setup takes about 0.36–1.16ms.
These are hot, tiny programs, not whole-game compile cost estimates.

The published patch required excluding an incompatible audio-frontend hunk
at its documented upstream revision. The CPU implementation is unchanged; the
custom adapter is included at the end of rec_wasm.cpp. Artifact hashes, exact
source revisions, submodules and build flags are recorded in the evidence.
The downloaded release binary was inspected but is not the measured artifact.

Voland was refreshed at 4aced55c5e632aed99ce76433b3dfec8a417240c: its README
still says guest code does not execute. A no-op backend receives no speed score.

This completes initial browser measurements for the seven functional projects
in the survey (SkyEmu contributes two ARM CPU variants). EKA generated regions
are an additional limited control. The suite distinguishes instruction/algorithm
throughput, not complete emulator performance or a best replacement for EKA2L1.
All benchmarks and observations are local; the live emulator is unchanged.
