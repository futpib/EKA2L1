# Chrome profiling

Use Chrome's sampling profiler for time attribution and its timeline for worker
scheduling, WASM compilation/tiering, GC and rendering. Default WASM builds omit
custom guest histograms, exit census, crash history and detailed scope timers.
Scripted input, presentation counts and guest clock measurements remain
available. Browser execution uses the watchdog; instruction totals stay zero
and the instruction-count verifier is removed. The default count-free Snakes
route is sampled from 42–46 guest seconds.

## Capture

Build the normal Release WASM target using the [build instructions](README.md).
`EKA2L1_WASM_DIAGNOSTICS` defaults to `OFF`. Keep optimization and game settings
identical to the execution being investigated. For the stock 5320 Snakes replay:

```sh
cd src/tests/wasm
EKA2L1_GPU=hardware EKA2L1_SHARED_AUDIO=1 \
EKA2L1_BENCHMARK_AOT=5 EKA2L1_AOT_IR_MODE=7 \
EKA2L1_THUMB_MEMORY=1 \
EKA2L1_HOTPATH=2 \
EKA2L1_CODE_COMPARE=2 \
EKA2L1_PREDICATED_LEAVES=1 EKA2L1_LEAF_FEATURES=128 \
node profile.ts /absolute/path/to/assets /absolute/path/to/new-capture 1 1 46000000
```

The launcher, profiler and replay harness share defaults for Thumb memory (1),
IR policy (7) and cache-policy specialization (`EKA2L1_HOTPATH=2`). The normal
launcher fixes the graduated lookup, syscall and Thumb-memory choices; explicit
environment overrides in `profile.ts` and `benchmark.ts` retain targeted
controls. Hotpath 0 retains the general lookup in those test harnesses. See the [cache adoption measurements](CACHE_POLICY_DEFAULT_RESULTS.md).
Match the remaining settings and build to the running service: the
[Thumb memory investigation](THUMB_MEMORY_DEFAULT_RESULTS.md)
found that an explicit profiling override had enabled an optimization absent
from normal play.

Arguments after the paths are capture mode, CPU sampling (0/1), and ending guest
time in microseconds. Mode 1 skips PNG compression while retaining readback and
frame metadata; it does not save pixel hashes. The default window is 42–46 guest
seconds. For Sky Force combat,
use its asset directory and add:

```sh
EKA2L1_APP_UID=0xa020d913
EKA2L1_ASSET_MANIFEST=../benchmark/sky-force-assets.json
EKA2L1_PROFILE_INPUT=../benchmark/watchdog-sky-force-countfree.input
EKA2L1_PROFILE_START_US=58000000
```

Pass these as environment variables on the command and use `62000000` as its
endpoint. Review the captured scene: the older Sky Force route can remain in a
menu under watchdog-only scheduling. The new route reaches combat, but equal
guest-clock windows and frame counts do not guarantee equal gameplay progress;
see the [handoff investigation](HANDOFF_NATIVE_PROFILE_RESULTS.md).
`EKA2L1_WASM_BUILD_DIR` selects an archived build for either game.

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
- `report.json`: build/input hashes, browser/GPU, settings, guest clock and presentation counts,
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

## Native samples with ARM and C++ source attribution

The [Sky Force investigation](NATIVE_ATTRIBUTION_RESULTS.md) uses actual warmed
browser code, including its natural Liftoff/TurboFan tiers and call feedback.
It does not force TurboFan or compile an unexecuted module for attribution.

Configure an existing Release WASM build with both metadata options, retaining
its normal optimization settings:

```sh
cmake -S . -B build-wasm -DEKA2L1_AOT_SOURCE_MAPS=ON -DEKA2L1_WASM_CPP_SOURCE_MAPS=ON
cmake --build build-wasm --target eka2l1_wasm -j4
```

Both options default to OFF. The first records non-executable guest provenance
in each generated module's `eka2l1.sources` custom section. It follows byte
insertions, state-transfer pruning, inlined ARM callees and outlined functions.
Offsets include the function's local declarations. Thumb PCs retain bit zero.
PC zero denotes shared compiler-generated setup or state transfers, without a
unique originating guest instruction. `guest` is a broad lowering category,
not a claim that all its instructions would exist on a physical ARM CPU.

The second retains C/C++ line tables and links `eka2l1.wasm.map`, including
optimized emulator code, templates and linked libraries. It does not lower
optimization. The capture records matching WASM/map hashes and the map's source
base directory. Build this separately from artifacts used for throughput tests.

For Sky Force combat, from the repository root:

```sh
EKA2L1_WASM_BUILD_DIR="$PWD/build-wasm/src/emu/wasm" \
EKA2L1_APP_UID=0xa020d913 \
EKA2L1_ASSET_MANIFEST="$PWD/src/tests/benchmark/sky-force-assets.json" \
EKA2L1_PROFILE_INPUT="$PWD/src/tests/benchmark/sky-force-combat.input" \
EKA2L1_PROFILE_START_US=42000000 \
EKA2L1_BENCHMARK_AOT=5 EKA2L1_SHARED_AUDIO=1 EKA2L1_GPU=hardware \
EKA2L1_NATIVE_PROFILE=1 EKA2L1_CHROME_TRACE=off \
node src/tests/wasm/profile.ts /absolute/sky-assets /absolute/new-capture 1 0 60000000

python3 src/tests/benchmark/native_attribution.py /absolute/new-capture
```

Match any additional game/compiler overrides to the execution being investigated.
Native mode requires CDP sampling and long monitoring off, and captures modules
from all isolates after measurement automatically. Allow roughly 1 GB for a
capture. Output paths must not contain whitespace.

This currently supports Linux x86-64 and **V8 15.3.76.13**. The sampler uses
`perf_event_open` directly for userspace task-clock samples, with a 1 ms CPU
period. It does not require the `perf` executable or per-instruction guest
counters. It samples renderer threads present at the start and records new or
missing threads at completion. Lost records, kernel sampling throttling, or
partial setup fail the capture. The guest worker is identified by samples in
generated guest functions, not by a thread name or the busiest thread alone.

V8's `--perf-prof` supplies timestamped native code loads and bytes. After
sampling stops, the browser's parent reads live `WasmCode` metadata through
`/proc/PID/mem`, before any Debugger attachment. The version-specific adapter
checks function index, tier, object layout, native bytes/hash and a second read.
It preserves raw source tables and V8 inline-function metadata. Unsupported V8
versions fail before guest startup; updating the version string alone is not a
validated port. Host ptrace/perf policy must permit these reads/events.

The installed Chrome does not export WASM line tables through
`--perf-prof-annotate-wasm`; supplying a source map alone did not make it do so.
The offline join therefore uses V8's live tables directly:

```text
userspace CPU sample + timestamp
  → V8 code load/version + native offset + matching bytes
  → V8 source position + inlined function ID
  → WASM function/body offset
      → generated ARM/Thumb PC and lowering category
      → C/C++ file, line and column
```

Outputs include:

- `native-samples.json`: CPU samples, event configuration and loss/coverage audit.
- `jit-*.dump`, `native-metadata.json`, `native-*.bin`: actual code versions,
  hashes, source positions, inlinees and registered trapping offsets.
- `runtime.wasm.map`, `runtime-source-map.json`, captured `*-module-*.wasm`:
  matching source inputs. The analyzer checks the runtime's capture hash.
- `native-attribution.json`: selected-worker totals, native functions and hot
  instruction offsets, with exact ARM/C++ locations when available.
- `native-*.annotated.asm`: native disassembly with sample counts and source
  anchors. Empty source fields remain empty.
- `source-maps/*.wasm.map` and `*.arm.txt`: standard version-3 source maps and
  guest-PC pseudo-sources exported from the embedded provenance. They are
  offline artifacts, not automatically installed DevTools breakpoints.

**Coverage is sparse.** Optimized V8 retains selected call/trap source anchors;
ordinary arithmetic often has no source position. Registered trapping operations
can inherit their source anchor; the analyzer never extends a line label across
arbitrary native instructions. Missing positions, uncaptured code versions and
samples outside JIT code remain explicit. The snapshot selects the 160 hottest
WASM versions; a retired version may no longer have live metadata. Address reuse
is matched against sample timestamps, and unsupported JIT movement is rejected.
C++ self samples can include inlined C++ callees. There are no inclusive stacks
or instruction-latency estimates in this native report. CPU timer samples may
land after the operation responsible for a stall; a hot branch is not proof of
branch misprediction. Report these diagnostics separately from throughput.

Validation commands:

```sh
node --test src/tests/wasm/native-profile.test.mjs
python3 -m unittest discover -s src/tests/benchmark -p test_native_attribution.py
node src/tests/wasm/native-profile-browser-check.mjs /absolute/new-probe-output
```

The browser check executes a loop through natural tiering and verifies that
native trapping loads resolve to the exact WASM load opcode. `test_source_maps`
checks metadata relocation through state caching/pruning. The investigation also
includes the full AOT suite and an exact gameplay image/audio/progress replay.

The adapter follows V8's [position table encoding](https://chromium.googlesource.com/v8/v8/+/15.3.76.13/src/codegen/source-position-table.h),
[WasmCode metadata](https://chromium.googlesource.com/v8/v8/+/15.3.76.13/src/wasm/wasm-code-manager.h)
and [Linux profiling support](https://v8.dev/docs/linux-perf).

## Timing and experiment decisions

For throughput, repeat the same command with sampling **0** and
`EKA2L1_CHROME_TRACE=off`. Keep custom counters, monitoring, crash/GL diagnostics
off. Such reports are labelled `purpose: "throughput"`;
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
within the margins. Compare equivalent visible gameplay and output sequences;
matching guest times or presentation totals alone does not establish equal work.
The watchdog clock can change its execution schedule between runs. CPU time removes scheduling/wait noise, but CPU frequency, cache contention
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

## Retired instruction diagnostics

Instruction-count verification, guest sampling, detailed instruction counters
and crash-history requests are rejected by the browser harness. They cannot be
restored by selecting a diagnostic build. Chrome sampling/tracing and host CPU
snapshots remain available without instruction accounting. Native diagnostics
and standalone compiler tests retain their own coverage.
