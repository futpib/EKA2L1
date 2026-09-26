# WASM performance profiling

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
