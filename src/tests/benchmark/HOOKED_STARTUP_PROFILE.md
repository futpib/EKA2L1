# Hooked On startup profile after the startup follow-up

Hooked On remains the slowest fresh installation. On the deployed build,
filesystem handoffs, guest hashing, compiler work and mapping maintenance
are all substantial. Almost all sampled compilation completes in the first
30 seconds; the remaining installation is not a long compilation stall.

This is profiling only. Production code, settings and deployment are unchanged.
The preceding [startup follow-up](STARTUP_FOLLOWUP_RESULTS.md) contains the
unprofiled startup timings and gameplay regression checks.

## Capture and validity

Captured on 2026-10-12 from `https://claude-laptop.lan:8188/`, source
`6ba90714f` on `wasm-port`. The downloaded WASM SHA-256 matches the deployed
artifact: `c0103c19db43678dcbe2c53ff41fb70f2a15e6955a444833c01fd0e09d244cb5`.
Both captures read back the normal 2,000-us watchdog, unsafe-code mode 3 and
memory mode 2. Instruction totals remain zero.

Chromium 153.0.8010.52, i7-10875H, NVIDIA T1000 ANGLE/Vulkan; normal paced
launcher, fresh browser/cache/guest installation, warm server ROM/game cache.
Internet/IPFS retrieval is excluded. Runs were serial, with no own build or
benchmark running concurrently. Ordinary background host work was retained.

CDP `Profiler` samples at 1 ms, without Debugger or coverage instrumentation.
Worker discovery starts before navigation. The first capture profiles the
page and all 33 workers; the repeat profiles only the page and the emulation
worker. Its identity is confirmed by `execute_chain`, interpreter and generated
guest stacks. Successful installer exit and the **Installation complete**
screenshot are checked in both runs. Neither run reports a guest/browser error
or a profiler attachment error. This endpoint is installation completion,
not gameplay readiness.

| # | Capture | First presentation | Installation complete | Purpose |
| ---: | --- | ---: | ---: | --- |
| 1 | All workers | 9.34 s | 93.63 s | Find worker roles and cross-thread waits |
| 2 | Focused repeat | 8.58 s | 78.54 s | Primary attribution with less profiling overhead |

The all-worker capture perturbs this workload substantially. Its higher costs
are retained in the evidence, not presented as normal performance. The focused
run is close to the preceding unprofiled 79.72–88.36-second results, but is still
a diagnostic run, not a new speedup measurement.

## Where the emulation worker spends its time

The table uses the focused repeat, clipped to receipt of the successful
installer-exit log. These are **sample-weighted elapsed seconds**, not exact
CPU timings or promised removable delays. Categories are disjoint; nested
examples later in this report must not be added to them.

| # | Category | Sampled seconds | Share of sampled interval |
| ---: | --- | ---: | ---: |
| 1 | Generated ARM translations | 13.81 | 17.7% |
| 2 | Waiting for main-thread filesystem operations | 13.27 | 17.0% |
| 3 | Sleep/pacing (`nanosleep`) | 11.31 | 14.5% |
| 4 | ARM compilation and module preparation subtree | 9.74 | 12.5% |
| 5 | Other active runtime work | 9.02 | 11.6% |
| 6 | Memory-view maintenance subtree | 6.97 | 8.9% |
| 7 | `execute_chain` self, including inlined work | 5.60 | 7.2% |
| 8 | Startup idle / unattributed program samples | 4.88 | 6.3% |
| 9 | Interpreter outer-loop self, including inlined work | 3.04 | 3.9% |
| 10 | Graphics and other waits | 0.35 | 0.4% |

Total: 78.00 sampled seconds. The emulation worker profiler starts about
0.19 seconds after navigation; chunk rotations leave small gaps. The cutoff
aligns CDP's monotonic clock using its start acknowledgement, so small RPC
skew is possible. Post-completion screenshot/polling time is excluded from
this table, but remains in the raw profiles.

The independent Linux scheduler bracket runs from first presentation through
the final screenshot: 73.01 seconds elapsed, 47.95 seconds on the busiest
renderer worker and 84.54 renderer CPU seconds across all threads. It has a
different window and includes the main thread, rendering and compiler threads;
it is not interchangeable with the table's sampled seconds.

### Filesystem handoffs: the clearest remaining overhead target

The 13.27 seconds are `emscripten_futex_wait` stacks under
`_emscripten_run_on_main_thread_js`, with file syscalls above them:

| # | Operation | Sampled wait |
| ---: | --- | ---: |
| 1 | `stat64`: existence/metadata checks | 4.91 s |
| 2 | `fd_seek`: positions and file sizes | 4.37 s |
| 3 | `getdents64`: directory enumeration | 2.03 s |
| 4 | Other file operations, including reads/writes/open/close | 1.97 s |

This is a real worker-to-main-thread JS boundary. It is not guest disk latency
or merely another WASM function call. Browser-main-thread filesystem work and
handoff/scheduling latency contribute to the wait; this capture does not
separate them exactly.

Source and stacks give concrete candidates:

- `physical_file::size()` saves the position with `ftell`, seeks to the end,
  calls `ftell` again and restores the position with `fseek`
  ([vfs.cpp](../../emu/vfs/src/vfs.cpp)). Its subtree accounts for 2.34 seconds
  of proxy waits. `tell()` adds another 0.64 seconds. These are subsets of
  the syscall table, not additional delays.
- Case-insensitive path resolution checks each component and scans directory
  entries when the exact spelling misses
  ([fileutils.cpp](../../emu/common/src/fileutils.cpp)). Its subtree accounts
  for 4.60 seconds of proxy waits; the containing physical-path resolution
  accounts for 5.80 seconds. These two values overlap.
- The main page's samples corroborate filesystem work: `ErrnoError`, path
  normalization, UTF-8 conversion, `lookupPath` and `lookupNode`. `ErrnoError`
  is a sampled filesystem helper, not evidence that the game crashed.

Reducing redundant metadata/position queries or avoiding synchronous
main-thread round trips is the first code-level experiment to consider.
Any cache must preserve create/rename/delete, case-preserving names and shared
file-handle semantics. This profile does not establish a particular design
or a guaranteed gain.

### Guest work: hashing dominates decompression

ROM regions are mapped against the exact served ROM, SHA-256
`89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490`.

| # | Guest module | Generated-code self samples |
| ---: | --- | ---: |
| 1 | `hash.dll` | 8.69 s |
| 2 | `EUser.dll` | 2.42 s |
| 3 | `EZLib.dll` | 0.82 s |
| 4 | `EStor.dll` | 0.79 s |
| 5 | `EDbms.dll` | 0.20 s |

`hash.dll` accounts for approximately 63% of generated-code self samples.
This is region-entry attribution: translations may include inlined callees,
and it does not isolate the cost of each ARM instruction. RAM entries without
mapping-lifetime metadata remain unresolved (0.18 seconds). This is evidence
for examining installer hashing, not proof that a particular hash algorithm
or replacement has been identified and validated.

### Compilation and mapping

Of the worker's 9.74 seconds in compilation/module preparation, 9.65 occur in
the first approximately 30 seconds. Only 0.09 occur in the remaining sampled
installation. The largest named compiler leaf is `state_local_cache::finish`
(2.15 seconds); this is code generation, not runtime register writeback.

Cumulative operation counters at the final screenshot record 8.87 seconds
of ARM translation, 0.57 seconds of emission and 1.38 seconds of installation.
These include initial preparation on the main page; the worker-only profile
does not capture that whole scope. Module preparation also includes calls into
browser compilation/instantiation. These counters and sampled times overlap
and must not be added.

The 6.97-second mapping subtree is the callback established in
[mmu.cpp](../../emu/mem/src/mmu.cpp), including the view rebuild in
[memory_experiment.cpp](../../emu/cpu/src/aot/memory_experiment.cpp): enumerate
page bindings, clear old published mappings, publish new mappings and check
the direct arena. The sampled callback itself is 6.85 seconds. Native
instruction attribution would be required to divide its inlined work further.
The previously rejected mapping caches remain rejected; this profile is not
a reason to reintroduce them without new gameplay regression evidence.

`execute_chain` and `InterpreterMainLoop` similarly contain inlined work and,
for the latter, real interpreted guest execution. Their totals are not exact
removable dispatch/check overhead.

## Implication for a prepared image

A pretranslated ARM-to-WASM pack addresses the approximately 11 seconds of
measured compiler operations. A preinstalled device/game filesystem can also
skip the repeated N-Gage import, including its hashing and much of its file
traffic and mapping churn. The profile therefore strengthens the case for
measuring a preinstalled filesystem first, optionally combined with a compiler
pack. Download size and deserialization costs still need measurement; this is
not a claim that the table's costs disappear or add up to a predicted gain.

No prepared image or optimization was implemented in this task.

## Reproduction and evidence

Compact results, policy, asset identities, thread counters, profile checksums
and source paths are in [HOOKED_STARTUP_PROFILE.json](HOOKED_STARTUP_PROFILE.json).
Raw profiles, screenshots, logs and scripts are retained under
`/home/claude/.scratch/eka-startup-profile-20261012/`.

```sh
PROFILE_WORKERS=worker-13 node /home/claude/.scratch/eka-startup-profile-20261012/profile.mjs https://claude-laptop.lan:8188/ NEW_OUTPUT hooked-on 240
python3 /home/claude/.scratch/eka-startup-profile-20261012/analyze.py NEW_OUTPUT
python3 /home/claude/.scratch/eka-startup-profile-20261012/attribute.py NEW_OUTPUT
```

The worker number is capture-specific: verify it contains emulator/generated
stacks. Omit `PROFILE_WORKERS` to rediscover all workers, accepting the larger
profiling disturbance. `analyze.py` conserves sample weight across categories;
`attribute.py` verifies the served ROM hash and conserves generated-code totals.
Both checks pass for these captures. No production test suite was rerun for
this documentation-only task.
