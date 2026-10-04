# Direct-memory regression attribution

> Historical experiment: current code retains only TLB (0) and the all-cuts direct implementation (2).
> See [current selection and commands](README.md#memory-implementations); removed controls below require the historical source revision.

The largest measured cause in Sky Force is the experimental runtime wrapper:
repeatedly entering/leaving an unchanged memory view and updating chain counters.
Removing that group saves **0.912 worker CPU seconds**, approximately **72% of
the mean direct-versus-TLB gap in this campaign**. The generated guest memory
code stays the same in that comparison.

Snakes has a different result: the worker slowdown is not consistently
reproduced, while the extra renderer CPU is concentrated on Chrome's threadpool.
A separate trace shows substantially more V8 WASM compilation.

This investigation starts from `d2e045d50` on `wasm-port`. Production code,
default TLB selection and all four memory modes remain unchanged. The temporary
controls are retained in a [reproduction patch](DIRECT_MEMORY_ATTRIBUTION.patch),
with [individual measurements and provenance](DIRECT_MEMORY_ATTRIBUTION.json).

## Controlled CPU-time comparison

One frozen research binary contains all four timed configurations. Each game
runs them forward, then in reverse order. Profiling, tracing and the address
census are off during timing. Each run boots a fresh browser and the stock 5320
device. The windows match the [previous comparison](DIRECT_MEMORY_RESULTS.md).

| # | Configuration | Memory mode | Research control | Behavior |
|---|---|---:|---:|---|
| 1 | TLB | 0 | 0 | Production allocation, TLB emission and runner |
| 2 | Original direct | 2 | 0 | Original zero-copy implementation, including callbacks and counters |
| 3 | Cached direct | 2 | 3 | Same guest memory emission; cache the view by mapping generation/address space and omit experimental chain/instruction counters |
| 4 | Arena + TLB | 2 | 4 | Keep private arena allocation but use production TLB emission, helpers and runner |

Snakes executes 644,728,231 instructions and 84 presentations over guest time
21–25 seconds. Sky Force executes 2,171,043,925 instructions and 192 presentations
over 42–48 seconds. Every configuration has identical instruction endpoints and
presentation journals within its game. All 16 timing runs pass the driver checks.

Values below are CPU seconds, with two runs per configuration. The primary
metric is scheduler runtime of the busiest matched renderer thread, named
`DedicatedWorker` in every observation. Renderer process CPU also includes
compilation, graphics and audio. Threadpool CPU sums the observed renderer
threads whose names begin with `ThreadPool`.

| # | Game | Configuration | Worker mean | Worker observations | Renderer mean | Threadpool mean |
|---|---|---|---:|---|---:|---:|
| 1 | Snakes | TLB | 2.136732 | 2.089251, 2.184212 | 2.540 | 0.240334 |
| 2 | Snakes | Original direct | 2.160744 | 2.206709, 2.114779 | 2.995 | 0.666102 |
| 3 | Snakes | Cached direct | 2.129710 | 2.106148, 2.153271 | 2.970 | 0.684714 |
| 4 | Snakes | Arena + TLB | 2.288412 | 2.560963, 2.015861 | 2.730 | 0.269145 |
| 5 | Sky Force | TLB | 9.536350 | 9.893559, 9.179142 | 10.190 | 0.287364 |
| 6 | Sky Force | Original direct | 10.811154 | 10.560984, 11.061323 | 11.460 | 0.278927 |
| 7 | Sky Force | Cached direct | 9.899405 | 9.924633, 9.874176 | 10.560 | 0.287548 |
| 8 | Sky Force | Arena + TLB | 9.599345 | 9.281642, 9.917048 | 10.280 | 0.299048 |

### Sky Force: runtime overhead is the largest isolated cause

Original direct costs 1.275 seconds more than TLB, a **13.4%** mean worker-CPU
increase in this binary. Cached direct removes **0.912 seconds**, an **8.4%**
reduction in direct's total worker CPU. The two removals are 0.636 and 1.187
seconds, both in the same direction. The ratio 0.912 / 1.275 is approximately
72%; this is an attribution of these sample means, not a precise population
percentage or an exact partition of the earlier 17.4% result.

The original path performs 17,110,139 compiled chains in this window. Each
chain calls the experimental enter/leave functions even when the mapping view
is unchanged, retrieves the process arena, checks view state and updates
counters. These operations are WASM runtime work, without a JS boundary or
synchronization copy. Snakes has only 178,416 chains in its window. This explains
why a small per-chain cost can matter much more in Sky Force.

The cached-view control replaces unchanged-view callbacks with a generation /
address-space check and removes the experimental chain/instruction counters.
It refreshes the view after a mapping/address-space change, including changes
made by memory helpers. This intervention measures those costs as a group;
it does not separately price each callback, counter write or optimization
caused by the changed runtime code.

Cached direct still averages **3.8% more worker CPU than TLB**; its paired gaps
are +0.3% and +7.6%. Arena + TLB averages only +0.7% relative to ordinary TLB,
but the pair changes are -6.2% and +8.0%. Cached direct versus arena + TLB is
+3.1% on average, with pair changes of +6.9% and -0.4%.

Those smaller differences are not cleanly resolved by two repetitions. They
do not establish a material penalty from arena allocation, nor identify which
remaining bounds checks, metadata loads, fallback accesses or V8 code-generation
decisions cause the residual. In particular, the remaining 3.8% must not be
reported as a measured cost of bounds checks alone. The main runtime intervention
is beneficial in both repetitions; finer attribution would require another
targeted experiment.

### Snakes: extra compiler CPU, uncertain worker regression

Original direct averages **+1.1% worker CPU**, with paired differences of +5.6%
and -3.2%. The earlier +4.3% result is therefore not a stable worker effect in
these observations. Removing the runtime wrapper saves only 0.031 seconds on
average, with opposite signs in the two pairs. This campaign cannot reliably
price that small effect in Snakes.

Renderer CPU increases by **0.455 seconds**, while observed threadpool CPU
increases by **0.426 seconds**: about 94% of the net renderer increase lands
on the threadpool. Both direct variants retain that cost, while arena + TLB
has threadpool CPU close to ordinary TLB. The Chrome trace below independently
identifies increased V8 WASM compilation. Threadpool totals alone are not a
function-level attribution of every one of those 0.426 seconds.

The slow first Snakes arena + TLB observation is retained, not discarded.
Its large spread prevents a reliable conclusion about allocation cost there.
CPU time excludes sleeping/descheduling but does not eliminate frequency,
cache, JIT or other host-work variation. The measurements include the harness's
polling margins. No owned build, replay, profile or compiler test overlaps timing.

## Generated address-path census

A separate instrumented run counts decisions inside generated ARM/Thumb memory
lowering. These are **span checks**, not individual ARM load/store instructions:
one check can cover a block transfer or multiple operations. Byte-weighted
figures are requested span widths, not physical memory traffic. Interpreter and
C++ memory accesses are outside this census. Census timings are not used in the
CPU comparisons above.

| # | Game | Span attempts | Direct arena | Page-table fallback | Metadata loads | Arena share of span bytes |
|---|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 137,504,275 | 122,215,085 (88.88%) | 15,279,973 (11.11%) | 19,161,840 | 90.48% |
| 2 | Sky Force | 528,501,972 | 452,067,697 (85.54%) | 76,410,406 (14.46%) | 158,500,636 | 92.61% |

The remaining 9,217 / 23,869 attempts do not produce a direct or page-table
host pointer and can take a helper/deferred path. There are zero view rebuilds
and zero synchronization bytes in either measured window. Direct addressing
is used extensively; copying, rebuilding alias maps and an overwhelmingly high
fallback rate cannot explain this regression.

Metadata loads count executions of the generated four-local cache-fill path,
not verified machine-code load instructions after V8 optimization. They show
that the generated path still does substantial setup, but do not assign CPU
time to that setup independently.

## Chrome diagnostic evidence

Four captures use the original committed binary, separately from the research
timing campaign. Sampled spans include waits and inlined work; they locate work
but do not measure the throughput regression. The direct Sky Force profile has
0.826 seconds of sampled self time in MMU experimental-memory callback frames;
TLB has none. Snakes has 0.028 seconds. Much dispatch is inlined into
`InterpreterMainLoop` and is not separately named.

The trace's `tdur` field records thread time in compilation events fully inside
the explicit measurement markers. The [analyzer](direct_memory_profiles.py)
checks that counted events do not overlap on a thread. It does not also add the
enclosing `ExecuteCompilationUnits` events. No selected compilation event crosses
a measurement boundary. These totals cover traced WASM compilation, not only
identified ARM translation functions.

| # | Game | Mode | Top-tier events | Top-tier thread seconds | Lazy compilation thread seconds |
|---|---|---|---:|---:|---:|
| 1 | Snakes | TLB | 86 | 0.326434 | 0.080368 |
| 2 | Snakes | Direct | 106 | 0.938985 | 0.096069 |
| 3 | Sky Force | TLB | 67 | 0.284547 | 0.049613 |
| 4 | Sky Force | Direct | 64 | 0.394556 | 0.077059 |

Snakes has **2.88 times** the top-tier compilation CPU in this diagnostic
capture. Compiler work is a material part of the renderer regression, distinct
from emulation-worker cost. The diagnostic durations must not be subtracted
from unprofiled CPU results: profiling substantially perturbs these runs.
The compilation events do not identify functions, so they do not establish
which emitted function or V8 optimization pass accounts for the extra cost.

## Correctness, restoration and reproduction

The research build passes the existing 1,407 focused memory comparisons.
Both new timed controls independently pass full 60-frame replay comparisons
in both games, with exact native pixels, presentation timing, guest instruction
counts, PCM audio and audio events. This validates these game paths; it is not
exhaustive validation of cached-view behavior across every configuration.
Research controls require activation from boot; the driver rejects delayed
activation.

All ten modified production/harness files were restored after measurement.
The temporary code is stored only in the patch. Original browser application
artifacts were restored in `build-wasm`; the frozen research artifacts remain
under `/home/claude/.scratch/eka-direct-regression/research-build`. The LAN
deployment was not changed. Normal subsequent CMake builds will rebuild objects
whose sources were restored.

To reproduce, apply the patch to the unchanged production sources and build
with the existing Emscripten toolchain. Use a frozen copy of the built browser
artifacts as `$BUILD`, and fresh output directories:

```sh
git apply --check src/tests/benchmark/DIRECT_MEMORY_ATTRIBUTION.patch
git apply src/tests/benchmark/DIRECT_MEMORY_ATTRIBUTION.patch
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j8
node build-wasm/src/tests/aot/test_aot_wasm.js --memory-implementations-only

python3 src/tests/benchmark/memory_implementations.py "$OUT/replays" replays \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 2 --research 3 4 --frames 60
python3 src/tests/benchmark/memory_implementations.py "$OUT/census" census \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 2 --research 16
python3 src/tests/benchmark/memory_implementations.py "$OUT/timings" timings \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 0 2 --research 0 3 4 --rounds 2

git apply --reverse src/tests/benchmark/DIRECT_MEMORY_ATTRIBUTION.patch
```

The evidence JSON contains exact commands, all individual timing reports,
replay comparisons, census data, profile summaries and raw-profile hashes.
Raw captures and logs are under `/home/claude/.scratch/eka-direct-regression/`.
`direct_memory_profiles.py ARTIFACT_ROOT` regenerates the Chrome summary.
All runs use Chrome 153.0.8010.52. Research hashes:

```text
WASM    9e81d54ea3d7f2f3938b5fec23529d8cdb7e928f367fd4f686145869a66a1dc8
Loader  4f099d42fb3ce2889e74a96ca31474ea43477c4b39a246633c1dd812a9544098
Patch   c8922868fb9d81e0c3b7a704eb182d449f81eb21594a7bb1357313d14709f657
```
