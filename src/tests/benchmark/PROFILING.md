# WASM performance profiling

Initial findings and ranked options: [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md).
Compiled execution follow-up: [AOT_RESULTS.md](AOT_RESULTS.md).

The profiling APIs are opt-in and use the existing deterministic replay. They
pause between guest dispatches at 21 guest seconds, let the browser profiler
attach, and measure until 25 guest seconds. Host measurements never advance the
guest. The benchmark's usual frame count, timing, and comparison behavior remain
unchanged when profiling is not configured.

Build both targets as described in README.md, then:

```sh
cd src/tests/wasm
node profile.ts /absolute/assets /absolute/new-output 0 1
cd ../../..
python3 src/tests/benchmark/summarize_profile.py /absolute/new-output
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/new-batch
```

The positional arguments after output are capture mode, CPU sampling (0/1), and
optional end guest microseconds (default 25000000). Mode 0 retains full PNG
capture; mode 1 retains framebuffer readback, deduplication and frame records
but skips PNG encoding; mode 2 skips the diagnostic readback/capture hook while
still rendering and synchronizing presentations. All modes stop at the same
guest deadline. No audio export is performed by this performance harness.

The runner saves `report.json`, a browser screenshot, any captured frames, and
one Chrome `.cpuprofile` per isolate when sampling is enabled. Import these files
into DevTools for stack inspection. It profiles the page and every pthread
worker: page-only profiling misses guest emulation. The summary marks workers
parked in condition/futex waits separately from busy work; sampled stacks in a
wait are not CPU consumption. `nonidle_seconds` is an approximate ranking aid,
not OS CPU accounting.

C++ scope durations overlap: `cpu_run` is inside `cpu_loop` and includes guest
services and synchronous graphics waits; renderer scopes run on another thread,
and contain the display hook, readback and capture. Do not add these durations
or per-isolate percentages together. Translation/cache counters are written on
the guest CPU thread and read only after the end-of-window handoff.

`profile_batch.py` warms four fresh browser fixtures concurrently, waits until
all guest threads are paused, then measures full capture, no PNG, no readback,
and full capture again **serially**. This prevents emulator runs competing during
the measured windows. Each run has fresh assets and the same scripted input.
`PROFILE_GATE=/absolute/path` allows an external coordinator to use the same
barrier: the runner creates `.ready`, then waits for the gate file before
measurement. Gates must be new paths. A full-versus-full repeat helps distinguish
small changes from host timing variation.

The default Chromium flags deliberately use SwiftShader, matching the correctness
harness. Reports include the actual WebGL renderer and browser version; these
measurements do not establish performance on a physical GPU. Build flags,
assertions, guest clock model, audio callbacks and graphics synchronization remain
the same across capture modes. Sampling and scope instrumentation have overhead;
compare the unsampled repeated runs for timing effects, and use samples to
identify functions rather than claim cycle-accurate costs.

For an AOT comparison, add `--compare-aot` to `profile_batch.py`. It measures
interpreter, repaired exports, hot ROM twice, and interpreter again, with full
capture and CPU sampling disabled for every fixture. Per-block verification is
also disabled. Reports include `aot_dispatches` and `aot_instructions`; divide
the latter by `last_instructions - first_instructions` for executed instruction
coverage. This is instruction coverage, not a percentage of CPU time. Stop
other benchmark/build jobs before running measured windows. The hot mode remains
opt-in; see [AOT.md](AOT.md) for configuration and safety boundaries.

## Long-run speed and memory monitoring

Use `EKA2L1_LONG_MONITOR=1` for windows beyond 120 guest seconds (up to 30
minutes). This disables retention of benchmark PCM/event exports, while keeping
all guest audio consumption and callbacks; live play already discards these
artifacts. Use capture mode 2 to avoid accumulating PNGs/frame records too.
It does not enable live host pacing or change scripted benchmark input.

```sh
EKA2L1_BENCHMARK_AOT=5 EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0 \
EKA2L1_LONG_MONITOR=1 node profile.ts ASSETS NEW_OUTPUT 2 0 600000000
```

`timeline.json` samples guest time, instructions, presentations, allocator
allocated/free bytes, page JS heap/backing storage and the atomic instantiated-AOT-function count,
and summed browser-process PSS approximately every two host seconds. Snapshots
are observational and not synchronized across guest execution and browser processes; guest counters
are atomic, while malloc accounting uses the allocator's own lock. The recorder
writes a scene screenshot each guest minute. Probe overhead is recorded, but
screenshots and sampling remain included in elapsed time: compare diagnostic
runs to one another, not as zero-overhead performance claims. V8 backing storage
can include shared buffers in multiple isolates; do not sum those values as
unique process memory. WASM linear-memory capacity alone cannot establish a leak.
Raw PSS can fluctuate with browser/GPU caches and other shared processes.

Summarize fixed guest-time windows with
`python3 ../benchmark/summarize_long_run.py NEW_OUTPUT` (from `src/tests/wasm`).
For a separate diagnostic run, set `EKA2L1_MONITOR_CPU_START_US` to start CPU
sampling partway through the replay and keep it running to the endpoint.
`cpu-window.json` records the actual start; those intervals include profiler
cost and are not timing controls. The monitor also records main-thread-visible
running/unused worker-pool sizes when exposed by the Emscripten runtime.

`EKA2L1_PROFILE_INPUT=../benchmark/snakes-long.input` uses menu entry/turns
repeated at four-minute offsets, intended to re-enter gameplay after the first
run ends without resetting the emulator. Inspect the saved scene images before
labeling any interval as gameplay; menu residence can be much faster and must
not be counted as sustained gameplay throughput. The long-monitor replay input
limit follows its configured endpoint; ordinary replays retain the 120s limit.
