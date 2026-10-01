# Comparing four routes toward Qt JIT throughput

2026-09-27 UTC. Research based on `d50abf943c9cde68756fd413a055d140314f0cda`,
`wasm-port`, single checkout `/home/claude/code/EKA2L1`.
No production CPU optimization or unsafe validity shortcut is retained from this investigation.
The connected-callee experiment is preserved as a patch. The LAN-served frontend is unchanged.

## Decision

There is **no measured new safe whole-game speedup** in these experiments.
Rank the next implementation investments **3 → 2 → 4**; option 1 has now supplied
useful evidence, but switching V8 tiers is not an optimization opportunity here.

1. **Option 3, comprehensive code-write tracking / validity generations:** the most
   concrete bounded opportunity. Removing *only* byte comparisons reduces mean
   elapsed time by 10.8% (1.121× throughput). This is an unsafe diagnostic ceiling,
   not an implementation or a promised gain: real barriers, alias handling and
   version checks cost time. It would still reach only 1.116× realtime in this
   batch, below the 1.25× target and far from Qt. Mapping/lookup costs remain.
2. **Option 2, connected paths with an optimizing IR:** the larger architectural
   opportunity, but not a demonstrated gain. The tested broader straight-line
   callee inlining and offline WASM optimization give overlapping timings with
   baseline. Do not expand those two changes unchanged. A more capable data-flow
   compiler must earn a gain while retaining precise exits; this bounded trial
   does not test a complete optimizing region compiler.
3. **Option 4, Dynarmic IR → WASM:** feasibility demonstrated on two restricted
   kernels. The substantial-looking math-kernel gain is partly explained by
   removing per-instruction budget exits. After a matching full-budget control,
   its remaining advantage is about 16%, with fault/code-write semantics still
   unequal. That is not evidence of a whole-game gain or justification for a full
   backend port ahead of the other work. Use the IR prototype to inform option 2.
4. **Option 1, browser lowering/tiering inspection:** completed as an investigation.
   Normal Chrome already benefits from optimizing compilation. Forcing that tier
   does not reliably improve the heavy replay. Native-code inspection confirms
   large generated functions and state/guard traffic; it does not assign an
   execution-time saving to each instruction or prove spills are the main cause.

This ranks evidence and engineering scope, not hypothetical maximum upside.
Neither Qt parity nor a new 1.25× heavy-window result is established.

## Comparable whole-game controls

Hardware: i7-10875H, physical Quadro T1000 Max-Q, Chrome 150.0.7871.186,
ANGLE Vulkan. Rendering enabled; capture/readback, sampled checking and detailed
counters disabled. Exact guest window 78–96 seconds. Each timed run executes
3,975,200,506 guest instructions and 676 presentations. All eight counter checks pass.

Each fixture warmed to a gate; timings ran serially in order
baseline, connected, validation ceiling, post-optimizer, then reverse order.
Paused fixtures remain resident (sufficient physical RAM, no swap); they are
closed after their timed run. Concurrent warmup duration is not a compilation
benchmark. No builds, correctness runs or profilers overlapped this timing batch.
Only two timing trials per variant on a shared host: this is exploratory evidence,
not a confidence interval or a hardware-independent speed guarantee.

| Variant | Host seconds for 18 guest seconds | Mean | Mean throughput |
| --- | --- | ---: | ---: |
| Baseline | 17.9338, 18.2382 | 18.0860 | 0.995× |
| Broader connected callees | 18.7347, 17.2559 | 17.9953 | 1.000× |
| **UNSAFE: skip byte comparisons** | 16.7347, 15.5242 | 16.1295 | 1.116× |
| Offline Binaryen on two hot modules | 17.6943, 18.7624 | 18.2284 | 0.987× |

The 0.5% mean connected improvement is overwhelmed by its trial spread. The
post-optimizer mean regresses slightly, also with overlapping ranges. Neither is
retained. This batch's baseline is slightly below realtime; the earlier 1.03×
result is not contradicted by a software regression, since the served binary is
unchanged. It illustrates the small and variable margin on this host.

Qt's previous native JIT mean was 4.154 seconds for this 18-second window (~4.33×).
It was a separate batch and has ~0.16% more guest instructions than the exact
reference. Do not treat the current timings as a newly interleaved Qt comparison.

### Option 1: tiering and actual native instructions

Separate serial tier controls, no sampling:

| V8 mode | Heavy-window host seconds |
| --- | --- |
| Default | 17.0755, 18.4465 |
| Force optimizing compiler (`--no-liftoff`) | 17.0615, 17.1214 |
| Baseline compiler only (`--liftoff-only`) | 33.0451 |

Default versus forced optimizing ranges overlap. Baseline-only is much slower;
there is no evidence that ordinary play is stuck entirely in the baseline tier.
The trace run records 12,737 Liftoff and 3,067 TurboFan compilations. These are
compile records, not distinct hot-function counts. Trace/sampling times are
excluded from the speed comparison.

A separate `--perf-prof` kernel run produced actual V8 jitdump native bytes,
extracted by `inspect_jitdump.py` and disassembled with objdump. The current
57-instruction kernel's TurboFan function is 37,760 native bytes versus 4,800
for the restricted Dynarmic-IR emitter. Current emission includes substantial
budget/exit reconstruction, memory guards and state loads/stores. The native
stack reservation is 224 bytes for current versus 248 for the IR probe: **do not
claim the IR result proves a smaller stack frame**. Much disassembled exit code
is cold; static instruction/stack-operand counts are not executed counts.
The dump has an incomplete final record, explicitly ignored by the extractor;
the reported complete function records precede it.

The independent whole-game sampled worker span attributes 57.6% inclusively to
generated code/callees; self samples include byte comparisons 7.8%, validated
cache find 8.9%, compiled lookup 5.1%, interpreter loop 12.0%. These overlapping
inclusive/self values are not additive speedup predictions.

### Option 2: two bounded real prototypes

`connected_callee_experiment.patch` expands recognized straight-line ARM callees
from 64 to 256 bytes, including stack save/restore and integer arithmetic.
Returns are dynamically checked; all included code is a dependency and exact
budgets, memory/code-write guards remain. It is pattern-based, with no Snakes
name/address dispatch. It passed 132 WASM tests, including 2,240 leaf differential
cases, and the full sampled-checker 1,600-image native comparison. Lack of a clear
timing win is why the source changes were removed, not a claimed correctness failure.

The second experiment runs Binaryen `wasm-opt -O3` offline on the two generated
modules containing `0x7006370c` and `0x70013edc`. It optimizes all functions in
those modules, using exact original-module-byte matching before substitution.
Both timed repetitions record exactly two replacements. Combined WASM size falls
407,091 → 294,663 bytes (~27.6%); no whole-game throughput gain follows.
These are profile-selected experimental inputs, not a proposed game-address
whitelist. Offline post-emission optimization is narrower than a guest IR compiler.
The checked 1,600-image comparison result is recorded in the evidence JSON.

### Option 3: what the ceiling does and does not measure

The isolated `ceiling` build makes `equal_code_bytes()` return true. Mapping,
backing-pointer, extent and ordinary dispatch remain. It is never copied to the
LAN build and is marked `UNSAFE_DIAGNOSTIC_ONLY.txt`. Matching execution counters
in this unchanged-code workload **does not make it safe**. This measures the
avoidable scan cost before paying for a real write-tracking implementation.

The existing generation tracks mappings, not every code mutation. A complete
implementation must cover inline guest stores, aliases, MMU writes, host-side
code/data copies (including `kernel/src/codeseg.cpp`), remaps, unloads and reuse.
No safe version-guard backend is implemented in this trial. Broader region
benefits enabled by write tracking are not bounded by this scan-only experiment.

### Option 4: restricted Dynarmic IR experiment and fair controls

`compiler_probe.cpp` runs the repository's actual A32 frontend and optimization
passes (get/set elimination, DCE, constant propagation, identity removal).
`ir_probe.py` emits WASM from the resulting IR and rejects unknown operations.
Inputs are a 57-instruction math routine at `0x7006370c` and a **seven-instruction
prefix**, not the entire hot routine, at `0x70013edc`.

The fixture uses aligned, permitted, ordinary memory and a full-kernel budget.
Invalid guards trap; partial budgets return zero for fallback. Full exception,
code-write, conditional/control-flow, interrupt and precise partial-exit behavior
is **not implemented**. It is not a complete Dynarmic WASM backend.

Sixty-four seeded memory/register fixtures per kernel, four alternatives each,
produce **512 exact state/memory/count comparisons** against the current emitter.
This is synthetic differential coverage, not two additional game compatibility tests.
Timing uses a WASM loop (no JS call per iteration), 500,000 warmup calls per
variant, then six alternating-order trials of five million calls. The common
loop restores registers between calls; timings include this driver cost.

Median milliseconds, five million calls:

| Kernel | Current | Current + Binaryen | Dynarmic IR | IR + Binaryen | Current full-budget-only + Binaryen |
| --- | ---: | ---: | ---: | ---: | ---: |
| Math, 57 instructions | 618.492 | 610.193 | 416.705 | 417.895 | 482.722 |
| Prefix, 7 instructions | 118.460 | 126.048 | 108.540 | 118.747 | 118.265 |

The math result initially appears 1.48× faster. However, the IR kernel lacks
per-instruction budget checks. `whole_budget_probe.py` removes those from the
current emitted kernel as a **full-budget-only unsafe diagnostic control**.
Against that control the ratio is only 1.16×. Memory-fault/code-write behavior is
still not equivalent, so even that residual is not isolated optimizer benefit.
The short prefix gains ~9%, and Binaryen sometimes regresses it. No microbenchmark
ratio is extrapolated to the game or multiplied with the validation ceiling.

Native frontend + IR passes, 100 warm repetitions excluding file IO and WASM
emission, take medians 51.0 µs (math), 5.8 µs (prefix). This is native host code,
**not a browser-hosted compiler throughput result**. Initial browser compile +
instantiate observations are 0.14–0.42 ms, single observations susceptible to
caching/tiering; retained raw, not used to rank cold compilation latency.
Offline wasm-opt on the two large modules took ~0.43/0.44 seconds in a diagnostic
run. Porting its optimizer or Dynarmic into the browser would add startup, code
size and compilation costs that this experiment does not quantify.

## Reproduction and artifacts

Raw artifacts are under `/home/claude/.scratch/eka-benchmark/`:
`options-whole-game`, `options-tier-*`, `options-v8-inspection2`,
`options-v8-summary.json`, `options-native-dumps`, `options-native-code2`,
`options-kernels`, `options-kernels-fair`, `options-connected-checked`,
`options-post-checked`, `options-compilation.log`.
The checked-in evidence JSON embeds reports, raw timing samples, selected native
code metadata and hashes. Proprietary guest binaries/generated guest code are
not checked into the repository.

- Tier runs: use `src/tests/wasm/profile.ts`, hardware GPU, AOT=5, detail=0,
  start=78000000, end=96000000, capture mode 2, sampling 0; vary only
  `EKA2L1_V8_FLAGS` between unset, `--no-liftoff`, `--liftoff-only`.
- `EKA2L1_V8_DUMP=1` forwards compiler trace output. Inspect in a separate run.
- `instrument_modules.py BUILD NEW_OUTPUT [--metadata-only]` archives and
  instruments only that copy. Set `EKA2L1_CAPTURE_MODULES` to the active guest
  worker name (worker-13 in these runs). Worker names are harness observations,
  not an emulator contract. With `--replacements DIR`, provide paired original
  `*module-N.wasm` and optimized `*module-N.opt.wasm` files.
- `build_probe_variant.py build-wasm NEW_OUTPUT --variant ceiling` reconstructs
  baseline CPU sources from its explicit default base ref, then links an isolated
  unsafe frontend. Never install it for gameplay. For `connected`, apply the
  experiment patch and rebuild the CPU archive first; the tool checks the marker.
- `compare_compiler_options.py --assets ASSETS --builds MAP.json --output NEW_DIR`
  consumes a name-to-build-directory JSON mapping, gates warmup and runs in
  insertion then reverse order. All timed counters must match.
- Build the optional native target `eka_compiler_probe`; invoke with the installed
  Snakes EXE and a new output directory. It is fixture-specific extraction code,
  not a general game loader. Run `ir_probe.py DIR --wasm-as SDK/bin/wasm-as`, then
  create `.opt.wasm` and `.ir-opt.wasm` using wasm-opt -O3 with threads enabled.
  `whole_budget_probe.py DIR SDK_BIN` creates the full-budget diagnostic control.
  The WASM loop driver used in timing is embedded in the evidence JSON as WAT.
  Run `node src/tests/wasm/compiler-probe.ts DIR NEW_OUTPUT`.
- For native V8 bytes, run the kernel harness separately with
  `EKA2L1_V8_FLAGS='--perf-prof --trace-wasm-compilation-times'`, then use
  `inspect_jitdump.py jit-PID.dump NEW_DIR`. Never use instrumented-run timings
  as the performance control.
- Replay validation: `compare.py NATIVE_FRAMES BROWSER_FRAMES`, all 1,600 images,
  guest timing/instructions, PCM and audio-event records, checker stride 1,024.

Earlier short/insufficiently warmed kernel trials, incomplete module-capture
attempts and profiled kernel timing are preserved in scratch but excluded from
the conclusions. The explicit budgets/fault limitations above govern the ranking.

## Final validation and retained changes

Both connected and post-optimized browser candidates match the native reference
across all 1,600 distinct images, guest timestamps/instruction counts, PCM and
audio events through 102.484363 guest seconds. Both complete the sampled
interpreter checker (stride 1,024). The connected candidate passes 132 WASM
tests; restoring the original CPU source also passes all 132. All three native
test targets and seven frontend smoke checks pass. Smoke tests use the installed
Chromium via `PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium`; the default Puppeteer
downloaded browser was absent.

Retained changes are optional research tools, profiling/export support, this
report/evidence, and the rejected connected prototype as a reproducible patch.
The production CPU source and served frontend are unchanged. No unsafe diagnostic
is installed, and no speedup is claimed for the retained tooling.
