# Browser performance investigation — 2026-09-26

The dominant measured cost is repeated guest instruction translation in DynCom.
The first optimization to investigate is preserving decoded code safely across
context loads. Audio quality work is deferred. This change adds instrumentation
and evidence, not a claimed emulator speedup.

## Workload and measured throughput

Snakes Level 1, guest time **21.000000–25.000000 seconds**, scripted input from
`snakes.input`, deterministic interpreter and guest clock. Every run executes
**655,867,149 guest instructions**, from 2,532,882,497 to 3,188,749,646, with
**170 presentations / 85 changing images**. Host clocks only measure performance.

Chromium 150.0.7871.186 on an Intel i7-10875H, Linux x86_64. Renderer:
`ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero) (0x0000C0DE)), SwiftShader driver)`.
This is a software-rendered reference workload, not a physical-GPU benchmark.
The build is already Release `-O3 -DNDEBUG`, with `--profiling-funcs`,
`ASSERTIONS=1`, `STACK_OVERFLOW_CHECK=2`, and a 64-worker pthread pool.

Four fresh browsers were warmed concurrently and paused at the same guest-time
boundary. Their measurement windows then ran serially with sampling disabled:

| Capture mode, in measurement order | Host seconds for four guest seconds |
| --- | ---: |
| Full capture, first repeat | 54.9874 |
| No PNG encoding; retain readback and frame records | 55.7426 |
| No diagnostic readback/capture; retain rendering and presentation synchronization | 52.6268 |
| Full capture, second repeat | 54.5589 |

The full repeats run at **0.0730× real time** (mean), about **13.7× slower than
real time**, delivering roughly **1.55 changing images per host second**. Their
elapsed-time spread is 0.4285 s (0.78%). Removing PNG encoding did not demonstrate
an end-to-end gain. Removing the whole diagnostic hook reduced elapsed time by
3.9% relative to the full-repeat mean in this batch. That control has one sample;
it is an indication, not a robust speedup estimate or an implemented change.

## Function profile and supporting counters

A separate run sampled the page and all pthread isolates with Chrome's CPU
profiler at a requested 1 ms interval. That run took 66.4566 s; do not substitute
its slower wall time for the unsampled timing controls. Sampling and host
variation are not separately isolated here.

| Guest worker sampled leaf | Share of that isolate's sampled span |
| --- | ---: |
| `InterpreterTranslateInstruction` | **66.08%** |
| `InterpreterMainLoop` | **16.49%** |
| Condition-variable wait | 5.60% |
| `ARMul_State::ReadCode` | 2.40% |
| `ReadMemory32` | 1.93% |
| `WriteMemory32` | 1.33% |

Translation includes inlined work, including the decoder; these samples do not
separately prove that all translation time is spent in the decoder's table scan.
One emulation worker dominates. The graphics worker mostly waits; the remaining
pool is mostly parked. Increasing worker count is not a supported priority.

The unsampled counters agree exactly in all four controls:

| Counter | Four-guest-second total |
| --- | ---: |
| Translated instructions, including repeated translations | **90,055,999** |
| Cached block dispatch hits | 82,190,219 |
| Cached block dispatch misses | **14,069,057** |
| Complete decoded-cache clears | **137,740** |
| Context loads | **137,740** |
| Guest IMB calls | **0** |

These are block-dispatch hit/miss counts, not per-instruction hit rates. The
lookup miss fraction is 14.62%; each miss can translate multiple instructions.
`dyncom_core::load_context()` unconditionally clears the entire decoded cache.
The measured clear count equals the context-load count and there are no IMB
calls. This is strong evidence of avoidable cache churn; the benefit of a safe
replacement still needs implementation and measurement.

In full control 1, `cpu_run` accounts for 53.9409 of 54.9874 wall seconds (98.1%),
including 2.94165 s of synchronous graphics waits (5.35% of wall time). The
renderer-side display hook takes 0.868312 s (1.58%), including 0.230154 s readback
(0.42%) and 0.567149 s PNG encoding (1.03%). Audio pumping takes 0.00556006 s.
These scopes nest and overlap across threads; **do not sum them**.

The sampled page spends 6.478497 s in `_flushLog` and 1.807509 s in `getError`.
The page is 82.6% idle. Those costs overlap guest work and cannot be converted
directly into emulator speedup percentages. Futex/condition-wait samples are
parked time, not busy CPU time.

## Ranked options

1. **Retain decoded blocks across context loads.** Highest evidence-backed
   priority. Start at `src/emu/cpu/src/dyncom/arm_dyncom.cpp:132` and the cache
   lookup in `arm_dyncom_interpreter.cpp:1718`. The current key is only the PC;
   simply deleting the clear is unsafe. Include address-space and ARM/Thumb
   identity, correct code-write/mapping/IMB invalidation, and a bounded cache.
   A conservative first version could retain only when those identities and
   code generations remain valid. Require strict native/WASM image, guest-time
   and instruction equality, plus targeted invalidation tests.
2. **Make cache-miss decoding cheaper.** `arm_dyncom_dec.cpp:443` scans encoding
   entries and bitfield predicates linearly. A generated indexed decision tree
   or table can reduce this work while preserving match priority, exclusions,
   invalid-instruction behavior and ARM/Thumb semantics. This targets the same
   translation budget as option 1, so gains must not be added together. Profile
   again after cache retention before deciding how much decoder work is justified.
3. **Repair and expand compiled guest hot blocks (AOT/JIT).** Higher effort and
   correctness risk, but a larger long-term opportunity than interpreter polish.
   Experimental game AOT is currently disabled by deterministic mode
   (`src/emu/system/src/aot_setup.cpp:85`); enabling it is not a validated switch.
   Preserve instruction budgets, interrupts, exceptions, memory behavior and
   guest-clock advancement. Even eliminating the entire sampled 66% translation
   cost would imply only about a 3× ceiling under an unchanged-cost model, short
   of this reference workload's approximately 14× real-time gap. This is an
   illustrative bound, not a prediction for a physical GPU or a new engine.
4. **Trim browser diagnostics and graphics proxy overhead.** Bound or disable
   the growing DOM log (`src/emu/wasm/shell.html:42`), gate per-call `glGetError`
   (`src/emu/drivers/src/graphics/backend/ogl/graphics_ogl.cpp:44`) to diagnostic
   builds, and make framebuffer diagnostic readback opt-in. Profile worker-owned
   OffscreenCanvas if graphics waits remain material. This is useful polish;
   the measured graphics-wait budget is around 5%, with capture accounting for
   only part of it. Retest on a hardware GPU before a renderer redesign.

Recommended next implementation: option 1, followed by the same deterministic
replay and fresh worker profiles. No speedup numbers are promised before that
experiment. Generic additional `-O3` or more worker threads do not address the
observed bottleneck; LTO/assertion changes are secondary unmeasured experiments.

## Provenance, verification and reproduction

Instrumentation commit: `1a71bc0a5d0fbb3fcab924000cd4a21d51608d6c`. All four
unsampled reports record this HEAD and a clean worktree. Their WASM SHA-256 is:
`eb04ad450f3e1969492931bc2a411f67ad6b99dafe5697edbcd5754ce3977343`.

The earlier sampled run used `f1bd98345ce33b5a72b6187ab1c38362d72fae60` plus
uncommitted profiling instrumentation, before adding the cache counters later
committed in `1a71bc0a`. Its WASM SHA-256 is
`8b929d033268a7c255ce3337ad08fac4ac9a15c56097428216912459f22423fe`.
Do not label its function percentages as a clean-commit counter-enabled run.

- Both full controls match the native reference's first **85 PNGs byte for
  byte**, and all corresponding frame records match. The no-PNG control matches
  those records. The no-readback control matches execution counters and
  presentation count; it intentionally provides no image evidence.
- All controls have identical guest endpoints, instruction counts and cache
  counters. No observed browser page/request/HTTP/runtime-abort errors.
- At `1a71bc0a`, native CTest: CPU target passes; `ekatests` has **77 passing,
  seven failing cases** (406/413 assertions). The new profiling test passes.
  The same seven allocator/parsing/app-registration failures were present in
  prior logs; no clean upstream-base test establishes their origin.
- WASM CPU/AOT suite at `1a71bc0a`: **121/121 pass**.
- This is four guest seconds of one scene, using the deterministic benchmark
  path, not a whole-game or interactive-play performance survey. Sampling,
  per-instruction counters and timing instrumentation add overhead. Audio
  callbacks remain in the workload, but no audio-quality claim is made.

[PERFORMANCE_EVIDENCE.json](PERFORMANCE_EVIDENCE.json) preserves the reports and
busiest-isolate summaries. Raw profiles/captures are local under
`/home/claude/.scratch/eka-benchmark/perf-full-sampled` and `perf-ablation`;
the reference is `audio-native-0/run-0/frames`. Full-suite logs are
`/home/claude/.scratch/eka-profile-tests-committed.log` and
`/home/claude/.scratch/eka-profile-wasm-tests-committed.log`.
See [PROFILING.md](PROFILING.md) for reproduction and
[Chrome's Profiler protocol](https://chromedevtools.github.io/devtools-protocol/v8/Profiler/)
for the sampling API. Emscripten documents symbol-preserving optimized builds
in its [emcc reference](https://emscripten.org/docs/tools_reference/emcc.html).
