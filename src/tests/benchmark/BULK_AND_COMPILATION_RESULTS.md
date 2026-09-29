# Bulk-loop eligibility and compilation scheduling

This completes the two investigations left unfinished in `FLAG_DATAFLOW_RESULTS.md`.
Baseline: `c551d62ab`, `wasm-port`. **No new performance optimization is deployed.**
The diagnostic patch is preserved separately; production emulator sources are restored.

## Bulk work: small measured coverage in the heavy scene

We captured bounded code snapshots through the side-effect-free mapping interface
alongside the existing execution-weighted compiled-region sample stream (stride
1,021). We ran the **unchanged production Thumb symbolic matcher** offline, with
bounded mock instruction reads, and inspected ARM backward loops using Capstone.
Positive and negative matcher fixtures pass. No upstream bulk shortcut was enabled.

| Observation | 0–96 guest seconds | Heavy 78–96 seconds |
| --- | ---: | ---: |
| Sampled compiled entries | 899,767 | 175,773 |
| Sum of sampled entry instruction counts | 14,722,486 | 3,844,153 |
| Sampled instruction weight in regions containing a matched Thumb loop | 2,622 (0.0178%) | 0 |
| Weight in regions containing simple ARM store/decrement/BNE candidates | 597,287 (4.06%) | 56,951 (1.48%) |

Every sampled entry has a captured snapshot; no edge records were dropped.
These are **sampled instruction weights**, not sampled CPU time. A containing
region includes instructions outside the loop. Snapshots extend up to 512 bytes
and static traversal deliberately overapproximates the compiled region, including
call continuations. Thus the candidate attribution overestimates actual loop
work within these snapshots. Zero sampled matches is not proof of zero executions
of every possible Thumb loop. Uncompiled fallback and complex/unrecognized loops
are outside this census's coverage.

To avoid confusing region weight with executed loop work, a second diagnostic
emits a counter at the terminal branch of a generic ARM pattern:

```
STR value, [base], #4
SUBS counter, counter, #1
BNE loop
```

It recognizes opcodes/register relationships, not a Snakes PC. The base, value
and counter registers must be distinct and none can be PC. The counter includes
the final not-taken branch and does not batch or alter any guest operation.
It records **34,710,469 completed iterations before 78 seconds**, and
**3,840,168 in the heavy window**. The latter accounts for **11,520,504 of
3,975,618,624 guest instructions: 0.290%**. This is completed compiled-loop work,
not a proof that all those iterations can safely be grouped into one memory span.
Partial transfers, short budgets, page ends and code writes still need handling.

The broader backward-loop search also finds:

- An eight-store ARM fill: containing-region weight **0.886%** of the heavy sample.
- A table-based pixel conversion: **1.110%**. It has two dependent loads, including
  a table lookup, so the existing one-load Thumb matcher cannot accelerate it.
- A strided conditional copy and other small memory loops. Search/reduction loops
  also appear; these must not be counted as memcpy/fill candidates.

**Decision:** do not port or activate the old Thumb accelerator for Snakes. Its
measured opportunity is negligible here and its exact-budget limitation remains.
Do not prioritize a general bulk engine as a substantial headroom solution based
on this route. The ARM fill/conversion patterns remain possible small future
experiments, but no safe implementation or speedup is claimed. Instruction
fractions are **not** an upper bound on wall-time savings, and this result does
not generalize to other games (including the upstream Brothers in Arms example).

## Compilation scheduling: measured, but not a warmed bottleneck

Both fresh browser runs capture 593 modules through 96 guest seconds: 14,872
exports and approximately 58.9 MB of module bytes. The explicit JavaScript
constructor timings are:

| Guest interval | Modules | `new WebAssembly.Module` total, run 1 / run 2 |
| --- | ---: | ---: |
| Before 21 seconds | 455 | 127.99 / 131.93 ms |
| 21–78 seconds | 108 | 38.65 / 60.34 ms |
| 78–96 seconds | 30 | **8.25 / 8.56 ms** |

In the heavy window, run 2 separately records **18.67 ms** of hot ARM/Thumb
translation, **1.64 ms** of module emission, **1.42 ms** of instantiation and
**0.55 ms** of function-table registration. The largest combined constructor /
instantiation / registration event is **0.71 ms**. These timed stages total
about **30.8 ms**; byte copying and C++ attachment bookkeeping are outside those
stage scopes. Run 1's translation scope accidentally included nested flushing;
its C++ stage totals are retained but not used as disjoint attribution.

The largest initial module contains 6,108 exports and 10.69 MB. Its browser
compile/install event is about **26–38 ms** across the two runs. Existing native
log timestamps bracket eager ROM scanning/translation/emission at **283–305 ms**,
before the emulator thread starts. This separate startup estimate includes logging;
it is not included in the hot-translation timing totals above.

We also performed a concrete scheduling experiment on the **same captured module
corpus**, fully loaded before timing. Each trial launches a fresh Chromium process.
A dedicated worker either constructs modules synchronously or awaits
`Promise.all(WebAssembly.compile(...))`. Both then instantiate every result and
verify the same export count (stub imports, no guest execution).

| Order | Compile time | Installation time | Largest compiler-worker heartbeat gap |
| --- | ---: | ---: | ---: |
| Synchronous | 249.76 ms | 29.03 ms | 250.77 ms |
| Asynchronous | 42.78 ms | 16.68 ms | 42.71 ms |
| Asynchronous | 41.83 ms | 15.78 ms | 41.82 ms |
| Synchronous | 101.72 ms | 17.03 ms | 102.49 ms |

All four compile/instantiate **593 modules / 14,872 exports**. Page heartbeat gaps
stay below 9 ms because the work occurs on another worker. Bulk async submission
still blocks its own compiler worker for roughly 42 ms; asynchronous API use does
not imply every caller thread stays continuously responsive. An initial main-thread
synchronous attempt was rejected by Chromium's >8 MiB module limit; final trials
use a worker without flags that override that restriction.

**Decision:** async precompilation is feasible and useful for scheduling a cold
corpus, but these measurements do not justify integrating it as a Snakes warmed
throughput optimization. The actual game compiles incrementally, not this whole
corpus in one pause, and spends only ~8.5 ms in explicit module compilation during
the heavy window. A warm-launch cache would still require source/dependency
validation, versioned artifacts and worker-local function-table installation;
those costs and cold-cache misses were not tested by the corpus experiment.
No cache or asynchronous emulator compiler was deployed.

These measurements do **not** include all V8 background optimizing-tier work,
first-call tier behavior, shader compilation, browser scheduling or external load.
They cannot establish the cause of every startup audio underrun. In particular,
our prior attribution of startup audio gaps to compilation was not a measured
causal result; explicit module creation does not explain multi-second stalls in
these runs. A paced 1x audio test also does not establish the available throughput.

## Correctness, observer effects and delivery

The exact-loop-counter diagnostic passes the sampled interpreter checker and
matches native across **1,600 images**, all guest records and **4,919,249 stereo
PCM frames**, ending at 102.484363 guest seconds / 16,263,331,210 instructions.
The heavy census executes exactly 3,975,618,624 instructions / 676 presentations,
the same work as the preceding shared-audio controls.

The census deliberately enables detailed counters, samples, per-byte snapshot
logging, capture files and generated-loop counters. Its **197.55-second full-run**
and **51.24-second heavy-window** wall times are heavily perturbed and are **not
production performance measurements**. The two runs differ in instrumentation;
do not use their wall times to estimate optimization gains. Shared DSP/resampling
runs, but these benchmark trials do not play through the host AudioWorklet.

The unchanged movement heuristic's known 409 ms native presentation gap and small
frame changes still apply; exact replay equality is not a new pass of that
heuristic. No new native/WASM unit-suite or live-play milestone is claimed for
these analysis-only tools. The offline matcher's positive/negative checks and
fresh full checked replay validate this investigation. Production sources are
restored and the default WASM rebuild succeeds. The HTTPS-served baseline hash
remains `a3f1809d29d81e1e0696592aa60f9a1272be759b12b568ecbdced400a729d8ff`.

## Reproduction

Apply `bulk_compile_census_experiment.patch` to the baseline for a diagnostic
**WASM-only** build, then build `eka2l1_wasm`. This patch is not a production option.
Use the same Chromium 153 / physical Quadro T1000 / i7-10875H configuration and
process-local NVIDIA 610.43 libraries as the previous reports.

```
EKA2L1_GPU=hardware EKA2L1_SHARED_AUDIO=1 EKA2L1_COMPILE_CENSUS=1 \
 EKA2L1_GUEST_PROFILE=1021 EKA2L1_BENCHMARK_AOT=5 \
 EKA2L1_PROFILE_START_US=78000000 \
 node src/tests/wasm/profile.ts ASSETS NEW_RUN 2 0 96000000
python3 src/tests/benchmark/build_bulk_matcher.py /tmp/bulk-matcher
# Python environment with capstone 5.0.7:
python3 src/tests/benchmark/bulk_census.py NEW_RUN --matcher /tmp/bulk-matcher
node src/tests/wasm/compile-scheduling.ts NEW_RUN/modules NEW_OUTPUT.json
```

Set the profile start to zero for the whole-run census. Do not run diagnostic
jobs concurrently with performance controls. Raw paths, hashes, stage records,
corpus manifest and results are in `BULK_AND_COMPILATION_EVIDENCE.json`; captured
modules and full snapshots remain local under `.scratch/eka-benchmark/`.
