# Chrome profiling

Use Chrome's sampling profiler for time attribution and its timeline for worker
scheduling, WASM compilation/tiering, GC and rendering. Default WASM builds omit
custom guest histograms, exit census, crash history and detailed scope timers.
The benchmark's deterministic input, guest instruction totals, presentation
count and measurement clock remain available. Correctness verification remains
independently selectable with `EKA2L1_AOT_VERIFY`.

## Capture

Build the normal Release WASM target using the [build instructions](README.md).
`EKA2L1_WASM_DIAGNOSTICS` defaults to `OFF`. Keep optimization and game settings
identical to the execution being investigated. For the stock 5320 Snakes replay:

```sh
cd src/tests/wasm
EKA2L1_GPU=hardware EKA2L1_SHARED_AUDIO=1 \
EKA2L1_BENCHMARK_AOT=5 EKA2L1_AOT_IR_MODE=17 \
EKA2L1_THUMB_MEMORY=1 \
EKA2L1_HOTPATH=2 \
EKA2L1_TLB_HASH=1 EKA2L1_CODE_COMPARE=2 \
EKA2L1_PREDICATED_LEAVES=1 EKA2L1_LEAF_FEATURES=128 \
node profile.ts /absolute/path/to/assets /absolute/path/to/new-capture 1 1 25000000
```

The launcher, profiler and replay harness share defaults for Thumb memory (1),
IR policy (17) and cache-policy specialization (`EKA2L1_HOTPATH=2`). Explicit
environment overrides still select controls or other experiments; hotpath 0
retains the general lookup. See the [cache adoption measurements](CACHE_POLICY_DEFAULT_RESULTS.md).
Match the remaining settings and build to the running service: the
[Thumb memory investigation](THUMB_MEMORY_DEFAULT_RESULTS.md)
found that an explicit profiling override had enabled an optimization absent
from normal play.

Arguments after the paths are capture mode, CPU sampling (0/1), and ending guest
time in microseconds. Mode 1 skips PNG compression while retaining readback and
frame hashes. The default window is 21–25 guest seconds. For Sky Force combat,
use its asset directory and add:

```sh
EKA2L1_APP_UID=0xa020d913
EKA2L1_ASSET_MANIFEST=../benchmark/sky-force-assets.json
EKA2L1_PROFILE_INPUT=../benchmark/sky-force-combat.input
EKA2L1_PROFILE_START_US=42000000
```

Pass these as environment variables on the command and use `46000000` as its
endpoint. `EKA2L1_WASM_BUILD_DIR` selects an archived build for either game.

The capture produces:

- `trace.json`: load into Chrome DevTools **Performance → Load profile**, or
  [Perfetto](https://ui.perfetto.dev). Includes all browser workers and compiler
  threads, including workers created after capture starts. Search for
  `eka2l1:measurement-start` and `eka2l1:measurement-end` to locate gameplay.
- `guest.cpuprofile`: load into DevTools for the likely guest worker, with
  readable guest entry addresses. Names alone are changed after collection;
  samples, stacks, timestamps and module URLs are preserved.
- `worker-N.cpuprofile` and `page.cpuprofile`: untouched CDP sampling output
  for the isolates present at the start of the measurement window.
- `chrome-profile.json`: per-worker time-weighted samples, generated-code entry
  PCs, RAM versions and WASM module URLs. Worker selection uses generated-code
  samples, avoiding the mistake of selecting a parked worker as the bottleneck.
- `report.json`: build/input hashes, browser/GPU, settings, exact guest work,
  wall and CPU timing, diagnostics capability, trace scope and data-loss status.
- `cpu-time.json`: raw before/after Chrome process CPU counters and, on local
  Linux, per-renderer-thread scheduler runtimes. The summary is also recorded in
  `report.json.cpu_time` and retained by `serial_variants.py`.

`EKA2L1_CHROME_TRACE=window` is the sampling default. `run` starts before loading
the emulator and includes startup/compilation; `off` collects only the requested
CPU profiles. Whole-run traces omit the trace's CPU sampler during startup to
avoid duplicate code logs from every isolate; use their separate measurement
window `.cpuprofile` files for CPU stacks. Traces have a 128 MiB buffer; overflow fails the capture and keeps
the partial trace. Shorten the window if this happens. Failed/interrupted runs
carry `incomplete.json`. No Debugger or precise-coverage domain is enabled.

The host markers bracket profiler resume and observed completion; the end mark
can trail actual guest completion by the polling interval. CPU profiles also
include attachment/stop overhead around the guest window. Use the trace to
distinguish gameplay from those margins. Percentages are per isolate's sampled
span, **not summed process CPU utilization**. Futex/condition waits may appear as
samples. Self time includes inlined work. A generated name identifies a region's
entry, not the sampled ARM instruction; its module URL identifies the WASM
module, not a Symbian DLL. DLL attribution or instruction-level investigation
requires additional symbols/disassembly. Do not infer an expensive lookup from
the entire inlined runner's self time.

## Map samples to guest code

Generated names already encode ARM/Thumb entry PCs. Map those entries to the
exact ROM's DLL code ranges offline, without rebuilding or instrumenting the
guest:

```sh
python3 src/tests/benchmark/map_chrome_guest.py /absolute/path/to/capture \
  /absolute/path/to/assets/SYM.ROM
```

This checks the ROM hash against `report.json`, then writes `guest-rom.json`
(module totals and sampled entries) and `guest-rom.cpuprofile` (a labelled copy
for DevTools). It supports expanded EKA2 ROMs. Optional matching Symbian export
definitions can add names with `--symbols DRTAEABI.dll=/path/to/drtaeabiu.def`;
only exact export entries receive symbols. Internal functions retain DLL offsets
until independently identified by disassembly. Never name an internal function
using its nearest preceding export. Keep definition files from the matching ABI;
the mapper records their hashes but cannot establish their version compatibility.

RAM entries retain their guest PC and compiled version but have no DLL name:
reliable attribution additionally needs process/ASID, load lifetime and the
corresponding executable mapping. Inlined callees can contribute time to their
containing region. Neither output assigns samples to individual ARM instructions.

In the [gameplay captures](CHROME_GAMEPLAY_ANALYSIS.md), Chrome trace `columns`
repeat the WASM function entry, including across hundreds of samples. More
precise attribution would require native instruction samples with V8 JIT
metadata, plus a generated WASM-offset-to-guest-PC map that records inlining.
Adding guest source labels alone cannot recover missing instruction positions.

## Timing and experiment decisions

For throughput, repeat the same command with sampling **0** and
`EKA2L1_CHROME_TRACE=off`. Keep custom counters, monitoring, crash/GL diagnostics
and AOT verification off. Such reports are labelled `purpose: "throughput"`;
profiled or instrumented reports are labelled `diagnostic`. Detailed custom
counter fields are absent when not collected, rather than reported as zeros.

The [CPU-time validation and game measurements](CPU_TIME_RESULTS.md) record the
initial verification and its remaining variance.

CPU time is collected automatically, with two host-side snapshots at the existing
pause/completion boundaries. No guest hot-path counters or sampling are needed.
`cpu_time.renderer_cpu_seconds` sums Chrome renderer process CPU deltas; it includes
emulation, graphics, audio and JIT/compiler threads and can exceed wall time.
It excludes GPU device execution. `cpu_time.threads` records Linux scheduler
runtime deltas in seconds, with PID, TID and thread name. Sleeping and descheduled
time are excluded. Thread deltas retain nanosecond precision; Chrome process
counters can have coarser platform resolution.

`cpu_time.busiest_renderer_thread.cpu_seconds` is useful for CPU-bound emulator
changes, but its name does not establish guest-worker identity. Confirm that
identity in a representative diagnostic trace: match `Profile` and `ProfileChunk`
events by PID/profile ID, find the stream containing generated ARM functions,
and compare the owning `Profile` event's TID with the CPU-time report. Retain
renderer totals to catch work shifted to other threads. A changed/missing process
makes the renderer total null; new, missing or recycled threads are explicitly
reported rather than silently charged to the wrong task.

CPU snapshots bracket host resume and observed completion, so they include small
collection/polling margins outside the exact guest wall-clock interval. Collection
cost and host interval are reported. The emulator CPU thread is paused before
resume and stops guest execution at completion; other renderer work may continue
within the margins. Compare identical guest instruction totals and presentation
counts. CPU time removes scheduling/wait noise, but CPU frequency, cache contention
and JIT work can still vary. Use serial runs in both orders and retain wall time
as the measure of actual elapsed gameplay throughput. CDP sample-weighted profile
durations are elapsed time, not a substitute for these OS CPU counters.

Validate CPU-vs-wait discrimination on a local browser with:

```sh
cd src/tests/wasm
node --test cpu-time.test.ts chrome-profiler.test.ts
node cpu-time-browser-check.ts
```

Use one short representative capture per game to pick a specific expensive
operation. Check its callers and the trace's wait/compilation activity. Then
test one hypothesis with focused correctness checks, a short exact replay, and
a small serial baseline/candidate comparison in both orders. Preserve startup
time, fixed guest-work totals and losing results. Extend testing only for a
repeatable useful gain. A large sample bucket is an upper bound on opportunity,
not evidence that a proposed change will remove that time.

The [iterative dispatch campaign](PROFILE_OPTIMIZATION_PLATEAU_RESULTS.md)
records an adopted ROM lookup improvement, a RAM lookup gain that shrank after
confirmation, and rejected verifier/guard-publication screens. It preserves
the prospective expansion and stopping rules alongside every observation.

Profiling can change V8 tiering and adds overhead, so never claim speedup from
profiled timings. See [V8's compilation and profiling documentation](https://v8.dev/docs/wasm-compilation-pipeline)
and the [Chrome tracing protocol](https://chromedevtools.github.io/devtools-protocol/tot/Tracing/).
For a machine-instruction hypothesis, the existing
[`inspect_jitdump.py`](inspect_jitdump.py) and [V8 Linux perf workflow](https://v8.dev/docs/linux-perf)
remain available. This switch does not turn sampling into a causal profiler.

## Optional emulator diagnostics

For a specific semantic question such as why a compiled region exits, configure
a separate build with `-DEKA2L1_WASM_DIAGNOSTICS=ON`. Point the runner at that
build, then explicitly set `EKA2L1_PROFILE_DETAIL=1`. Guest sampling additionally
uses `EKA2L1_GUEST_PROFILE=1021`; exit reasons use `EKA2L1_EXIT_CENSUS=1` with guest
sampling. Crash history uses `EKA2L1_AOT_DIAGNOSTICS=1`.

Ordinary builds reject these requests with configuration error `-2`. Disabling
them succeeds in both builds. Native builds retain their existing diagnostic
capabilities. Census-specific tests report a skip when diagnostics are absent;
run them on the diagnostic build to verify census behavior. Chrome capture and
unprofiled timing require neither a diagnostic build nor injected guest code.
