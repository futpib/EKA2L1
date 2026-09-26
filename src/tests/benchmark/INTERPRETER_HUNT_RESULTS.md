# Interpreter fallback hunt

**The interpreter hunt reached its practical stopping point as a route to
realtime.** Compiled instruction coverage rose from **82.29% to 99.61%**, and
observed throughput improved **1.2814x**. The benchmark still runs at **0.14195x
realtime**. Further fallback cleanup remains possible, but reaching realtime now
requires making the compiled execution path substantially cheaper.

Runtime: `305a3e38eb9a975ee40d88c9c7502aa4de04301f`. Baseline binary source:
`71016e32d6643a20652561a867217e071b6826d9`. Measurements: 2026-09-26.
Reports, exact counters, profiles, source annotations, hashes and artifact paths:
[INTERPRETER_HUNT_EVIDENCE.json](INTERPRETER_HUNT_EVIDENCE.json).

## Fixes found by profiling

The diagnostic baseline counted **24,909,457 rejected RAM lookup attempts** in
four guest seconds. These are lookup events, not interpreted instructions;
compiled-chain and dispatcher lookups can both count. The leading rejected PCs
were the Snakes comparison helpers at `0x70065b00` and `0x70065a64`, the store
loop at `0x700002a8`, and other comparison/load entries.

- The ARM MRS/MSR mask incorrectly accepted ordinary register TST/TEQ/CMP/CMN
  encodings. Those instructions were rejected despite having working arithmetic
  emitters. Testing the S bit fixes classification.
- Post-indexed loads/stores were explicitly unsupported. Byte, halfword and word
  forms now compile, including word loads to PC for returns. Address writeback
  follows the reference interpreter before the memory access. Privilege variants,
  base/destination overlap and unsupported PC combinations retain fallback.
- PLD hints at hot ROM routine entries now consume one instruction without a
  memory access, matching DynCom's optional-prefetch behavior.
- Register `MSR CPSR_f` now updates NZCVQ in supported user contexts. The control
  flow walker also mistook its fixed encoding bits for a write to PC, incorrectly
  truncating fallthrough; a budget regression test caught this, and the walker is fixed.
- The active CPU mode is stored separately from CPSR. Guest thread contexts can
  contain zero CPSR mode bits while `ARMul_State::Mode` remains `USER32MODE`.
  The MSR guard now accepts that legacy representation and ordinary user mode;
  it rejects privileged modes and mismatches that could bank registers. It does
  not implement SPSR or control-field changes.

No cache safety checks, guest instruction budgets or deterministic scheduling
rules were removed. Audio-quality work remains deferred.

## Correctness

Two final browser runs, one checking every **1,024th** compiled block against the
interpreter, match the saved native reference across:

- **1,000 distinct gameplay images**, every guest timestamp and instruction count;
- complete PCM output and audio-event records;
- endpoint **68.040042 guest seconds**, **10,342,580,529 guest instructions**;
- **47.013860 seconds** of captured gameplay after the first frame at 21.026182.

The sampled per-block checker completed without divergence. It is not exhaustive
per-block validation. Every intermediate 85-image diagnostic window, the final
CPU profile and all four timing windows also match native pixels and guest
records exactly. Full-game compatibility and physical-handset timing accuracy
are not established by this replay.

The WASM suite reports **125 passed, 0 failed**, including **115,920 exact
budget/state/memory comparisons**, **46,080 long-multiply comparisons**, and
explicit MSR tests for accepted legacy/user contexts and rejected mode/banking
combinations. An existing explicitly expected crash-reproduction harness
limitation remains in the log. Native CPU tests pass. The broader native package
has **83 passes and seven previously observed failures** (allocator, number
parsing, app registration); no clean-base run establishes their origin. Final
tests were run at runtime commit `305a3e38`.

Two earlier full replays and their queued performance fixtures were stopped
incomplete after diagnosing the overly restrictive CPSR-only MSR guard. They
are excluded from final pass and speed claims. Intermediate diagnostic runs were
launched before their progress commits; their raw checkout metadata and explicit
source annotations are retained. Final replay/timing runs started from the clean
committed runtime, and their binary hashes agree.

## Serial performance

Identical assets, input, AOT mode 4, SwiftShader and full PNG capture; the measured
window is **21–25 guest seconds**. Warmup is excluded. Fixtures warmed together,
paused, then ran serially in old/new/new/old order. Full correctness/tests finished
first; the isolated CPU profile completed before timing trials began. Guest
profiling, CPU sampling, AOT diagnostics and verification are disabled in the
four timing trials.

| Trial | Host seconds for four guest seconds |
| --- | ---: |
| Before 1 | 34.6987 |
| After 1 | 27.1361 |
| After 2 | 29.2231 |
| Before 2 | 37.5205 |
| Before mean | **36.1096** |
| After mean | **28.1796** |

That is **28.14% more throughput**, or **21.96% less elapsed time**. Two trials
per build on a shared host establish an observed gain, not a universal speed
multiplier. The result is **0.14195 guest seconds per host second**; this workload
would need about **7.04x** additional throughput for realtime. No physical-GPU
interactive run or normal Qt JIT comparison was made. The full replay runs were
concurrent and their elapsed times are not used for this comparison.

## Remaining interpreter work

All windows execute **655,867,149 guest instructions**, with unchanged guest
clock endpoints and frame records.

| Counter | Baseline | Final |
| --- | ---: | ---: |
| Compiled instructions | 539,682,871 | **653,318,300** |
| Compiled coverage | 82.285% | **99.611%** |
| Interpreted instructions | 116,184,278 | **2,548,849** |
| Decoded instructions | 19,224,720 | **1,783,129** |
| Compiled blocks | 123,797,767 | 132,212,243 |
| Instructions per compiled block | 4.36 | **4.94** |
| Compiled runner entries | 13,683,043 | **2,599,448** |
| Rejected RAM lookup events | 24,909,457 | **1,328** |

Interpreted instructions fall **97.81%**, decoding **90.72%**, and compiled runner
entries **81.00%**. More guest work stays compiled, but still executes about
**132 million short compiled blocks** in this window.

Remaining interpreted types include Thumb BL prefix (456,836), BLX suffix
(373,524), BL suffix (83,312), ARM LDR (373,187), SMLAL (305,031), MOV (264,650),
and STR (151,060). The three Thumb call halves total 913,672 instructions—about
0.139% of all guest work. Some remaining ARM fallback is already supported code
entered at uncompiled positions, not missing opcode semantics. Diagnostic rows
retain PC/mode, address space and miss reason; ROM-miss opcode zero means that
field was not collected, not a literal zero guest opcode.

The final guest histogram reconciles all AOT/interpreter/decode totals with no
sample drops. An intermediate sampled-verifier profile also executes a private
reference CPU; its general performance decode/context counters include that
reference work, while its guest histogram excludes it. Those polluted counters
are explicitly marked in the evidence and are not final performance inputs.

## Why further interpreter hunting will not close the gap

A separate isolated CPU profile, with guest profiling and verification disabled,
measured **31.2361 host seconds** (sampling adds overhead; this is not the timing
headline). The emulation worker's sampled span was 31.5234 seconds.

| Sampled self-time | Share of worker span |
| --- | ---: |
| RAM validated-code-cache lookup | **17.12%** |
| WASM-to-JS transitions | **11.18%** |
| Waits | 11.13% |
| Compiled-function lookup | **6.34%** |
| Interpreter loop, including shared compiled dispatch | 5.85% |
| Executable mapping resolution | **4.41%** |
| Code byte comparison | 3.12% |
| Instruction translation | **2.36%** |

Cache lookup, compiled-function lookup and mapping resolution together account
for **27.87%**. The interpreter-loop entry is shared with AOT dispatch; it must
not all be labeled remaining interpreted execution.

Stack attribution finds **12.339 sampled seconds (39.14% of the worker span)**
inside generated AOT functions or their callees, including memory imports.
Generated modules have `wasm://` URLs; the linked emulator uses an HTTP WASM URL.
This inclusive subset overlaps the self-time table and must not be added to it.
It excludes surrounding compiled lookup/dispatch, so it is not the whole cost
of AOT. Its sampled time alone is already substantially above the four-second
realtime budget.

That is the evidence for stopping this *interpreter-coverage* hunt. Even the
optimistic thought experiment of removing all work outside the current generated
functions leaves substantial cost above realtime. This is a profile-based
assessment on this host, not a proof that browser emulation itself is impossible.
Further small opcode gains are possible. Realtime now requires changes to
compiled execution: longer regions with fewer state spills and returns, cheaper
validated lookup, and cheaper memory/cross-module call paths. Those changes and
their gains are not implemented or claimed here.

## Reproduction and source

Build and test setup: [README.md](README.md). AOT modes and safety constraints:
[AOT.md](AOT.md). Diagnostics: [GUEST_PROFILING.md](GUEST_PROFILING.md).

From `src/tests/wasm`, using new output directories:

```sh
EKA2L1_BENCHMARK_AOT=4 EKA2L1_AOT_VERIFY=1024 node benchmark.ts /absolute/assets /absolute/checked 1000
EKA2L1_BENCHMARK_AOT=4 node benchmark.ts /absolute/assets /absolute/repeat 1000
EKA2L1_BENCHMARK_AOT=4 EKA2L1_GUEST_PROFILE=1021 node profile.ts /absolute/assets /absolute/guest-profile 0 0
EKA2L1_BENCHMARK_AOT=4 PROFILE_GATE=/absolute/cpu-gate node profile.ts /absolute/assets /absolute/cpu-profile 0 1
```

From the repository root, compare an archived baseline frontend with the current
build. Finish correctness work, release the CPU gate and let sampling finish,
then release the timing gate:

```sh
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/timings --compare-build /absolute/archived-frontend --measure-gate /absolute/timing-gate
# Wait for both *.ready markers and finish correctness/tests first.
touch /absolute/cpu-gate
# After the CPU-profile runner exits:
touch /absolute/timing-gate
python3 src/tests/benchmark/summarize_profile.py /absolute/cpu-profile
python3 src/tests/benchmark/summarize_guest_profile.py /absolute/guest-profile
python3 src/tests/benchmark/compare.py /absolute/native-reference /absolute/checked
python3 src/tests/benchmark/validate_gameplay.py /absolute/checked
```

Runner `git_head` describes the harness checkout; archived binary source is
identified separately by its recorded commit and WASM hash. Raw final artifacts
are under `/home/claude/.scratch/eka-benchmark/hunt-final-*`.

Main changes: `src/emu/cpu/src/aot/arm_translator.cpp`,
`src/emu/cpu/src/aot/aot_runtime.cpp`, `src/emu/common/include/common/guest_profile.h`,
`src/tests/aot/test_aot_wasm.cpp`, and `src/tests/benchmark/summarize_profile.py`.
