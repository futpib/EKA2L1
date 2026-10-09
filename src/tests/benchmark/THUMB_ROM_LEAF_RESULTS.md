# Inline short immutable Thumb helpers

Not adopted. The complete prototype is archived and removed from active source.

The sampled Sky Force scheduler calls short ordinary Thumb helpers. These
are guest function calls, not syscalls or JavaScript transitions. Previously
the caller returned to the C++ WASM runner, which looked up and called the
helper; its return caused another dispatch to the caller continuation.
This prototype emits eligible immutable helper bodies inside the caller.

## Controlled runtime result

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.1006 → 7.0838 | +0.24% | +0.28% | -0.05% | 2/4 |
| 2 | Sky Force | 22.0232 → 22.5390 | -2.29% | -2.12% | -1.24% | 1/4 |

The prototype does not earn adoption: Sky Force loses 2.29% CPU throughput, with three of four pairs slower, despite 1.24% fewer native instructions. Snakes is effectively flat (+0.24%, mixed pairs). The [single-budget-proof follow-up](THUMB_LEAF_BUDGET_RESULTS.md) records the subsequent result.

Control is `536ea5af3`, including the previous syscall-return continuation.
These are incremental measurements. Four fresh launches per build per game
use ABBA then BAAB order, Snakes 78–96 guest seconds and Sky Force combat
42–60. Every valid observation is retained. Correctness, instruction progress
and presentation journals match. Source maps and sampling are disabled in
normal timing builds.

There are 16 valid observations and 0 retained invalid attempts.
The clock request is 3.6 GHz, the worker uses CPU 7 and sibling 15 is reserved.
Clock, throttle, affinity and counter checks apply; there are no temperature
gates or cooldown waits. All 88 host-restoration checks pass.
See [method](CONTROLLED_BENCHMARKS.md) and [evidence](THUMB_ROM_LEAF_RESULTS.json).

## Scope and actual native code

Eligibility uses instruction bytes, not game-specific addresses. The helper
must lie in immutable ROM and contain only supported register operations,
direct reads and forward branches ending in `BX LR`. Expanded paths share
the existing 32-instruction leaf limit and eight-site cap. Stores, nested
calls, indirect branches, backward branches and incomplete windows are
rejected. Guest counts are static on each path; no runtime accumulator is
introduced. A direct-read miss resumes at the original guest instruction.

Equal-length return paths continue inside the caller, removing both dispatches.
Unequal-length paths return their precise static counts to the runner after
the helper, removing the call-side dispatch. Short budgets, raw return PCs,
flags, stop/IRQ boundaries and memory access order remain observable.

Actual warmed gameplay V8 code confirms that caller `0x801a0f96` includes
the boolean helper at `0x801b9a48`, including its load at `0x801b9a4a`.
The caller now returns static combined counts 9 or 10, where it formerly
returned 3 before dispatching the helper. Its native body grows from 448 to
1,472 bytes; the formerly separate helper is 1,280 bytes. The standalone
helper is absent from the candidate sampled/selected versions. This proves
the code-shape change, not a call frequency or a performance guarantee.

All 160 candidate selected native versions were recovered without snapshot
or sampling errors or lost samples. The control capture is reused from its
identical adopted artifact. These diagnostic captures are not controlled
timing comparisons or new source-line attribution.

## Verification

The full AOT suite passes 184 tests with zero failures on the prototype before
a source-address annotation correction. That correction changes profiling
metadata from tagged Thumb addresses to actual even instruction addresses.
The final artifact passes 38,720 independent interpreter comparisons, 336
raw-PC/IRQ/stop/backend boundary checks and ten rejected-shape cases.
Both final-artifact 60-frame replays match reference images, guest progress,
audio PCM and audio events exactly. Native capture and controlled timings
use the same final artifact. Existing suite diagnostic skips and XFAIL remain.

Production source and LAN retain the preceding adopted implementation.

Candidate unpaced wall speed at 3.6 GHz: Snakes **2.10×**
realtime; Sky Force combat **0.72×**.

Raw builds, source patch, plans, commands, all observations and test/profile
artifacts are archived at `/home/claude/.scratch/eka-thumb-hot-inline`.
