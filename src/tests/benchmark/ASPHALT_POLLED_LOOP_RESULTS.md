# Asphalt 2: keep callback-free Thumb backedges compiled

Source baseline: `8d67829de`, `wasm-port`, 2026-10-12. Follow-up to
[RAM helper and branch fusion](ASPHALT_RAM_FUSION_RESULTS.md).

## Current native evidence

A fresh hardware Chrome capture of the deployed RAM-fusion build collected
7,016 CPU-worker userspace task-clock samples, with no lost samples, errors,
new threads or missing threads. Native bytes and V8 metadata were recovered for
all 100 selected hot functions. The debugger collected generated modules only
after sampling and metadata collection. All native profiling FPS is excluded
from the performance comparison below.

`execute_chain` accounts for 1,790 samples (25.51%). Reading the current x86
disassembly identifies these disjoint sampled ranges; percentages use all
CPU-worker samples. Sample skid and preceding stalls prevent treating these as
intrinsic costs of individual instructions.

| # | Dispatcher work | Native offsets, end exclusive | Samples | Worker share |
|---|---|---|---:|---:|
| 1 | Indirect-call setup, checks and call | `0xc62..0xcc8` | 702 | 10.01% |
| 2 | RAM dispatch lookup and next-function selection | `0xe64..0xf36` | 624 | 8.89% |
| 3 | Stop, IRQ and watchdog checks | `0xcd0..0xd45` | 199 | 2.84% |
| 4 | PC normalization and ROM routing | `0xd45..0xd9f` | 167 | 2.38% |

The next hot functions are the framebuffer converter (7.35%), texture loop
`0x70004c6e` (5.87%), texture loop `0x70006aba` (5.73%) and drawing loop
`0x70007132` (4.63%). Their native prologues reload guest registers and spill
values. The first two texture functions occupy 18,048 and 28,032 native bytes;
those are whole-function inventories, not executed instruction counts.
The framebuffer still has separate byte stores and repeated memory checks.

Sparse source anchors map 524 samples to guest instructions and 377 to C++;
4,715 selected-function samples lack an exact instruction position, 904 fall in
JIT code outside the selected snapshots, and 496 lie outside logged JIT code.
Those unknowns remain explicit. The sampled ranges above are classified from
native instructions, not interpolation of sparse source positions.

The candidate capture contains 7,433 CPU-worker samples, again with no losses,
errors or thread-set changes and all 100 selected native functions recovered.
Dispatcher share falls to 1,302 samples (17.52%). This supports the intended
mechanism; it is not an independently measured 31% reduction in dispatcher time.
The profiles are separate diagnostic runs, excluded from the timings below.

The native texture function at `0x70006aba` now branches from offset `0x623`
to `0x140` after polling the watchdog and stop/IRQ state. That successful edge
does not return to the dispatcher or call another compiled function. Its native
body still has register spills, memory guards and a V8 interrupt check. Native
size grows from 28,032 to 33,984 bytes: the improvement is less repeated dispatch,
not smaller functions. The framebuffer converter remains at 1,600 bytes and
8.41% of candidate samples; texture entries `0x70004c52`, `0x70006aba` and
`0x70006a34` account for 6.31%, 6.00% and 5.42%. Those memory/checking paths and
the remaining 17.52% dispatcher share are still material costs.

Candidate sparse positions map 553 samples to guest instructions and 295 to C++;
4,940 lack an exact position, 1,050 are JIT code outside the selected snapshots,
and 595 are outside logged JIT code. No unknown sample bucket is silently
assigned to guest instructions or emulator overhead.

## Change and safety boundaries

For trusted direct-memory Thumb code, a conditional or unconditional backedge
to the current block's entry can branch inside the compiled WASM function.
It retains registers and derived memory state instead of returning, performing
another lookup and indirect call, and reloading those values.

Eligibility requires a prefix with no memory callbacks. Scalar misses return
before the original guest access, preserving prior stores and exact state.
The backedge polls the existing external atomic watchdog plus stop/IRQ state.
Existing statically proved finite loops remain poll-free. Nested loops share
function-entry initialization of their cached registers so a later iteration
cannot reload stale values from the published CPU state. Conservative memory
and executable-byte modes retain their previous behavior.

This adds no launch option, instruction accounting, guest-time adjustment,
game-specific address check or asset patch. The earlier pre-branch-fusion
loop experiment was inconclusive; these measurements apply to the current
fusion configuration and are not a reinterpretation of its historical result.

## Matched paced comparison

Original signed Asphalt 2 1.0.1 N95 8GB, stock Nokia 5320, Arcade / Single Race /
red Volkswagen Golf GTI Mk V / San Francisco / no steering / first outside
barrier. Car, track and measured scenes were checked in screenshots. The car
nudges along the barrier: this is matched gameplay, not an exact replay.

Chrome 153.0.8010.52 on i7-10875H, NVIDIA ANGLE/Vulkan, ordinary paced count-free
execution, 2 ms watchdog, trusted code 3 and direct memory 2. Browser output is
muted while guest audio remains active. The baseline uses the HTTPS LAN picker;
the candidate uses the same production launcher on a private localhost server.
Both browsers are fresh and have no profiler or debugger attached before or
during timing. Owned competing renderer processes are suspended, with five
seconds to recover before resumed measurements. No build or test runs overlap
the accepted windows.

Order: candidate, baseline, baseline, candidate; 25 seconds per window.

| # | Build | First / second FPS | Pooled FPS | CPU-worker ms/frame |
|---|---|---:|---:|---:|
| 1 | RAM fusion baseline | 15.91 / 14.95 | 15.43 | 23.63 |
| 2 | RAM fusion with polled backedges | 16.70 / 15.59 | 16.14 | 20.28 |

The aggregate gain is **4.63% FPS**, with **14.18% less CPU-worker time per
presentation**. Both adjacent comparisons favor the candidate: approximately
4.95% / 4.29% FPS and 15.8% / 12.7% lower CPU time per presentation.
These short shared-host runs show material variation, not confidence intervals.
The adjacent controls, not earlier campaign FPS, establish this comparison.
Presentation counts include repeated images; GPU device time is not included
in CPU-worker time. Guest/host time remains approximately 1.0 and instruction
totals remain zero.

Recorded translation/emission/installation time remains included: baseline
43.2 / 5.9 ms, candidate 59.4 / 18.3 ms. This cannot account for the measured
per-frame CPU difference. Browser JIT compilation was not separately measured.

## Correctness and evidence

The full compiler suite passes 187 tests, zero failures. The final focused
fixture additionally passes 360 interpreter comparisons for ordinary polled
loops and 336 for loops containing conditional RAM helpers, both directly
entered and reached through branch fusion. It compares registers, flags and
complete memory across multiple iterations, transparent/opaque stores,
permission failures, page boundaries, endian/unaligned fallback and watchdog
requests. A real external worker interrupts an otherwise infinite guest loop.
Callback-bearing bodies and conservative configurations are rejected.

The candidate is deployed at `https://claude-laptop.lan:8188/`. The downloaded
WASM hash matches the frozen candidate below. The production game-picker E2E
passes Sky Force and Snakes gameplay, switching, keyboard, simultaneous touch,
mobile/landscape layout, manual launch and non-silent browser audio, with no
recorded errors. Gameplay screenshots were checked. Its private audio service
uses a null sink; this proves browser PCM delivery, not audible speaker quality.
Owned benchmark browsers, the private candidate server and private audio
daemons were stopped after verification.

Raw archive: `/home/claude/.scratch/eka-asphalt-native-20261012/`.
`wasm-before/` and `wasm-after/` hold native captures, annotated disassemblies,
generated modules and source maps. `wasm-control/` and `wasm-polled-d/` hold
accepted timing, CPU counters, host frequency/load records and screenshots.
`comparison.json` contains the pooled calculation; `picker/` holds live E2E
results. The compact [committed evidence](asphalt-polled-loop-results.json)
retains measurements, native summaries, artifact hashes and browser checks.

Two early candidate launches lacked the frozen frontend companion files and
failed before game selection; a third preceded server readiness. These are
retained as failed setup attempts and contribute no timing data.

| # | Frozen WASM | SHA-256 |
|---|---|---|
| 1 | Baseline | `c9bbc6779e43e90eb1d90b53b97b94719d2ae7378de290d7340a02b5d7708e75` |
| 2 | Candidate | `72458db489c3d5502c8372fb341e5bf8a313ca9ccf00434fdb75c947f84c2c9b` |
