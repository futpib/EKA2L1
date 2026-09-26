# ARM long-multiply AOT results

Runtime: `eef67480e89fb860f0da4fd530831e3be1989070`.
Timing harness: `26b4e4ad33c7bc5c01d1689794170ba2a046df20`.
Baseline browser binary preserved from `87fd0b13cd66f1212e841bf76a086939fdf2cfb8`.
Measured 2026-09-26. Full reports, hashes, counter summaries and artifact paths:
[LONG_MULTIPLY_EVIDENCE.json](LONG_MULTIPLY_EVIDENCE.json).

## Change and correctness

ARM `SMLAL`, `SMULL`, `UMLAL` and `UMULL` now compile to WASM i64 arithmetic.
Products use the appropriate signed/unsigned extension; accumulation wraps modulo
2^64. Inputs are read before either destination changes, preserving overlapping
source/destination behavior. S forms update N/Z and retain C/V. PC operands or
identical destination registers fall back. The existing per-instruction budget,
condition checks, RAM validation, store exits and scheduling rules remain in use.
A fixed i64 scratch local precedes the lazily allocated i32 register locals.

The complete WASM suite passes **124 tests**, including **46,080 new exact
budget/state comparisons** and 93,072 existing budget/state/memory comparisons.
The new matrix covers four multiply forms, flag setting, EQ/NE/AL conditions,
signed/unsigned edge values and accumulation overflow, six register layouts,
cached/uncached registers, and budgets 0, 1, 2, 3 and 4 for three-instruction blocks.
Invalid register forms have rejection checks. Native CPU tests pass; the broader
native package has **83 passes and seven previously observed failures** in
allocator, number parsing and app registration. No clean-base run establishes
when those failures began. Tests were rerun at harness commit `26b4e4ad`, whose
runtime and test source are unchanged from `eef67480`.

Two complete browser runs match the saved native interpreter reference:

- All **1,000 distinct gameplay PNGs**, guest timestamps and instruction counts.
- Complete PCM output and audio-event records (repeatability only; audio-quality
  work remains deferred).
- Endpoint **68.040042 guest seconds**, **10,342,580,529 guest instructions**.
- Gameplay capture span **47.013860 guest seconds**, starting at 21.026182.
- One run checks every 1,024th compiled block against the interpreter, selected by
  deterministic execution count; it completed without divergence. This is not
  an exhaustive every-block replay check.

Both diagnostic profiles and all four timing fixtures also match their **85**
images and guest records against the same native reference prefix.

## Serial timing

All trials use AOT mode 4, SwiftShader, full PNG capture, identical assets/input,
and the **21–25 guest-second** gameplay window. Warmup is excluded. Fixtures warm
up together, pause at 21 seconds, then resume serially in old/new/new/old order.
All owned correctness, unit-test, build and diagnostic jobs finished before the
measurement gate was released. Guest profiling, Chrome sampling, AOT diagnostics
and interpreter verification are disabled in these timing trials.

| Trial | Host seconds for four guest seconds |
| --- | ---: |
| Before 1 | 37.2568 |
| After 1 | 34.8793 |
| After 2 | 36.8284 |
| Before 2 | 38.9242 |
| Before mean | **38.0905** |
| After mean | **35.85385** |

Observed mean throughput improves **1.0624x** (about **6.2%**), with **5.87%** less
elapsed time. This is a modest gain from two trials per build on a shared host,
not a general speed guarantee. It is **0.1116 guest seconds per host second**;
approximately **8.96x** additional throughput would be needed for realtime in
this synthetic-clock workload. No physical-GPU interactive measurement or normal
Qt JIT comparison was made. Do not compare the concurrently executed full-replay
wall times or diagnostic-profile wall times to these serial timings.

The old binary is archived under `.scratch/eka-benchmark/longmul-before-build`.
Its SHA-256 is `53bd06408f704c3898ff449c9c74eca73a993d0144a1a455f5b1552aca6e9fab`.
Runner report `git_head` identifies the harness checkout, not an archived binary's
source; use the binary hash and `baseline_binary_source` in the evidence file.

## Guest work and remaining costs

Each window executes exactly **655,867,149 guest instructions**. Two diagnostic
runs use different sample strides (1,021 and 1,009); their exact instruction-type
histograms are identical, and all guest/AOT/decode counters reconcile.

| Counter | Before | After |
| --- | ---: | ---: |
| Compiled instructions | 458,913,845 | 539,682,871 |
| Compiled coverage | 69.97% | **82.29%** |
| Interpreted instructions | 196,953,304 | **116,184,278** |
| Decoded instructions | 26,137,201 | **19,224,720** |
| Interpreted SMLAL | 36,798,864 | **14,713,038** |
| Interpreted SMULL | 1,610,318 | **275** |
| Interpreted UMULL | 145,020 | 135,054 |
| Compiled blocks | 117,675,732 | 123,797,767 |
| Guest instructions per compiled block | 3.90 | **4.36** |
| Compiled runner calls | 16,860,275 | 13,683,043 |

Interpreted work falls **41.01%**, decoding **26.45%**, and runner calls **18.84%**.
Compiled blocks increase **5.20%**: improved coverage still means many short
invocations. Instruction-work reductions are not wall-time percentages.

The targeted Snakes region, **0x70063700–0x70063800** (end-exclusive), falls from
**30.24–30.30%** of interpreter samples to **0.58–0.59%**. With stride 1,021,
its sample count falls from 58,341 to 663. The percentage denominator also shrinks,
so raw counts and full type histograms are retained in the evidence.

In the new stride-1,021 run, Snakes (`6r45_1b.exe`) remains **86.22%** of interpreter
samples, followed by `euser.dll` (10.38%) and `dfpaeabi.dll` (2.72%). Leading exact
interpreter handlers are MOV (19.85%), LDR (14.77%), SMLAL (12.66%), CMP (10.49%),
B/BL (8.58%) and STR (7.24%). Remaining SMLAL samples include `0x700635e0`
(`0xe0e6579e`) and `0x700636ec` (`0xe0e4369c`). Supporting an opcode does not imply
that every block containing it is selected and reached through compiled execution.
The current profile does not distinguish selection misses from preceding exits or
cache rejection for each of those remaining PCs.

A supplementary Chrome profile of the second diagnostic run samples translation
at **20.0%**, interpreter loop **13.4%**, RAM-cache lookup **12.6%**, WASM-to-JS
**7.8%**, mapping resolution **3.5%**, and byte comparison **2.3%** of the emulation
worker's sampled span. Guest profiling itself is about **3.9%**, and waits about
8%. This diagnostic ran alongside correctness jobs; use it to locate costs, not
as a clean quantitative comparison to earlier CPU profiles or serial wall time.

The measured target is largely removed. Remaining work is still dominated by
fallback execution and frequent short compiled invocations. The next focused
investigation should identify why the remaining hot, now-supported blocks stay
interpreted, before expanding opcode support without evidence. Block selection,
longer regions and cheaper validated dispatch remain candidates; their gains
have not been measured here.

## Reproduction

Build/test instructions are in [README.md](README.md) and [AOT.md](AOT.md).
Run from `src/tests/wasm` (outputs must be new directories):

```sh
EKA2L1_BENCHMARK_AOT=4 EKA2L1_AOT_VERIFY=1024 node benchmark.ts /absolute/assets /absolute/checked 1000
EKA2L1_BENCHMARK_AOT=4 node benchmark.ts /absolute/assets /absolute/repeat 1000
EKA2L1_BENCHMARK_AOT=4 EKA2L1_GUEST_PROFILE=1021 node profile.ts /absolute/assets /absolute/profile 0 0
EKA2L1_BENCHMARK_AOT=4 EKA2L1_GUEST_PROFILE=1009 node profile.ts /absolute/assets /absolute/profile-1009 0 1
```

From the repository root, preserve the baseline frontend build before rebuilding,
then compare its archived directory with the current build:

```sh
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/timings --compare-build /absolute/archived-frontend --measure-gate /absolute/new-gate
# Wait for new-gate.ready and finish other CPU-heavy work, then:
touch /absolute/new-gate
python3 src/tests/benchmark/compare.py /absolute/native-reference /absolute/checked
python3 src/tests/benchmark/validate_gameplay.py /absolute/checked
python3 src/tests/benchmark/summarize_guest_profile.py /absolute/profile
```

Implementation: `src/emu/cpu/src/aot/arm_translator.cpp`,
`src/emu/cpu/{include/cpu/aot/wasm_emitter.h,src/aot/wasm_emitter.cpp}`.
Differential matrix: `src/tests/aot/test_aot_wasm.cpp::test_arm_long_multiply`.
