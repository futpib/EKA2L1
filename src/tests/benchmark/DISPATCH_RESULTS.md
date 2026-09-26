# Raw WASM imports and recent compiled-block lookup

**Implemented both stages.** Paired no-capture throughput improves **1.5156x**,
from 27.0629 to 17.8559 host seconds per four guest seconds (**0.22402x
realtime**, still needing about **4.46x** more throughput). With full capture,
the observed gain is **1.1429x**. Exact 1,000-image correctness gates pass.
Detailed reports, hashes and raw artifact paths: [DISPATCH_RESULTS_EVIDENCE.json](DISPATCH_RESULTS_EVIDENCE.json).

Runtime commits: `7c9f76a1` (raw imports / verifier separation) and `c692eb5e`
(recent validated lookup and serial stage harness). Baseline: `0e2396e7`.

## Implementation

AOT memory imports previously came from `wasmExports`, which Emscripten's
`ABORT_ON_WASM_EXCEPTIONS=1` replaces with JavaScript wrappers. The generated
loader also overrides `wasmTable.get`, so that route would still return wrappers.
AOT now receives C++ function pointers as table indices and obtains the actual
WASM functions using `WebAssembly.Table.prototype.get.call`. The original
outer export and thread-entry abort handling remains enabled. No assertion,
stack-check or abort-handling build flags were changed.

A normal run selects memory helpers with no verifier hook. Compiled chains use
a separate template specialization with no validation begin/end calls. Verifier
runs retain the checked helpers and the same sampled differential checks. RAM
instruction-dispatch accounting is independent of validation and remains exact.
The normal path still implements all emulated memory semantics through the
existing ReadMemory/WriteMemory helpers; direct guest-memory inlining is outside
this change.

A 256-slot recent-block cache skips the hash-table/deque search when a live
entry matches the full address-space/PC/mode key. It does **not** cache mapping
resolution: every RAM entry still resolves the current mapping and validates
backing address, mapped extent and every compiled code byte. Pointer targets
remain allocated until reset. Replacement/invalidation marks old entries dead;
reset drops all recent pointers. Copying is disabled because recent pointers
belong to their owning storage. The existing hash map handles collisions.
No claim of mapping-generation coverage or complete write interception is made.

Tests cover process/mode separation, aliased/host code mutation, remapping,
unmapping, short mappings, late compilation, recent-cache collisions,
replacement, invalidation and reset. The new tests are part of the full native
CPU/package suites; they are not part of the translator-only WASM executable.

## Correctness and tests

The raw-import stage and **two combined-build repeats** each match the native
reference across all **1,000 distinct gameplay images**, guest timestamps,
instruction counts, PCM samples and audio-event records. The endpoint remains
**68.040042 guest seconds / 10,342,580,529 guest instructions**, covering
47.013860 seconds of gameplay. The stage-1 run and one combined repeat checked
every 1,024th compiled block against the interpreter without divergence; this
is sampled, not exhaustive, differential replay.

The WASM suite passes **125 tests**, including 115,920 budget/state/memory and
46,080 long-multiply differential comparisons. The existing expected
crash-reproduction harness limitation remains. The full native CPU target
passes. The broader native package has **84 passing cases / seven previously
observed failures** (480/487 assertions pass); no clean-base run establishes
their origin. Runtime tests ran on the source committed as `c692eb5e`.

All six full-capture timing windows and the CPU profile also match the native
reference's first 85 images and guest records exactly. All controls retain
655,867,149 guest instructions, 653,318,300 compiled instructions (99.611%),
132,212,243 compiled blocks and 122,142,017 RAM dispatches per four guest seconds.
This confirms that separating validation did not lose RAM accounting.

## Serial stage comparison

Full captures; SwiftShader; identical 21–25 guest-second window; profiling and
verification disabled. Trials ran baseline/step1/combined/combined/step1/baseline.

| Build | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Baseline | 28.6118, 28.4854 | **28.5486** |
| Raw imports / separate verifier path | 26.4003, 25.6588 | **26.02955** |
| Combined with recent lookup | 24.7876, 25.1699 | **24.97875** |

The observed throughput gains are **1.0968x** for step 1 and another **1.0421x**
for step 2, **1.1429x combined**. Full-capture throughput is **0.16014x realtime**.
These are two trials per build on a shared host, not universal speed guarantees.

Capture introduces substantial main-thread work in this configuration. Measured
graphics waits were 2.90–3.04 seconds for baseline, 7.65–9.07 seconds for step 1,
and 9.11–9.71 seconds for the combined build. These wait scopes are already
inside elapsed time and must not be added to it. They are synchronization waits,
not measurements of GPU execution alone.

Two initial serial combined no-readback/no-capture controls took **17.2488 and
17.5474 seconds** with the same guest execution counters. Their lack of images
is deliberate; full-capture runs provide the separate pixel-correctness gate.
Fresh paired no-capture trials ran old/new/new/old after the full-capture
experiments, with all four fixtures warmed and paused before serial release:

| Build | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Baseline, no capture | 27.4165, 26.7093 | **27.0629** |
| Combined, no capture | 18.5003, 17.2115 | **17.8559** |

That is **1.5156x throughput**, **0.22402 guest seconds per host second**, or a
remaining **4.46x** realtime gap for this synthetic-clock workload. Rendering
remains enabled. No-capture trials have identical guest timestamps and execution
counters but deliberately contain no pixel evidence; the full replay supplies
that gate. The initial two combined no-capture runs are retained as exploratory
controls, not mixed into the paired comparison's means. Two trials per binary
and a shared host limit precision. The physical-GPU/normal-Qt-JIT comparison
remains unmeasured.

## Reprofile

The isolated final CPU profile took 28.1484 seconds; the emulation worker's
sampled span was 28.618945 seconds. Sampling adds overhead, so use serial
unsampled trials for speed comparisons.

- WASM-to-JS self-time is **0.023213 seconds** in this worker, versus 3.52304
  seconds in the earlier saved profile. No `validation_*` function received
  samples. Raw memory helper frames are now visible without the former abort
  wrapper path. Source specialization additionally establishes that unchecked
  helpers contain no verifier hook.
- Cache lookup has **4.107848 sampled seconds**; compiled lookup **2.057012**;
  mapping resolution **1.658515**. Mapping still runs on every RAM entry. These
  are self times, not estimates of independent removable savings. Byte comparison
  can inline into lookup; the missing standalone `memcmp` symbol does not mean
  byte checks were removed.
- The page isolate spends **6.650395 sampled seconds in `_flushLog`** and
  **2.227207 in `getError`**. The shell appends to the entire log's text content
  and forces a scroll-height read. This is a concrete remaining diagnostic/UI
  hotspot on the thread servicing graphics. It was not changed in this task.
- Worker waits account for 9.081907 sampled seconds. Page and worker scopes
  overlap across threads: do not add them as independent wall-time costs.

The fresh profile changes the next-step priority: bound/gate DOM logging and
GL diagnostics, then remeasure presentation waits before attributing the full
remaining elapsed time to compiled CPU execution. Longer regions, mapping-aware
lookup and memory fast paths remain possible CPU work. Neither physical-GPU
interactive performance nor realtime gameplay is established here. Audio-quality
work remains deferred.

## Reproduction

Use the assets, build commands and reference described in [README.md](README.md).
Archive the frontend directory before rebuilding, preserving JS/WASM/data/HTML
files and recording source commit and SHA-256. From `src/tests/wasm`:

```sh
EKA2L1_BENCHMARK_AOT=4 EKA2L1_AOT_VERIFY=1024 node benchmark.ts /absolute/assets /absolute/checked 1000
EKA2L1_BENCHMARK_AOT=4 node benchmark.ts /absolute/assets /absolute/repeat 1000
EKA2L1_BENCHMARK_AOT=4 PROFILE_GATE=/absolute/cpu-gate node profile.ts /absolute/assets /absolute/cpu 0 1
```

From the repository root:

```sh
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/timings --compare-steps /absolute/baseline-frontend /absolute/step1-frontend --measure-gate /absolute/timing-gate
# Finish correctness/tests; wait for both gates' .ready markers.
touch /absolute/cpu-gate
# Once the isolated CPU profile exits:
touch /absolute/timing-gate
python3 src/tests/benchmark/summarize_profile.py /absolute/cpu
python3 src/tests/benchmark/compare.py /absolute/native-reference /absolute/checked
python3 src/tests/benchmark/validate_gameplay.py /absolute/checked
```

The timing harness warms six fixtures, pauses them at 21 guest seconds, and
releases baseline/step1/combined/combined/step1/baseline serially. It disables
sampling, guest profiling, AOT diagnostics and verification. Keep unrelated
benchmark fixtures paused while measuring. CPU sampling is a separate run.
A no-readback control uses `profile.ts ASSETS OUTPUT 2 0` with AOT mode 4.
Use `--compare-build /absolute/baseline-frontend --capture-mode 2` for paired
no-readback comparisons. This disables capture only; normal rendering remains.

Raw output directories in this session use the prefix
`/home/claude/.scratch/eka-benchmark/dispatch-`. A stage-1 replay started before
its progress commit; its archived source patch and binary hashes retain
provenance. Some harness checkout metadata includes a generated Python bytecode
cache subsequently removed; runtime source did not change. Archived-binary
source commits are separate from the harness checkout's `git_head`. Later
no-capture paired trials also include report and harness edits in the dirty-tree
flag; runtime code remained at `c692eb5e`.
