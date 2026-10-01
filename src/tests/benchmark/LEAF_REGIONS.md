# Inlining short ARM leaf helpers

The late 78–96 guest-second diagnostic window executes 386,957,216 compiled blocks. Its hottest caller (`0x70013f08–0x70013fb4`) repeatedly invokes distant 1–12-instruction helpers at `0x70064fb4`, `0x7006503c`, `0x70065050` and `0x70065b60`. The leading 13 successor pairs account for roughly 29% of sampled compiled blocks. These are instruction/dispatch measurements, not wall-time shares. Evidence: `/home/claude/.scratch/eka-benchmark/late-guest-profile`.

Region compilation can inline up to eight unconditional ARM calls to straight-line leaves of at most 16 instructions, ending in BX LR. Eligibility excludes nested branches, status transfers, special memory forms and accesses to SP/LR/PC. Ordinary guarded loads/stores and ALU operations retain their existing semantics. Each emitted instruction carries its original guest PC, consumes one instruction of budget, and can exit independently. The call sets LR; the leaf return resumes the caller without spilling registers. Other calls still return to the runner.

Each included leaf has a separate exact-byte snapshot and mapping dependency. A mapping-generation change resolves every dependency before touching cached backing. Every entry compares every snapshot. Explicit invalidation covers dependencies, and the current region's write guard conservatively spans all included backing ranges, including their aliases. Writes into that span and helper callbacks force an exit before another guest instruction. The envelope can cause unnecessary exits for gaps; it does not omit code safety checks.

Initial validation: all 131 WASM tests pass, including 1,280 new exact budget/register/flag/memory comparisons with partial leaf execution, repeated calls, loops, memory fallback and writes into included code. Native mapping tests cover patched, remapped and unmapped leaf code, explicit invalidation and address spaces. All three native test targets pass. The native suite also exposed an outdated 64-block-cap expectation from the preceding 512-block runner change; the test now exercises a 600-instruction budget and expects 512 blocks.

The checked 85-image browser replay matches native pixels, guest timestamps/instructions and PCM/events exactly through 25.235380 guest seconds (`leaf-smoke/comparison.json`). The checked 1,600-image replay also matches native exactly through 102.484363 guest seconds and 16,261,337,499 guest instructions, including all PCM/audio events. Artifact: `leaf-extended1600/comparison.json`.

## Measurements

Serial old/new/new/old physical-NVIDIA-GPU trials, no readback/capture or diagnostic counters, over guest time 78–96 seconds:

| Build | Host seconds for 18 guest seconds |
| --- | --- |
| Archived block-transfer build, runner cap 64 | 23.0886, 23.0984 |
| Leaf regions, runner cap 512 | 18.0999, 18.2646 |

This is 1.270x throughput; the comparison includes the small runner change as well as leaf inlining. All trials execute the same guest instruction endpoints and 676 presentations. These are two repeats per build on a shared host. The new heavy-window mean remains 0.990x realtime, without sufficient headroom.

The actual upload/Start/input route advances 120.516844 guest seconds in 120.467147 host seconds, **1.000413x average**. Keyboard/touch, narrow layout, blur release, visible gameplay and shutdown pass. Input queue delivery is 0.90–4.25ms; this is not input-to-display latency. Intermediate screenshots cover active gameplay. Maximum accumulated lag relative to measurement start is 0.690 seconds; the slowest one-second sample is 0.873x and slowest ten-sample interval 0.941x, followed by catch-up. The next profile targets this remaining slowdown; an average of 1x is not proof of uniformly realtime playback.

A separate normal-Qt/Dynarmic startup attempt under Xvfb/software GL exited with SIGSEGV before any frame. It provides **no usable Qt performance comparison**; no cause is assigned from that attempt. Evidence: `qt-jit-live/report.json` and `run.log`.

Raw timing/live records and binary hashes: `LEAF_EVIDENCE.json`. Browser artifacts remain under `/home/claude/.scratch/eka-benchmark/leaf-*`.
